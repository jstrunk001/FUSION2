// filterdata.cpp : Modernized FilterData Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <filesystem>
#include <optional>
#include <cmath>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("filterdata", "Point Cloud Filtering Tool (Elevation, Return Type, Scan Angle)");
    parser.SetPositionalArgsUsage("<input.las/laz>");
    parser.AddOption("output", "Output filtered LAS/LAZ file path", "filtered_output.laz");
    parser.AddOption("minz", "Minimum Z elevation threshold");
    parser.AddOption("maxz", "Maximum Z elevation threshold");
    parser.AddOption("return", "Return number filter (e.g. 1 for first return)");

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
    std::string outputPath = parser.GetOption("output").value_or("filtered_output.laz");

    std::optional<double> minZ;
    if (auto opt = parser.GetOption("minz")) minZ = std::stod(*opt);

    std::optional<double> maxZ;
    if (auto opt = parser.GetOption("maxz")) maxZ = std::stod(*opt);

    std::optional<int> returnNum;
    if (auto opt = parser.GetOption("return")) returnNum = std::stoi(*opt);

    fusion::lidar::LASReader reader;
    if (!reader.Open(inputPath)) {
        std::cerr << "Error: Failed to open input point cloud: " << inputPath << "\n";
        return 1;
    }

    fusion::lidar::LASWriter writer;
    if (!writer.Open(outputPath, reader.GetHeader())) {
        std::cerr << "Error: Failed to open output point cloud for writing: " << outputPath << "\n";
        return 1;
    }

    std::cout << "[FilterData] Filtering point cloud: " << inputPath << "\n";

    fusion::lidar::PointRecord pt;
    uint64_t inCount = 0;
    uint64_t outCount = 0;

    while (reader.ReadNextPoint(pt)) {
        inCount++;

        if (minZ && pt.z < *minZ) continue;
        if (maxZ && pt.z > *maxZ) continue;
        if (returnNum && pt.returnNumber != *returnNum) continue;

        writer.WritePoint(pt);
        outCount++;
    }

    reader.Close();
    writer.Close();

    std::cout << "[FilterData] Read " << inCount << " points, retained " << outCount << " points.\n";
    std::cout << "[FilterData] Output written to: " << outputPath << "\n";

    return 0;
}
