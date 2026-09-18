// gridsurfacestats.cpp : Surface Area Ratio / Roughness / Cut-Fill Volume Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/metrics/SurfaceStats.h"
#include "fusion/table/TableWriter.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("gridsurfacestats", "Computes surface area ratio, roughness, and (with /reference) cut/fill volume from a DEM/CHM GeoTIFF");
    parser.SetPositionalArgsUsage("<input_surface.tif>");
    parser.AddOption("output", "Output multi-band GeoTIFF raster path", "surface_stats.tif");
    parser.AddOption("reference", "Second surface GeoTIFF (same cols x rows) to diff against for cut/fill volume_diff");
    parser.AddOption("output-table", "Also write a multicolumn table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely");
    parser.AddFlag("noraster", "Skip writing the GeoTIFF raster -- only valid together with /output-table, since a run must produce at least one output");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input DEM/CHM GeoTIFF raster file is required.\n";
        parser.PrintHelp();
        return 1;
    }

    bool noRaster = parser.HasFlag("noraster");
    bool wantTable = parser.WasExplicit("output-table");
    if (noRaster && !wantTable) {
        std::cerr << "Error: /noraster requires /output-table:<path> -- a run must produce at least one output.\n";
        return 1;
    }

    std::filesystem::path surfacePath = posArgs[0];
    std::string outputPath = parser.GetOption("output").value_or("surface_stats.tif");

    fusion::raster::GDALRaster surfaceRaster;
    if (!surfaceRaster.Open(surfacePath)) {
        std::cerr << "Error: Failed to open surface raster: " << surfacePath << "\n";
        return 1;
    }

    const auto& info = surfaceRaster.GetInfo();
    int cols = info.width;
    int rows = info.height;
    double cellSize = std::abs(info.pixelWidth);
    float noData = static_cast<float>(info.noDataValue);

    std::vector<float> elevation;
    if (!surfaceRaster.ReadBandData(1, elevation)) {
        std::cerr << "Error: Failed to read surface raster band data.\n";
        return 1;
    }

    std::vector<float> reference;
    bool hasReference = false;
    if (auto refPath = parser.GetOption("reference")) {
        fusion::raster::GDALRaster refRaster;
        if (!refRaster.Open(*refPath)) {
            std::cerr << "Error: Failed to open reference raster: " << *refPath << "\n";
            return 1;
        }
        const auto& refInfo = refRaster.GetInfo();
        if (refInfo.width != cols || refInfo.height != rows) {
            std::cerr << "Error: /reference raster must be the same cols x rows as the input surface ("
                      << cols << "x" << rows << "), got " << refInfo.width << "x" << refInfo.height << ".\n";
            return 1;
        }
        if (!refRaster.ReadBandData(1, reference)) {
            std::cerr << "Error: Failed to read reference raster band data.\n";
            return 1;
        }
        hasReference = true;
    }

    std::cout << "[GridSurfaceStats] Computing surface area ratio and roughness (" << cols << "x" << rows
              << ")... Cell size: " << cellSize << "m\n";

    fusion::metrics::SurfaceStatsGrid grid = fusion::metrics::ComputeSurfaceStatsGrid(
        elevation, cols, rows, cellSize, noData, hasReference ? &reference : nullptr);

    double geotransform[6] = { info.minX, cellSize, 0.0, info.maxY, 0.0, -cellSize };

    if (!noRaster) {
        fusion::raster::GDALRaster outRaster;
        int numBands = hasReference ? 3 : 2;
        if (outRaster.Create(outputPath, cols, rows, numBands, "Float32", "GTiff", info.projectionWKT, geotransform, noData)) {
            outRaster.SetBandDescription(1, "surface_area_ratio");
            outRaster.WriteBandData(1, grid.surfaceAreaRatio);

            outRaster.SetBandDescription(2, "roughness");
            outRaster.WriteBandData(2, grid.roughness);

            if (hasReference) {
                outRaster.SetBandDescription(3, "volume_diff");
                outRaster.WriteBandData(3, grid.volumeDiff);
            }

            outRaster.Close();
            std::cout << "[GridSurfaceStats] Output multi-band GeoTIFF: " << outputPath << "\n";
        }
    }

    if (wantTable) {
        std::vector<fusion::table::BandDef> bandDefs = {
            {"surface_area_ratio", &grid.surfaceAreaRatio},
            {"roughness", &grid.roughness}
        };
        if (hasReference) {
            bandDefs.push_back({"volume_diff", &grid.volumeDiff});
        }
        std::filesystem::path tablePath = *parser.GetOption("output-table");
        if (fusion::table::WriteGridTable(tablePath, cols, rows, geotransform, bandDefs, noData)) {
            std::cout << "[GridSurfaceStats] Successfully output table: " << tablePath.string() << "\n";
        }
    }

    return 0;
}
