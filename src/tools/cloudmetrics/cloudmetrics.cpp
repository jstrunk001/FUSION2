// cloudmetrics.cpp : Modernized CloudMetrics Executable for FUSION2
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/cli/ParseUtil.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/PointFilter.h"
#include "fusion/metrics/ExperimentalMetrics.h"
#include "fusion/metrics/SurfaceStats.h"
#include "fusion/metrics/SentinelPolicy.h"
#include "fusion/metrics/PointCloudStats.h"
#include "fusion/metrics/SpectralChannels.h"
#include "fusion/geom/PolygonFeatureSet.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <iomanip>

// CSV output writes the literal text "NA" wherever a value resolved to an
// NA sentinel, not the numeric NaN (Shared groundwork C) -- real computed
// statistics in this domain are never themselves NaN, so a NaN value
// uniquely identifies "this came from an NA-resolved /nodata or /noheight".
static void WriteCSVDouble(std::ofstream& out, double value) {
    if (std::isnan(value)) {
        out << "NA";
    } else {
        out << value;
    }
}

struct CloudStatsRow {
    uint64_t totalPoints{0};
    uint64_t pointsAboveMinHt{0};
    double canopyCover{0.0};
    std::vector<float> elevVals; // PointStatBundleAsVector("elev_") order, or all-sentinel
    float elevProfileArea{0.0f};
    std::vector<float> intVals;  // same, for intensities
};

// Reduces one point set's already-height-filtered elevations (heights) and
// intensities plus its unfiltered totalPts/ptsAboveMin counts into one CSV
// row's worth of values. Shared by the single-cloud path (called once) and
// /shape polygon mode (called once per feature) so the two modes can't
// drift apart numerically. Applies Shared groundwork C's two-tier sentinel
// rule: totalPts == 0 (nothing at all landed here) resolves every field,
// including the counts themselves, to /nodata; totalPts > 0 but
// heights.empty() (points exist but none clear /minht) resolves only the
// height-filtered fields (elev_*, int_*, elev_profile_area) to /noheight --
// canopyCover is well-defined directly from the unfiltered return count, so
// it's never subject to /noheight.
static CloudStatsRow ComputeCloudStats(
        std::vector<double> heights, std::vector<double> intensities,
        uint64_t totalPts, uint64_t ptsAboveMin,
        size_t elevColCount, size_t intColCount,
        const fusion::metrics::SentinelPolicy& policy) {
    CloudStatsRow row;
    row.totalPoints = totalPts;
    row.pointsAboveMinHt = ptsAboveMin;

    double ND = policy.nodata.value;
    double NH = policy.noheight.value;
    row.canopyCover = ND;
    row.elevVals.assign(elevColCount, static_cast<float>(ND));
    row.elevProfileArea = static_cast<float>(ND);
    row.intVals.assign(intColCount, static_cast<float>(ND));

    if (totalPts == 0) {
        return row;
    }

    row.canopyCover = (static_cast<double>(ptsAboveMin) / totalPts) * 100.0;

    if (!heights.empty()) {
        std::vector<float> heightsF(heights.begin(), heights.end());
        fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(heightsF);
        row.elevVals = fusion::metrics::PointStatBundleAsVector(bundle);
        row.elevProfileArea = fusion::metrics::ComputeProfileArea(bundle);
    } else {
        row.elevVals.assign(elevColCount, static_cast<float>(NH));
        row.elevProfileArea = static_cast<float>(NH);
    }

    if (!intensities.empty()) {
        std::vector<float> intensitiesF(intensities.begin(), intensities.end());
        fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(intensitiesF);
        row.intVals = fusion::metrics::PointStatBundleAsVector(bundle);
    } else {
        row.intVals.assign(intColCount, static_cast<float>(NH));
    }

    return row;
}

// Writes one CSV row's stat fields, starting from TotalPoints -- callers
// write their own leading identity column(s) (Filename, or
// FeatureIndex/Label) and the comma separating them from this row first.
static void WriteCloudStatsRow(std::ofstream& outFile, const CloudStatsRow& row) {
    if (row.totalPoints == 0) {
        outFile << "NA,NA,";
    } else {
        outFile << row.totalPoints << "," << row.pointsAboveMinHt << ",";
    }
    WriteCSVDouble(outFile, row.canopyCover);
    for (float v : row.elevVals) {
        outFile << ",";
        WriteCSVDouble(outFile, v);
    }
    outFile << ",";
    WriteCSVDouble(outFile, row.elevProfileArea);
    for (float v : row.intVals) {
        outFile << ",";
        WriteCSVDouble(outFile, v);
    }
}

// /strata and /intstrata (item 6): both bucket points by elevation using
// the shared fusion::metrics::AssignStratumIndex helper, identical bucket
// boundary semantics to gridmetrics -- but /strata's buckets hold each
// point's elevation (an elevation statistic bundle per bucket) while
// /intstrata's buckets hold each point's intensity (an intensity statistic
// bundle per bucket), so the two options can use independent threshold
// lists and independent bucket counts.
static void AssignToStrataBuckets(
        double elevation, double intensity,
        const std::vector<double>& strata, std::vector<std::vector<double>>& strataElevBuckets,
        const std::vector<double>& intStrata, std::vector<std::vector<double>>& intStrataIntBuckets) {
    if (!strata.empty()) {
        strataElevBuckets[fusion::metrics::AssignStratumIndex(elevation, strata)].push_back(elevation);
    }
    if (!intStrata.empty()) {
        intStrataIntBuckets[fusion::metrics::AssignStratumIndex(elevation, intStrata)].push_back(intensity);
    }
}

static void WriteStrataHeader(std::ofstream& out, size_t numBuckets, const std::string& prefixBase) {
    for (size_t s = 0; s < numBuckets; ++s) {
        std::ostringstream label;
        label << std::setw(2) << std::setfill('0') << s;
        std::string prefix = prefixBase + label.str() + "_";
        for (const auto& name : fusion::metrics::StrataStatBundleColumnNames(prefix)) {
            out << "," << name;
        }
    }
}

// cellEmpty (totalPts == 0 for the whole cloud/feature) forces every column
// to /nodata, matching every other column in the row; otherwise each
// bucket's count/proportion are always real (a return count is always
// well-defined once the cloud/feature has data), and mean/stddev/min/max
// fall back to /noheight only for a bucket that itself has zero points.
// Six simplified metrics per bucket (count, proportion, mean, stddev, min,
// max) in place of the full ~40-field statistic bundle.
static void WriteStrataColumns(std::ofstream& out, const std::vector<std::vector<double>>& buckets,
                                uint64_t totalPts, const fusion::metrics::SentinelPolicy& policy, bool cellEmpty) {
    for (const auto& bucket : buckets) {
        if (cellEmpty) {
            out << ",NA,NA,NA,NA,NA,NA";
            continue;
        }
        fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucket, totalPts);
        out << "," << b.count << ",";
        WriteCSVDouble(out, b.proportion);
        if (b.count > 0) {
            out << ","; WriteCSVDouble(out, b.mean);
            out << ","; WriteCSVDouble(out, b.stddev);
            out << ","; WriteCSVDouble(out, b.min);
            out << ","; WriteCSVDouble(out, b.max);
        } else {
            for (int k = 0; k < 4; ++k) { out << ","; WriteCSVDouble(out, policy.noheight.value); }
        }
    }
}

// /rgbstrata (new): each stratum bucket's spectral-channel values, indexed
// [channel][bucket] -- populated unconditionally alongside /strata's own
// elevation buckets (not gated by /minht), since height-strata partitioning
// covers the whole vertical return profile, not just canopy returns.
static void AssignToSpectralStrataBuckets(
        double elevation, const fusion::lidar::PointRecord& pt, const std::vector<double>& strata,
        const std::vector<fusion::metrics::SpectralChannelSpec>& channels,
        std::vector<std::vector<std::vector<double>>>& spectralStrataBuckets) {
    if (strata.empty() || channels.empty()) return;
    size_t sIdx = fusion::metrics::AssignStratumIndex(elevation, strata);
    for (size_t c = 0; c < channels.size(); ++c) {
        spectralStrataBuckets[c][sIdx].push_back(static_cast<double>(pt.*(channels[c].field)));
    }
}

static void WriteSpectralStrataHeader(std::ofstream& out, size_t numBuckets,
                                       const std::vector<fusion::metrics::SpectralChannelSpec>& channels) {
    for (const auto& spec : channels) {
        for (size_t s = 0; s < numBuckets; ++s) {
            std::ostringstream label;
            label << std::setw(2) << std::setfill('0') << s;
            std::string prefix = spec.prefix + "_stratum_" + label.str() + "_";
            out << "," << prefix << "mean," << prefix << "stddev," << prefix << "min," << prefix << "max";
        }
    }
}

// Four metrics per channel per bucket (mean, stddev, min, max) -- count and
// proportion are already reported once by the elevation /strata columns
// sharing the same bucket boundaries, so they aren't repeated here.
static void WriteSpectralStrataColumns(std::ofstream& out,
                                        const std::vector<std::vector<std::vector<double>>>& spectralStrataBuckets,
                                        const fusion::metrics::SentinelPolicy& policy, bool cellEmpty) {
    for (const auto& perChannelBuckets : spectralStrataBuckets) {
        for (const auto& bucket : perChannelBuckets) {
            if (cellEmpty) {
                out << ",NA,NA,NA,NA";
                continue;
            }
            fusion::metrics::StrataStatBundle b = fusion::metrics::ComputeStrataStatBundle(bucket, bucket.size());
            if (b.count > 0) {
                out << ","; WriteCSVDouble(out, b.mean);
                out << ","; WriteCSVDouble(out, b.stddev);
                out << ","; WriteCSVDouble(out, b.min);
                out << ","; WriteCSVDouble(out, b.max);
            } else {
                for (int k = 0; k < 4; ++k) { out << ","; WriteCSVDouble(out, policy.noheight.value); }
            }
        }
    }
}

// /rgb: (item 7) -- one full statistic bundle per selected spectral
// channel, same call shape as elev_/int_ but looped over the channel list.
// Applies the same /noheight-when-empty-but-not-nodata rule as int_.
static void WriteSpectralHeader(std::ofstream& out, const std::vector<fusion::metrics::SpectralChannelSpec>& channels) {
    for (const auto& spec : channels) {
        for (const auto& name : fusion::metrics::PointStatBundleColumnNames(spec.prefix + "_")) {
            out << "," << name;
        }
    }
}

static void WriteSpectralColumns(std::ofstream& out, const std::vector<fusion::metrics::SpectralChannelSpec>& channels,
                                  const std::vector<std::vector<double>>& valuesByChannel,
                                  size_t bundleColCount, const fusion::metrics::SentinelPolicy& policy, bool cellEmpty) {
    for (size_t c = 0; c < channels.size(); ++c) {
        if (cellEmpty) {
            for (size_t k = 0; k < bundleColCount; ++k) out << ",NA";
            continue;
        }
        const auto& vals = valuesByChannel[c];
        if (!vals.empty()) {
            std::vector<float> valsF(vals.begin(), vals.end());
            fusion::metrics::PointStatBundle bundle = fusion::metrics::ComputePointStatBundle(valsF);
            for (float v : fusion::metrics::PointStatBundleAsVector(bundle)) {
                out << ",";
                WriteCSVDouble(out, v);
            }
        } else {
            for (size_t k = 0; k < bundleColCount; ++k) {
                out << ",";
                WriteCSVDouble(out, policy.noheight.value);
            }
        }
    }
}

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("cloudmetrics", "Computes Summary Metrics for Point Cloud Clips");
    parser.SetPositionalArgsUsage("<input.las/laz or directory> [optional ground DTM path]");
    parser.AddOption("output", "Output CSV file path", "cloud_metrics.csv");
    parser.AddOption("ground", "Path to ground DEM raster for height normalization, or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("minht", "Minimum height cutoff for canopy metrics (same units as the input)", "2.0");
    parser.AddOption("cellsize", "Grid cell size for 2D area/volume metrics (same units as the input)", "10.0");
    parser.AddOption("voxelsize", "3D voxel resolution for voxel volume metrics (same units as the input)", "20.0");
    parser.AddFlag("exp", "Compute additional experimental metrics from RSForTools");
    parser.AddFlag("surfstats", "Compute surface area ratio, roughness, planimetric area, and 3D surface area from a /cellsize elevation grid (top-of-cloud max per cell). Not available with /shape.");
    parser.AddOption("shape", "Polygon shapefile -- compute one metrics row per polygon feature instead of one row for the whole cloud");
    parser.AddOption("field", "Attribute field used to label each /shape polygon's output row; falls back to a zero-padded feature index when omitted or missing on a feature");
    parser.AddOption("nodata", "Value for a cloud/feature with zero points at all: NA, or a number such as 0, -9999, or inf", "NA");
    parser.AddOption("noheight", "Value for height-dependent columns when points exist but none clear the height cutoff: NA, or a number such as 0, -9999, or inf", "0");
    parser.AddOption("strata", "Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0) -- same syntax as gridmetrics' /strata. Appends stratum_N_count/proportion plus a full elevation statistic bundle per bucket.");
    parser.AddOption("intstrata", "Comma-separated height thresholds bucketing points the same way as /strata, but reporting an intensity statistic bundle per bucket instead of elevation (defaults to /strata's thresholds if omitted).");
    parser.AddOption("rgb", "Comma-separated spectral channels to compute a statistic bundle for: R, G, B, N, or all (every channel the input file's LAS point format actually carries)");
    parser.AddFlag("rgbstrata", "With /rgb and /strata both set, also report a mean/stddev/min/max bundle per selected spectral channel within each height-stratum bucket (<channel>_stratum_NN_*).");
    fusion::lidar::PointFilter::RegisterOptions(parser);

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    std::string outputPath = parser.GetOption("output").value_or("cloud_metrics.csv");
    double minHt = std::stod(parser.GetOption("minht").value_or("2.0"));
    double cellSize = std::stod(parser.GetOption("cellsize").value_or("10.0"));
    double voxelSize = std::stod(parser.GetOption("voxelsize").value_or("20.0"));
    bool enableExp = parser.HasFlag("exp");
    bool enableSurfStats = parser.HasFlag("surfstats");
    std::string fieldName = parser.GetOption("field").value_or("");

    fusion::metrics::SentinelPolicy sentinel;
    sentinel.nodata = fusion::metrics::ParseSentinelOption(parser.GetOption("nodata").value_or("NA"));
    sentinel.noheight = fusion::metrics::ParseSentinelOption(parser.GetOption("noheight").value_or("0"));

    std::vector<double> strata = parser.GetOption("strata") ? fusion::cli::ParseFloatList(*parser.GetOption("strata")) : std::vector<double>{};
    std::vector<double> intStrata = parser.GetOption("intstrata") ? fusion::cli::ParseFloatList(*parser.GetOption("intstrata")) : strata;
    size_t numStrataBuckets = strata.empty() ? 0 : strata.size() + 1;

    std::vector<std::string> elevColNames = fusion::metrics::PointStatBundleColumnNames("elev_");
    std::vector<std::string> intColNames = fusion::metrics::PointStatBundleColumnNames("int_");
    // Column count for /rgb's own whole-cloud/feature per-channel bundle
    // (unaffected by the /strata simplification below -- still the full
    // ~38-stat bundle, same as elev_/int_).
    size_t fullBundleColCount = fusion::metrics::PointStatBundleColumnNames("").size();

    fusion::lidar::PointFilter pointFilter = fusion::lidar::PointFilter::FromParser(parser);

    // 1. Resolve ground surface DTM (single file or directory of tiles)
    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    std::string groundPathStr;
    if (auto groundPath = parser.GetOption("ground")) {
        groundPathStr = *groundPath;
    } else if (posArgs.size() > 1 && (std::filesystem::is_directory(posArgs.back()) ||
                                      posArgs.back().find(".tif") != std::string::npos ||
                                      posArgs.back().find(".dtm") != std::string::npos ||
                                      posArgs.back().find(".img") != std::string::npos ||
                                      posArgs.back().find(".asc") != std::string::npos)) {
        groundPathStr = posArgs.back();
    }
    if (!groundPathStr.empty()) {
        hasGround = groundRaster.Open(groundPathStr);
        if (hasGround) {
            const auto& gi = groundRaster.GetInfo();
            std::cout << "[CloudMetrics] Loaded ground surface DEM: " << groundPathStr
                      << " | Extent: [" << gi.minX << ", " << gi.minY << "] to [" << gi.maxX << ", " << gi.maxY << "]"
                      << " | Size: " << gi.width << "x" << gi.height << " | NoData: " << gi.noDataValue << "\n";
        } else {
            std::cerr << "Warning: Failed to open ground DEM: " << groundPathStr << ". Processing using raw elevations.\n";
        }
    }

    // 2. Resolve polygon feature set, if /shape was given
    fusion::geom::PolygonFeatureSet featureSet;
    bool haveShape = false;
    if (auto shapePath = parser.GetOption("shape")) {
        if (!featureSet.LoadShapefile(*shapePath, fieldName)) {
            std::cerr << "Error: Failed to load polygon shapefile: " << *shapePath << "\n";
            return 1;
        }
        haveShape = true;
    }

    // 3. Resolve input point cloud files and directories
    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    fusion::lidar::MergedPointCloudReader reader;
    if (!reader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open input point cloud(s).\n";
        return 1;
    }

    // /rgb: -- a requested channel absent from the file's point format is a
    // printed warning, never a hard failure, since a batch run over
    // mixed-format tiles is a real, expected case.
    fusion::metrics::SpectralChannelSelection spectralSelection;
    if (auto rgbOpt = parser.GetOption("rgb")) {
        spectralSelection = fusion::metrics::ParseSpectralChannels(*rgbOpt, reader.GetHeader().pointFormat);
        for (const auto& token : spectralSelection.unknownTokens) {
            std::cerr << "Warning: /rgb channel '" << token << "' is not available for LAS point format "
                      << static_cast<int>(reader.GetHeader().pointFormat) << " -- skipped.\n";
        }
    }

    bool enableRgbStrata = parser.HasFlag("rgbstrata");
    if (enableRgbStrata && (strata.empty() || spectralSelection.channels.empty())) {
        std::cerr << "Warning: /rgbstrata requires both /strata and /rgb to be set -- ignored.\n";
        enableRgbStrata = false;
    }

    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Error: Failed to create output CSV file: " << outputPath << "\n";
        return 1;
    }

    auto expNames = enableExp ? fusion::metrics::GetExperimentalMetricsNames() : std::vector<std::string>{};

    if (haveShape) {
        std::cout << "[CloudMetrics] Processing point cloud metrics for " << inputFiles.size()
                  << " file(s) against " << featureSet.FeatureCount() << " polygon feature(s)...\n";

        // A point belongs to at most one feature, so this is one
        // accumulator's worth of memory per point, not N -- the full,
        // uncropped cloud is streamed exactly once.
        std::vector<std::vector<double>> heightsByFeature(featureSet.FeatureCount());
        std::vector<std::vector<double>> intensitiesByFeature(featureSet.FeatureCount());
        std::vector<std::vector<fusion::metrics::Point3D>> pointsByFeature(
            enableExp ? featureSet.FeatureCount() : 0);
        std::vector<uint64_t> totalPtsByFeature(featureSet.FeatureCount(), 0);
        std::vector<uint64_t> ptsAboveMinByFeature(featureSet.FeatureCount(), 0);
        std::vector<std::vector<std::vector<double>>> strataElevBucketsByFeature(
            featureSet.FeatureCount(), std::vector<std::vector<double>>(numStrataBuckets));
        std::vector<std::vector<std::vector<double>>> intStrataIntBucketsByFeature(
            featureSet.FeatureCount(), std::vector<std::vector<double>>(intStrata.empty() ? 0 : intStrata.size() + 1));
        std::vector<std::vector<std::vector<double>>> spectralByFeature(
            featureSet.FeatureCount(), std::vector<std::vector<double>>(spectralSelection.channels.size()));
        std::vector<std::vector<std::vector<std::vector<double>>>> spectralStrataByFeature(
            enableRgbStrata ? featureSet.FeatureCount() : 0,
            std::vector<std::vector<std::vector<double>>>(
                spectralSelection.channels.size(), std::vector<std::vector<double>>(numStrataBuckets)));

        fusion::lidar::PointRecord pt;
        while (reader.ReadNextPoint(pt)) {
            if (!pointFilter.Keep(pt)) continue;
            size_t featureIdx = featureSet.FindContaining(pt.x, pt.y);
            if (featureIdx == fusion::geom::PolygonFeatureSet::npos) {
                continue;
            }

            double h = pt.z;
            if (hasGround) {
                auto gz = groundRaster.GetElevation(pt.x, pt.y);
                if (!gz) {
                    continue;
                }
                h -= *gz;
            }

            totalPtsByFeature[featureIdx]++;
            if (enableExp) {
                pointsByFeature[featureIdx].push_back({pt.x, pt.y, h});
            }
            AssignToStrataBuckets(h, static_cast<double>(pt.intensity), strata, strataElevBucketsByFeature[featureIdx],
                                   intStrata, intStrataIntBucketsByFeature[featureIdx]);
            if (enableRgbStrata) {
                AssignToSpectralStrataBuckets(h, pt, strata, spectralSelection.channels, spectralStrataByFeature[featureIdx]);
            }
            if (h >= minHt) {
                heightsByFeature[featureIdx].push_back(h);
                intensitiesByFeature[featureIdx].push_back(static_cast<double>(pt.intensity));
                ptsAboveMinByFeature[featureIdx]++;
                for (size_t ch = 0; ch < spectralSelection.channels.size(); ++ch) {
                    spectralByFeature[featureIdx][ch].push_back(static_cast<double>(pt.*(spectralSelection.channels[ch].field)));
                }
            }
        }
        reader.Close();

        outFile << "FeatureIndex,Label,TotalPoints,CanopyPoints,CanopyCoverPct";
        for (const auto& name : elevColNames) outFile << "," << name;
        outFile << ",elev_profile_area";
        for (const auto& name : intColNames) outFile << "," << name;
        WriteStrataHeader(outFile, numStrataBuckets, "stratum_");
        WriteStrataHeader(outFile, intStrata.empty() ? 0 : intStrata.size() + 1, "intstratum_");
        WriteSpectralHeader(outFile, spectralSelection.channels);
        if (enableRgbStrata) {
            WriteSpectralStrataHeader(outFile, numStrataBuckets, spectralSelection.channels);
        }
        if (enableExp) {
            for (const auto& name : expNames) {
                outFile << "," << name;
            }
        }
        outFile << "\n";

        for (size_t f = 0; f < featureSet.FeatureCount(); ++f) {
            CloudStatsRow row = ComputeCloudStats(
                std::move(heightsByFeature[f]), std::move(intensitiesByFeature[f]),
                totalPtsByFeature[f], ptsAboveMinByFeature[f],
                elevColNames.size(), intColNames.size(), sentinel);

            outFile << f << "," << featureSet.Label(f) << ",";
            WriteCloudStatsRow(outFile, row);
            bool featureEmpty = (totalPtsByFeature[f] == 0);
            WriteStrataColumns(outFile, strataElevBucketsByFeature[f], totalPtsByFeature[f], sentinel, featureEmpty);
            WriteStrataColumns(outFile, intStrataIntBucketsByFeature[f], totalPtsByFeature[f], sentinel, featureEmpty);
            WriteSpectralColumns(outFile, spectralSelection.channels, spectralByFeature[f], fullBundleColCount, sentinel, featureEmpty);
            if (enableRgbStrata) {
                WriteSpectralStrataColumns(outFile, spectralStrataByFeature[f], sentinel, featureEmpty);
            }
            if (enableExp) {
                fusion::metrics::ExperimentalMetricsResults expRes;
                if (!pointsByFeature[f].empty()) {
                    fusion::metrics::ExperimentalMetricsOptions opts;
                    opts.minHt = minHt;
                    opts.cellSize = cellSize;
                    opts.voxelSize = voxelSize;
                    expRes = fusion::metrics::ComputeExperimentalMetrics(pointsByFeature[f], opts);
                }
                auto expMap = fusion::metrics::GetExperimentalMetricsAsMap(expRes);
                for (const auto& name : expNames) {
                    outFile << "," << expMap[name];
                }
            }
            outFile << "\n";
        }

        outFile.close();
        std::cout << "[CloudMetrics] Successfully computed metrics for " << featureSet.FeatureCount() << " polygon feature(s).\n";
        if (hasGround) {
            std::cout << "[CloudMetrics] Height normalization applied using ground DEM.\n";
        }
        std::cout << "[CloudMetrics] Output written to: " << outputPath << "\n";
        return 0;
    }

    std::cout << "[CloudMetrics] Processing point cloud metrics for " << inputFiles.size() << " file(s)...\n";

    // /surfstats scratch grid: bins each point's height into a cols x rows
    // grid (same col/row indexing idiom gridmetrics uses), tracking each
    // cell's max height -- never written to disk, only used to feed
    // ComputeSurfaceStatsGrid/SummarizeSurfaceStats below.
    const auto& header = reader.GetHeader();
    constexpr float kSurfNoValue = -3.0e38f; // private, reliably `==`-comparable marker -- see gridmetrics.cpp's identical use
    int surfCols = 1, surfRows = 1;
    std::vector<float> surfCellMax;
    if (enableSurfStats) {
        surfCols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
        surfRows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
        if (surfCols <= 0) surfCols = 1;
        if (surfRows <= 0) surfRows = 1;
        surfCellMax.assign(static_cast<size_t>(surfCols) * surfRows, kSurfNoValue);
    }

    std::vector<double> heights;
    std::vector<double> intensities;
    std::vector<fusion::metrics::Point3D> allPoints;
    std::vector<std::vector<double>> strataElevBuckets(numStrataBuckets);
    std::vector<std::vector<double>> intStrataIntBuckets(intStrata.empty() ? 0 : intStrata.size() + 1);
    std::vector<std::vector<double>> spectralValues(spectralSelection.channels.size());
    std::vector<std::vector<std::vector<double>>> spectralStrataBuckets(
        spectralSelection.channels.size(), std::vector<std::vector<double>>(numStrataBuckets));
    fusion::lidar::PointRecord pt;
    uint64_t totalPts = 0;
    uint64_t ptsAboveMin = 0;

    while (reader.ReadNextPoint(pt)) {
        if (!pointFilter.Keep(pt)) continue;
        totalPts++;
        double h = pt.z;
        if (hasGround) {
            auto gz = groundRaster.GetElevation(pt.x, pt.y);
            if (!gz) {
                continue;
            }
            h -= *gz;
        }
        if (enableExp) {
            allPoints.push_back({pt.x, pt.y, h});
        }
        AssignToStrataBuckets(h, static_cast<double>(pt.intensity), strata, strataElevBuckets, intStrata, intStrataIntBuckets);
        if (enableRgbStrata) {
            AssignToSpectralStrataBuckets(h, pt, strata, spectralSelection.channels, spectralStrataBuckets);
        }
        if (enableSurfStats) {
            int col = static_cast<int>((pt.x - header.minX) / cellSize);
            int row = static_cast<int>((header.maxY - pt.y) / cellSize);
            if (col == surfCols && pt.x == header.maxX) col = surfCols - 1;
            if (row == surfRows && pt.y == header.minY) row = surfRows - 1;
            if (col >= 0 && col < surfCols && row >= 0 && row < surfRows) {
                float& cellMax = surfCellMax[static_cast<size_t>(row) * surfCols + col];
                cellMax = (cellMax == kSurfNoValue) ? static_cast<float>(h) : std::max(cellMax, static_cast<float>(h));
            }
        }
        if (h >= minHt) {
            heights.push_back(h);
            intensities.push_back(static_cast<double>(pt.intensity));
            ptsAboveMin++;
            for (size_t ch = 0; ch < spectralSelection.channels.size(); ++ch) {
                spectralValues[ch].push_back(static_cast<double>(pt.*(spectralSelection.channels[ch].field)));
            }
        }
    }
    reader.Close();

    fusion::metrics::SurfaceStatsSummary surfSummary;
    if (enableSurfStats) {
        fusion::metrics::SurfaceStatsGrid surfGrid = fusion::metrics::ComputeSurfaceStatsGrid(
            surfCellMax, surfCols, surfRows, cellSize, kSurfNoValue);
        surfSummary = fusion::metrics::SummarizeSurfaceStats(surfGrid, surfCellMax, surfCols, surfRows, cellSize, kSurfNoValue);
    }

    CloudStatsRow row = ComputeCloudStats(
        std::move(heights), std::move(intensities), totalPts, ptsAboveMin,
        elevColNames.size(), intColNames.size(), sentinel);

    fusion::metrics::ExperimentalMetricsResults expRes;
    if (enableExp && !allPoints.empty()) {
        fusion::metrics::ExperimentalMetricsOptions opts;
        opts.minHt = minHt;
        opts.cellSize = cellSize;
        opts.voxelSize = voxelSize;
        expRes = fusion::metrics::ComputeExperimentalMetrics(allPoints, opts);
    }

    outFile << "Filename,TotalPoints,CanopyPoints,CanopyCoverPct";
    for (const auto& name : elevColNames) outFile << "," << name;
    outFile << ",elev_profile_area";
    for (const auto& name : intColNames) outFile << "," << name;
    WriteStrataHeader(outFile, numStrataBuckets, "stratum_");
    WriteStrataHeader(outFile, intStrata.empty() ? 0 : intStrata.size() + 1, "intstratum_");
    WriteSpectralHeader(outFile, spectralSelection.channels);
    if (enableRgbStrata) {
        WriteSpectralStrataHeader(outFile, numStrataBuckets, spectralSelection.channels);
    }
    if (enableSurfStats) {
        outFile << ",SurfaceAreaRatioMean,RoughnessMean,PlanimetricArea,SurfaceArea3D";
    }
    if (enableExp) {
        for (const auto& name : expNames) {
            outFile << "," << name;
        }
    }
    outFile << "\n";

    std::string fileLabel = (inputFiles.size() == 1)
        ? inputFiles[0].filename().string()
        : ("merged_" + std::to_string(inputFiles.size()) + "_files");

    outFile << fileLabel << ",";
    WriteCloudStatsRow(outFile, row);
    {
        bool cloudEmpty = (totalPts == 0);
        WriteStrataColumns(outFile, strataElevBuckets, totalPts, sentinel, cloudEmpty);
        WriteStrataColumns(outFile, intStrataIntBuckets, totalPts, sentinel, cloudEmpty);
        WriteSpectralColumns(outFile, spectralSelection.channels, spectralValues, fullBundleColCount, sentinel, cloudEmpty);
        if (enableRgbStrata) {
            WriteSpectralStrataColumns(outFile, spectralStrataBuckets, sentinel, cloudEmpty);
        }
    }

    if (enableSurfStats) {
        outFile << "," << surfSummary.surfaceAreaRatioMean << "," << surfSummary.roughnessMean
                << "," << surfSummary.planimetricArea << "," << surfSummary.surfaceArea3D;
    }

    if (enableExp) {
        auto expMap = fusion::metrics::GetExperimentalMetricsAsMap(expRes);
        for (const auto& name : expNames) {
            outFile << "," << expMap[name];
        }
    }
    outFile << "\n";
    outFile.close();

    std::cout << "[CloudMetrics] Successfully computed metrics (" << totalPts << " total points).\n";
    if (hasGround) {
        std::cout << "[CloudMetrics] Height normalization applied using ground DEM.\n";
    }
    if (enableExp) {
        std::cout << "[CloudMetrics] Experimental metrics calculated using cellSize=" << cellSize
                  << ", voxelSize=" << voxelSize << ".\n";
    }
    std::cout << "[CloudMetrics] Output written to: " << outputPath << "\n";

    return 0;
}
