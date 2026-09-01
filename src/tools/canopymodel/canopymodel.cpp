// canopymodel.cpp : Modernized CanopyModel Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("canopymodel", "Generates Canopy Height Model (CHM) GeoTIFF from point cloud");
    parser.AddOption("cellsize", "Output CHM cell size", "1.0");
    parser.AddOption("ground", "Path to ground DEM raster for height normalization");
    parser.AddOption("output", "Output GeoTIFF CHM file path");

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
    auto optOutput = parser.GetOption("output");
    if (!optOutput) {
        std::cerr << "Error: /output file parameter is required.\n";
        return 1;
    }

    double cellSize = std::stod(parser.GetOption("cellsize").value_or("1.0"));

    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    if (auto groundPath = parser.GetOption("ground")) {
        hasGround = groundRaster.Open(*groundPath);
    }

    fusion::lidar::LASReader reader;
    if (!reader.Open(inputPath)) {
        std::cerr << "Error: Failed to open input point cloud: " << inputPath << "\n";
        return 1;
    }

    const auto& header = reader.GetHeader();
    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));

    std::vector<float> maxElevGrid(cols * rows, -9999.0f);

    fusion::lidar::PointRecord pt;
    while (reader.ReadNextPoint(pt)) {
        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);

        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            double chmZ = pt.z;
            if (hasGround) {
                if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                    chmZ -= *gz;
                }
            }

            size_t idx = row * cols + col;
            if (static_cast<float>(chmZ) > maxElevGrid[idx]) {
                maxElevGrid[idx] = static_cast<float>(chmZ);
            }
        }
    }
    reader.Close();

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    fusion::raster::GDALRaster chmRaster;
    if (chmRaster.Create(*optOutput, cols, rows, 1, "Float32", "GTiff", "", geotransform, -9999.0)) {
        chmRaster.SetBandDescription(1, "canopy_height");
        chmRaster.WriteBandData(1, maxElevGrid);
        chmRaster.Close();
        std::cout << "[CanopyModel] Successfully output CHM GeoTIFF: " << *optOutput << "\n";
    }

    return 0;
}
