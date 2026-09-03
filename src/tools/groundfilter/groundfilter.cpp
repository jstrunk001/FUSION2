// groundfilter.cpp : Modernized GroundFilter Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("groundfilter", "Filters ground points from LAS/LAZ point cloud and generates GeoTIFF ground DEM");
    parser.SetPositionalArgsUsage("<input.las/laz or directory>");
    parser.AddOption("cellsize", "Output DEM cell size", "1.0");
    parser.AddOption("output-raster", "Output GeoTIFF ground DEM file path");
    parser.AddOption("output-points", "Output filtered ground LAS/LAZ file path");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    double cellSize = std::stod(parser.GetOption("cellsize").value_or("1.0"));

    fusion::lidar::MergedPointCloudReader reader;
    if (!reader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open input point cloud(s).\n";
        return 1;
    }

    const auto header = reader.GetHeader();
    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::vector<float> minElevGrid(cols * rows, 99999.0f);

    fusion::lidar::PointRecord pt;
    while (reader.ReadNextPoint(pt)) {
        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);

        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            size_t idx = row * cols + col;
            if (pt.z < minElevGrid[idx]) {
                minElevGrid[idx] = static_cast<float>(pt.z);
            }
        }
    }
    reader.Close();

    for (size_t i = 0; i < minElevGrid.size(); ++i) {
        if (minElevGrid[i] > 90000.0f) {
            minElevGrid[i] = -9999.0f;
        }
    }

    if (auto outDem = parser.GetOption("output-raster")) {
        double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
        fusion::raster::GDALRaster demRaster;
        if (demRaster.Create(*outDem, cols, rows, 1, "Float32", "GTiff", "", geotransform, -9999.0)) {
            demRaster.SetBandDescription(1, "ground_elevation");
            demRaster.WriteBandData(1, minElevGrid);
            demRaster.Close();
            std::cout << "[GroundFilter] Successfully output ground DEM GeoTIFF: " << *outDem << "\n";
        }
    }

    return 0;
}
