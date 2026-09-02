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
    parser.SetPositionalArgsUsage("<input.las/laz>");
    parser.AddOption("cellsize", "Output CHM cell size", "1.0");
    parser.AddOption("ground", "Path to ground DEM raster for height normalization, or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("output", "Output GeoTIFF CHM file path");
    parser.AddFlag("slope", "Normalize heights perpendicular to local terrain slope plane");
    parser.AddOption("smooth", "Spatial smoothing window size (e.g. 3 for 3x3 filter)", "");

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
    bool useSlope = parser.HasFlag("slope");
    
    int smoothWidth = 0;
    if (auto smoothOpt = parser.GetOption("smooth")) {
        if (!smoothOpt->empty()) {
            smoothWidth = std::stoi(*smoothOpt);
        } else {
            smoothWidth = 3;
        }
    } else if (parser.HasFlag("smooth")) {
        smoothWidth = 3;
    }

    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    if (auto groundPath = parser.GetOption("ground")) {
        hasGround = groundRaster.Open(*groundPath);
        if (hasGround) {
            std::cout << "[CanopyModel] Loaded ground surface DEM: " << *groundPath << "\n";
        }
    }

    fusion::lidar::LASReader reader;
    if (!reader.Open(inputPath)) {
        std::cerr << "Error: Failed to open input point cloud: " << inputPath << "\n";
        return 1;
    }

    const auto& header = reader.GetHeader();
    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::vector<float> maxElevGrid(cols * rows, -9999.0f);
    double groundPixelSize = hasGround ? groundRaster.GetInfo().pixelWidth : 1.0;
    if (groundPixelSize <= 0) groundPixelSize = 1.0;

    fusion::lidar::PointRecord pt;
    while (reader.ReadNextPoint(pt)) {
        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);

        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            double chmZ = pt.z;
            if (hasGround) {
                if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                    double normHt = pt.z - *gz;
                    if (useSlope) {
                        double gzE = groundRaster.GetElevation(pt.x + groundPixelSize, pt.y).value_or(*gz);
                        double gzW = groundRaster.GetElevation(pt.x - groundPixelSize, pt.y).value_or(*gz);
                        double gzN = groundRaster.GetElevation(pt.x, pt.y + groundPixelSize).value_or(*gz);
                        double gzS = groundRaster.GetElevation(pt.x, pt.y - groundPixelSize).value_or(*gz);
                        double dzdx = (gzE - gzW) / (2.0 * groundPixelSize);
                        double dzdy = (gzN - gzS) / (2.0 * groundPixelSize);
                        double slopeFactor = std::sqrt(1.0 + dzdx * dzdx + dzdy * dzdy);
                        normHt /= slopeFactor;
                    }
                    chmZ = normHt;
                }
            }

            size_t idx = row * cols + col;
            if (static_cast<float>(chmZ) > maxElevGrid[idx]) {
                maxElevGrid[idx] = static_cast<float>(chmZ);
            }
        }
    }
    reader.Close();

    // Apply spatial smoothing filter if requested
    if (smoothWidth >= 3) {
        if (smoothWidth % 2 == 0) smoothWidth += 1; // Ensure odd window size
        std::cout << "[CanopyModel] Applying " << smoothWidth << "x" << smoothWidth << " spatial smoothing filter...\n";
        int half = smoothWidth / 2;
        std::vector<float> smoothedGrid = maxElevGrid;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if (maxElevGrid[r * cols + c] == -9999.0f) continue;

                double sum = 0.0;
                int count = 0;
                for (int dr = -half; dr <= half; ++dr) {
                    for (int dc = -half; dc <= half; ++dc) {
                        int nr = r + dr;
                        int nc = c + dc;
                        if (nr >= 0 && nr < rows && nc >= 0 && nc < cols) {
                            float val = maxElevGrid[nr * cols + nc];
                            if (val != -9999.0f) {
                                sum += val;
                                count++;
                            }
                        }
                    }
                }
                if (count > 0) {
                    smoothedGrid[r * cols + c] = static_cast<float>(sum / count);
                }
            }
        }
        maxElevGrid = std::move(smoothedGrid);
    }

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
