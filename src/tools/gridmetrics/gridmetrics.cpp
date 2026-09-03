// gridmetrics.cpp : Comprehensive GridMetrics Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/cli/ParseUtil.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/batch/BatchPipeline.h"
#include "fusion/batch/StatusMessenger.h"
#include "fusion/metrics/ExperimentalMetrics.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <map>

struct CellAccumulator {
    std::vector<float> elevations;
    std::vector<float> intensities;
    std::vector<fusion::metrics::Point3D> cellPoints;
    int totalReturns{0};
    int firstReturns{0};
    int returnsAboveGround{0};
    int returnsAboveMinHt{0};
    int returnsAboveHeightCut{0};
    std::vector<int> strataCounts;
    std::vector<double> strataIntSums;
    std::vector<int> strataIntCounts;
};

using fusion::cli::ParseFloatList;
using fusion::cli::ParseIntSet;

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

// Per-tile accumulator for batch/tiled mode. Deliberately the smaller, original
// metric set (no experimental metrics, no per-cell CSV) -- batch mode is a direct
// port of what ltktools.exe computed per tile, not an extension of it.
struct TileCellAccumulator {
    std::vector<float> elevations;
    std::vector<float> intensities;
    int totalReturns{0};
    int firstReturns{0};
    int returnsAboveGround{0};
    int returnsAboveMinHt{0};
    int returnsAboveHeightCut{0};
};

// Batch/tiled mode: tiles the input directory of LAS/LAZ files, buffers each tile,
// computes the same core grid metrics per tile in-process (multithreaded via
// BatchPipeline), and mosaics the per-tile rasters into a VRT. This is today's
// ltktools.exe behavior, moved here wholesale -- triggered when the positional
// input argument is a directory rather than a single file.
static int RunBatchTiledMode(fusion::cli::ArgumentParser& parser, const std::filesystem::path& inputDir, const std::filesystem::path& outDir) {
    fusion::batch::TileGridSpec gridSpec;
    if (auto ext = parser.GetOption("extent")) {
        std::stringstream ss(*ext);
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

    gridSpec.resolution = std::stod(parser.GetOption("cellsize").value_or("10.0"));

    fusion::batch::PipelineJobOptions jobOpts;
    jobOpts.inputPointCloudDir = inputDir;
    jobOpts.outputDir = outDir;
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

    fusion::batch::StatusMessenger::Instance().SetLogFile(jobOpts.outputDir / "gridmetrics_batch.log");
    fusion::batch::StatusMessenger::Instance().SendStatus("Initializing gridmetrics batch/tiled pipeline...");

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
        fusion::batch::StatusMessenger::Instance().SendStatus("gridmetrics batch/tiled processing completed successfully.");
    } else {
        fusion::batch::StatusMessenger::Instance().SendStatus("gridmetrics batch/tiled processing encountered errors.");
    }

    return ok ? 0 : 1;
}

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("gridmetrics", "Computes comprehensive canopy elevation and intensity metrics grid from point clouds");
    parser.SetPositionalArgsUsage("<input.las/laz or directory> [optional raster ground path]");
    parser.AddOption("ground", "Path to ground surface DEM raster (GeoTIFF, ENVI, IMG), or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("cellsize", "Output grid cell size in project units", "10.0");
    parser.AddOption("minht", "Minimum height above ground for canopy metrics calculation", "2.0");
    parser.AddOption("heightcut", "Height cutoff threshold for canopy cover calculations (defaults to minht)");
    parser.AddOption("outlier", "Trim elevation outliers outside min,max values (e.g. -5,150)");
    parser.AddOption("class", "Comma-separated point classifications to include (e.g. 2,3,4,5)");
    parser.AddFlag("first", "Use only first returns for metric calculations");
    parser.AddFlag("all", "Use all returns for canopy cover and metric calculations");
    parser.AddFlag("nointensity", "Skip computing intensity metrics");
    parser.AddOption("strata", "Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0)");
    parser.AddOption("intstrata", "Comma-separated intensity strata height thresholds");
    parser.AddOption("voxelsize", "3D voxel resolution for voxel volume metrics (m)", "20.0");
    parser.AddFlag("exp", "Compute additional experimental metrics from RSForTools");
    parser.AddOption("outroot", "Base root name for output CSV summary metrics tables");
    parser.AddOption("outdir", "Output directory for rasters and CSV reports (also the batch/tiled mode output directory)", ".");
    parser.AddOption("output-mode", "Output raster mode: multiband or singleband", "multiband");

    // Batch/tiled mode options (used only when the positional input is a directory --
    // see RunBatchTiledMode above). Ignored in single-file mode.
    parser.AddOption("extent", "Batch/tiled mode: project extent LLX,LLY,URX,URY");
    parser.AddOption("tilesize", "Batch/tiled mode: tile width,height in project units", "1000,1000");
    parser.AddOption("buffer", "Batch/tiled mode: tile buffer distance", "50");
    parser.AddOption("threads", "Batch/tiled mode: number of parallel worker threads", "4");
    parser.AddFlag("vrt", "Batch/tiled mode: generate a GDAL Virtual Raster (.vrt) across tile rasters (on by default)");
    parser.AddFlag("merge", "Batch/tiled mode: merge the VRT into a single global GeoTIFF file");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    std::filesystem::path inputPath = posArgs[0];
    std::filesystem::path outDir = parser.GetOption("outdir").value_or(".");

    if (std::filesystem::is_directory(inputPath)) {
        std::filesystem::create_directories(outDir);
        return RunBatchTiledMode(parser, inputPath, outDir);
    }

    double cellSize = std::stod(parser.GetOption("cellsize").value_or("10.0"));
    double minHt = std::stod(parser.GetOption("minht").value_or("2.0"));
    double heightCut = parser.GetOption("heightcut") ? std::stod(*parser.GetOption("heightcut")) : minHt;
    double voxelSize = std::stod(parser.GetOption("voxelsize").value_or("20.0"));
    bool enableExp = parser.HasFlag("exp");
    std::string outputMode = parser.GetOption("output-mode").value_or("multiband");
    bool firstOnly = parser.HasFlag("first");
    bool noIntensity = parser.HasFlag("nointensity");

    double outlierMin = -99999.0, outlierMax = 99999.0;
    bool hasOutlier = false;
    if (auto outlierOpt = parser.GetOption("outlier")) {
        auto vals = ParseFloatList(*outlierOpt);
        if (vals.size() >= 2) {
            outlierMin = vals[0];
            outlierMax = vals[1];
            hasOutlier = true;
        }
    }

    std::unordered_set<int> validClasses;
    if (auto classOpt = parser.GetOption("class")) {
        validClasses = ParseIntSet(*classOpt);
    }

    std::vector<double> strata = parser.GetOption("strata") ? ParseFloatList(*parser.GetOption("strata")) : std::vector<double>{};
    std::vector<double> intStrata = parser.GetOption("intstrata") ? ParseFloatList(*parser.GetOption("intstrata")) : strata;

    std::filesystem::create_directories(outDir);

    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    std::string groundPathStr;
    if (auto groundPath = parser.GetOption("ground")) {
        groundPathStr = *groundPath;
    } else if (posArgs.size() > 1 && std::filesystem::exists(posArgs[1])) {
        groundPathStr = posArgs[1];
    }
    if (!groundPathStr.empty() && groundRaster.Open(groundPathStr)) {
        hasGround = true;
        std::cout << "[GridMetrics] Loaded ground surface DEM: " << groundPathStr << "\n";
    }

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    fusion::lidar::MergedPointCloudReader lasReader;
    if (!lasReader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open point cloud file(s).\n";
        return 1;
    }

    const auto& header = lasReader.GetHeader();
    std::cout << "[GridMetrics] Processing Point Cloud: " << (inputFiles.size() == 1 ? inputFiles[0].filename().string() : ("merged " + std::to_string(inputFiles.size()) + " files"))
              << " (" << header.pointCount << " points)\n";

    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::vector<CellAccumulator> grid(cols * rows);
    for (auto& cell : grid) {
        if (!strata.empty()) {
            cell.strataCounts.resize(strata.size() + 1, 0);
        }
        if (!intStrata.empty()) {
            cell.strataIntSums.resize(intStrata.size() + 1, 0.0);
            cell.strataIntCounts.resize(intStrata.size() + 1, 0);
        }
    }

    fusion::lidar::PointRecord pt;
    while (lasReader.ReadNextPoint(pt)) {
        if (!validClasses.empty() && validClasses.find(pt.classification) == validClasses.end()) {
            continue;
        }

        if (firstOnly && pt.returnNumber != 1) {
            continue;
        }

        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);

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

            if (enableExp) {
                cell.cellPoints.push_back({pt.x, pt.y, elevation});
            }

            if (elevation >= 0.0) {
                cell.returnsAboveGround++;
            }

            if (elevation >= heightCut) {
                cell.returnsAboveHeightCut++;
            }

            if (!strata.empty()) {
                size_t sIdx = 0;
                while (sIdx < strata.size() && elevation >= strata[sIdx]) {
                    sIdx++;
                }
                cell.strataCounts[sIdx]++;
            }

            if (!noIntensity && !intStrata.empty()) {
                size_t sIdx = 0;
                while (sIdx < intStrata.size() && elevation >= intStrata[sIdx]) {
                    sIdx++;
                }
                cell.strataIntSums[sIdx] += pt.intensity;
                cell.strataIntCounts[sIdx]++;
            }

            if (elevation >= minHt) {
                cell.returnsAboveMinHt++;
                cell.elevations.push_back(static_cast<float>(elevation));
                if (!noIntensity) {
                    cell.intensities.push_back(static_cast<float>(pt.intensity));
                }
            }
        }
    }
    lasReader.Close();

    // Prepare metric output arrays
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

    // Experimental metrics bands setup
    std::vector<std::string> expNames;
    std::map<std::string, std::vector<float>> expBands;
    if (enableExp) {
        expNames = fusion::metrics::GetExperimentalMetricsNames();
        for (const auto& name : expNames) {
            expBands[name] = std::vector<float>(numCells, -9999.0f);
        }
    }

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

        int denom = firstOnly ? cell.firstReturns : cell.totalReturns;
        if (denom > 0) {
            bandCover[i] = static_cast<float>(100.0 * cell.returnsAboveHeightCut / denom);
        }
        bandDensity[i] = static_cast<float>(cell.totalReturns / (cellSize * cellSize));

        if (!noIntensity && !cell.intensities.empty()) {
            double sumInt = std::accumulate(cell.intensities.begin(), cell.intensities.end(), 0.0);
            double meanInt = sumInt / cell.intensities.size();
            bandIntMean[i] = static_cast<float>(meanInt);

            double sqInt = 0.0;
            for (float iv : cell.intensities) {
                sqInt += (iv - meanInt) * (iv - meanInt);
            }
            bandIntStdDev[i] = static_cast<float>(std::sqrt(sqInt / cell.intensities.size()));
        }

        if (enableExp && !cell.cellPoints.empty()) {
            fusion::metrics::ExperimentalMetricsOptions opts;
            opts.minHt = minHt;
            opts.cellSize = cellSize;
            opts.voxelSize = voxelSize;
            auto expRes = fusion::metrics::ComputeExperimentalMetrics(cell.cellPoints, opts);
            auto expMap = fusion::metrics::GetExperimentalMetricsAsMap(expRes);
            for (const auto& name : expNames) {
                expBands[name][i] = static_cast<float>(expMap[name]);
            }
        }
    }

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    std::string stem = (inputFiles.size() == 1) ? inputFiles[0].stem().string() : "merged_gridmetrics";
    if (auto outRoot = parser.GetOption("outroot")) {
        stem = *outRoot;
    }
    std::filesystem::path outRasterPath = outDir / (stem + "_gridmetrics.tif");

    fusion::raster::GDALRaster outRaster;

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

    if (!noIntensity) {
        bandDefs.push_back({"int_mean", bandIntMean});
        bandDefs.push_back({"int_stddev", bandIntStdDev});
    }

    if (enableExp) {
        for (const auto& name : expNames) {
            bandDefs.push_back({"exp_" + name, expBands[name]});
        }
    }

    if (outputMode == "singleband") {
        std::cout << "[GridMetrics] Writing Single-band GeoTIFF rasters to: " << outDir << "...\n";
        for (const auto& bdef : bandDefs) {
            std::filesystem::path bpath = outDir / (stem + "_" + bdef.name + ".tif");
            if (outRaster.Create(bpath, cols, rows, 1, "Float32", "GTiff", "", geotransform, -9999.0)) {
                outRaster.SetBandDescription(1, bdef.name);
                outRaster.WriteBandData(1, bdef.data);
                outRaster.Close();
            }
        }
    } else {
        std::cout << "[GridMetrics] Writing Multi-band GeoTIFF raster to: " << outRasterPath << " ("
                  << bandDefs.size() << " bands)...\n";
        if (outRaster.Create(outRasterPath, cols, rows, static_cast<int>(bandDefs.size()), "Float32", "GTiff", "", geotransform, -9999.0)) {
            for (size_t b = 0; b < bandDefs.size(); ++b) {
                outRaster.SetBandDescription(static_cast<int>(b + 1), bandDefs[b].name);
                outRaster.WriteBandData(static_cast<int>(b + 1), bandDefs[b].data);
            }
            outRaster.Close();
        }
    }

    // Export CSV Summary Report if requested
    std::string outRootStr = parser.GetOption("outroot").value_or(stem);
    std::filesystem::path csvPath = outDir / (outRootStr + "_grid_elevation_metrics.csv");
    std::ofstream csv(csvPath);
    if (csv.is_open()) {
        std::cout << "[GridMetrics] Exporting CSV Elevation Metrics table to: " << csvPath << "...\n";
        csv << "Col,Row,X,Y,TotalReturns,FirstReturns,ElevMin,ElevMax,ElevMean,ElevStdDev,ElevVar,ElevCV,ElevSkew,ElevKurt,ElevCRR,ElevMode,ElevMedian,ElevIQR,"
            << "ElevP01,ElevP05,ElevP10,ElevP20,ElevP25,ElevP30,ElevP40,ElevP50,ElevP60,ElevP70,ElevP75,ElevP80,ElevP90,ElevP95,ElevP99,CanopyCover,PointDensity";
        for (size_t s = 0; s < strata.size(); ++s) {
            csv << ",StrataCnt_" << s;
        }
        if (enableExp) {
            for (const auto& name : expNames) {
                csv << "," << name;
            }
        }
        csv << "\n";

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                size_t idx = r * cols + c;
                const auto& cell = grid[idx];
                double x = header.minX + (c + 0.5) * cellSize;
                double y = header.maxY - (r + 0.5) * cellSize;

                csv << c << "," << r << "," << x << "," << y << ","
                    << cell.totalReturns << "," << cell.firstReturns << ","
                    << bandMin[idx] << "," << bandMax[idx] << "," << bandMean[idx] << ","
                    << bandStdDev[idx] << "," << bandVar[idx] << "," << bandCV[idx] << ","
                    << bandSkew[idx] << "," << bandKurt[idx] << "," << bandCRR[idx] << ","
                    << bandMode[idx] << "," << bandMedian[idx] << "," << bandIQR[idx] << ","
                    << bandP01[idx] << "," << bandP05[idx] << "," << bandP10[idx] << ","
                    << bandP20[idx] << "," << bandP25[idx] << "," << bandP30[idx] << ","
                    << bandP40[idx] << "," << bandP50[idx] << "," << bandP60[idx] << ","
                    << bandP70[idx] << "," << bandP75[idx] << "," << bandP80[idx] << ","
                    << bandP90[idx] << "," << bandP95[idx] << "," << bandP99[idx] << ","
                    << bandCover[idx] << "," << bandDensity[idx];

                for (size_t s = 0; s < cell.strataCounts.size(); ++s) {
                    csv << "," << cell.strataCounts[s];
                }
                if (enableExp) {
                    for (const auto& name : expNames) {
                        csv << "," << expBands[name][idx];
                    }
                }
                csv << "\n";
            }
        }
        csv.close();
    }

    std::cout << "[GridMetrics] Grid metrics processing completed successfully.\n";
    return 0;
}

