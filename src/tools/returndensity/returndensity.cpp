// returndensity.cpp : Modernized ReturnDensity Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/table/TableWriter.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("returndensity", "Generates Point Density (pts/m2) and Return Ratio GeoTIFF Rasters");
    parser.SetPositionalArgsUsage("<input.las/laz or directory>");
    parser.AddOption("output", "Output multi-band GeoTIFF raster path", "density_metrics.tif");
    parser.AddOption("cellsize", "Output grid cell size (m)", "5.0");
    parser.AddOption("output-table", "Also write a multicolumn table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely");
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

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    std::string outputPath = parser.GetOption("output").value_or("density_metrics.tif");
    double cellSize = std::stod(parser.GetOption("cellsize").value_or("5.0"));

    fusion::lidar::MergedPointCloudReader reader;
    if (!reader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open point cloud(s).\n";
        return 1;
    }

    const auto header = reader.GetHeader();
    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::cout << "[ReturnDensity] Grid dimensions: " << cols << "x" << rows << " | Cell size: " << cellSize << "m\n";

    std::vector<uint32_t> totalCount(cols * rows, 0);
    std::vector<uint32_t> firstReturnCount(cols * rows, 0);

    fusion::lidar::PointRecord pt;
    uint64_t ptsRead = 0;

    while (reader.ReadNextPoint(pt)) {
        ptsRead++;
        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);

        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            size_t idx = row * cols + col;
            totalCount[idx]++;
            if (pt.returnNumber == 1) {
                firstReturnCount[idx]++;
            }
        }
    }
    reader.Close();

    double cellArea = cellSize * cellSize;
    std::vector<float> densityData(cols * rows, 0.0f);
    std::vector<float> firstReturnRatioData(cols * rows, 0.0f);

    for (size_t i = 0; i < totalCount.size(); ++i) {
        densityData[i] = static_cast<float>(totalCount[i] / cellArea);
        if (totalCount[i] > 0) {
            firstReturnRatioData[i] = static_cast<float>((100.0 * firstReturnCount[i]) / totalCount[i]);
        }
    }

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };

    if (!noRaster) {
        fusion::raster::GDALRaster outRaster;
        if (outRaster.Create(outputPath, cols, rows, 2, "Float32", "GTiff", "", geotransform, -9999.0)) {
            outRaster.SetBandDescription(1, "point_density_pts_m2");
            outRaster.WriteBandData(1, densityData);

            outRaster.SetBandDescription(2, "first_return_ratio_pct");
            outRaster.WriteBandData(2, firstReturnRatioData);

            outRaster.Close();
            std::cout << "[ReturnDensity] Successfully output density GeoTIFF (" << ptsRead << " points processed from "
                      << inputFiles.size() << " file(s)): " << outputPath << "\n";
        }
    }

    if (wantTable) {
        std::vector<fusion::table::BandDef> bandDefs = {
            {"point_density_pts_m2", &densityData},
            {"first_return_ratio_pct", &firstReturnRatioData}
        };
        std::filesystem::path tablePath = *parser.GetOption("output-table");
        if (fusion::table::WriteGridTable(tablePath, cols, rows, geotransform, bandDefs, -9999.0f)) {
            std::cout << "[ReturnDensity] Successfully output table: " << tablePath.string() << "\n";
        }
    }

    return 0;
}
