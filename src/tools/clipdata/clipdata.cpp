// clipdata.cpp : Modernized ClipData Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <filesystem>
#include <sstream>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("clipdata", "Clips LAS/LAZ point clouds by bounding box or spatial extents");
    parser.SetPositionalArgsUsage("<input.las/laz or directory>");
    parser.AddOption("extent", "Bounding box LLX,LLY,URX,URY");
    parser.AddOption("ground", "Path to ground surface raster (GeoTIFF, ENVI, IMG) for height normalization, or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("minz", "Minimum height above ground or elevation");
    parser.AddOption("maxz", "Maximum height above ground or elevation");
    parser.AddOption("output", "Output LAS/LAZ file path");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    auto optOutput = parser.GetOption("output");
    if (!optOutput) {
        std::cerr << "Error: /output file parameter is required.\n";
        return 1;
    }
    std::filesystem::path outputPath = *optOutput;

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    double minX = -1e9, minY = -1e9, maxX = 1e9, maxY = 1e9;
    if (auto ext = parser.GetOption("extent")) {
        std::stringstream ss(*ext);
        char ch;
        ss >> minX >> ch >> minY >> ch >> maxX >> ch >> maxY;
    }

    double zMin = -1e9, zMax = 1e9;
    if (auto zm = parser.GetOption("minz")) zMin = std::stod(*zm);
    if (auto zm = parser.GetOption("maxz")) zMax = std::stod(*zm);

    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    if (auto groundPath = parser.GetOption("ground")) {
        hasGround = groundRaster.Open(*groundPath);
    }

    fusion::lidar::MergedPointCloudReader reader;
    if (!reader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open input point cloud(s).\n";
        return 1;
    }

    fusion::lidar::LASWriter writer;
    if (!writer.Open(outputPath, reader.GetHeader())) {
        std::cerr << "Error: Failed to create output point cloud: " << outputPath << "\n";
        return 1;
    }

    std::cout << "[ClipData] Clipping " << inputFiles.size() << " point cloud file(s) -> " << outputPath.filename().string() << "...\n";

    fusion::lidar::PointRecord pt;
    uint64_t clippedCount = 0;
    while (reader.ReadNextPoint(pt)) {
        if (pt.x >= minX && pt.x <= maxX && pt.y >= minY && pt.y <= maxY) {
            double elevation = pt.z;
            if (hasGround) {
                if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                    elevation -= *gz;
                }
            }

            if (elevation >= zMin && elevation <= zMax) {
                writer.WritePoint(pt);
                clippedCount++;
            }
        }
    }

    reader.Close();
    writer.Close();

    std::cout << "[ClipData] Successfully wrote " << clippedCount << " clipped points to " << outputPath << "\n";
    return 0;
}
