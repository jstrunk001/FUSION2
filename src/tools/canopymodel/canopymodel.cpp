// canopymodel.cpp : Modernized CanopyModel Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/raster/ChmSmoothing.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/PointFilter.h"
#include "fusion/table/TableWriter.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("canopymodel", "Generates Canopy Height Model (CHM) GeoTIFF from point cloud");
    parser.SetPositionalArgsUsage("<input.las/laz or directory>");
    parser.AddOption("cellsize", "Output CHM cell size", "1.0");
    parser.AddOption("ground", "Path to ground DEM raster for height normalization, or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("output", "Output GeoTIFF CHM file path");
    parser.AddFlag("slope", "Normalize heights perpendicular to local terrain slope plane");
    parser.AddOption("smooth", "Spatial smoothing window size (e.g. 3 for 3x3 filter)", "");
    fusion::lidar::PointFilter::RegisterOptions(parser);
    parser.AddOption("output-table", "Also write a one-band multicolumn table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely");
    parser.AddFlag("noraster", "Skip writing the GeoTIFF raster -- only valid together with /output-table, since a run must produce at least one output");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    bool noRaster = parser.HasFlag("noraster");
    bool wantTable = parser.WasExplicit("output-table");
    if (noRaster && !wantTable) {
        std::cerr << "Error: /noraster requires /output-table:<path> -- a run must produce at least one output.\n";
        return 1;
    }

    auto optOutput = parser.GetOption("output");
    if (!noRaster && !optOutput) {
        std::cerr << "Error: /output file parameter is required (or pass /noraster together with /output-table:<path>).\n";
        return 1;
    }

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
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

    std::vector<float> maxElevGrid(cols * rows, -9999.0f);
    double groundPixelSize = hasGround ? groundRaster.GetInfo().pixelWidth : 1.0;
    if (groundPixelSize <= 0) groundPixelSize = 1.0;

    fusion::lidar::PointFilter pointFilter = fusion::lidar::PointFilter::FromParser(parser);
    fusion::lidar::PointRecord pt;
    while (reader.ReadNextPoint(pt)) {
        if (!pointFilter.Keep(pt)) continue;
        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);
        if (col == cols && pt.x == header.maxX) col = cols - 1;
        if (row == rows && pt.y == header.minY) row = rows - 1;

        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            double chmZ = pt.z;
            if (hasGround) {
                auto gz = groundRaster.GetElevation(pt.x, pt.y);
                if (!gz) {
                    continue;
                }
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

            size_t idx = row * cols + col;
            if (static_cast<float>(chmZ) > maxElevGrid[idx]) {
                maxElevGrid[idx] = static_cast<float>(chmZ);
            }
        }
    }
    reader.Close();

    // Apply spatial smoothing filter if requested -- see ChmSmoothing.cpp for
    // why the separable two-pass box sum matches the brute-force WxW average.
    if (smoothWidth >= 3) {
        if (smoothWidth % 2 == 0) smoothWidth += 1; // Ensure odd window size
        std::cout << "[CanopyModel] Applying " << smoothWidth << "x" << smoothWidth << " spatial smoothing filter...\n";
        maxElevGrid = fusion::raster::SmoothNodataAwareBox(maxElevGrid, cols, rows, smoothWidth, -9999.0f);
    }

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    if (!noRaster) {
        fusion::raster::GDALRaster chmRaster;
        if (chmRaster.Create(*optOutput, cols, rows, 1, "Float32", "GTiff", header.projectionWKT, geotransform, -9999.0)) {
            chmRaster.SetBandDescription(1, "canopy_height");
            chmRaster.WriteBandData(1, maxElevGrid);
            chmRaster.Close();
            std::cout << "[CanopyModel] Successfully output CHM GeoTIFF: " << *optOutput << "\n";
        }
    }

    if (wantTable) {
        std::vector<fusion::table::BandDef> bandDefs = {{"canopy_height", &maxElevGrid}};
        std::filesystem::path tablePath = *parser.GetOption("output-table");
        if (fusion::table::WriteGridTable(tablePath, cols, rows, geotransform, bandDefs, -9999.0f)) {
            std::cout << "[CanopyModel] Successfully output table: " << tablePath.string() << "\n";
        }
    }

    return 0;
}
