// catalog.cpp : Modernized Catalog Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <fstream>
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

    std::vector<std::filesystem::path> files;
    std::filesystem::path inputPath = posArgs[0];

    if (std::filesystem::is_directory(inputPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(inputPath)) {
            if (entry.path().extension() == ".las" || entry.path().extension() == ".laz") {
                files.push_back(entry.path());
            }
        }
    } else {
        files.push_back(inputPath);
    }

    std::cout << "[Catalog] Cataloging " << files.size() << " point cloud file(s)...\n";

    std::ofstream csv;
    if (auto outCsv = parser.GetOption("output")) {
        csv.open(*outCsv);
        if (csv.is_open()) {
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

    return 0;
}
