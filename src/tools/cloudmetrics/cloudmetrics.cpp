// cloudmetrics.cpp : Modernized CloudMetrics Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/metrics/ExperimentalMetrics.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <filesystem>

struct LidarStats {
    uint64_t totalPoints{0};
    uint64_t pointsAboveMinHt{0};
    double minZ{1e9};
    double maxZ{-1e9};
    double meanZ{0.0};
    double stdDevZ{0.0};
    double p05{0.0};
    double p25{0.0};
    double p50{0.0};
    double p75{0.0};
    double p95{0.0};
    double p99{0.0};
    double canopyCover{0.0};
    double canopyReliefRatio{0.0};
};

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("cloudmetrics", "Computes Summary Metrics for Point Cloud Clips");
    parser.SetPositionalArgsUsage("<input.las/laz or directory> [optional ground DTM path]");
    parser.AddOption("output", "Output CSV file path", "cloud_metrics.csv");
    parser.AddOption("ground", "Path to ground DEM raster for height normalization, or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("minht", "Minimum height cutoff for canopy metrics (m)", "2.0");
    parser.AddOption("cellsize", "Grid cell size for 2D area/volume metrics (m)", "10.0");
    parser.AddOption("voxelsize", "3D voxel resolution for voxel volume metrics (m)", "20.0");
    parser.AddFlag("exp", "Compute additional experimental metrics from RSForTools");

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

    // 2. Resolve input point cloud files and directories
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

    std::cout << "[CloudMetrics] Processing point cloud metrics for " << inputFiles.size() << " file(s)...\n";

    std::vector<double> heights;
    std::vector<fusion::metrics::Point3D> allPoints;
    fusion::lidar::PointRecord pt;
    uint64_t totalPts = 0;
    uint64_t ptsAboveMin = 0;

    while (reader.ReadNextPoint(pt)) {
        totalPts++;
        double h = pt.z;
        if (hasGround) {
            if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                h -= *gz;
            }
        }
        if (enableExp) {
            allPoints.push_back({pt.x, pt.y, h});
        }
        if (h >= minHt) {
            heights.push_back(h);
            ptsAboveMin++;
        }
    }
    reader.Close();

    LidarStats stats;
    stats.totalPoints = totalPts;
    stats.pointsAboveMinHt = ptsAboveMin;

    if (totalPts > 0) {
        stats.canopyCover = (static_cast<double>(ptsAboveMin) / totalPts) * 100.0;
    }

    if (!heights.empty()) {
        std::sort(heights.begin(), heights.end());
        stats.minZ = heights.front();
        stats.maxZ = heights.back();

        double sum = std::accumulate(heights.begin(), heights.end(), 0.0);
        stats.meanZ = sum / heights.size();

        double sqSum = 0.0;
        for (double h : heights) {
            sqSum += (h - stats.meanZ) * (h - stats.meanZ);
        }
        stats.stdDevZ = std::sqrt(sqSum / heights.size());

        auto getPercentile = [&](double pct) {
            size_t idx = static_cast<size_t>(std::round(pct * (heights.size() - 1)));
            return heights[std::min(idx, heights.size() - 1)];
        };

        stats.p05 = getPercentile(0.05);
        stats.p25 = getPercentile(0.25);
        stats.p50 = getPercentile(0.50);
        stats.p75 = getPercentile(0.75);
        stats.p95 = getPercentile(0.95);
        stats.p99 = getPercentile(0.99);

        if (stats.maxZ > stats.minZ) {
            stats.canopyReliefRatio = (stats.meanZ - stats.minZ) / (stats.maxZ - stats.minZ);
        }
    }

    fusion::metrics::ExperimentalMetricsResults expRes;
    if (enableExp && !allPoints.empty()) {
        fusion::metrics::ExperimentalMetricsOptions opts;
        opts.minHt = minHt;
        opts.cellSize = cellSize;
        opts.voxelSize = voxelSize;
        expRes = fusion::metrics::ComputeExperimentalMetrics(allPoints, opts);
    }

    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Error: Failed to create output CSV file: " << outputPath << "\n";
        return 1;
    }

    outFile << "Filename,TotalPoints,CanopyPoints,MinZ,MaxZ,MeanZ,StdDevZ,P05,P25,P50,P75,P95,P99,CanopyCoverPct,CRR";
    if (enableExp) {
        auto expNames = fusion::metrics::GetExperimentalMetricsNames();
        for (const auto& name : expNames) {
            outFile << "," << name;
        }
    }
    outFile << "\n";

    std::string fileLabel = (inputFiles.size() == 1)
        ? inputFiles[0].filename().string()
        : ("merged_" + std::to_string(inputFiles.size()) + "_files");

    outFile << fileLabel << ","
            << stats.totalPoints << ","
            << stats.pointsAboveMinHt << ","
            << stats.minZ << ","
            << stats.maxZ << ","
            << stats.meanZ << ","
            << stats.stdDevZ << ","
            << stats.p05 << ","
            << stats.p25 << ","
            << stats.p50 << ","
            << stats.p75 << ","
            << stats.p95 << ","
            << stats.p99 << ","
            << stats.canopyCover << ","
            << stats.canopyReliefRatio;

    if (enableExp) {
        auto expMap = fusion::metrics::GetExperimentalMetricsAsMap(expRes);
        auto expNames = fusion::metrics::GetExperimentalMetricsNames();
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
                  << "m, voxelSize=" << voxelSize << "m.\n";
    }
    std::cout << "[CloudMetrics] Output written to: " << outputPath << "\n";

    return 0;
}
