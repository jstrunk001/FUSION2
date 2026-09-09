// densitymetrics.cpp : Vertical Height-Slice Return Density Raster Stack for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/cli/ParseUtil.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/metrics/SentinelPolicy.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <filesystem>
#include <cmath>
#include <iomanip>
#include <unordered_set>

using fusion::cli::ParseFloatList;
using fusion::cli::ParseIntSet;

struct DensityCellAccumulator {
    int totalReturns{0};
    std::vector<int> strataCounts; // one bucket per /strata threshold, plus one "above the last threshold" bucket
};

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("densitymetrics", "Computes a return-density raster stack across vertical height slices (one band per /strata bucket)");
    parser.SetPositionalArgsUsage("<input.las/laz or directory> [optional raster ground path]");
    parser.AddOption("strata", "Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0) -- same syntax as gridmetrics' /strata", "0.5,2.0,5.0,10.0,20.0");
    parser.AddOption("cellsize", "Output grid cell size in project units", "10.0");
    parser.AddOption("ground", "Path to ground surface DEM raster (GeoTIFF, ENVI, IMG), or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("class", "Comma-separated point classifications to include (e.g. 2,3,4,5)");
    parser.AddOption("output", "Base output name (stem) for the raster/CSV files");
    parser.AddOption("outdir", "Output directory for the raster and CSV report", ".");
    parser.AddOption("nodata", "Value for cells with zero returns at all: NA, or a number such as 0, -9999, or inf. A non-empty cell's stratum bands are always real computed counts, never this sentinel.", "NA");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    double cellSize = std::stod(parser.GetOption("cellsize").value_or("10.0"));
    std::vector<double> strata = ParseFloatList(parser.GetOption("strata").value_or("0.5,2.0,5.0,10.0,20.0"));
    std::filesystem::path outDir = parser.GetOption("outdir").value_or(".");
    std::filesystem::create_directories(outDir);
    float noDataValue = fusion::metrics::ParseSentinelOption(parser.GetOption("nodata").value_or("NA")).value;

    std::unordered_set<int> validClasses;
    if (auto classOpt = parser.GetOption("class")) {
        validClasses = ParseIntSet(*classOpt);
    }

    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    std::string groundPathStr;
    if (auto groundPath = parser.GetOption("ground")) {
        groundPathStr = *groundPath;
    } else if (posArgs.size() > 1 && std::filesystem::exists(posArgs[1])) {
        groundPathStr = posArgs[1];
    }
    if (!groundPathStr.empty() && groundRaster.Open(groundPathStr)) {
        hasGround = true;
        std::cout << "[DensityMetrics] Loaded ground surface DEM: " << groundPathStr << "\n";
    }

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    fusion::lidar::MergedPointCloudReader lasReader;
    if (!lasReader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open point cloud file(s).\n";
        return 1;
    }

    const auto& header = lasReader.GetHeader();
    std::cout << "[DensityMetrics] Processing Point Cloud: "
              << (inputFiles.size() == 1 ? inputFiles[0].filename().string() : ("merged " + std::to_string(inputFiles.size()) + " files"))
              << " (" << header.pointCount << " points) across " << (strata.size() + 1) << " height stratum bucket(s)\n";

    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::vector<DensityCellAccumulator> grid(cols * rows);
    for (auto& cell : grid) {
        cell.strataCounts.resize(strata.size() + 1, 0);
    }

    // Same grid-binning and strata-bucket-assignment logic as gridmetrics
    // (gridmetrics.cpp's main point loop) -- reused here rather than
    // re-derived, so the two tools can't drift out of numeric agreement.
    fusion::lidar::PointRecord pt;
    while (lasReader.ReadNextPoint(pt)) {
        if (!validClasses.empty() && validClasses.find(pt.classification) == validClasses.end()) {
            continue;
        }

        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);

        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            auto& cell = grid[row * cols + col];
            cell.totalReturns++;

            double elevation = pt.z;
            if (hasGround) {
                if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                    elevation -= *gz;
                }
            }

            size_t sIdx = 0;
            while (sIdx < strata.size() && elevation >= strata[sIdx]) {
                sIdx++;
            }
            cell.strataCounts[sIdx]++;
        }
    }
    lasReader.Close();

    // One band per stratum bucket: return density (count per unit area) for
    // cells with at least one return; a fully empty cell gets the resolved
    // /nodata value across every stratum band (Shared groundwork C) -- a
    // non-empty cell's stratum bands are always real computed counts
    // (including a legitimate 0 for an empty bucket), never this sentinel.
    size_t numCells = static_cast<size_t>(cols) * rows;
    double cellArea = cellSize * cellSize;
    size_t numStrataBuckets = strata.size() + 1;
    std::vector<std::vector<float>> strataBands(numStrataBuckets, std::vector<float>(numCells, noDataValue));

    for (size_t i = 0; i < numCells; ++i) {
        const auto& cell = grid[i];
        if (cell.totalReturns == 0) {
            continue;
        }
        for (size_t s = 0; s < numStrataBuckets; ++s) {
            strataBands[s][i] = static_cast<float>(cell.strataCounts[s] / cellArea);
        }
    }

    std::string stem = (inputFiles.size() == 1) ? inputFiles[0].stem().string() : "merged_densitymetrics";
    if (auto outName = parser.GetOption("output")) {
        stem = *outName;
    }

    struct BandDef {
        std::string name;
        const std::vector<float>& data;
    };
    std::vector<BandDef> bandDefs;
    for (size_t s = 0; s < numStrataBuckets; ++s) {
        std::ostringstream nameStream;
        nameStream << "density_stratum_" << std::setw(2) << std::setfill('0') << s;
        bandDefs.push_back({nameStream.str(), strataBands[s]});
    }

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    std::filesystem::path outRasterPath = outDir / (stem + "_densitymetrics.tif");
    fusion::raster::GDALRaster outRaster;
    std::cout << "[DensityMetrics] Writing Multi-band GeoTIFF raster to: " << outRasterPath << " ("
              << bandDefs.size() << " bands)...\n";
    if (outRaster.Create(outRasterPath, cols, rows, static_cast<int>(bandDefs.size()), "Float32", "GTiff", "", geotransform, noDataValue)) {
        for (size_t b = 0; b < bandDefs.size(); ++b) {
            outRaster.SetBandDescription(static_cast<int>(b + 1), bandDefs[b].name);
            outRaster.WriteBandData(static_cast<int>(b + 1), bandDefs[b].data);
        }
        outRaster.Close();
    }

    std::filesystem::path csvPath = outDir / (stem + "_density_metrics.csv");
    std::ofstream csv(csvPath);
    if (csv.is_open()) {
        std::cout << "[DensityMetrics] Exporting CSV per-cell stratum counts to: " << csvPath << "...\n";
        csv << "Col,Row,X,Y,TotalReturns";
        for (size_t s = 0; s < numStrataBuckets; ++s) {
            csv << ",StrataCnt_" << s;
        }
        csv << "\n";

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                size_t idx = static_cast<size_t>(r) * cols + c;
                const auto& cell = grid[idx];
                double x = header.minX + (c + 0.5) * cellSize;
                double y = header.maxY - (r + 0.5) * cellSize;

                bool cellEmpty = (cell.totalReturns == 0);
                csv << c << "," << r << "," << x << "," << y << ",";
                csv << (cellEmpty ? "NA" : std::to_string(cell.totalReturns));
                for (size_t s = 0; s < numStrataBuckets; ++s) {
                    csv << ",";
                    csv << (cellEmpty ? "NA" : std::to_string(cell.strataCounts[s]));
                }
                csv << "\n";
            }
        }
        csv.close();
    }

    std::cout << "[DensityMetrics] Density metrics processing completed successfully.\n";
    return 0;
}
