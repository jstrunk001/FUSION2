// gridmetrics.cpp : Comprehensive GridMetrics Executable for FUSION2
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/cli/ParseUtil.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/PointFilter.h"
#include "fusion/batch/BatchPipeline.h"
#include "fusion/batch/TilePointSource.h"
#include "fusion/batch/StatusMessenger.h"
#include "fusion/metrics/ExperimentalMetrics.h"
#include "fusion/metrics/SurfaceStats.h"
#include "fusion/metrics/SentinelPolicy.h"
#include "fusion/metrics/PointCloudStats.h"
#include "fusion/metrics/SpectralChannels.h"
#include "fusion/table/TableWriter.h"
#include "fusion/cuda/GridMetricsCuda.h"

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
#include <limits>
#include <memory>
#include <chrono>

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

    // /strata, /intstrata, and /rgb (spectral) per-point values, grouped
    // behind one lazily-allocated pointer instead of four always-present
    // containers (two vector<vector<float>>, two std::map<string,...>).
    // On a large grid (e.g. 5000x5000 = 25M cells) those four containers as
    // direct members add roughly 140 bytes/cell -- about 3.5 GB -- even
    // when a run uses none of /strata, /intstrata, or /rgb, and even for
    // cells that end up with zero points. Extras is only allocated for a
    // given cell the first time a point actually needs to record a value
    // in one of these four containers (see CellAccumulator::EnsureExtras
    // and the point loops in RunBatchTiledMode()/main()) -- so a run
    // without those options never allocates it at all, and a cell with no
    // points (or whose only points were filtered out before reaching the
    // strata/spectral bookkeeping) never allocates it either.
    struct Extras {
        // One bucket per /strata threshold, plus one "above the last
        // threshold" bucket (item 6: full per-stratum elevation values, not
        // just a count -- ComputePointStatBundle runs per non-empty bucket).
        std::vector<std::vector<float>> strataElevations;
        // One bucket per /intstrata threshold, plus one "above the last
        // threshold" bucket -- same shape as strataElevations above, but
        // holding each bucket's raw intensity values instead of elevation,
        // so ComputeStrataStatBundle can report stddev/min/max, not just a
        // mean.
        std::vector<std::vector<float>> strataIntensities;
        // /rgb: (item 7) -- one entry per selected, format-carried spectral
        // channel ("red","green","blue","nir"), populated in lockstep with
        // elevations/intensities (same elevation >= /minht gate).
        std::map<std::string, std::vector<float>> spectralValues;
        // /rgbstrata (new): per-channel spectral values bucketed by
        // elevation stratum -- keyed by channel prefix (a run may select
        // any subset), each holding one vector per /strata bucket,
        // populated unconditionally alongside strataElevations (not gated
        // by /minht).
        std::map<std::string, std::vector<std::vector<float>>> strataSpectralValues;
    };
    std::unique_ptr<Extras> extras;

    // Allocates extras on first use for this cell, sizing strataElevations/
    // strataIntensities to the requested bucket counts (0 buckets = that
    // stratification wasn't requested, so the corresponding vector stays
    // empty). A no-op if extras is already allocated -- numStrataBuckets/
    // numIntStrataBuckets are constant for the whole run, so re-sizing on a
    // later call would be redundant, never a correction.
    Extras& EnsureExtras(size_t numStrataBuckets, size_t numIntStrataBuckets) {
        if (!extras) {
            extras = std::make_unique<Extras>();
            if (numStrataBuckets > 0) {
                extras->strataElevations.resize(numStrataBuckets);
            }
            if (numIntStrataBuckets > 0) {
                extras->strataIntensities.resize(numIntStrataBuckets);
            }
        }
        return *extras;
    }
};

// Null-safe read helpers for CellAccumulator::Extras, used by every output
// loop below. A cell whose extras was never allocated (the /strata,
// /intstrata, or /rgb option wasn't in use, or no point that reached this
// cell needed it -- e.g. every point landing here was outlier-filtered
// before the strata bookkeeping step) is treated exactly like a cell whose
// bucket vector exists but is empty: both report zero points in that
// bucket, so output is identical either way.
static const std::vector<float>& StrataElevationBucket(const CellAccumulator& cell, size_t bucketIdx) {
    static const std::vector<float> kEmpty;
    if (!cell.extras || bucketIdx >= cell.extras->strataElevations.size()) {
        return kEmpty;
    }
    return cell.extras->strataElevations[bucketIdx];
}

static const std::vector<float>& StrataIntensityBucket(const CellAccumulator& cell, size_t bucketIdx) {
    static const std::vector<float> kEmpty;
    if (!cell.extras || bucketIdx >= cell.extras->strataIntensities.size()) {
        return kEmpty;
    }
    return cell.extras->strataIntensities[bucketIdx];
}

// Returns nullptr if this cell has no recorded spectral values for the
// given channel prefix at all (extras never allocated, or this channel
// never appeared for this cell).
static const std::vector<float>* SpectralValueBucket(const CellAccumulator& cell, const std::string& prefix) {
    if (!cell.extras) {
        return nullptr;
    }
    auto it = cell.extras->spectralValues.find(prefix);
    return (it != cell.extras->spectralValues.end()) ? &it->second : nullptr;
}

// Returns nullptr under the same conditions as SpectralValueBucket(), but
// for the per-stratum-bucket spectral values (one vector<float> per
// stratum bucket, for the given channel prefix).
static const std::vector<std::vector<float>>* StrataSpectralValueBuckets(const CellAccumulator& cell, const std::string& prefix) {
    if (!cell.extras) {
        return nullptr;
    }
    auto it = cell.extras->strataSpectralValues.find(prefix);
    return (it != cell.extras->strataSpectralValues.end()) ? &it->second : nullptr;
}

using fusion::cli::ParseFloatList;

// The four stat fields every per-stratum band set reports, in band order.
static const std::vector<std::string> kStrataSimpleStatNames = {"mean", "stddev", "min", "max"};

// /strataraster's per-stratum bands for one grid, in output band order;
// names[k] labels data[k]. Used by single-file and batch mode alike.
struct StrataRasterBands {
    std::vector<std::string> names;
    std::vector<std::vector<float>> data;
};

// Builds /strataraster's bands from the per-cell stratum buckets the point
// loop fills whenever /strata or /intstrata is given:
//  - per height stratum: a return-density band (same as densitymetrics'
//    own stratum bands), a count band, a proportion band, and
//    mean/stddev/min/max (count and proportion are already their own
//    bands, so StrataStatBundle's own copies aren't repeated)
//  - per intensity stratum: the same, minus the density band, since return
//    density belongs to the height bucketing, not to intensity
// A stratum with zero points in an otherwise non-empty cell gets
// noheightValue for its stat bands and a real 0 for its count/proportion
// (and density); a cell with no returns at all gets nodataValue throughout.
// Either bucket count may be 0, in which case that band group is skipped.
static StrataRasterBands BuildStrataRasterBands(const std::vector<CellAccumulator>& grid, size_t numStrataBuckets,
                                                size_t numIntStrataBuckets, double cellSize,
                                                float nodataValue, float noheightValue) {
    StrataRasterBands out;
    size_t numCells = grid.size();
    double cellArea = cellSize * cellSize;

    //1. adds one stratum's band set, from either the height or intensity buckets
    auto addStratum = [&](const std::string& prefix, bool withDensity, bool fromIntensity, size_t s,
                          const std::string& densityName) {
        size_t firstBand = out.data.size();
        size_t numBands = (withDensity ? 1 : 0) + 2 + kStrataSimpleStatNames.size();
        if (withDensity) out.names.push_back(densityName);
        out.names.push_back(prefix + "count");
        out.names.push_back(prefix + "proportion");
        for (const auto& stat : kStrataSimpleStatNames) out.names.push_back(prefix + stat);
        out.data.resize(firstBand + numBands, std::vector<float>(numCells, nodataValue));

        for (size_t i = 0; i < numCells; ++i) {
            const auto& cell = grid[i];
            if (cell.totalReturns == 0) continue;
            const std::vector<float>& bucket = fromIntensity ? StrataIntensityBucket(cell, s) : StrataElevationBucket(cell, s);
            std::vector<double> bucketD(bucket.begin(), bucket.end());
            fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketD, cell.totalReturns);
            size_t band = firstBand;
            if (withDensity) out.data[band++][i] = static_cast<float>(b.count / cellArea);
            out.data[band++][i] = static_cast<float>(b.count);
            out.data[band++][i] = static_cast<float>(b.proportion);
            float stats[4] = {static_cast<float>(b.mean), static_cast<float>(b.stddev),
                              static_cast<float>(b.min), static_cast<float>(b.max)};
            for (size_t k = 0; k < kStrataSimpleStatNames.size(); ++k) {
                out.data[band++][i] = (b.count > 0) ? stats[k] : noheightValue;
            }
        }
    };

    //2. height strata, then intensity strata
    for (size_t s = 0; s < numStrataBuckets; ++s) {
        std::ostringstream label;
        label << std::setw(2) << std::setfill('0') << s;
        addStratum("stratum_" + label.str() + "_", true, false, s, "density_stratum_" + label.str());
    }
    for (size_t s = 0; s < numIntStrataBuckets; ++s) {
        std::ostringstream label;
        label << std::setw(2) << std::setfill('0') << s;
        addStratum("intstratum_" + label.str() + "_", false, true, s, "");
    }
    return out;
}

// Finds the ground raster for a run, in either mode: /ground if given,
// otherwise the second positional argument, as in the usage line's
// "[optional raster ground path]" -- but only when that argument is not
// itself a LAS/LAZ file or a folder of them, since single-file mode also
// accepts several point files as positional arguments. Returns an empty
// string when no ground raster was given.
static std::string ResolveGroundPath(const fusion::cli::ArgumentParser& parser, const std::vector<std::string>& posArgs) {
    if (auto g = parser.GetOption("ground")) return *g;
    if (posArgs.size() > 1 && std::filesystem::exists(posArgs[1]) &&
        fusion::lidar::ResolveInputFiles({posArgs[1]}).empty()) {
        return posArgs[1];
    }
    return "";
}

// Batch/tiled mode: tiles the input directory of LAS/LAZ files, buffers each
// tile, computes the same full metric set single-file mode does (elevation
// stat bundle, cover/density, return-number counts, intensity stats,
// spectral stats, strata/intstrata/rgbstrata table columns, surface stats,
// experimental metrics) per tile in-process (multithreaded via
// BatchPipeline), and mosaics the per-tile rasters into a VRT -- triggered
// when the positional input argument is a directory rather than a single
// file. /strata and /intstrata values are written as /strataraster bands
// (same bands as single-file mode) and/or /output-table columns;
// /rgbstrata values go to /output-table only.
static int RunBatchTiledMode(fusion::cli::ArgumentParser& parser, const std::filesystem::path& inputDir,
                              const std::filesystem::path& outDir, const fusion::lidar::PointFilter& pointFilter,
                              const std::string& groundPath) {
    // Without /extent, the project extent comes from the LAS/LAZ headers
    // (see ResolveProjectExtent), snapped outward to the cell size.
    fusion::batch::TileGridSpec gridSpec;
    bool hasExtent = false;
    if (auto ext = parser.GetOption("extent")) {
        std::stringstream ss(*ext);
        char ch;
        ss >> gridSpec.minX >> ch >> gridSpec.minY >> ch >> gridSpec.maxX >> ch >> gridSpec.maxY;
        hasExtent = true;
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

    std::string extentMessage;
    if (!fusion::batch::ResolveProjectExtent(inputDir, hasExtent, gridSpec.resolution, gridSpec, extentMessage)) {
        std::cerr << "Error: " << extentMessage << "\n";
        return 1;
    }
    if (!extentMessage.empty()) std::cout << "[GridMetrics] " << extentMessage << "\n";

    fusion::batch::PipelineJobOptions jobOpts;
    jobOpts.inputPointCloudDir = inputDir;
    jobOpts.outputDir = outDir;
    jobOpts.outputMode = parser.GetOption("output-mode").value_or("multiband");
    jobOpts.numThreads = std::stoi(parser.GetOption("threads").value_or("4"));
    jobOpts.generateVRT = true;
    jobOpts.mergeGeoTIFF = parser.HasFlag("merge");

    jobOpts.groundPath = groundPath;
    jobOpts.minHt = std::stod(parser.GetOption("minht").value_or("2.0"));
    jobOpts.heightCut = parser.GetOption("heightcut") ? std::stod(*parser.GetOption("heightcut")) : jobOpts.minHt;
    if (auto o = parser.GetOption("outlier")) jobOpts.outlier = *o;
    if (auto c = parser.GetOption("class")) jobOpts.pointClass = *c;
    jobOpts.firstOnly = parser.HasFlag("first");
    jobOpts.noIntensity = parser.HasFlag("nointensity");
    if (auto s = parser.GetOption("strata")) jobOpts.strata = *s;
    if (auto is = parser.GetOption("intstrata")) jobOpts.intStrata = *is;
    if (auto rgb = parser.GetOption("rgb")) jobOpts.rgb = *rgb;
    jobOpts.enableExp = parser.HasFlag("exp");
    jobOpts.enableSurfStats = parser.HasFlag("surfstats");
    jobOpts.surfStatsSource = parser.GetOption("surfstats-source").value_or("max");
    jobOpts.voxelSize = std::stod(parser.GetOption("voxelsize").value_or("20.0"));
    jobOpts.enableRgbStrata = parser.HasFlag("rgbstrata");
    jobOpts.noRaster = parser.HasFlag("noraster");
    jobOpts.enableStrataRaster = parser.HasFlag("strataraster") && !jobOpts.noRaster;
    if (parser.WasExplicit("output-table")) {
        jobOpts.outputTablePath = *parser.GetOption("output-table");
    }

    // stratum values reach the output only through /strataraster bands or
    // the /output-table table -- say so rather than silently computing and
    // discarding them
    bool wantsStrata = !jobOpts.strata.empty() || !jobOpts.intStrata.empty();
    if (wantsStrata && !jobOpts.enableStrataRaster && jobOpts.outputTablePath.empty()) {
        std::cerr << "Warning: /strata and /intstrata values are written only with /strataraster (raster bands) "
                     "or /output-table (table columns) -- neither was given, so no stratum output will be written.\n";
    }
    if (jobOpts.enableRgbStrata && jobOpts.outputTablePath.empty()) {
        std::cerr << "Warning: in batch mode /rgbstrata values are written only to /output-table -- "
                     "none was given, so no /rgbstrata output will be written.\n";
    }

    fusion::metrics::SentinelPolicy batchSentinel;
    batchSentinel.nodata = fusion::metrics::ParseSentinelOption(parser.GetOption("nodata").value_or("NA"));
    batchSentinel.noheight = fusion::metrics::ParseSentinelOption(parser.GetOption("noheight").value_or("0"));
    jobOpts.nodataValue = batchSentinel.nodata.value;
    jobOpts.noheightValue = batchSentinel.noheight.value;

    // Resolve representative point format (for /rgb) and coordinate reference
    // system (WKT) from the input directory. Every tile uses these single
    // values so every tile's table schema and raster CRS match.
    if (std::filesystem::exists(inputDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(inputDir)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext != ".las" && ext != ".laz") continue;
            fusion::lidar::LASReader probeReader;
            if (probeReader.Open(entry.path())) {
                const auto& h = probeReader.GetHeader();
                if (!jobOpts.rgb.empty() && jobOpts.pointFormat == 0) {
                    jobOpts.pointFormat = h.pointFormat;
                }
                if (jobOpts.projectionWKT.empty() && !h.projectionWKT.empty()) {
                    jobOpts.projectionWKT = h.projectionWKT;
                }
                probeReader.Close();
                bool formatResolved = jobOpts.rgb.empty() || (jobOpts.pointFormat != 0);
                if (formatResolved && !jobOpts.projectionWKT.empty()) break;
            }
        }
    }

    // Fall back to ground DEM projection if point clouds carried no WKT VLR
    if (jobOpts.projectionWKT.empty() && !jobOpts.groundPath.empty()) {
        fusion::raster::GDALRaster groundProbe;
        if (groundProbe.Open(jobOpts.groundPath)) {
            jobOpts.projectionWKT = groundProbe.GetInfo().projectionWKT;
            groundProbe.Close();
        }
    }

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
        if (!opts.groundPath.empty()) {
            // checked once before tiling, so a failure here fails the tile
            // rather than computing it from raw elevations
            if (!groundRaster.Open(opts.groundPath)) return false;
            hasGround = true;
        }

        fusion::metrics::SpectralChannelSelection spectralSelection;
        if (!opts.rgb.empty()) {
            spectralSelection = fusion::metrics::ParseSpectralChannels(opts.rgb, opts.pointFormat);
        }

        std::vector<double> strata = !opts.strata.empty() ? ParseFloatList(opts.strata) : std::vector<double>{};
        std::vector<double> intStrata = !opts.intStrata.empty() ? ParseFloatList(opts.intStrata) : strata;

        // Bucket counts for this run, constant across every cell -- used
        // both to lazily size CellAccumulator::Extras the first time a
        // given cell needs it (point loop below) and as the fixed output
        // column/band count downstream (a cell's own extras may be null
        // even when these are nonzero, e.g. a cell with zero points).
        size_t numStrataBuckets = strata.empty() ? 0 : strata.size() + 1;
        size_t numIntStrataBuckets = (intStrata.empty() || opts.noIntensity) ? 0 : intStrata.size() + 1;

        // No per-cell strata/intstrata/spectral containers are allocated
        // here -- CellAccumulator::extras is left null for all cols*rows
        // cells and only allocated lazily, per cell, in the point loop
        // below (see the big comment on CellAccumulator::Extras).
        std::vector<CellAccumulator> grid(cols * rows);

        // Read the tile's points: every input file overlapping its buffered
        // extent, read directly or from the split file BatchPipeline wrote
        // when several tiles share one input file (see ForEachTilePoint)
        bool pointsRead = fusion::batch::ForEachTilePoint(tile, opts.inputPointCloudDir, nullptr,
                                                         [&](const fusion::lidar::PointRecord& pt) {
            if (!pointFilter.Keep(pt)) {
                return;
            }

            if (opts.firstOnly && pt.returnNumber != 1) {
                return;
            }

            // buffer points are read but never given a cell, so
            // a point near a tile seam is counted by one tile only
            int col = 0;
            int row = 0;
            if (fusion::batch::TileCellForPoint(pt.x, pt.y, tile, gridSpec, cols, rows, col, row)) {
                double elevation = pt.z;
                if (hasGround) {
                    auto gz = groundRaster.GetElevation(pt.x, pt.y);
                    if (!gz) {
                        return;
                    }
                    elevation -= *gz;
                }

                auto& cell = grid[row * cols + col];
                cell.totalReturns++;
                if (pt.returnNumber == 1) cell.firstReturns++;
                {
                    int rnIdx = (pt.returnNumber >= 1 && pt.returnNumber <= 8) ? (pt.returnNumber - 1) : 8;
                    cell.returnNumberCounts[rnIdx]++;
                }

                if (hasOutlier && (elevation < outlierMin || elevation > outlierMax)) {
                    return;
                }

                if (opts.enableExp) {
                    cell.cellPoints.push_back({pt.x, pt.y, elevation});
                }

                if (elevation >= 0.0) cell.returnsAboveGround++;
                if (elevation >= opts.heightCut) cell.returnsAboveHeightCut++;

                if (!strata.empty()) {
                    size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, strata);
                    auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                    extras.strataElevations[sIdx].push_back(static_cast<float>(elevation));
                    if (opts.enableRgbStrata) {
                        for (const auto& spec : spectralSelection.channels) {
                            auto& perPrefix = extras.strataSpectralValues[spec.prefix];
                            if (perPrefix.size() != numStrataBuckets) {
                                perPrefix.resize(numStrataBuckets);
                            }
                            perPrefix[sIdx].push_back(static_cast<float>(pt.*(spec.field)));
                        }
                    }
                }

                if (!opts.noIntensity && !intStrata.empty()) {
                    size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, intStrata);
                    auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                    extras.strataIntensities[sIdx].push_back(static_cast<float>(pt.intensity));
                }

                if (elevation >= opts.minHt) {
                    cell.returnsAboveMinHt++;
                    cell.elevations.push_back(static_cast<float>(elevation));
                    if (!opts.noIntensity) {
                        cell.intensities.push_back(static_cast<float>(pt.intensity));
                    }
                    if (!spectralSelection.channels.empty()) {
                        auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                        for (const auto& spec : spectralSelection.channels) {
                            extras.spectralValues[spec.prefix].push_back(static_cast<float>(pt.*(spec.field)));
                        }
                    }
                }
            }
        });
        if (!pointsRead) return false;

        size_t numCells = static_cast<size_t>(cols) * rows;
        float ND = opts.nodataValue;

        std::vector<std::string> elevColNames = fusion::metrics::PointStatBundleColumnNames("elev_");
        std::vector<std::string> intColNames = fusion::metrics::PointStatBundleColumnNames("int_");
        std::vector<std::vector<float>> elevBands(elevColNames.size(), std::vector<float>(numCells, ND));
        std::vector<std::vector<float>> intBands(intColNames.size(), std::vector<float>(numCells, ND));
        std::vector<float> elevProfileArea(numCells, ND);

        constexpr float kSurfNoValue = -3.0e38f;
        std::vector<float> surfSourceMax(opts.enableSurfStats ? numCells : 0, kSurfNoValue);
        std::vector<float> surfSourceMean(opts.enableSurfStats ? numCells : 0, kSurfNoValue);

        std::vector<float> bandCover(numCells, ND), bandDensity(numCells, ND);
        std::vector<std::vector<float>> returnNumberBands(9, std::vector<float>(numCells, ND));
        std::vector<float> bandAllCover(numCells, ND), bandAfCover(numCells, ND);
        std::vector<float> bandAllAboveMean(numCells, ND), bandAllAboveMode(numCells, ND);
        std::vector<float> bandAfAboveMean(numCells, ND), bandAfAboveMode(numCells, ND);

        std::vector<std::string> spectralChannelPrefixes;
        std::map<std::string, std::vector<std::string>> spectralColNames;
        std::map<std::string, std::vector<std::vector<float>>> spectralBands;
        for (const auto& spec : spectralSelection.channels) {
            spectralChannelPrefixes.push_back(spec.prefix);
            auto colNames = fusion::metrics::PointStatBundleColumnNames(spec.prefix + "_");
            spectralBands[spec.prefix] = std::vector<std::vector<float>>(colNames.size(), std::vector<float>(numCells, ND));
            spectralColNames[spec.prefix] = std::move(colNames);
        }

        std::vector<std::string> expNames;
        std::map<std::string, std::vector<float>> expBands;
        if (opts.enableExp) {
            expNames = fusion::metrics::GetExperimentalMetricsNames();
            for (const auto& name : expNames) expBands[name] = std::vector<float>(numCells, ND);
        }

        for (size_t i = 0; i < numCells; ++i) {
            auto& cell = grid[i];
            if (cell.totalReturns == 0) continue;

            if (!cell.elevations.empty()) {
                fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(cell.elevations);
                std::vector<float> vals = fusion::metrics::PointStatBundleAsVector(bundle);
                for (size_t k = 0; k < vals.size(); ++k) elevBands[k][i] = vals[k];
                elevProfileArea[i] = fusion::metrics::ComputeProfileArea(bundle);
                if (opts.enableSurfStats) {
                    surfSourceMax[i] = bundle.max;
                    surfSourceMean[i] = bundle.mean;
                }
                for (float v : cell.elevations) {
                    if (v > bundle.mean) cell.returnsAboveMean++;
                    if (v > bundle.mode) cell.returnsAboveMode++;
                }
            } else {
                float NH = opts.noheightValue;
                for (auto& band : elevBands) band[i] = NH;
                elevProfileArea[i] = NH;
            }

            int denom = opts.firstOnly ? cell.firstReturns : cell.totalReturns;
            if (denom > 0) {
                bandCover[i] = static_cast<float>(100.0 * cell.returnsAboveHeightCut / denom);
            }
            bandDensity[i] = static_cast<float>(cell.totalReturns / (res * res));

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

            if (!opts.noIntensity) {
                if (!cell.intensities.empty()) {
                    fusion::metrics::PointStatBundle intBundle = fusion::metrics::ComputePointStatBundle(cell.intensities);
                    std::vector<float> intVals = fusion::metrics::PointStatBundleAsVector(intBundle);
                    for (size_t k = 0; k < intVals.size(); ++k) intBands[k][i] = intVals[k];
                } else {
                    float NH = opts.noheightValue;
                    for (auto& band : intBands) band[i] = NH;
                }
            }

            for (const auto& prefix : spectralChannelPrefixes) {
                const std::vector<float>* vals_ptr = SpectralValueBucket(cell, prefix);
                auto& bands = spectralBands[prefix];
                if (vals_ptr && !vals_ptr->empty()) {
                    fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(*vals_ptr);
                    std::vector<float> vals = fusion::metrics::PointStatBundleAsVector(bundle);
                    for (size_t k = 0; k < vals.size(); ++k) bands[k][i] = vals[k];
                } else {
                    float NH = opts.noheightValue;
                    for (auto& band : bands) band[i] = NH;
                }
            }

            if (opts.enableExp && !cell.cellPoints.empty()) {
                fusion::metrics::ExperimentalMetricsOptions expOpts;
                expOpts.minHt = opts.minHt;
                expOpts.cellSize = res;
                expOpts.voxelSize = opts.voxelSize;
                auto expRes = fusion::metrics::ComputeExperimentalMetrics(cell.cellPoints, expOpts);
                auto expMap = fusion::metrics::GetExperimentalMetricsAsMap(expRes);
                for (const auto& name : expNames) expBands[name][i] = static_cast<float>(expMap[name]);
            }
        }

        std::vector<float> surfStatsAreaRatio, surfStatsRoughness;
        if (opts.enableSurfStats) {
            const std::vector<float>& sourceElev = (opts.surfStatsSource == "mean") ? surfSourceMean : surfSourceMax;
            fusion::metrics::SurfaceStatsGrid surfGrid = fusion::metrics::ComputeSurfaceStatsGrid(
                sourceElev, cols, rows, res, kSurfNoValue);
            surfStatsAreaRatio = std::move(surfGrid.surfaceAreaRatio);
            surfStatsRoughness = std::move(surfGrid.roughness);
            for (size_t i = 0; i < numCells; ++i) {
                if (surfStatsAreaRatio[i] == kSurfNoValue) surfStatsAreaRatio[i] = ND;
                if (surfStatsRoughness[i] == kSurfNoValue) surfStatsRoughness[i] = ND;
            }
        }

        struct BandDef {
            std::string name;
            const std::vector<float>& data;
        };

        std::vector<BandDef> bandDefs;
        for (size_t k = 0; k < elevColNames.size(); ++k) bandDefs.push_back({elevColNames[k], elevBands[k]});
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
        if (!opts.noIntensity) {
            for (size_t k = 0; k < intColNames.size(); ++k) bandDefs.push_back({intColNames[k], intBands[k]});
        }
        for (const auto& prefix : spectralChannelPrefixes) {
            const auto& colNames = spectralColNames[prefix];
            const auto& bands = spectralBands[prefix];
            for (size_t k = 0; k < colNames.size(); ++k) bandDefs.push_back({colNames[k], bands[k]});
        }
        if (opts.enableSurfStats) {
            bandDefs.push_back({"surface_area_ratio", surfStatsAreaRatio});
            bandDefs.push_back({"roughness", surfStatsRoughness});
        }
        StrataRasterBands strataRasterBands;
        if (opts.enableStrataRaster) {
            strataRasterBands = BuildStrataRasterBands(grid, numStrataBuckets, numIntStrataBuckets, res,
                                                       ND, opts.noheightValue);
        }
        for (size_t k = 0; k < strataRasterBands.names.size(); ++k) {
            bandDefs.push_back({strataRasterBands.names[k], strataRasterBands.data[k]});
        }
        if (opts.enableExp) {
            for (const auto& name : expNames) bandDefs.push_back({"exp_" + name, expBands[name]});
        }

        double geotransform[6] = { tile.minX, res, 0.0, tile.maxY, 0.0, -res };

        if (!opts.noRaster) {
            fusion::raster::GDALRaster raster;
            if (!raster.Create(outTif, cols, rows, static_cast<int>(bandDefs.size()), "Float32", "GTiff", opts.projectionWKT, geotransform, -9999.0)) {
                return false;
            }
            for (size_t b = 0; b < bandDefs.size(); ++b) {
                raster.SetBandDescription(static_cast<int>(b + 1), bandDefs[b].name);
                raster.WriteBandData(static_cast<int>(b + 1), bandDefs[b].data);
            }
            raster.Close();
        }

        if (!opts.outputTablePath.empty()) {
            constexpr double kNA = std::numeric_limits<double>::quiet_NaN();
            std::filesystem::path tileTablePath = opts.outputDir / (tile.name + opts.outputTablePath.extension().string());

            std::vector<std::string> columnNames = {"Col", "Row", "X", "Y", "TotalReturns", "FirstReturns"};
            for (const auto& name : elevColNames) columnNames.push_back(name);
            columnNames.push_back("elev_profile_area");
            columnNames.push_back("CanopyCover");
            columnNames.push_back("PointDensity");
            for (int rn = 1; rn <= 9; ++rn) columnNames.push_back("r" + std::to_string(rn) + "count");
            for (const char* n : {"allcover", "afcover", "allabovemean", "allabovemode", "afabovemean", "afabovemode"}) {
                columnNames.push_back(n);
            }
            if (!opts.noIntensity) {
                for (const auto& name : intColNames) columnNames.push_back(name);
            }
            for (const auto& prefix : spectralChannelPrefixes) {
                for (const auto& name : spectralColNames[prefix]) columnNames.push_back(name);
            }
            for (size_t s = 0; !strata.empty() && s < strata.size() + 1; ++s) {
                std::ostringstream stratumLabel;
                stratumLabel << std::setw(2) << std::setfill('0') << s;
                std::string prefix = "stratum_" + stratumLabel.str() + "_";
                for (const auto& name : fusion::metrics::StrataStatBundleColumnNames(prefix)) columnNames.push_back(name);
            }
            for (size_t s = 0; !intStrata.empty() && !opts.noIntensity && s < intStrata.size() + 1; ++s) {
                std::ostringstream stratumLabel;
                stratumLabel << std::setw(2) << std::setfill('0') << s;
                std::string prefix = "intstratum_" + stratumLabel.str() + "_";
                for (const auto& name : fusion::metrics::StrataStatBundleColumnNames(prefix)) columnNames.push_back(name);
            }
            if (opts.enableRgbStrata) {
                for (const auto& prefix : spectralChannelPrefixes) {
                    for (size_t s = 0; s < strata.size() + 1; ++s) {
                        std::ostringstream stratumLabel;
                        stratumLabel << std::setw(2) << std::setfill('0') << s;
                        std::string bandPrefix = prefix + "_stratum_" + stratumLabel.str() + "_";
                        columnNames.push_back(bandPrefix + "mean");
                        columnNames.push_back(bandPrefix + "stddev");
                        columnNames.push_back(bandPrefix + "min");
                        columnNames.push_back(bandPrefix + "max");
                    }
                }
            }
            if (opts.enableExp) {
                for (const auto& name : expNames) columnNames.push_back(name);
            }

            fusion::table::RowTableWriter tableWriter;
            if (tableWriter.Open(tileTablePath, columnNames)) {
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        size_t idx = static_cast<size_t>(r) * cols + c;
                        const auto& cell = grid[idx];
                        double x = tile.minX + (c + 0.5) * res;
                        double y = tile.maxY - (r + 0.5) * res;
                        bool cellEmpty = (cell.totalReturns == 0);

                        std::vector<double> row;
                        row.push_back(c);
                        row.push_back(r);
                        row.push_back(x);
                        row.push_back(y);
                        row.push_back(cellEmpty ? kNA : static_cast<double>(cell.totalReturns));
                        row.push_back(cellEmpty ? kNA : static_cast<double>(cell.firstReturns));

                        for (const auto& band : elevBands) row.push_back(band[idx]);
                        row.push_back(elevProfileArea[idx]);
                        row.push_back(bandCover[idx]);
                        row.push_back(bandDensity[idx]);
                        for (const auto& band : returnNumberBands) row.push_back(band[idx]);
                        row.push_back(bandAllCover[idx]);
                        row.push_back(bandAfCover[idx]);
                        row.push_back(bandAllAboveMean[idx]);
                        row.push_back(bandAllAboveMode[idx]);
                        row.push_back(bandAfAboveMean[idx]);
                        row.push_back(bandAfAboveMode[idx]);
                        if (!opts.noIntensity) {
                            for (const auto& band : intBands) row.push_back(band[idx]);
                        }
                        for (const auto& prefix : spectralChannelPrefixes) {
                            for (const auto& band : spectralBands[prefix]) row.push_back(band[idx]);
                        }

                        for (size_t s = 0; s < numStrataBuckets; ++s) {
                            if (cellEmpty) {
                                for (int k = 0; k < 6; ++k) row.push_back(kNA);
                                continue;
                            }
                            const std::vector<float>& bucketElev = StrataElevationBucket(cell, s);
                            std::vector<double> bucketElevD(bucketElev.begin(), bucketElev.end());
                            fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketElevD, cell.totalReturns);
                            row.push_back(b.count);
                            row.push_back(b.proportion);
                            if (b.count > 0) {
                                row.push_back(b.mean); row.push_back(b.stddev); row.push_back(b.min); row.push_back(b.max);
                            } else {
                                for (int k = 0; k < 4; ++k) row.push_back(opts.noheightValue);
                            }
                        }
                        for (size_t s = 0; s < numIntStrataBuckets; ++s) {
                            if (cellEmpty) {
                                for (int k = 0; k < 6; ++k) row.push_back(kNA);
                                continue;
                            }
                            std::vector<double> bucketIntD(StrataIntensityBucket(cell, s).begin(), StrataIntensityBucket(cell, s).end());
                            fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketIntD, cell.totalReturns);
                            row.push_back(b.count);
                            row.push_back(b.proportion);
                            if (b.count > 0) {
                                row.push_back(b.mean); row.push_back(b.stddev); row.push_back(b.min); row.push_back(b.max);
                            } else {
                                for (int k = 0; k < 4; ++k) row.push_back(opts.noheightValue);
                            }
                        }
                        if (opts.enableRgbStrata) {
                            for (const auto& prefix : spectralChannelPrefixes) {
                                const std::vector<std::vector<float>>* perPrefix = StrataSpectralValueBuckets(cell, prefix);
                                for (size_t s = 0; s < strata.size() + 1; ++s) {
                                    if (cellEmpty) {
                                        for (int k = 0; k < 4; ++k) row.push_back(kNA);
                                        continue;
                                    }
                                    std::vector<double> bucketValsD;
                                    if (perPrefix && s < perPrefix->size()) {
                                        const auto& bucketVals = (*perPrefix)[s];
                                        bucketValsD.assign(bucketVals.begin(), bucketVals.end());
                                    }
                                    fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketValsD, bucketValsD.size());
                                    if (b.count > 0) {
                                        row.push_back(b.mean); row.push_back(b.stddev); row.push_back(b.min); row.push_back(b.max);
                                    } else {
                                        for (int k = 0; k < 4; ++k) row.push_back(opts.noheightValue);
                                    }
                                }
                            }
                        }
                        if (opts.enableExp) {
                            for (const auto& name : expNames) row.push_back(expBands[name][idx]);
                        }

                        tableWriter.WriteRow(row);
                    }
                }
                tableWriter.Close();
            }
        }

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
    parser.SetPositionalArgsUsage("<input.las/laz/raster or directory> [optional raster ground path]");
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
    parser.AddFlag("strataraster", "With /strata or /intstrata, also append per-stratum bands (density_stratum_NN, stratum_NN_count/proportion/mean/stddev/min/max, intstratum_NN_...) to the multiband output, in single-file and batch mode");
    parser.AddFlag("rgbstrata", "With /rgb and /strata both set, also append a mean/stddev/min/max band set (and matching CSV columns) per selected spectral channel within each height-stratum bucket (<channel>_stratum_NN_*).");
    parser.AddOption("nodata", "Value for cells with zero returns at all: NA, or a number such as 0, -9999, or inf", "NA");
    parser.AddOption("noheight", "Value for height-dependent bands (elev_*, int_*) when a cell has returns but none clear the height cutoff: NA, or a number such as 0, -9999, or inf", "0");
    parser.AddOption("rgb", "Comma-separated spectral channels to compute a statistic bundle for: R, G, B, N, or all (every channel the input file's LAS point format actually carries)");
    parser.AddOption("voxelsize", "3D voxel resolution for voxel volume metrics (same units as the input)", "20.0");
    parser.AddFlag("exp", "Compute additional experimental metrics from RSForTools");
    parser.AddFlag("surfstats", "Compute surface_area_ratio and roughness bands from the per-cell elevation grid (see /surfstats-source)");
    parser.AddOption("surfstats-source", "Elevation source for /surfstats: max (typical CHM top-surface use) or mean (ground-DTM-style runs)", "max");
    parser.AddOption("outroot", "Base root name for output CSV summary metrics tables");
    parser.AddOption("outdir", "Output directory for rasters and CSV reports (also the batch/tiled mode output directory)", ".");
    parser.AddOption("output-mode", "Output raster mode: multiband or singleband", "multiband");
    parser.AddOption("output-table", "Write the full per-cell metrics table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely");
    parser.AddFlag("noraster", "Skip writing the GeoTIFF raster(s) -- only valid together with /output-table, since a run must produce at least one output");
    parser.AddFlag("profile", "Report high-resolution wall-clock timing breakdown across decompression, binning, metric computation, and I/O");
    parser.AddFlag("gpu", "Enable CUDA GPU acceleration for DSM raster zonal metric calculations");
    parser.AddFlag("nogpu", "Disable CUDA GPU acceleration (force CPU scanline processing)");

    // Batch/tiled mode options (used only when the positional input is a directory --
    // see RunBatchTiledMode above). Ignored in single-file mode.
    parser.AddOption("extent", "Batch/tiled mode: project extent LLX,LLY,URX,URY (default: the extent of the input files, snapped to the cell size)");
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

    if (parser.HasFlag("noraster") && !parser.WasExplicit("output-table")) {
        std::cerr << "Error: /noraster requires /output-table:<path> -- a run must produce at least one output.\n";
        return 1;
    }

    std::filesystem::path inputPath = posArgs[0];
    std::filesystem::path outDir = parser.GetOption("outdir").value_or(".");
    fusion::lidar::PointFilter pointFilter = fusion::lidar::PointFilter::FromParser(parser);

    // A ground raster that was given but cannot be opened stops the run --
    // otherwise heights stay as raw elevations and, with /outlier, every
    // point can be dropped while the run still reports success.
    std::string groundPathStr = ResolveGroundPath(parser, posArgs);
    if (!groundPathStr.empty()) {
        fusion::raster::GDALRaster groundCheck;
        if (!groundCheck.Open(groundPathStr)) {
            std::cerr << "Error: could not open ground raster '" << groundPathStr << "'.\n";
            return 1;
        }
        groundCheck.Close();
    }

    if (std::filesystem::is_directory(inputPath)) {
        std::filesystem::create_directories(outDir);
        return RunBatchTiledMode(parser, inputPath, outDir, pointFilter, groundPathStr);
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

    bool enableProfile = parser.HasFlag("profile");
    auto tProfileStart = std::chrono::steady_clock::now();
    double secReadDecomp = 0.0;
    double secBinning = 0.0;
    double secMetrics = 0.0;
    double secIO = 0.0;
    if (rasterNoData.conflict) {
        std::cerr << "Warning: /nodata and /noheight were both set to different, non-NA values -- "
                     "GDAL supports only one registered NoData value per raster. Using /nodata's value ("
                  << rasterNoData.value << ") as the file's registered NoData; /noheight's value will be "
                     "written as an ordinary, valid pixel, not flagged as NoData by the GeoTIFF header.\n";
    }
    std::string outputMode = parser.GetOption("output-mode").value_or("multiband");
    bool firstOnly = parser.HasFlag("first");
    bool noIntensity = parser.HasFlag("nointensity");
    bool wantGpu = parser.HasFlag("gpu");
    bool forceCpu = parser.HasFlag("nogpu");
    bool enableGpu = (wantGpu && !forceCpu);

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
    if (!groundPathStr.empty() && groundRaster.Open(groundPathStr)) {
        hasGround = true;
        std::cout << "[GridMetrics] Loaded ground surface DEM: " << groundPathStr << "\n";
    }

    bool isRasterInput = false;
    fusion::raster::GDALRaster dsmRaster;
    std::string inExt = inputPath.extension().string();
    std::transform(inExt.begin(), inExt.end(), inExt.begin(), ::tolower);
    if (inExt == ".tif" || inExt == ".tiff" || inExt == ".vrt" || inExt == ".img" || inExt == ".asc" || inExt == ".dem") {
        if (dsmRaster.Open(inputPath)) {
            isRasterInput = true;
        }
    } else if (inExt != ".las" && inExt != ".laz") {
        if (dsmRaster.Open(inputPath)) {
            isRasterInput = true;
        }
    }

    double gridMinX = 0.0, gridMinY = 0.0, gridMaxX = 0.0, gridMaxY = 0.0;
    std::string gridProjectionWKT;
    std::vector<std::filesystem::path> inputFiles;
    fusion::lidar::MergedPointCloudReader lasReader;
    fusion::metrics::SpectralChannelSelection spectralSelection;

    if (isRasterInput) {
        const auto& dsmInfo = dsmRaster.GetInfo();
        std::cout << "[GridMetrics] Processing DSM Raster: " << inputPath.filename().string()
                  << " (" << dsmInfo.width << "x" << dsmInfo.height << " pixels, cell size "
                  << dsmInfo.pixelWidth << "x" << std::abs(dsmInfo.pixelHeight) << ")\n";
        gridMinX = dsmInfo.minX;
        gridMinY = dsmInfo.minY;
        gridMaxX = dsmInfo.maxX;
        gridMaxY = dsmInfo.maxY;
        gridProjectionWKT = dsmInfo.projectionWKT;
        noIntensity = true;
    } else {
        inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
        if (inputFiles.empty()) {
            std::cerr << "Error: No valid .las, .laz, or raster files found from input arguments.\n";
            return 1;
        }

        if (!lasReader.Open(inputFiles)) {
            std::cerr << "Error: Failed to open point cloud file(s).\n";
            return 1;
        }

        auto header = lasReader.GetHeader();
        std::cout << "[GridMetrics] Processing Point Cloud: " << (inputFiles.size() == 1 ? inputFiles[0].filename().string() : ("merged " + std::to_string(inputFiles.size()) + " files"))
                  << " (" << header.pointCount << " points)\n";
        gridMinX = header.minX;
        gridMinY = header.minY;
        gridMaxX = header.maxX;
        gridMaxY = header.maxY;
        gridProjectionWKT = header.projectionWKT;

        if (auto rgbOpt = parser.GetOption("rgb")) {
            spectralSelection = fusion::metrics::ParseSpectralChannels(*rgbOpt, header.pointFormat);
            for (const auto& token : spectralSelection.unknownTokens) {
                std::cerr << "Warning: /rgb channel '" << token << "' is not available for LAS point format "
                          << static_cast<int>(header.pointFormat) << " -- skipped.\n";
            }
        }
    }

    if (wantGpu && !fusion::cuda::IsCudaAvailable()) {
        std::cout << "[GridMetrics] Notice: /gpu requested but CUDA support is not compiled into this build. Falling back to CPU.\n";
    }
    if (wantGpu && !isRasterInput) {
        std::cout << "[GridMetrics] Notice: /gpu is optimized for high-resolution DSM raster zonal metric calculation. Proceeding with CPU point cloud processing.\n";
    }

    bool enableRgbStrata = parser.HasFlag("rgbstrata");
    if (enableRgbStrata && (strata.empty() || spectralSelection.channels.empty())) {
        std::cerr << "Warning: /rgbstrata requires both /strata and /rgb to be set -- ignored.\n";
        enableRgbStrata = false;
    }

    int cols = static_cast<int>(std::ceil((gridMaxX - gridMinX) / cellSize));
    int rows = static_cast<int>(std::ceil((gridMaxY - gridMinY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    size_t numStrataBuckets = strata.empty() ? 0 : strata.size() + 1;
    size_t numIntStrataBuckets = (intStrata.empty() || noIntensity) ? 0 : intStrata.size() + 1;

    bool gpuExecuted = false;
    fusion::cuda::CudaRasterMetricsBundle cudaBundle;

    if (isRasterInput && enableGpu && fusion::cuda::IsCudaAvailable()) {
        const auto& dsmInfo = dsmRaster.GetInfo();
        std::cout << "[GridMetrics] Ingesting DSM raster for GPU zonal metric calculation...\n";
        auto tRead0 = std::chrono::steady_clock::now();
        std::vector<float> dsmFull;
        if (dsmRaster.ReadBandData(1, dsmFull)) {
            auto tRead1 = std::chrono::steady_clock::now();
            secReadDecomp += std::chrono::duration<double>(tRead1 - tRead0).count();

            std::vector<float> dtmFull;
            const float* dtmPtr = nullptr;
            if (hasGround) {
                dtmFull.resize(static_cast<size_t>(dsmInfo.width) * dsmInfo.height, 0.0f);
                for (int r = 0; r < dsmInfo.height; ++r) {
                    double y = dsmInfo.originY + (r + 0.5) * dsmInfo.pixelHeight;
                    for (int c = 0; c < dsmInfo.width; ++c) {
                        double x = dsmInfo.originX + (c + 0.5) * dsmInfo.pixelWidth;
                        auto gz = groundRaster.GetElevation(x, y, fusion::raster::SampleMethod::Bilinear);
                        dtmFull[static_cast<size_t>(r) * dsmInfo.width + c] = gz ? static_cast<float>(*gz) : 0.0f;
                    }
                }
                dtmPtr = dtmFull.data();
            }

            fusion::cuda::CudaGridMetricsOptions cudaOpts;
            cudaOpts.fineCols = dsmInfo.width;
            cudaOpts.fineRows = dsmInfo.height;
            cudaOpts.fineCellSize = static_cast<float>(dsmInfo.pixelWidth);
            cudaOpts.coarseCellSize = static_cast<float>(cellSize);
            cudaOpts.minHt = static_cast<float>(minHt);
            cudaOpts.heightCut = static_cast<float>(heightCut);
            cudaOpts.nodataValue = rasterNoData.value;

            auto tCuda0 = std::chrono::steady_clock::now();
            gpuExecuted = fusion::cuda::ComputeRasterGridMetrics(dsmFull.data(), dtmPtr, cudaOpts, cudaBundle);
            auto tCuda1 = std::chrono::steady_clock::now();
            if (gpuExecuted) {
                secMetrics += std::chrono::duration<double>(tCuda1 - tCuda0).count();
                cols = cudaBundle.outCols;
                rows = cudaBundle.outRows;
                std::cout << "[GridMetrics] GPU raster zonal metrics (33 bands) computed successfully.\n";
            } else {
                std::cout << "[GridMetrics] Notice: GPU raster zonal computation failed. Falling back to CPU scanlines.\n";
            }
        }
    }

    std::vector<CellAccumulator> grid;
    if (!gpuExecuted) {
        grid.resize(cols * rows);
        if (isRasterInput) {
        const auto& dsmInfo = dsmRaster.GetInfo();
        int blockSizeRows = 512;
        std::vector<float> dsmBlock;
        for (int rowStart = 0; rowStart < dsmInfo.height; rowStart += blockSizeRows) {
            int currentBlockRows = std::min(blockSizeRows, dsmInfo.height - rowStart);
            auto tRead0 = std::chrono::steady_clock::now();
            if (!dsmRaster.ReadBandWindow(1, 0, rowStart, dsmInfo.width, currentBlockRows, dsmBlock)) {
                std::cerr << "Error: Failed to read DSM raster window at row " << rowStart << "\n";
                return 1;
            }
            auto tRead1 = std::chrono::steady_clock::now();
            secReadDecomp += std::chrono::duration<double>(tRead1 - tRead0).count();

            auto tBin0 = std::chrono::steady_clock::now();

            for (int br = 0; br < currentBlockRows; ++br) {
                int rGlobal = rowStart + br;
                double y = fusion::raster::PixelCentreY(dsmInfo, rGlobal);
                for (int cGlobal = 0; cGlobal < dsmInfo.width; ++cGlobal) {
                    double x = fusion::raster::PixelCentreX(dsmInfo, cGlobal);
                    float val = dsmBlock[br * dsmInfo.width + cGlobal];

                    if (dsmInfo.hasNoData && val == static_cast<float>(dsmInfo.noDataValue)) {
                        continue;
                    }
                    if (std::isnan(val)) {
                        continue;
                    }

                    int col = static_cast<int>((x - gridMinX) / cellSize);
                    int row = static_cast<int>((gridMaxY - y) / cellSize);
                    if (col == cols && x == gridMaxX) col = cols - 1;
                    if (row == rows && y == gridMinY) row = rows - 1;

                    if (col >= 0 && col < cols && row >= 0 && row < rows) {
                        double elevation = val;
                        if (hasGround) {
                            auto gz = groundRaster.GetElevation(x, y, fusion::raster::SampleMethod::Bilinear);
                            if (!gz) {
                                continue;
                            }
                            elevation -= *gz;
                        }

                        auto& cell = grid[row * cols + col];
                        cell.totalReturns++;
                        cell.firstReturns++;
                        cell.returnNumberCounts[0]++;

                        if (hasOutlier && (elevation < outlierMin || elevation > outlierMax)) {
                            continue;
                        }

                        if (enableExp) {
                            cell.cellPoints.push_back({x, y, elevation});
                        }

                        if (elevation >= 0.0) {
                            cell.returnsAboveGround++;
                        }

                        if (elevation >= heightCut) {
                            cell.returnsAboveHeightCut++;
                        }

                        if (!strata.empty()) {
                            size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, strata);
                            auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                            extras.strataElevations[sIdx].push_back(static_cast<float>(elevation));
                        }

                        if (elevation >= minHt) {
                            cell.returnsAboveMinHt++;
                            cell.elevations.push_back(static_cast<float>(elevation));
                        }
                    }
                }
            }
            auto tBin1 = std::chrono::steady_clock::now();
            secBinning += std::chrono::duration<double>(tBin1 - tBin0).count();
        }
        dsmRaster.Close();
    } else {
        if (enableProfile) {
            constexpr size_t kBatchSize = 8192;
            std::vector<fusion::lidar::PointRecord> batch;
            batch.reserve(kBatchSize);
            while (true) {
                auto tRead0 = std::chrono::steady_clock::now();
                batch.clear();
                fusion::lidar::PointRecord pt;
                while (batch.size() < kBatchSize && lasReader.ReadNextPoint(pt)) {
                    batch.push_back(pt);
                }
                auto tRead1 = std::chrono::steady_clock::now();
                secReadDecomp += std::chrono::duration<double>(tRead1 - tRead0).count();

                if (batch.empty()) break;

                auto tBin0 = std::chrono::steady_clock::now();
                for (const auto& p : batch) {
                    if (!pointFilter.Keep(p)) continue;
                    if (firstOnly && p.returnNumber != 1) continue;

                    int col = static_cast<int>((p.x - gridMinX) / cellSize);
                    int row = static_cast<int>((gridMaxY - p.y) / cellSize);
                    if (col == cols && p.x == gridMaxX) col = cols - 1;
                    if (row == rows && p.y == gridMinY) row = rows - 1;

                    if (col >= 0 && col < cols && row >= 0 && row < rows) {
                        double elevation = p.z;
                        if (hasGround) {
                            auto gz = groundRaster.GetElevation(p.x, p.y);
                            if (!gz) continue;
                            elevation -= *gz;
                        }

                        auto& cell = grid[row * cols + col];
                        cell.totalReturns++;
                        if (p.returnNumber == 1) cell.firstReturns++;
                        {
                            int rnIdx = (p.returnNumber >= 1 && p.returnNumber <= 8) ? (p.returnNumber - 1) : 8;
                            cell.returnNumberCounts[rnIdx]++;
                        }

                        if (hasOutlier && (elevation < outlierMin || elevation > outlierMax)) continue;
                        if (enableExp) cell.cellPoints.push_back({p.x, p.y, elevation});
                        if (elevation >= 0.0) cell.returnsAboveGround++;
                        if (elevation >= heightCut) cell.returnsAboveHeightCut++;

                        if (!strata.empty()) {
                            size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, strata);
                            auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                            extras.strataElevations[sIdx].push_back(static_cast<float>(elevation));
                            if (enableRgbStrata) {
                                for (const auto& spec : spectralSelection.channels) {
                                    auto& perPrefix = extras.strataSpectralValues[spec.prefix];
                                    if (perPrefix.size() != numStrataBuckets) perPrefix.resize(numStrataBuckets);
                                    perPrefix[sIdx].push_back(static_cast<float>(p.*(spec.field)));
                                }
                            }
                        }

                        if (!noIntensity && !intStrata.empty()) {
                            size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, intStrata);
                            auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                            extras.strataIntensities[sIdx].push_back(static_cast<float>(p.intensity));
                        }

                        if (elevation >= minHt) {
                            cell.returnsAboveMinHt++;
                            cell.elevations.push_back(static_cast<float>(elevation));
                            if (!noIntensity) cell.intensities.push_back(static_cast<float>(p.intensity));
                            if (!spectralSelection.channels.empty()) {
                                auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                                for (const auto& spec : spectralSelection.channels) {
                                    extras.spectralValues[spec.prefix].push_back(static_cast<float>(p.*(spec.field)));
                                }
                            }
                        }
                    }
                }
                auto tBin1 = std::chrono::steady_clock::now();
                secBinning += std::chrono::duration<double>(tBin1 - tBin0).count();
            }
        } else {
            fusion::lidar::PointRecord pt;
            while (lasReader.ReadNextPoint(pt)) {
                if (!pointFilter.Keep(pt)) {
                    continue;
                }

                if (firstOnly && pt.returnNumber != 1) {
                    continue;
                }

                int col = static_cast<int>((pt.x - gridMinX) / cellSize);
                int row = static_cast<int>((gridMaxY - pt.y) / cellSize);
                if (col == cols && pt.x == gridMaxX) col = cols - 1;
                if (row == rows && pt.y == gridMinY) row = rows - 1;

                if (col >= 0 && col < cols && row >= 0 && row < rows) {
                    double elevation = pt.z;
                    if (hasGround) {
                        auto gz = groundRaster.GetElevation(pt.x, pt.y);
                        if (!gz) {
                            continue;
                        }
                        elevation -= *gz;
                    }

                    auto& cell = grid[row * cols + col];
                    cell.totalReturns++;
                    if (pt.returnNumber == 1) cell.firstReturns++;
                    {
                        int rnIdx = (pt.returnNumber >= 1 && pt.returnNumber <= 8) ? (pt.returnNumber - 1) : 8;
                        cell.returnNumberCounts[rnIdx]++;
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
                        auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                        extras.strataElevations[sIdx].push_back(static_cast<float>(elevation));
                        if (enableRgbStrata) {
                            for (const auto& spec : spectralSelection.channels) {
                                auto& perPrefix = extras.strataSpectralValues[spec.prefix];
                                if (perPrefix.size() != numStrataBuckets) {
                                    perPrefix.resize(numStrataBuckets);
                                }
                                perPrefix[sIdx].push_back(static_cast<float>(pt.*(spec.field)));
                            }
                        }
                    }

                    if (!noIntensity && !intStrata.empty()) {
                        size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, intStrata);
                        auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                        extras.strataIntensities[sIdx].push_back(static_cast<float>(pt.intensity));
                    }

                    if (elevation >= minHt) {
                        cell.returnsAboveMinHt++;
                        cell.elevations.push_back(static_cast<float>(elevation));
                        if (!noIntensity) {
                            cell.intensities.push_back(static_cast<float>(pt.intensity));
                        }
                        if (!spectralSelection.channels.empty()) {
                            auto& extras = cell.EnsureExtras(numStrataBuckets, numIntStrataBuckets);
                            for (const auto& spec : spectralSelection.channels) {
                                extras.spectralValues[spec.prefix].push_back(static_cast<float>(pt.*(spec.field)));
                            }
                        }
                    }
                }
            }
        }
        lasReader.Close();
    }
    } else {
        if (isRasterInput) {
            dsmRaster.Close();
        }
    }

    auto tMetrics0 = std::chrono::steady_clock::now();

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
    std::vector<float> surfStatsAreaRatio, surfStatsRoughness;
    StrataRasterBands strataRasterBands;
    std::vector<std::string> rgbStrataChannelPrefixes;
    std::map<std::string, std::vector<std::vector<float>>> rgbStrataBands;

    if (!gpuExecuted) {
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
            const std::vector<float>* vals_ptr = SpectralValueBucket(cell, prefix);
            auto& bands = spectralBands[prefix];
            if (vals_ptr && !vals_ptr->empty()) {
                fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(*vals_ptr);
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

    // /strataraster: per-stratum bands, built the same way as in batch mode
    // (see BuildStrataRasterBands). Only built when /strataraster is set.
    StrataRasterBands strataRasterBands;
    if (enableStrataRaster) {
        strataRasterBands = BuildStrataRasterBands(grid, numStrataBuckets, numIntStrataBuckets, cellSize,
                                                   ND, sentinel.noheight.value);
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
                const std::vector<std::vector<float>>* perPrefix = StrataSpectralValueBuckets(cell, prefix);
                for (size_t s = 0; s < numStrataBuckets; ++s) {
                    std::vector<double> bucketValsD;
                    if (perPrefix && s < perPrefix->size()) {
                        const auto& bucketVals = (*perPrefix)[s];
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

        auto tMetrics1 = std::chrono::steady_clock::now();
        secMetrics += std::chrono::duration<double>(tMetrics1 - tMetrics0).count();
    }

    auto tIO0 = std::chrono::steady_clock::now();

    double geotransform[6] = { gridMinX, cellSize, 0.0, gridMaxY, 0.0, -cellSize };
    std::string stem = isRasterInput
        ? inputPath.stem().string()
        : ((inputFiles.size() == 1) ? inputFiles[0].stem().string() : "merged_gridmetrics");
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
    if (gpuExecuted) {
        for (size_t b = 0; b < cudaBundle.bandNames.size(); ++b) {
            bandDefs.push_back({cudaBundle.bandNames[b], cudaBundle.bands[b]});
        }
    } else {
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

        for (size_t k = 0; k < strataRasterBands.names.size(); ++k) {
            bandDefs.push_back({strataRasterBands.names[k], strataRasterBands.data[k]});
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
    }

    bool noRaster = parser.HasFlag("noraster");
    bool wantTable = parser.WasExplicit("output-table");

    std::string projWKT = !gridProjectionWKT.empty()
        ? gridProjectionWKT
        : (hasGround ? groundRaster.GetInfo().projectionWKT : "");

    if (!noRaster) {
        if (outputMode == "singleband") {
            std::cout << "[GridMetrics] Writing Single-band GeoTIFF rasters to: " << outDir << "...\n";
            for (const auto& bdef : bandDefs) {
                std::filesystem::path bpath = outDir / (stem + "_" + bdef.name + ".tif");
                if (outRaster.Create(bpath, cols, rows, 1, "Float32", "GTiff", projWKT, geotransform, rasterNoData.value)) {
                    outRaster.SetBandDescription(1, bdef.name);
                    outRaster.WriteBandData(1, bdef.data);
                    outRaster.Close();
                }
            }
        } else {
            std::cout << "[GridMetrics] Writing Multi-band GeoTIFF raster to: " << outRasterPath << " ("
                      << bandDefs.size() << " bands)...\n";
            if (outRaster.Create(outRasterPath, cols, rows, static_cast<int>(bandDefs.size()), "Float32", "GTiff", projWKT, geotransform, rasterNoData.value)) {
                for (size_t b = 0; b < bandDefs.size(); ++b) {
                    outRaster.SetBandDescription(static_cast<int>(b + 1), bandDefs[b].name);
                    outRaster.WriteBandData(static_cast<int>(b + 1), bandDefs[b].data);
                }
                outRaster.Close();
            }
        }
    }

    // Export the full per-cell metrics table (CSV or SQLite) only when
    // /output-table was explicitly given -- no default-named file is ever
    // written on its own.
    if (wantTable) {
        constexpr double kNA = std::numeric_limits<double>::quiet_NaN();
        std::filesystem::path tablePath = *parser.GetOption("output-table");

        if (gpuExecuted) {
            std::vector<std::string> columnNames = {"Col", "Row", "X", "Y"};
            for (const auto& name : cudaBundle.bandNames) {
                columnNames.push_back(name);
            }
            fusion::table::RowTableWriter tableWriter;
            if (!tableWriter.Open(tablePath, columnNames)) {
                std::cerr << "Error: failed to open table output '" << tablePath.string() << "'.\n";
            } else {
                std::cout << "[GridMetrics] Exporting per-cell metrics table to: " << tablePath.string() << "...\n";

                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        size_t idx = static_cast<size_t>(r) * cols + c;
                        double x = gridMinX + (c + 0.5) * cellSize;
                        double y = gridMaxY - (r + 0.5) * cellSize;

                        std::vector<double> row;
                        row.push_back(c);
                        row.push_back(r);
                        row.push_back(x);
                        row.push_back(y);

                        for (size_t b = 0; b < cudaBundle.bands.size(); ++b) {
                            float val = cudaBundle.bands[b][idx];
                            row.push_back((val == rasterNoData.value) ? kNA : static_cast<double>(val));
                        }
                        tableWriter.WriteRow(row);
                    }
                }
                tableWriter.Close();
            }
        } else {
            std::vector<std::string> columnNames = {"Col", "Row", "X", "Y", "TotalReturns", "FirstReturns"};
        for (const auto& name : elevColNames) columnNames.push_back(name);
        columnNames.push_back("elev_profile_area");
        columnNames.push_back("CanopyCover");
        columnNames.push_back("PointDensity");
        for (int rn = 1; rn <= 9; ++rn) columnNames.push_back("r" + std::to_string(rn) + "count");
        for (const char* n : {"allcover", "afcover", "allabovemean", "allabovemode", "afabovemean", "afabovemode"}) {
            columnNames.push_back(n);
        }
        if (!noIntensity) {
            for (const auto& name : intColNames) columnNames.push_back(name);
        }
        for (const auto& prefix : spectralChannelPrefixes) {
            for (const auto& name : spectralColNames[prefix]) columnNames.push_back(name);
        }
        // strata.size() + 1 buckets, not strata.size() -- AssignStratumIndex
        // reserves one extra "above the last threshold" bucket, and the
        // per-row loop below already writes numStrataBuckets buckets' worth
        // of columns; a header short by one bucket here would silently
        // misalign every downstream column.
        for (size_t s = 0; !strata.empty() && s < strata.size() + 1; ++s) {
            std::ostringstream stratumLabel;
            stratumLabel << std::setw(2) << std::setfill('0') << s;
            std::string prefix = "stratum_" + stratumLabel.str() + "_";
            for (const auto& name : fusion::metrics::StrataStatBundleColumnNames(prefix)) columnNames.push_back(name);
        }
        for (size_t s = 0; !intStrata.empty() && !noIntensity && s < intStrata.size() + 1; ++s) {
            std::ostringstream stratumLabel;
            stratumLabel << std::setw(2) << std::setfill('0') << s;
            std::string prefix = "intstratum_" + stratumLabel.str() + "_";
            for (const auto& name : fusion::metrics::StrataStatBundleColumnNames(prefix)) columnNames.push_back(name);
        }
        if (enableRgbStrata) {
            for (const auto& prefix : rgbStrataChannelPrefixes) {
                for (size_t s = 0; s < strata.size() + 1; ++s) {
                    std::ostringstream stratumLabel;
                    stratumLabel << std::setw(2) << std::setfill('0') << s;
                    std::string bandPrefix = prefix + "_stratum_" + stratumLabel.str() + "_";
                    columnNames.push_back(bandPrefix + "mean");
                    columnNames.push_back(bandPrefix + "stddev");
                    columnNames.push_back(bandPrefix + "min");
                    columnNames.push_back(bandPrefix + "max");
                }
            }
        }
        if (enableExp) {
            for (const auto& name : expNames) columnNames.push_back(name);
        }

        fusion::table::RowTableWriter tableWriter;
        if (!tableWriter.Open(tablePath, columnNames)) {
            std::cerr << "Error: failed to open table output '" << tablePath.string() << "'.\n";
        } else {
            std::cout << "[GridMetrics] Exporting per-cell metrics table to: " << tablePath.string() << "...\n";

            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < cols; ++c) {
                    size_t idx = r * cols + c;
                    const auto& cell = grid[idx];
                    double x = gridMinX + (c + 0.5) * cellSize;
                    double y = gridMaxY - (r + 0.5) * cellSize;
                    bool cellEmpty = (cell.totalReturns == 0); // /nodata applies to every column, including counts

                    std::vector<double> row;
                    row.push_back(c);
                    row.push_back(r);
                    row.push_back(x);
                    row.push_back(y);
                    row.push_back(cellEmpty ? kNA : static_cast<double>(cell.totalReturns));
                    row.push_back(cellEmpty ? kNA : static_cast<double>(cell.firstReturns));

                    for (const auto& band : elevBands) row.push_back(band[idx]);
                    row.push_back(elevProfileArea[idx]);
                    row.push_back(bandCover[idx]);
                    row.push_back(bandDensity[idx]);
                    for (const auto& band : returnNumberBands) row.push_back(band[idx]);
                    row.push_back(bandAllCover[idx]);
                    row.push_back(bandAfCover[idx]);
                    row.push_back(bandAllAboveMean[idx]);
                    row.push_back(bandAllAboveMode[idx]);
                    row.push_back(bandAfAboveMean[idx]);
                    row.push_back(bandAfAboveMode[idx]);
                    if (!noIntensity) {
                        for (const auto& band : intBands) row.push_back(band[idx]);
                    }
                    for (const auto& prefix : spectralChannelPrefixes) {
                        for (const auto& band : spectralBands[prefix]) row.push_back(band[idx]);
                    }

                    for (size_t s = 0; s < numStrataBuckets; ++s) {
                        if (cellEmpty) {
                            for (int k = 0; k < 6; ++k) row.push_back(kNA);
                            continue;
                        }
                        const std::vector<float>& bucketElev = StrataElevationBucket(cell, s);
                        std::vector<double> bucketElevD(bucketElev.begin(), bucketElev.end());
                        fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketElevD, cell.totalReturns);
                        row.push_back(b.count);
                        row.push_back(b.proportion);
                        if (b.count > 0) {
                            row.push_back(b.mean);
                            row.push_back(b.stddev);
                            row.push_back(b.min);
                            row.push_back(b.max);
                        } else {
                            for (int k = 0; k < 4; ++k) row.push_back(sentinel.noheight.value);
                        }
                    }
                    for (size_t s = 0; s < numIntStrataBuckets; ++s) {
                        if (cellEmpty) {
                            for (int k = 0; k < 6; ++k) row.push_back(kNA);
                            continue;
                        }
                        const std::vector<float>& bucketInt = StrataIntensityBucket(cell, s);
                        std::vector<double> bucketIntD(bucketInt.begin(), bucketInt.end());
                        fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketIntD, cell.totalReturns);
                        row.push_back(b.count);
                        row.push_back(b.proportion);
                        if (b.count > 0) {
                            row.push_back(b.mean);
                            row.push_back(b.stddev);
                            row.push_back(b.min);
                            row.push_back(b.max);
                        } else {
                            for (int k = 0; k < 4; ++k) row.push_back(sentinel.noheight.value);
                        }
                    }
                    if (enableRgbStrata) {
                        for (const auto& prefix : rgbStrataChannelPrefixes) {
                            const std::vector<std::vector<float>>* perPrefix = StrataSpectralValueBuckets(cell, prefix);
                            for (size_t s = 0; s < strata.size() + 1; ++s) {
                                if (cellEmpty) {
                                    for (int k = 0; k < 4; ++k) row.push_back(kNA);
                                    continue;
                                }
                                std::vector<double> bucketValsD;
                                if (perPrefix && s < perPrefix->size()) {
                                    const auto& bucketVals = (*perPrefix)[s];
                                    bucketValsD.assign(bucketVals.begin(), bucketVals.end());
                                }
                                fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucketValsD, bucketValsD.size());
                                if (b.count > 0) {
                                    row.push_back(b.mean);
                                    row.push_back(b.stddev);
                                    row.push_back(b.min);
                                    row.push_back(b.max);
                                } else {
                                    for (int k = 0; k < 4; ++k) row.push_back(sentinel.noheight.value);
                                }
                            }
                        }
                    }
                    if (enableExp) {
                        for (const auto& name : expNames) row.push_back(expBands[name][idx]);
                    }

                    tableWriter.WriteRow(row);
                }
            }
            tableWriter.Close();
        }
        }
    }
    auto tIO1 = std::chrono::steady_clock::now();
    secIO = std::chrono::duration<double>(tIO1 - tIO0).count();

    if (enableProfile) {
        auto tProfileEnd = std::chrono::steady_clock::now();
        double secTotal = std::chrono::duration<double>(tProfileEnd - tProfileStart).count();
        double secOther = secTotal - (secReadDecomp + secBinning + secMetrics + secIO);
        if (secOther < 0.0) secOther = 0.0;

        std::cout << "\n"
                  << "================================================================================\n"
                  << "GridMetrics High-Resolution Execution Profile\n"
                  << "================================================================================\n"
                  << std::left << std::setw(36) << "Section"
                  << std::right << std::setw(12) << "Time (s)"
                  << std::setw(12) << "Percent\n"
                  << "--------------------------------------------------------------------------------\n";
        auto printRow = [&](const char* name, double sec) {
            double pct = (secTotal > 0.0) ? (sec / secTotal * 100.0) : 0.0;
            std::cout << std::left << std::setw(36) << name
                      << std::right << std::setw(10) << std::fixed << std::setprecision(3) << sec << " s"
                      << std::setw(10) << std::fixed << std::setprecision(1) << pct << " %\n";
        };
        printRow("Read & Decompression", secReadDecomp);
        printRow("Spatial Binning & Ground Lookup", secBinning);
        printRow("Metric Computation & Statistics", secMetrics);
        printRow("GeoTIFF Raster & Table I/O", secIO);
        printRow("Setup & Other", secOther);
        std::cout << "--------------------------------------------------------------------------------\n";
        printRow("Total Elapsed Time", secTotal);
        std::cout << "================================================================================\n\n";
    }

    std::cout << "[GridMetrics] Grid metrics processing completed successfully.\n";
    return 0;
}

