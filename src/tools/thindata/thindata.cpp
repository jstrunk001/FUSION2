// thindata.cpp : Modernized ThinData Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <filesystem>
#include <unordered_set>
#include <cmath>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("thindata", "Spatial Point Cloud Thinning / Decimation Tool");
    parser.AddOption("output", "Output thinned LAS/LAZ file path", "thinned_output.laz");
    parser.AddOption("cellsize", "Grid cell size for 2D spatial thinning (m)", "1.0");

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
    std::string outputPath = parser.GetOption("output").value_or("thinned_output.laz");
    double cellSize = std::stod(parser.GetOption("cellsize").value_or("1.0"));

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

    std::cout << "[ThinData] Thinning point cloud (cell size: " << cellSize << "m)... " << inputPath << "\n";

    fusion::lidar::PointRecord pt;
    uint64_t inCount = 0;
    uint64_t outCount = 0;

    std::unordered_set<uint64_t> occupiedCells;

    while (reader.ReadNextPoint(pt)) {
        inCount++;

        int64_t cellX = static_cast<int64_t>(std::floor(pt.x / cellSize));
        int64_t cellY = static_cast<int64_t>(std::floor(pt.y / cellSize));

        // Pack 2D grid cell coordinate into 64-bit hash key
        uint64_t key = (static_cast<uint64_t>(cellX) & 0xFFFFFFFF) | ((static_cast<uint64_t>(cellY) & 0xFFFFFFFF) << 32);

        if (occupiedCells.find(key) == occupiedCells.end()) {
            occupiedCells.insert(key);
            writer.WritePoint(pt);
            outCount++;
        }
    }

    reader.Close();
    writer.Close();

    std::cout << "[ThinData] Read " << inCount << " points, retained " << outCount << " points ("
              << (100.0 * outCount / (inCount > 0 ? inCount : 1)) << "%).\n";
    std::cout << "[ThinData] Output written to: " << outputPath << "\n";

    return 0;
}
