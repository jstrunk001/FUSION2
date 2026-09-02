// cloudmetrics.cpp : Modernized CloudMetrics Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/lidar/LASPointCloud.h"

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
    parser.AddOption("output", "Output CSV file path", "cloud_metrics.csv");
    parser.AddOption("minht", "Minimum height cutoff for canopy metrics (m)", "2.0");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file is required.\n";
        parser.PrintHelp();
        return 1;
    }

    std::filesystem::path inputPath = posArgs[0];
    std::string outputPath = parser.GetOption("output").value_or("cloud_metrics.csv");
    double minHt = std::stod(parser.GetOption("minht").value_or("2.0"));

    fusion::lidar::LASReader reader;
    if (!reader.Open(inputPath)) {
        std::cerr << "Error: Failed to open point cloud: " << inputPath << "\n";
        return 1;
    }

    std::cout << "[CloudMetrics] Processing point cloud metrics for: " << inputPath << "\n";

    std::vector<double> heights;
    fusion::lidar::PointRecord pt;
    uint64_t totalPts = 0;
    uint64_t ptsAboveMin = 0;

    while (reader.ReadNextPoint(pt)) {
        totalPts++;
        if (pt.z >= minHt) {
            heights.push_back(pt.z);
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

    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Error: Failed to create output CSV file: " << outputPath << "\n";
        return 1;
    }

    outFile << "Filename,TotalPoints,CanopyPoints,MinZ,MaxZ,MeanZ,StdDevZ,P05,P25,P50,P75,P95,P99,CanopyCoverPct,CRR\n";
    outFile << inputPath.filename().string() << ","
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
            << stats.canopyReliefRatio << "\n";
    outFile.close();

    std::cout << "[CloudMetrics] Successfully computed metrics (" << totalPts << " total points).\n";
    std::cout << "[CloudMetrics] Output written to: " << outputPath << "\n";

    return 0;
}
