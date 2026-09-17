// gridmetrics.cpp : Comprehensive GridMetrics Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/cli/ParseUtil.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/PointFilter.h"
#include "fusion/batch/BatchPipeline.h"
#include "fusion/batch/StatusMessenger.h"
#include "fusion/metrics/ExperimentalMetrics.h"
#include "fusion/metrics/SurfaceStats.h"
#include "fusion/metrics/SentinelPolicy.h"
#include "fusion/metrics/PointCloudStats.h"
#include "fusion/metrics/SpectralChannels.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <map>
#include <iomanip>
#include <array>

struct CellAccumulator {
    std::vector<float> elevations;
    std::vector<float> intensities;
    std::vector<fusion::metrics::Point3D> cellPoints;
    int totalReturns{0};
    int firstReturns{0};
    int returnsAboveGround{0};
    int returnsAboveMinHt{0};
    int returnsAboveHeightCut{0};
    // item 8: per-return-number counts (index 0..7 = return number 1..8,
    // index 8 = "9 or higher") and cover-variant support counts, all
    // computed directly from the unfiltered return set.
    std::array<int, 9> returnNumberCounts{};
    int returnsAboveMean{0};
    int returnsAboveMode{0};
    // One bucket per /strata threshold, plus one "above the last threshold"
    // bucket (item 6: full per-stratum elevation values, not just a count --
    // ComputePointStatBundle runs per non-empty bucket).
    std::vector<std::vector<float>> strataElevations;
    std::vector<double> strataIntSums;
    std::vector<int> strataIntCounts;
    // /rgb: (item 7) -- one entry per selected, format-carried spectral
    // channel ("red","green","blue","nir"), populated in lockstep with
    // elevations/intensities (same elevation >= /minht gate).
    std::map<std::string, std::vector<float>> spectralValues;
    // /rgbstrata (new): per-channel spectral values bucketed by elevation
    // stratum -- keyed by channel prefix (a run may select any subset),
    // each holding one vector per /strata bucket, populated unconditionally
    // alongside strataElevations (not gated by /minht).
    std::map<std::string, std::vector<std::vector<float>>> strataSpectralValues;
};

using fusion::cli::ParseFloatList;

// CSV output writes the literal text "NA" wherever a value resolved to an
// NA sentinel, not the numeric NaN (Shared groundwork C) -- real computed
// statistics in this domain are never themselves NaN, so a NaN value
// uniquely identifies "this came from an NA-resolved /nodata or /noheight",
// regardless of which of the two produced it.
static void WriteCSVFloat(std::ofstream& csv, float value) {
    if (std::isnan(value)) {
        csv << "NA";
    } else {
        csv << value;
    }
}

// Used only by RunBatchTiledMode below -- single-file mode's per-cell loop
// in main() calls fusion::metrics::ComputePointStatBundle instead (item 5),
// which owns an equivalent internal copy of this same logic. Batch/tiled
// mode intentionally keeps the smaller, original metric set (see
// TileCellAccumulator's comment), so it isn't switched over too.
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
static int RunBatchTiledMode(fusion::cli::ArgumentParser& parser, const std::filesystem::path& inputDir,
                              const std::filesystem::path& outDir, const fusion::lidar::PointFilter& pointFilter) {
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

    fusion::metrics::SentinelPolicy batchSentinel;
    batchSentinel.nodata = fusion::metrics::ParseSentinelOption(parser.GetOption("nodata").value_or("NA"));
    batchSentinel.noheight = fusion::metrics::ParseSentinelOption(parser.GetOption("noheight").value_or("0"));
    jobOpts.nodataValue = batchSentinel.nodata.value;
    jobOpts.noheightValue = batchSentinel.noheight.value;

    fusion::batch::StatusMessenger::Instance().SetLogFile(jobOpts.outputDir / "gridmetrics_batch.log");
    fusion::batch::StatusMessenger::Instance().SendStatus("Initializing gridmetrics batch/tiled pipeline...");

    fusion::batch::BatchPipeline pipeline(gridSpec, jobOpts);

    auto tileTask = [gridSpec, pointFilter](const fusion::batch::TileInfo& tile, const fusion::batch::PipelineJobOptions& opts) -> bool {
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

                    // COPC files (see LASReader::IsCOPC) seek straight to
                    // the chunks overlapping this tile's buffered extent
                    // instead of reading every point sequentially -- a
                    // plain LAS/LAZ file falls back to that same sequential
                    // read internally, so this call is correct either way.
                    reader.ReadPointsInExtent(tile.bufferedMinX, tile.bufferedMinY, tile.bufferedMaxX, tile.bufferedMaxY,
                                               [&](const fusion::lidar::PointRecord& pt) {
                        if (!pointFilter.Keep(pt)) {
                            return;
                        }

                        if (opts.firstOnly && pt.returnNumber != 1) {
                            return;
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
                                return;
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
                    });
                    reader.Close();
                }
            }
        }

        size_t numCells = cols * rows;
        float ND = opts.nodataValue;
        std::vector<float> bandMin(numCells, ND), bandMax(numCells, ND);
        std::vector<float> bandMean(numCells, ND), bandStdDev(numCells, ND);
        std::vector<float> bandVar(numCells, ND), bandCV(numCells, ND);
        std::vector<float> bandSkew(numCells, ND), bandKurt(numCells, ND);
        std::vector<float> bandCRR(numCells, ND), bandMode(numCells, ND);
        std::vector<float> bandMedian(numCells, ND), bandIQR(numCells, ND);
        std::vector<float> bandP01(numCells, ND), bandP05(numCells, ND);
        std::vector<float> bandP10(numCells, ND), bandP20(numCells, ND);
        std::vector<float> bandP25(numCells, ND), bandP30(numCells, ND);
        std::vector<float> bandP40(numCells, ND), bandP50(numCells, ND);
        std::vector<float> bandP60(numCells, ND), bandP70(numCells, ND);
        std::vector<float> bandP75(numCells, ND), bandP80(numCells, ND);
        std::vector<float> bandP90(numCells, ND), bandP95(numCells, ND);
        std::vector<float> bandP99(numCells, ND);
        std::vector<float> bandCover(numCells, ND), bandDensity(numCells, ND);
        std::vector<float> bandIntMean(numCells, ND), bandIntStdDev(numCells, ND);

        for (size_t i = 0; i < numCells; ++i) {
            auto& cell = grid[i];
            if (cell.totalReturns == 0) {
                continue;
            }
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
            } else {
                float NH = opts.noheightValue;
                bandMin[i] = bandMax[i] = bandMean[i] = bandStdDev[i] = NH;
                bandVar[i] = bandCV[i] = bandSkew[i] = bandKurt[i] = bandCRR[i] = NH;
                bandMode[i] = bandMedian[i] = bandIQR[i] = NH;
                bandP01[i] = bandP05[i] = bandP10[i] = bandP20[i] = bandP25[i] = NH;
                bandP30[i] = bandP40[i] = bandP50[i] = bandP60[i] = bandP70[i] = NH;
                bandP75[i] = bandP80[i] = bandP90[i] = bandP95[i] = bandP99[i] = NH;
            }

            int denom = opts.firstOnly ? cell.firstReturns : cell.totalReturns;
            if (denom > 0) {
                bandCover[i] = static_cast<float>(100.0 * cell.returnsAboveHeightCut / denom);
            }
            bandDensity[i] = static_cast<float>(cell.totalReturns / (res * res));

            if (!opts.noIntensity) {
                if (!cell.intensities.empty()) {
                    double sumInt = std::accumulate(cell.intensities.begin(), cell.intensities.end(), 0.0);
                    double meanInt = sumInt / cell.intensities.size();
                    bandIntMean[i] = static_cast<float>(meanInt);

                    double sqInt = 0.0;
                    for (float iv : cell.intensities) {
                        sqInt += (iv - meanInt) * (iv - meanInt);
                    }
                    bandIntStdDev[i] = static_cast<float>(std::sqrt(sqInt / cell.intensities.size()));
                } else {
                    bandIntMean[i] = bandIntStdDev[i] = opts.noheightValue;
                }
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
    fusion::lidar::PointFilter::RegisterOptions(parser);
    parser.AddFlag("first", "Use only first returns for metric calculations");
    parser.AddFlag("all", "Use all returns for canopy cover and metric calculations");
    parser.AddFlag("nointensity", "Skip computing intensity metrics");
    parser.AddOption("strata", "Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0)");
    parser.AddOption("intstrata", "Comma-separated intensity strata height thresholds");
    parser.AddFlag("strataraster", "With /strata, also append one return-density band per stratum bucket to the multiband output (density_stratum_NN)");
    parser.AddFlag("rgbstrata", "With /rgb and /strata both set, also append a mean/stddev/min/max band set (and matching CSV columns) per selected spectral channel within each height-stratum bucket (<channel>_stratum_NN_*).");
    parser.AddOption("nodata", "Value for cells with zero returns at all: NA, or a number such as 0, -9999, or inf", "NA");
    parser.AddOption("noheight", "Value for height-dependent bands (elev_*, int_*) when a cell has returns but none clear the height cutoff: NA, or a number such as 0, -9999, or inf", "0");
    parser.AddOption("rgb", "Comma-separated spectral channels to compute a statistic bundle for: R, G, B, N, or all (every channel the input file's LAS point format actually carries)");
    parser.AddOption("voxelsize", "3D voxel resolution for voxel volume metrics (m)", "20.0");
    parser.AddFlag("exp", "Compute additional experimental metrics from RSForTools");
    parser.AddFlag("surfstats", "Compute surface_area_ratio and roughness bands from the per-cell elevation grid (see /surfstats-source)");
    parser.AddOption("surfstats-source", "Elevation source for /surfstats: max (typical CHM top-surface use) or mean (ground-DTM-style runs)", "max");
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
    fusion::lidar::PointFilter pointFilter = fusion::lidar::PointFilter::FromParser(parser);

    if (std::filesystem::is_directory(inputPath)) {
        std::filesystem::create_directories(outDir);
        return RunBatchTiledMode(parser, inputPath, outDir, pointFilter);
    }

    double cellSize = std::stod(parser.GetOption("cellsize").value_or("10.0"));
    double minHt = std::stod(parser.GetOption("minht").value_or("2.0"));
    double heightCut = parser.GetOption("heightcut") ? std::stod(*parser.GetOption("heightcut")) : minHt;
    double voxelSize = std::stod(parser.GetOption("voxelsize").value_or("20.0"));
    bool enableExp = parser.HasFlag("exp");
    bool enableSurfStats = parser.HasFlag("surfstats");
    std::string surfStatsSource = parser.GetOption("surfstats-source").value_or("max");
    bool enableStrataRaster = parser.HasFlag("strataraster");

    fusion::metrics::SentinelPolicy sentinel;
    sentinel.nodata = fusion::metrics::ParseSentinelOption(parser.GetOption("nodata").value_or("NA"));
    sentinel.noheight = fusion::metrics::ParseSentinelOption(parser.GetOption("noheight").value_or("0"));
    fusion::metrics::RasterNoDataResolution rasterNoData = fusion::metrics::ResolveRasterNoData(
        sentinel, parser.WasExplicit("nodata"), parser.WasExplicit("noheight"));
    if (rasterNoData.conflict) {
        std::cerr << "Warning: /nodata and /noheight were both set to different, non-NA values -- "
                     "GDAL supports only one registered NoData value per raster. Using /nodata's value ("
                  << rasterNoData.value << ") as the file's registered NoData; /noheight's value will be "
                     "written as an ordinary, valid pixel, not flagged as NoData by the GeoTIFF header.\n";
    }
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

    // /rgb: -- a requested channel absent from the file's point format is a
    // printed warning, never a hard failure, since a batch run over
    // mixed-format tiles is a real, expected case.
    fusion::metrics::SpectralChannelSelection spectralSelection;
    if (auto rgbOpt = parser.GetOption("rgb")) {
        spectralSelection = fusion::metrics::ParseSpectralChannels(*rgbOpt, header.pointFormat);
        for (const auto& token : spectralSelection.unknownTokens) {
            std::cerr << "Warning: /rgb channel '" << token << "' is not available for LAS point format "
                      << static_cast<int>(header.pointFormat) << " -- skipped.\n";
        }
    }

    bool enableRgbStrata = parser.HasFlag("rgbstrata");
    if (enableRgbStrata && (strata.empty() || spectralSelection.channels.empty())) {
        std::cerr << "Warning: /rgbstrata requires both /strata and /rgb to be set -- ignored.\n";
        enableRgbStrata = false;
    }

    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::vector<CellAccumulator> grid(cols * rows);
    for (auto& cell : grid) {
        if (!strata.empty()) {
            cell.strataElevations.resize(strata.size() + 1);
            if (enableRgbStrata) {
                for (const auto& spec : spectralSelection.channels) {
                    cell.strataSpectralValues[spec.prefix].resize(strata.size() + 1);
                }
            }
        }
        if (!intStrata.empty()) {
            cell.strataIntSums.resize(intStrata.size() + 1, 0.0);
            cell.strataIntCounts.resize(intStrata.size() + 1, 0);
        }
    }

    fusion::lidar::PointRecord pt;
    while (lasReader.ReadNextPoint(pt)) {
        if (!pointFilter.Keep(pt)) {
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
            {
                int rnIdx = (pt.returnNumber >= 1 && pt.returnNumber <= 8) ? (pt.returnNumber - 1) : 8;
                cell.returnNumberCounts[rnIdx]++;
            }

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
                size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, strata);
                cell.strataElevations[sIdx].push_back(static_cast<float>(elevation));
                if (enableRgbStrata) {
                    for (const auto& spec : spectralSelection.channels) {
                        cell.strataSpectralValues[spec.prefix][sIdx].push_back(static_cast<float>(pt.*(spec.field)));
                    }
                }
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
                for (const auto& spec : spectralSelection.channels) {
                    cell.spectralValues[spec.prefix].push_back(static_cast<float>(pt.*(spec.field)));
                }
            }
        }
    }
    lasReader.Close();

    // Prepare metric output arrays. Every band defaults to the resolved
    // /nodata value -- a cell that never enters any of the branches below
    // (cell.totalReturns == 0) simply keeps this default, satisfying the
    // "zero total points -> nodata everywhere, including counts and
    // density" rule without extra code at each band.
    size_t numCells = cols * rows;
    float ND = sentinel.nodata.value;
    std::vector<float> bandCover(numCells, ND), bandDensity(numCells, ND);

    // item 8: per-return-number counts (r1count..r9count) and cover-variant
    // bands, all computed directly from the unfiltered return set -- so
    // they follow the /nodata-when-cell-empty rule only, never /noheight.
    std::vector<std::vector<float>> returnNumberBands(9, std::vector<float>(numCells, ND));
    std::vector<float> bandAllCover(numCells, ND), bandAfCover(numCells, ND);
    std::vector<float> bandAllAboveMean(numCells, ND), bandAllAboveMode(numCells, ND);
    std::vector<float> bandAfAboveMean(numCells, ND), bandAfAboveMode(numCells, ND);

    // Full elevation/intensity statistic bundle (item 5) -- one band per
    // PointStatBundle column, keyed by the same column-name list used for
    // both the raster bandDefs and the CSV header so they can't drift out
    // of sync. elevProfileArea is the one legacy column PointStatBundle
    // doesn't cover (it needs the full percentile curve, not a single
    // value-vector reduction).
    std::vector<std::string> elevColNames = fusion::metrics::PointStatBundleColumnNames("elev_");
    std::vector<std::string> intColNames = fusion::metrics::PointStatBundleColumnNames("int_");
    std::vector<std::vector<float>> elevBands(elevColNames.size(), std::vector<float>(numCells, ND));
    std::vector<std::vector<float>> intBands(intColNames.size(), std::vector<float>(numCells, ND));
    std::vector<float> elevProfileArea(numCells, ND);

    // Raw (pre-sentinel-substitution) top-of-cell elevation, used only to
    // feed /surfstats -- kept separate from the elev_max/elev_mean bands
    // above so ComputeSurfaceStatsGrid's single-noData-value contract isn't
    // confused by two different sentinel values (nodata for an empty cell,
    // noheight for a below-cutoff one) both possibly appearing in the same
    // array. kSurfNoValue is a private, reliably `==`-comparable marker
    // (unlike NaN, which policy.nodata.value may well be) never exposed in
    // final output.
    constexpr float kSurfNoValue = -3.0e38f;
    std::vector<float> surfSourceMax(enableSurfStats ? numCells : 0, kSurfNoValue);
    std::vector<float> surfSourceMean(enableSurfStats ? numCells : 0, kSurfNoValue);

    // /rgb: (item 7) -- same call shape as elev_/int_ above, looped over
    // the selected channel list instead of called once. Keyed by prefix
    // ("red","green","blue","nir") since a run may select any subset.
    std::vector<std::string> spectralChannelPrefixes;
    std::map<std::string, std::vector<std::string>> spectralColNames;
    std::map<std::string, std::vector<std::vector<float>>> spectralBands;
    for (const auto& spec : spectralSelection.channels) {
        spectralChannelPrefixes.push_back(spec.prefix);
        auto colNames = fusion::metrics::PointStatBundleColumnNames(spec.prefix + "_");
        spectralBands[spec.prefix] = std::vector<std::vector<float>>(colNames.size(), std::vector<float>(numCells, ND));
        spectralColNames[spec.prefix] = std::move(colNames);
    }

    // Experimental metrics bands setup
    std::vector<std::string> expNames;
    std::map<std::string, std::vector<float>> expBands;
    if (enableExp) {
        expNames = fusion::metrics::GetExperimentalMetricsNames();
        for (const auto& name : expNames) {
            expBands[name] = std::vector<float>(numCells, ND);
        }
    }

    for (size_t i = 0; i < numCells; ++i) {
        auto& cell = grid[i];
        if (cell.totalReturns == 0) {
            // Nothing landed in this cell at all -- every band above
            // already defaults to /nodata, so there's nothing further to do.
            continue;
        }

        if (!cell.elevations.empty()) {
            fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(cell.elevations);
            std::vector<float> vals = fusion::metrics::PointStatBundleAsVector(bundle);
            for (size_t k = 0; k < vals.size(); ++k) {
                elevBands[k][i] = vals[k];
            }
            elevProfileArea[i] = fusion::metrics::ComputeProfileArea(bundle);

            if (enableSurfStats) {
                surfSourceMax[i] = bundle.max;
                surfSourceMean[i] = bundle.mean;
            }

            // item 8 cover-variant support: how many of this cell's
            // height-filtered elevations (the only per-point elevations
            // retained) clear the cell's own mean/mode.
            for (float v : cell.elevations) {
                if (v > bundle.mean) cell.returnsAboveMean++;
                if (v > bundle.mode) cell.returnsAboveMode++;
            }
        } else {
            // Points landed in this cell, but none cleared /minht -- every
            // elev_* band is a real "no canopy height data" answer, not a
            // "nothing here" one, so it gets /noheight rather than /nodata.
            float NH = sentinel.noheight.value;
            for (auto& band : elevBands) {
                band[i] = NH;
            }
            elevProfileArea[i] = NH;
        }

        int denom = firstOnly ? cell.firstReturns : cell.totalReturns;
        if (denom > 0) {
            bandCover[i] = static_cast<float>(100.0 * cell.returnsAboveHeightCut / denom);
        }
        bandDensity[i] = static_cast<float>(cell.totalReturns / (cellSize * cellSize));

        for (size_t rn = 0; rn < returnNumberBands.size(); ++rn) {
            returnNumberBands[rn][i] = static_cast<float>(cell.returnNumberCounts[rn]);
        }
        bandAllCover[i] = static_cast<float>(100.0 * cell.returnsAboveHeightCut / cell.totalReturns);
        if (cell.firstReturns > 0) {
            bandAfCover[i] = static_cast<float>(100.0 * cell.returnsAboveHeightCut / cell.firstReturns);
        }
        bandAllAboveMean[i] = static_cast<float>(100.0 * cell.returnsAboveMean / cell.totalReturns);
        bandAllAboveMode[i] = static_cast<float>(100.0 * cell.returnsAboveMode / cell.totalReturns);
        if (cell.firstReturns > 0) {
            bandAfAboveMean[i] = static_cast<float>(100.0 * cell.returnsAboveMean / cell.firstReturns);
            bandAfAboveMode[i] = static_cast<float>(100.0 * cell.returnsAboveMode / cell.firstReturns);
        }

        if (!noIntensity) {
            if (!cell.intensities.empty()) {
                fusion::metrics::PointStatBundle intBundle = fusion::metrics::ComputePointStatBundle(cell.intensities);
                std::vector<float> intVals = fusion::metrics::PointStatBundleAsVector(intBundle);
                for (size_t k = 0; k < intVals.size(); ++k) {
                    intBands[k][i] = intVals[k];
                }
            } else {
                float NH = sentinel.noheight.value;
                for (auto& band : intBands) {
                    band[i] = NH;
                }
            }
        }

        for (const auto& prefix : spectralChannelPrefixes) {
            auto valIt = cell.spectralValues.find(prefix);
            auto& bands = spectralBands[prefix];
            if (valIt != cell.spectralValues.end() && !valIt->second.empty()) {
                fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(valIt->second);
                std::vector<float> vals = fusion::metrics::PointStatBundleAsVector(bundle);
                for (size_t k = 0; k < vals.size(); ++k) {
                    bands[k][i] = vals[k];
                }
            } else {
                float NH = sentinel.noheight.value;
                for (auto& band : bands) {
                    band[i] = NH;
                }
            }
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

    // /surfstats: derive surface_area_ratio/roughness from the per-cell
    // elevation grid gridmetrics already holds in memory (surfSourceMax or
    // surfSourceMean) -- no second raster round-trip. surface_area_ratio
    // and roughness are never subject to /noheight (Shared groundwork C) --
    // a cell without a computed top-surface elevation (whether because it's
    // fully empty or because no points cleared /minht) simply has no basis
    // for a surface-stats value either, so both cases collapse to /nodata
    // in the final output; kSurfNoValue exists only so
    // ComputeSurfaceStatsGrid's single-noData contract isn't confused by
    // /nodata potentially being NaN (not reliably `==`-comparable to itself).
    std::vector<float> surfStatsAreaRatio, surfStatsRoughness;
    if (enableSurfStats) {
        const std::vector<float>& sourceElev = (surfStatsSource == "mean") ? surfSourceMean : surfSourceMax;
        fusion::metrics::SurfaceStatsGrid surfGrid = fusion::metrics::ComputeSurfaceStatsGrid(
            sourceElev, cols, rows, cellSize, kSurfNoValue);
        surfStatsAreaRatio = std::move(surfGrid.surfaceAreaRatio);
        surfStatsRoughness = std::move(surfGrid.roughness);
        for (size_t i = 0; i < numCells; ++i) {
            if (surfStatsAreaRatio[i] == kSurfNoValue) surfStatsAreaRatio[i] = ND;
            if (surfStatsRoughness[i] == kSurfNoValue) surfStatsRoughness[i] = ND;
        }
    }

    // /strataraster: gridmetrics already buckets points' elevations into
    // cell.strataElevations whenever /strata is given (see the point loop
    // above) but only ever wrote counts to CSV -- append, per stratum
    // bucket: a return-density band (item 3, same as densitymetrics' own
    // stratum bands), a count band, a proportion band, and the simplified
    // 4-field mean/stddev/min/max set (count and proportion are already
    // their own bands, so StrataStatBundle's own copies of those two fields
    // aren't repeated here). A stratum bucket with zero points in an
    // otherwise non-empty cell gets /noheight for its stat columns and a
    // real (not sentinel) 0 for its own count/proportion, consistent with
    // the cover-metric precedent in Shared groundwork C; density bands
    // follow the same rule as item 3. Only built when /strataraster is set.
    static const std::vector<std::string> kStrataSimpleStatNames = {"mean", "stddev", "min", "max"};
    std::vector<std::vector<float>> strataDensityBands, strataCountBands, strataPropBands;
    std::vector<std::vector<std::vector<float>>> strataStatBands; // [bucket][statIdx(mean,stddev,min,max)][cell]
    if (enableStrataRaster && !strata.empty()) {
        size_t numStrataBuckets = strata.size() + 1;
        double cellArea = cellSize * cellSize;
        strataDensityBands.assign(numStrataBuckets, std::vector<float>(numCells, ND));
        strataCountBands.assign(numStrataBuckets, std::vector<float>(numCells, ND));
        strataPropBands.assign(numStrataBuckets, std::vector<float>(numCells, ND));
        strataStatBands.assign(numStrataBuckets, std::vector<std::vector<float>>(
            kStrataSimpleStatNames.size(), std::vector<float>(numCells, ND)));

        for (size_t i = 0; i < numCells; ++i) {
            const auto& cell = grid[i];
            if (cell.totalReturns == 0) continue;
            for (size_t s = 0; s < numStrataBuckets; ++s) {
                std::vector<double> bucketElevD(cell.strataElevations[s].begin(), cell.strataElevations[s].end());
                fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketElevD, cell.totalReturns);
                strataDensityBands[s][i] = static_cast<float>(b.count / cellArea);
                strataCountBands[s][i] = static_cast<float>(b.count);
                strataPropBands[s][i] = static_cast<float>(b.proportion);
                if (b.count > 0) {
                    strataStatBands[s][0][i] = b.mean;
                    strataStatBands[s][1][i] = b.stddev;
                    strataStatBands[s][2][i] = b.min;
                    strataStatBands[s][3][i] = b.max;
                } else {
                    for (auto& statBand : strataStatBands[s]) {
                        statBand[i] = sentinel.noheight.value;
                    }
                }
            }
        }
    }

    // /rgbstrata: mean/stddev/min/max per selected spectral channel within
    // each height-stratum bucket -- [channel][bucket][statIdx][cell].
    std::vector<std::string> rgbStrataChannelPrefixes;
    std::map<std::string, std::vector<std::vector<float>>> rgbStrataBands; // [prefix][bucket*4 + statIdx][cell]
    if (enableRgbStrata) {
        size_t numStrataBuckets = strata.size() + 1;
        for (const auto& spec : spectralSelection.channels) {
            rgbStrataChannelPrefixes.push_back(spec.prefix);
            rgbStrataBands[spec.prefix] = std::vector<std::vector<float>>(
                numStrataBuckets * kStrataSimpleStatNames.size(), std::vector<float>(numCells, ND));
        }

        for (size_t i = 0; i < numCells; ++i) {
            const auto& cell = grid[i];
            if (cell.totalReturns == 0) continue;
            for (const auto& prefix : rgbStrataChannelPrefixes) {
                auto& bands = rgbStrataBands[prefix];
                auto valIt = cell.strataSpectralValues.find(prefix);
                for (size_t s = 0; s < numStrataBuckets; ++s) {
                    std::vector<double> bucketValsD;
                    if (valIt != cell.strataSpectralValues.end()) {
                        const auto& bucketVals = valIt->second[s];
                        bucketValsD.assign(bucketVals.begin(), bucketVals.end());
                    }
                    fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketValsD, bucketValsD.size());
                    size_t base = s * kStrataSimpleStatNames.size();
                    if (b.count > 0) {
                        bands[base + 0][i] = b.mean;
                        bands[base + 1][i] = b.stddev;
                        bands[base + 2][i] = b.min;
                        bands[base + 3][i] = b.max;
                    } else {
                        for (size_t k = 0; k < kStrataSimpleStatNames.size(); ++k) {
                            bands[base + k][i] = sentinel.noheight.value;
                        }
                    }
                }
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

    std::vector<BandDef> bandDefs;
    for (size_t k = 0; k < elevColNames.size(); ++k) {
        bandDefs.push_back({elevColNames[k], elevBands[k]});
    }
    bandDefs.push_back({"elev_profile_area", elevProfileArea});
    bandDefs.push_back({"canopy_cover", bandCover});
    bandDefs.push_back({"point_density", bandDensity});
    for (size_t rn = 0; rn < returnNumberBands.size(); ++rn) {
        bandDefs.push_back({"r" + std::to_string(rn + 1) + "count", returnNumberBands[rn]});
    }
    bandDefs.push_back({"allcover", bandAllCover});
    bandDefs.push_back({"afcover", bandAfCover});
    bandDefs.push_back({"allabovemean", bandAllAboveMean});
    bandDefs.push_back({"allabovemode", bandAllAboveMode});
    bandDefs.push_back({"afabovemean", bandAfAboveMean});
    bandDefs.push_back({"afabovemode", bandAfAboveMode});

    if (!noIntensity) {
        for (size_t k = 0; k < intColNames.size(); ++k) {
            bandDefs.push_back({intColNames[k], intBands[k]});
        }
    }

    for (const auto& prefix : spectralChannelPrefixes) {
        const auto& colNames = spectralColNames[prefix];
        const auto& bands = spectralBands[prefix];
        for (size_t k = 0; k < colNames.size(); ++k) {
            bandDefs.push_back({colNames[k], bands[k]});
        }
    }

    if (enableSurfStats) {
        bandDefs.push_back({"surface_area_ratio", surfStatsAreaRatio});
        bandDefs.push_back({"roughness", surfStatsRoughness});
    }

    if (enableStrataRaster && !strata.empty()) {
        for (size_t s = 0; s < strataDensityBands.size(); ++s) {
            std::ostringstream stratumLabel;
            stratumLabel << std::setw(2) << std::setfill('0') << s;
            std::string prefix = "stratum_" + stratumLabel.str() + "_";

            bandDefs.push_back({"density_stratum_" + stratumLabel.str(), strataDensityBands[s]});
            bandDefs.push_back({prefix + "count", strataCountBands[s]});
            bandDefs.push_back({prefix + "proportion", strataPropBands[s]});
            for (size_t k = 0; k < kStrataSimpleStatNames.size(); ++k) {
                bandDefs.push_back({prefix + kStrataSimpleStatNames[k], strataStatBands[s][k]});
            }
        }
    }

    if (enableRgbStrata) {
        size_t numStrataBuckets = strata.size() + 1;
        for (const auto& prefix : rgbStrataChannelPrefixes) {
            const auto& bands = rgbStrataBands[prefix];
            for (size_t s = 0; s < numStrataBuckets; ++s) {
                std::ostringstream stratumLabel;
                stratumLabel << std::setw(2) << std::setfill('0') << s;
                std::string bandPrefix = prefix + "_stratum_" + stratumLabel.str() + "_";
                size_t base = s * kStrataSimpleStatNames.size();
                for (size_t k = 0; k < kStrataSimpleStatNames.size(); ++k) {
                    bandDefs.push_back({bandPrefix + kStrataSimpleStatNames[k], bands[base + k]});
                }
            }
        }
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
            if (outRaster.Create(bpath, cols, rows, 1, "Float32", "GTiff", "", geotransform, rasterNoData.value)) {
                outRaster.SetBandDescription(1, bdef.name);
                outRaster.WriteBandData(1, bdef.data);
                outRaster.Close();
            }
        }
    } else {
        std::cout << "[GridMetrics] Writing Multi-band GeoTIFF raster to: " << outRasterPath << " ("
                  << bandDefs.size() << " bands)...\n";
        if (outRaster.Create(outRasterPath, cols, rows, static_cast<int>(bandDefs.size()), "Float32", "GTiff", "", geotransform, rasterNoData.value)) {
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
        csv << "Col,Row,X,Y,TotalReturns,FirstReturns";
        for (const auto& name : elevColNames) {
            csv << "," << name;
        }
        csv << ",elev_profile_area,CanopyCover,PointDensity";
        for (int rn = 1; rn <= 9; ++rn) {
            csv << ",r" << rn << "count";
        }
        csv << ",allcover,afcover,allabovemean,allabovemode,afabovemean,afabovemode";
        if (!noIntensity) {
            for (const auto& name : intColNames) {
                csv << "," << name;
            }
        }
        for (const auto& prefix : spectralChannelPrefixes) {
            for (const auto& name : spectralColNames[prefix]) {
                csv << "," << name;
            }
        }
        // strata.size() + 1 buckets, not strata.size() -- AssignStratumIndex
        // (and the resize a few hundred lines above) reserve one extra
        // "above the last threshold" bucket, and the per-row loop below
        // already writes cell.strataElevations.size() buckets' worth of
        // columns; a header short by one bucket here would silently
        // misalign every downstream column.
        for (size_t s = 0; !strata.empty() && s < strata.size() + 1; ++s) {
            std::ostringstream stratumLabel;
            stratumLabel << std::setw(2) << std::setfill('0') << s;
            std::string prefix = "stratum_" + stratumLabel.str() + "_";
            for (const auto& name : fusion::metrics::StrataStatBundleColumnNames(prefix)) {
                csv << "," << name;
            }
        }
        if (enableRgbStrata) {
            for (const auto& prefix : rgbStrataChannelPrefixes) {
                for (size_t s = 0; s < strata.size() + 1; ++s) {
                    std::ostringstream stratumLabel;
                    stratumLabel << std::setw(2) << std::setfill('0') << s;
                    std::string bandPrefix = prefix + "_stratum_" + stratumLabel.str() + "_";
                    csv << "," << bandPrefix << "mean," << bandPrefix << "stddev,"
                        << bandPrefix << "min," << bandPrefix << "max";
                }
            }
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
                bool cellEmpty = (cell.totalReturns == 0); // /nodata applies to every column, including counts

                csv << c << "," << r << "," << x << "," << y << ",";
                if (cellEmpty) {
                    csv << "NA,NA,";
                } else {
                    csv << cell.totalReturns << "," << cell.firstReturns << ",";
                }
                for (const auto& band : elevBands) {
                    WriteCSVFloat(csv, band[idx]); csv << ",";
                }
                WriteCSVFloat(csv, elevProfileArea[idx]); csv << ",";
                WriteCSVFloat(csv, bandCover[idx]); csv << ",";
                WriteCSVFloat(csv, bandDensity[idx]);
                for (const auto& band : returnNumberBands) {
                    csv << ",";
                    WriteCSVFloat(csv, band[idx]);
                }
                csv << ",";
                WriteCSVFloat(csv, bandAllCover[idx]); csv << ",";
                WriteCSVFloat(csv, bandAfCover[idx]); csv << ",";
                WriteCSVFloat(csv, bandAllAboveMean[idx]); csv << ",";
                WriteCSVFloat(csv, bandAllAboveMode[idx]); csv << ",";
                WriteCSVFloat(csv, bandAfAboveMean[idx]); csv << ",";
                WriteCSVFloat(csv, bandAfAboveMode[idx]);
                if (!noIntensity) {
                    for (const auto& band : intBands) {
                        csv << ",";
                        WriteCSVFloat(csv, band[idx]);
                    }
                }
                for (const auto& prefix : spectralChannelPrefixes) {
                    for (const auto& band : spectralBands[prefix]) {
                        csv << ",";
                        WriteCSVFloat(csv, band[idx]);
                    }
                }

                for (size_t s = 0; s < cell.strataElevations.size(); ++s) {
                    if (cellEmpty) {
                        csv << ",NA,NA,NA,NA,NA,NA";
                        continue;
                    }
                    std::vector<double> bucketElevD(cell.strataElevations[s].begin(), cell.strataElevations[s].end());
                    fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketElevD, cell.totalReturns);
                    csv << "," << b.count << ",";
                    WriteCSVFloat(csv, static_cast<float>(b.proportion));
                    if (b.count > 0) {
                        csv << ","; WriteCSVFloat(csv, b.mean);
                        csv << ","; WriteCSVFloat(csv, b.stddev);
                        csv << ","; WriteCSVFloat(csv, b.min);
                        csv << ","; WriteCSVFloat(csv, b.max);
                    } else {
                        for (int k = 0; k < 4; ++k) {
                            csv << ",";
                            WriteCSVFloat(csv, sentinel.noheight.value);
                        }
                    }
                }
                if (enableRgbStrata) {
                    for (const auto& prefix : rgbStrataChannelPrefixes) {
                        auto valIt = cell.strataSpectralValues.find(prefix);
                        for (size_t s = 0; s < strata.size() + 1; ++s) {
                            if (cellEmpty) {
                                csv << ",NA,NA,NA,NA";
                                continue;
                            }
                            std::vector<double> bucketValsD;
                            if (valIt != cell.strataSpectralValues.end()) {
                                const auto& bucketVals = valIt->second[s];
                                bucketValsD.assign(bucketVals.begin(), bucketVals.end());
                            }
                            fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketValsD, bucketValsD.size());
                            if (b.count > 0) {
                                csv << ","; WriteCSVFloat(csv, b.mean);
                                csv << ","; WriteCSVFloat(csv, b.stddev);
                                csv << ","; WriteCSVFloat(csv, b.min);
                                csv << ","; WriteCSVFloat(csv, b.max);
                            } else {
                                for (int k = 0; k < 4; ++k) {
                                    csv << ",";
                                    WriteCSVFloat(csv, sentinel.noheight.value);
                                }
                            }
                        }
                    }
                }
                if (enableExp) {
                    for (const auto& name : expNames) {
                        csv << ",";
                        WriteCSVFloat(csv, expBands[name][idx]);
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

