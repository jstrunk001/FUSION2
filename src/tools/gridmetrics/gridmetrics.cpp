// gridmetrics.cpp : Modernized GridMetrics Executable for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/batch/StatusMessenger.h"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <numeric>

struct MetricAccumulator {
    std::vector<float> elevations;
    std::vector<float> intensities;
    int totalReturns{0};
    int firstReturns{0};
    int returnsAboveGround{0};
};

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("gridmetrics", "Computes elevation and intensity metrics grid from point clouds");
    parser.AddOption("ground", "Path to ground surface raster (GeoTIFF, ENVI, IMG)");
    parser.AddOption("cellsize", "Output grid cell size in project units", "10.0");
    parser.AddOption("minht", "Minimum height above ground for metrics calculation", "2.0");
    parser.AddOption("output-mode", "Output raster mode: multiband or singleband", "multiband");
    parser.AddOption("outdir", "Output directory for rasters", ".");
    parser.AddFlag("nointensity", "Skip computing intensity metrics");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    std::filesystem::path inputPath = posArgs[0];
    std::filesystem::path outDir = parser.GetOption("outdir").value_or(".");
    double cellSize = std::stod(parser.GetOption("cellsize").value_or("10.0"));
    double minHt = std::stod(parser.GetOption("minht").value_or("2.0"));
    std::string outputMode = parser.GetOption("output-mode").value_or("multiband");

    std::filesystem::create_directories(outDir);

    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    if (auto groundPath = parser.GetOption("ground")) {
        if (groundRaster.Open(*groundPath)) {
            hasGround = true;
            std::cout << "[GridMetrics] Loaded ground surface DEM: " << *groundPath << "\n";
        } else {
            std::cerr << "Warning: Could not open ground surface DEM raster: " << *groundPath << "\n";
        }
    }

    fusion::lidar::LASReader lasReader;
    if (!lasReader.Open(inputPath)) {
        std::cerr << "Error: Failed to open point cloud file: " << inputPath << "\n";
        return 1;
    }

    const auto& header = lasReader.GetHeader();
    std::cout << "[GridMetrics] Processing Point Cloud: " << inputPath.filename().string()
              << " (" << header.pointCount << " points)\n";

    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::vector<MetricAccumulator> grid(cols * rows);

    fusion::lidar::PointRecord pt;
    while (lasReader.ReadNextPoint(pt)) {
        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);

        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            auto& cell = grid[row * cols + col];
            cell.totalReturns++;
            if (pt.returnNumber == 1) cell.firstReturns++;

            double elevation = pt.z;
            if (hasGround) {
                auto groundZ = groundRaster.GetElevation(pt.x, pt.y);
                if (groundZ) {
                    elevation -= *groundZ;
                }
            }

            if (elevation >= minHt) {
                cell.elevations.push_back(static_cast<float>(elevation));
                cell.intensities.push_back(static_cast<float>(pt.intensity));
                cell.returnsAboveGround++;
            }
        }
    }
    lasReader.Close();

    std::vector<float> meanElev(cols * rows, -9999.0f);
    std::vector<float> stdDevElev(cols * rows, -9999.0f);
    std::vector<float> p95Elev(cols * rows, -9999.0f);
    std::vector<float> canopyCover(cols * rows, -9999.0f);
    std::vector<float> pointDensity(cols * rows, -9999.0f);

    for (int i = 0; i < cols * rows; ++i) {
        auto& cell = grid[i];
        if (!cell.elevations.empty()) {
            double sum = std::accumulate(cell.elevations.begin(), cell.elevations.end(), 0.0);
            double mean = sum / cell.elevations.size();
            meanElev[i] = static_cast<float>(mean);

            double sqSum = 0.0;
            for (float val : cell.elevations) {
                sqSum += (val - mean) * (val - mean);
            }
            stdDevElev[i] = static_cast<float>(std::sqrt(sqSum / cell.elevations.size()));

            std::vector<float> sortedElev = cell.elevations;
            std::sort(sortedElev.begin(), sortedElev.end());
            size_t p95Idx = static_cast<size_t>(0.95 * (sortedElev.size() - 1));
            p95Elev[i] = sortedElev[p95Idx];
        }

        if (cell.firstReturns > 0) {
            canopyCover[i] = static_cast<float>(100.0 * cell.returnsAboveGround / cell.firstReturns);
        }
        pointDensity[i] = static_cast<float>(cell.totalReturns / (cellSize * cellSize));
    }

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    std::filesystem::path outRasterPath = outDir / (inputPath.stem().string() + "_gridmetrics.tif");

    fusion::raster::GDALRaster outRaster;

    if (outputMode == "singleband") {
        std::cout << "[GridMetrics] Writing Single-band GeoTIFF rasters to: " << outDir << "...\n";

        std::filesystem::path meanPath = outDir / (inputPath.stem().string() + "_elev_mean.tif");
        outRaster.Create(meanPath, cols, rows, 1, "Float32", "GTiff", "", geotransform, -9999.0);
        outRaster.SetBandDescription(1, "elev_mean");
        outRaster.WriteBandData(1, meanElev);
        outRaster.Close();

        std::filesystem::path p95Path = outDir / (inputPath.stem().string() + "_elev_p95.tif");
        outRaster.Create(p95Path, cols, rows, 1, "Float32", "GTiff", "", geotransform, -9999.0);
        outRaster.SetBandDescription(1, "elev_p95");
        outRaster.WriteBandData(1, p95Elev);
        outRaster.Close();
    } else {
        std::cout << "[GridMetrics] Writing Multi-band GeoTIFF raster to: " << outRasterPath << "...\n";
        outRaster.Create(outRasterPath, cols, rows, 5, "Float32", "GTiff", "", geotransform, -9999.0);

        outRaster.SetBandDescription(1, "elev_mean");
        outRaster.WriteBandData(1, meanElev);

        outRaster.SetBandDescription(2, "elev_stddev");
        outRaster.WriteBandData(2, stdDevElev);

        outRaster.SetBandDescription(3, "elev_p95");
        outRaster.WriteBandData(3, p95Elev);

        outRaster.SetBandDescription(4, "canopy_cover");
        outRaster.WriteBandData(4, canopyCover);

        outRaster.SetBandDescription(5, "point_density");
        outRaster.WriteBandData(5, pointDensity);

        outRaster.Close();
    }

    std::cout << "[GridMetrics] Grid metrics processing completed successfully.\n";
    return 0;
}
