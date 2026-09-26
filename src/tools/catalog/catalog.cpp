// catalog.cpp : Modernized Catalog Executable for FUSION2
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/PointDensity.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <filesystem>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("catalog", "Summarizes LAS/LAZ point cloud acquisition stats and density rasters");
    parser.SetPositionalArgsUsage("<input.las/laz or directory>");
    parser.AddOption("density", "Output GeoTIFF point density raster cell size");
    parser.AddOption("output", "Output CSV summary report file path");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    auto files = fusion::lidar::ResolveInputFiles(posArgs);
    if (files.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    std::cout << "[Catalog] Cataloging " << files.size() << " point cloud file(s)...\n";

    std::ofstream csv;
    if (auto outCsv = parser.GetOption("output")) {
        csv.open(*outCsv);
        if (csv.is_open()) {
            csv << std::fixed << std::setprecision(2);
            csv << "FileName,PointCount,MinX,MaxX,MinY,MaxY,MinZ,MaxZ,Format,IsCompressed\n";
        }
    }

    for (const auto& file : files) {
        fusion::lidar::LASReader reader;
        if (reader.Open(file)) {
            const auto& h = reader.GetHeader();
            std::cout << "  File: " << file.filename().string()
                      << " | Points: " << h.pointCount
                      << " | Extent: [" << h.minX << ", " << h.minY << "] to [" << h.maxX << ", " << h.maxY << "]\n";

            if (csv.is_open()) {
                csv << file.filename().string() << "," << h.pointCount << ","
                    << h.minX << "," << h.maxX << "," << h.minY << "," << h.maxY << ","
                    << h.minZ << "," << h.maxZ << "," << (int)h.pointFormat << ","
                    << (h.isCompressed ? "1" : "0") << "\n";
            }
            reader.Close();
        }
    }

    if (csv.is_open()) {
        csv.close();
        std::cout << "[Catalog] Wrote catalog summary CSV report.\n";
    }

    // /density takes the raster cell size directly (there is no separate
    // output-path option for it) -- name the raster after the CSV /output
    // path when one was given, or fall back to a fixed default alongside
    // the current directory.
    if (auto densityOpt = parser.GetOption("density")) {
        double cellSize = std::stod(*densityOpt);

        std::filesystem::path densityPath = "catalog_density.tif";
        if (auto outCsv = parser.GetOption("output")) {
            densityPath = std::filesystem::path(*outCsv).replace_extension(".tif");
        }

        if (fusion::lidar::WritePointCountRaster(files, cellSize, densityPath)) {
            std::cout << "[Catalog] Successfully output point density GeoTIFF: " << densityPath.string() << "\n";
        } else {
            std::cerr << "Error: Failed to write point density raster: " << densityPath.string() << "\n";
            return 1;
        }
    }

    return 0;
}
