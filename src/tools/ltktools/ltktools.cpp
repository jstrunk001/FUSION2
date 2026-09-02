// ltktools.cpp : Modern C++ Batch Processing Utility for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/batch/BatchPipeline.h"
#include "fusion/batch/StatusMessenger.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <map>

struct TileCellAccumulator {
    std::vector<float> elevations;
    std::vector<float> intensities;
    int totalReturns{0};
    int firstReturns{0};
    int returnsAboveGround{0};
    int returnsAboveMinHt{0};
    int returnsAboveHeightCut{0};
};

static std::vector<double> ParseFloatList(const std::string& str) {
    std::vector<double> values;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            try {
                values.push_back(std::stod(item));
            } catch (...) {}
        }
    }
    return values;
}

static std::unordered_set<int> ParseIntSet(const std::string& str) {
    std::unordered_set<int> values;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) {
            try {
                values.insert(std::stoi(item));
            } catch (...) {}
        }
    }
    return values;
}

static float GetPercentile(const std::vector<float>& sortedData, double p) {
    if (sortedData.empty()) return -9999.0f;
    if (sortedData.size() == 1) return sortedData[0];
    double idx = p * (sortedData.size() - 1);
    size_t i0 = static_cast<size_t>(std::floor(idx));
    size_t i1 = std::min(i0 + 1, sortedData.size() - 1);
    double frac = idx - i0;
    return static_cast<float>((1.0 - frac) * sortedData[i0] + frac * sortedData[i1]);
}

static float GetMode(const std::vector<float>& data, float binSize = 0.5f) {
    if (data.empty()) return -9999.0f;
    std::map<int, int> bins;
    for (float v : data) {
        int b = static_cast<int>(std::floor(v / binSize));
        bins[b]++;
    }
    int maxCount = 0;
    int maxBin = 0;
    for (const auto& [b, cnt] : bins) {
        if (cnt > maxCount) {
            maxCount = cnt;
            maxBin = b;
        }
    }
    return (maxBin + 0.5f) * binSize;
}

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("ltktools", "FUSION Batch Processing & Tile Pipeline Utility");
    parser.SetPositionalArgsUsage("");
    parser.AddOption("extent", "Project extent LLX,LLY,URX,URY");
    parser.AddOption("tilesize", "Tile width,height in project units", "1000,1000");
    parser.AddOption("buffer", "Tile buffer distance", "50");
    parser.AddOption("resolution", "Raster resolution", "1.0");
    parser.AddOption("input", "Input directory containing LAS/LAZ files");
    parser.AddOption("output", "Output directory for rasters and metrics");
    parser.AddOption("output-mode", "Output raster mode: multiband or singleband", "multiband");
    parser.AddOption("threads", "Number of parallel worker threads", "4");
    parser.AddFlag("vrt", "Generate GDAL Virtual Raster (.vrt) across tile rasters (default: true)");
    parser.AddFlag("merge", "Merge VRT into a single global GeoTIFF file");

    // GridMetrics forward switches
    parser.AddOption("ground", "Path to ground surface DEM raster");
    parser.AddOption("minht", "Minimum height cutoff for canopy metrics", "2.0");
    parser.AddOption("heightcut", "Height cutoff threshold for canopy cover");
    parser.AddOption("outlier", "Trim elevation outliers outside min,max values");
    parser.AddOption("class", "Comma-separated point classifications to include");
    parser.AddFlag("first", "Use only first returns for metric calculations");
    parser.AddFlag("nointensity", "Skip computing intensity metrics");
    parser.AddOption("strata", "Comma-separated height strata thresholds");
    parser.AddOption("intstrata", "Comma-separated intensity strata height thresholds");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    auto optExtent = parser.GetOption("extent");
    auto optInput = parser.GetOption("input");
    auto optOutput = parser.GetOption("output");

    if (!optInput || !optOutput) {
        std::cerr << "Error: /input and /output directory parameters are required.\n";
        parser.PrintHelp();
        return 1;
    }

    fusion::batch::TileGridSpec gridSpec;
    if (optExtent) {
        std::stringstream ss(*optExtent);
        char ch;
        ss >> gridSpec.minX >> ch >> gridSpec.minY >> ch >> gridSpec.maxX >> ch >> gridSpec.maxY;
    } else {
        gridSpec.minX = 0; gridSpec.minY = 0; gridSpec.maxX = 5000; gridSpec.maxY = 5000;
    }

    if (auto ts = parser.GetOption("tilesize")) {
        std::stringstream ss(*ts);
        char ch;
        ss >> gridSpec.tileSizeX >> ch >> gridSpec.tileSizeY;
    }

    if (auto buf = parser.GetOption("buffer")) {
        gridSpec.bufferDistance = std::stod(*buf);
    }

    if (auto res = parser.GetOption("resolution")) {
        gridSpec.resolution = std::stod(*res);
    }

    fusion::batch::PipelineJobOptions jobOpts;
    jobOpts.inputPointCloudDir = *optInput;
    jobOpts.outputDir = *optOutput;
    jobOpts.outputMode = parser.GetOption("output-mode").value_or("multiband");
    jobOpts.numThreads = std::stoi(parser.GetOption("threads").value_or("4"));
    jobOpts.generateVRT = true;
    jobOpts.mergeGeoTIFF = parser.HasFlag("merge");

    if (auto g = parser.GetOption("ground")) jobOpts.groundPath = *g;
    jobOpts.minHt = std::stod(parser.GetOption("minht").value_or("2.0"));
    jobOpts.heightCut = parser.GetOption("heightcut") ? std::stod(*parser.GetOption("heightcut")) : jobOpts.minHt;
    if (auto o = parser.GetOption("outlier")) jobOpts.outlier = *o;
    if (auto c = parser.GetOption("class")) jobOpts.pointClass = *c;
    jobOpts.firstOnly = parser.HasFlag("first");
    jobOpts.noIntensity = parser.HasFlag("nointensity");
    if (auto s = parser.GetOption("strata")) jobOpts.strata = *s;
    if (auto is = parser.GetOption("intstrata")) jobOpts.intStrata = *is;

    fusion::batch::StatusMessenger::Instance().SetLogFile(jobOpts.outputDir / "ltktools_batch.log");
    fusion::batch::StatusMessenger::Instance().SendStatus("Initializing ltktools batch pipeline...");

    fusion::batch::BatchPipeline pipeline(gridSpec, jobOpts);

    auto tileTask = [gridSpec](const fusion::batch::TileInfo& tile, const fusion::batch::PipelineJobOptions& opts) -> bool {
        std::filesystem::path outTif = opts.outputDir / (tile.name + ".tif");

        double res = gridSpec.resolution;
        int cols = static_cast<int>(std::ceil((tile.maxX - tile.minX) / res));
        int rows = static_cast<int>(std::ceil((tile.maxY - tile.minY) / res));
        if (cols <= 0) cols = 1;
        if (rows <= 0) rows = 1;

        double outlierMin = -99999.0, outlierMax = 99999.0;
        bool hasOutlier = false;
        if (!opts.outlier.empty()) {
            auto vals = ParseFloatList(opts.outlier);
            if (vals.size() >= 2) {
                outlierMin = vals[0];
                outlierMax = vals[1];
                hasOutlier = true;
            }
        }

        std::unordered_set<int> validClasses;
        if (!opts.pointClass.empty()) {
            validClasses = ParseIntSet(opts.pointClass);
        }

        fusion::raster::GDALRaster groundRaster;
        bool hasGround = false;
        if (!opts.groundPath.empty() && groundRaster.Open(opts.groundPath)) {
            hasGround = true;
        }

        std::vector<TileCellAccumulator> grid(cols * rows);

        // Process matching LAS/LAZ files in input directory
        if (std::filesystem::exists(opts.inputPointCloudDir)) {
            for (const auto& entry : std::filesystem::directory_iterator(opts.inputPointCloudDir)) {
                if (!entry.is_regular_file()) continue;
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".las" && ext != ".laz") continue;

                fusion::lidar::LASReader reader;
                if (reader.Open(entry.path())) {
                    const auto& header = reader.GetHeader();
                    // Spatial bounding box overlap check
                    if (header.maxX < tile.bufferedMinX || header.minX > tile.bufferedMaxX ||
                        header.maxY < tile.bufferedMinY || header.minY > tile.bufferedMaxY) {
                        reader.Close();
                        continue;
                    }

                    fusion::lidar::PointRecord pt;
                    while (reader.ReadNextPoint(pt)) {
                        if (pt.x < tile.bufferedMinX || pt.x > tile.bufferedMaxX ||
                            pt.y < tile.bufferedMinY || pt.y > tile.bufferedMaxY) {
                            continue;
                        }

                        if (!validClasses.empty() && validClasses.find(pt.classification) == validClasses.end()) {
                            continue;
                        }

                        if (opts.firstOnly && pt.returnNumber != 1) {
                            continue;
                        }

                        int col = static_cast<int>((pt.x - tile.minX) / res);
                        int row = static_cast<int>((tile.maxY - pt.y) / res);

                        if (col >= 0 && col < cols && row >= 0 && row < rows) {
                            auto& cell = grid[row * cols + col];
                            cell.totalReturns++;
                            if (pt.returnNumber == 1) cell.firstReturns++;

                            double elevation = pt.z;
                            if (hasGround) {
                                if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                                    elevation -= *gz;
                                }
                            }

                            if (hasOutlier && (elevation < outlierMin || elevation > outlierMax)) {
                                continue;
                            }

                            if (elevation >= 0.0) cell.returnsAboveGround++;
                            if (elevation >= opts.heightCut) cell.returnsAboveHeightCut++;

                            if (elevation >= opts.minHt) {
                                cell.returnsAboveMinHt++;
                                cell.elevations.push_back(static_cast<float>(elevation));
                                if (!opts.noIntensity) {
                                    cell.intensities.push_back(static_cast<float>(pt.intensity));
                                }
                            }
                        }
                    }
                    reader.Close();
                }
            }
        }

        size_t numCells = cols * rows;
        std::vector<float> bandMin(numCells, -9999.0f), bandMax(numCells, -9999.0f);
        std::vector<float> bandMean(numCells, -9999.0f), bandStdDev(numCells, -9999.0f);
        std::vector<float> bandVar(numCells, -9999.0f), bandCV(numCells, -9999.0f);
        std::vector<float> bandSkew(numCells, -9999.0f), bandKurt(numCells, -9999.0f);
        std::vector<float> bandCRR(numCells, -9999.0f), bandMode(numCells, -9999.0f);
        std::vector<float> bandMedian(numCells, -9999.0f), bandIQR(numCells, -9999.0f);
        std::vector<float> bandP01(numCells, -9999.0f), bandP05(numCells, -9999.0f);
        std::vector<float> bandP10(numCells, -9999.0f), bandP20(numCells, -9999.0f);
        std::vector<float> bandP25(numCells, -9999.0f), bandP30(numCells, -9999.0f);
        std::vector<float> bandP40(numCells, -9999.0f), bandP50(numCells, -9999.0f);
        std::vector<float> bandP60(numCells, -9999.0f), bandP70(numCells, -9999.0f);
        std::vector<float> bandP75(numCells, -9999.0f), bandP80(numCells, -9999.0f);
        std::vector<float> bandP90(numCells, -9999.0f), bandP95(numCells, -9999.0f);
        std::vector<float> bandP99(numCells, -9999.0f);
        std::vector<float> bandCover(numCells, -9999.0f), bandDensity(numCells, -9999.0f);
        std::vector<float> bandIntMean(numCells, -9999.0f), bandIntStdDev(numCells, -9999.0f);

        for (size_t i = 0; i < numCells; ++i) {
            auto& cell = grid[i];
            if (!cell.elevations.empty()) {
                std::vector<float> sortedElev = cell.elevations;
                std::sort(sortedElev.begin(), sortedElev.end());

                size_t n = sortedElev.size();
                double minV = sortedElev.front();
                double maxV = sortedElev.back();
                double sum = std::accumulate(sortedElev.begin(), sortedElev.end(), 0.0);
                double mean = sum / n;

                double sqSum = 0.0, cubeSum = 0.0, quadSum = 0.0;
                for (float v : sortedElev) {
                    double diff = v - mean;
                    sqSum += diff * diff;
                    cubeSum += diff * diff * diff;
                    quadSum += diff * diff * diff * diff;
                }

                double var = (n > 1) ? (sqSum / (n - 1)) : 0.0;
                double stdDev = std::sqrt(sqSum / n);
                double cv = (mean != 0.0) ? (stdDev / mean) : 0.0;
                double skew = (stdDev > 0.0) ? ((cubeSum / n) / std::pow(stdDev, 3.0)) : 0.0;
                double kurt = (stdDev > 0.0) ? ((quadSum / n) / std::pow(stdDev, 4.0)) : 0.0;
                double crr = (maxV > minV) ? ((mean - minV) / (maxV - minV)) : 0.0;

                bandMin[i] = static_cast<float>(minV);
                bandMax[i] = static_cast<float>(maxV);
                bandMean[i] = static_cast<float>(mean);
                bandStdDev[i] = static_cast<float>(stdDev);
                bandVar[i] = static_cast<float>(var);
                bandCV[i] = static_cast<float>(cv);
                bandSkew[i] = static_cast<float>(skew);
                bandKurt[i] = static_cast<float>(kurt);
                bandCRR[i] = static_cast<float>(crr);

                bandMode[i] = GetMode(sortedElev);
                bandMedian[i] = GetPercentile(sortedElev, 0.50);
                bandIQR[i] = GetPercentile(sortedElev, 0.75) - GetPercentile(sortedElev, 0.25);

                bandP01[i] = GetPercentile(sortedElev, 0.01);
                bandP05[i] = GetPercentile(sortedElev, 0.05);
                bandP10[i] = GetPercentile(sortedElev, 0.10);
                bandP20[i] = GetPercentile(sortedElev, 0.20);
                bandP25[i] = GetPercentile(sortedElev, 0.25);
                bandP30[i] = GetPercentile(sortedElev, 0.30);
                bandP40[i] = GetPercentile(sortedElev, 0.40);
                bandP50[i] = GetPercentile(sortedElev, 0.50);
                bandP60[i] = GetPercentile(sortedElev, 0.60);
                bandP70[i] = GetPercentile(sortedElev, 0.70);
                bandP75[i] = GetPercentile(sortedElev, 0.75);
                bandP80[i] = GetPercentile(sortedElev, 0.80);
                bandP90[i] = GetPercentile(sortedElev, 0.90);
                bandP95[i] = GetPercentile(sortedElev, 0.95);
                bandP99[i] = GetPercentile(sortedElev, 0.99);
            }

            int denom = opts.firstOnly ? cell.firstReturns : cell.totalReturns;
            if (denom > 0) {
                bandCover[i] = static_cast<float>(100.0 * cell.returnsAboveHeightCut / denom);
            }
            bandDensity[i] = static_cast<float>(cell.totalReturns / (res * res));

            if (!opts.noIntensity && !cell.intensities.empty()) {
                double sumInt = std::accumulate(cell.intensities.begin(), cell.intensities.end(), 0.0);
                double meanInt = sumInt / cell.intensities.size();
                bandIntMean[i] = static_cast<float>(meanInt);

                double sqInt = 0.0;
                for (float iv : cell.intensities) {
                    sqInt += (iv - meanInt) * (iv - meanInt);
                }
                bandIntStdDev[i] = static_cast<float>(std::sqrt(sqInt / cell.intensities.size()));
            }
        }

        struct BandDef {
            std::string name;
            const std::vector<float>& data;
        };

        std::vector<BandDef> bandDefs = {
            {"elev_min", bandMin}, {"elev_max", bandMax}, {"elev_mean", bandMean},
            {"elev_stddev", bandStdDev}, {"elev_variance", bandVar}, {"elev_cv", bandCV},
            {"elev_skewness", bandSkew}, {"elev_kurtosis", bandKurt}, {"elev_crr", bandCRR},
            {"elev_mode", bandMode}, {"elev_median", bandMedian}, {"elev_iqr", bandIQR},
            {"elev_p01", bandP01}, {"elev_p05", bandP05}, {"elev_p10", bandP10},
            {"elev_p20", bandP20}, {"elev_p25", bandP25}, {"elev_p30", bandP30},
            {"elev_p40", bandP40}, {"elev_p50", bandP50}, {"elev_p60", bandP60},
            {"elev_p70", bandP70}, {"elev_p75", bandP75}, {"elev_p80", bandP80},
            {"elev_p90", bandP90}, {"elev_p95", bandP95}, {"elev_p99", bandP99},
            {"canopy_cover", bandCover}, {"point_density", bandDensity}
        };

        if (!opts.noIntensity) {
            bandDefs.push_back({"int_mean", bandIntMean});
            bandDefs.push_back({"int_stddev", bandIntStdDev});
        }

        double geotransform[6] = { tile.minX, res, 0.0, tile.maxY, 0.0, -res };
        fusion::raster::GDALRaster raster;
        if (!raster.Create(outTif, cols, rows, static_cast<int>(bandDefs.size()), "Float32", "GTiff", "", geotransform, -9999.0)) {
            return false;
        }

        for (size_t b = 0; b < bandDefs.size(); ++b) {
            raster.SetBandDescription(static_cast<int>(b + 1), bandDefs[b].name);
            raster.WriteBandData(static_cast<int>(b + 1), bandDefs[b].data);
        }

        raster.Close();
        return true;
    };

    bool ok = pipeline.ExecutePipeline(tileTask);
    if (ok) {
        fusion::batch::StatusMessenger::Instance().SendStatus("ltktools batch processing completed successfully.");
    } else {
        fusion::batch::StatusMessenger::Instance().SendStatus("ltktools batch processing encountered errors.");
    }

    return ok ? 0 : 1;
}
