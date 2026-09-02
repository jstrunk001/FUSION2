// topometrics.cpp : Modernized TopoMetrics Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("topometrics", "Computes Topographic Terrain Metrics (Slope, Aspect) from DEM GeoTIFF");
    parser.AddOption("output", "Output multi-band GeoTIFF raster path", "topo_metrics.tif");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input DEM GeoTIFF raster file is required.\n";
        parser.PrintHelp();
        return 1;
    }

    std::filesystem::path demPath = posArgs[0];
    std::string outputPath = parser.GetOption("output").value_or("topo_metrics.tif");

    fusion::raster::GDALRaster demRaster;
    if (!demRaster.Open(demPath)) {
        std::cerr << "Error: Failed to open DEM raster: " << demPath << "\n";
        return 1;
    }

    const auto& info = demRaster.GetInfo();
    int cols = info.width;
    int rows = info.height;
    double cellSize = std::abs(info.pixelWidth);
    double noData = info.noDataValue;

    std::vector<float> demData;
    if (!demRaster.ReadBandData(1, demData)) {
        std::cerr << "Error: Failed to read DEM band data.\n";
        return 1;
    }

    std::cout << "[TopoMetrics] Computing slope and aspect surfaces (" << cols << "x" << rows << ")... Cell size: " << cellSize << "m\n";

    std::vector<float> slopeData(cols * rows, -9999.0f);
    std::vector<float> aspectData(cols * rows, -9999.0f);

    double rad2deg = 180.0 / 3.14159265358979323846;

    for (int r = 1; r < rows - 1; ++r) {
        for (int c = 1; c < cols - 1; ++c) {
            float z5 = demData[r * cols + c];
            if (z5 == static_cast<float>(noData)) continue;

            float z1 = demData[(r - 1) * cols + (c - 1)];
            float z2 = demData[(r - 1) * cols + c];
            float z3 = demData[(r - 1) * cols + (c + 1)];
            float z4 = demData[r * cols + (c - 1)];
            float z6 = demData[r * cols + (c + 1)];
            float z7 = demData[(r + 1) * cols + (c - 1)];
            float z8 = demData[(r + 1) * cols + c];
            float z9 = demData[(r + 1) * cols + (c + 1)];

            if (z1 == noData || z2 == noData || z3 == noData || z4 == noData ||
                z6 == noData || z7 == noData || z8 == noData || z9 == noData) {
                continue;
            }

            // Horn's method for partial derivatives
            double dz_dx = ((z3 + 2 * z6 + z9) - (z1 + 2 * z4 + z7)) / (8.0 * cellSize);
            double dz_dy = ((z7 + 2 * z8 + z9) - (z1 + 2 * z2 + z3)) / (8.0 * cellSize);

            double slopeRad = std::atan(std::sqrt(dz_dx * dz_dx + dz_dy * dz_dy));
            double slopeDeg = slopeRad * rad2deg;
            slopeData[r * cols + c] = static_cast<float>(slopeDeg);

            double aspectRad = std::atan2(dz_dy, -dz_dx);
            double aspectDeg = aspectRad * rad2deg;
            if (aspectDeg < 0.0) aspectDeg += 360.0;
            aspectData[r * cols + c] = static_cast<float>(aspectDeg);
        }
    }

    double geotransform[6] = { info.minX, cellSize, 0.0, info.maxY, 0.0, -cellSize };
    fusion::raster::GDALRaster outRaster;
    if (outRaster.Create(outputPath, cols, rows, 2, "Float32", "GTiff", info.projectionWKT, geotransform, -9999.0)) {
        outRaster.SetBandDescription(1, "slope_degrees");
        outRaster.WriteBandData(1, slopeData);

        outRaster.SetBandDescription(2, "aspect_degrees");
        outRaster.WriteBandData(2, aspectData);

        outRaster.Close();
        std::cout << "[TopoMetrics] Output multi-band terrain GeoTIFF: " << outputPath << "\n";
    }

    return 0;
}
