// groundfilter.cpp : Modernized GroundFilter Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/PointFilter.h"
#include "fusion/table/TableWriter.h"

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
    fusion::lidar::PointFilter::RegisterOptions(parser);
    parser.AddOption("output-table", "Also write a one-band multicolumn table alongside the ground DEM raster (path ending in .csv or .sqlite) -- omit to skip table output entirely");
    parser.AddFlag("noraster", "Skip writing the ground DEM GeoTIFF -- only valid together with /output-table, since a run must produce at least one output");

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

    // /output-points writes out every point that passes the ground filter
    // (the same points that feed the DEM below), so a caller can inspect
    // the ground-classified returns directly instead of only the rasterized
    // minimum-elevation surface.
    bool wantOutputPoints = parser.WasExplicit("output-points");
    fusion::lidar::LASWriter pointWriter;
    if (wantOutputPoints) {
        fusion::lidar::LASHeaderInfo writeHeader;
        writeHeader.pointFormat = header.pointFormat;
        writeHeader.versionMajor = header.versionMajor;
        writeHeader.versionMinor = header.versionMinor;
        writeHeader.xScaleFactor = header.xScaleFactor;
        writeHeader.yScaleFactor = header.yScaleFactor;
        writeHeader.zScaleFactor = header.zScaleFactor;
        writeHeader.xOffset = header.xOffset;
        writeHeader.yOffset = header.yOffset;
        writeHeader.zOffset = header.zOffset;
        writeHeader.minX = header.minX;
        writeHeader.maxX = header.maxX;
        writeHeader.minY = header.minY;
        writeHeader.maxY = header.maxY;
        writeHeader.minZ = header.minZ;
        writeHeader.maxZ = header.maxZ;
        if (!pointWriter.Open(*parser.GetOption("output-points"), writeHeader)) {
            std::cerr << "Error: Failed to open ground point output file: " << *parser.GetOption("output-points") << "\n";
            return 1;
        }
    }

    fusion::lidar::PointFilter pointFilter = fusion::lidar::PointFilter::FromParser(parser);
    fusion::lidar::PointRecord pt;
    while (reader.ReadNextPoint(pt)) {
        if (!pointFilter.Keep(pt)) continue;
        if (wantOutputPoints) {
            pointWriter.WritePoint(pt);
        }
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
    if (wantOutputPoints) {
        pointWriter.Close();
        std::cout << "[GroundFilter] Successfully output ground points: " << *parser.GetOption("output-points") << "\n";
    }

    for (size_t i = 0; i < minElevGrid.size(); ++i) {
        if (minElevGrid[i] > 90000.0f) {
            minElevGrid[i] = -9999.0f;
        }
    }

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    if (!noRaster) {
        if (auto outDem = parser.GetOption("output-raster")) {
            fusion::raster::GDALRaster demRaster;
            if (demRaster.Create(*outDem, cols, rows, 1, "Float32", "GTiff", header.projectionWKT, geotransform, -9999.0)) {
                demRaster.SetBandDescription(1, "ground_elevation");
                demRaster.WriteBandData(1, minElevGrid);
                demRaster.Close();
                std::cout << "[GroundFilter] Successfully output ground DEM GeoTIFF: " << *outDem << "\n";
            }
        }
    }

    if (wantTable) {
        std::vector<fusion::table::BandDef> bandDefs = {{"ground_elevation", &minElevGrid}};
        std::filesystem::path tablePath = *parser.GetOption("output-table");
        if (fusion::table::WriteGridTable(tablePath, cols, rows, geotransform, bandDefs, -9999.0f)) {
            std::cout << "[GroundFilter] Successfully output table: " << tablePath.string() << "\n";
        }
    }

    return 0;
}
