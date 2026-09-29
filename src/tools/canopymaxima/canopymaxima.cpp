// canopymaxima.cpp : Modernized CanopyMaxima Executable for FUSION2
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>

struct TreeTop {
    int id;
    double x;
    double y;
    double z;
    double crownDiameter;
};

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("canopymaxima", "Individual Tree Top Detection via Variable Window Local Maxima (VLM) on CHM GeoTIFF");
    parser.SetPositionalArgsUsage("<input_chm.tif>");
    parser.AddOption("output", "Output CSV file path for tree tops", "tree_tops.csv");
    parser.AddOption("minht", "Minimum tree height threshold (same units as the input)", "2.0");
    parser.AddOption("window-a", "Window polynomial coefficient A (constant)", "0.5");
    parser.AddOption("window-b", "Window polynomial coefficient B (linear)", "0.05");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input CHM GeoTIFF raster file is required.\n";
        parser.PrintHelp();
        return 1;
    }

    std::filesystem::path chmPath = posArgs[0];
    std::string outputPath = parser.GetOption("output").value_or("tree_tops.csv");
    double minHt = std::stod(parser.GetOption("minht").value_or("2.0"));
    double winA = std::stod(parser.GetOption("window-a").value_or("0.5"));
    double winB = std::stod(parser.GetOption("window-b").value_or("0.05"));

    fusion::raster::GDALRaster chmRaster;
    if (!chmRaster.Open(chmPath)) {
        std::cerr << "Error: Failed to open CHM raster: " << chmPath << "\n";
        return 1;
    }

    const auto& info = chmRaster.GetInfo();
    int cols = info.width;
    int rows = info.height;
    double cellSize = std::abs(info.pixelWidth);
    double noData = info.noDataValue;

    std::vector<float> chmData;
    if (!chmRaster.ReadBandData(1, chmData)) {
        std::cerr << "Error: Failed to read CHM band data.\n";
        return 1;
    }

    std::cout << "[CanopyMaxima] Scanning CHM (" << cols << "x" << rows << ")... Minimum height: " << minHt << "\n";

    std::vector<TreeTop> trees;
    int treeId = 1;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float h = chmData[r * cols + c];
            if (h == static_cast<float>(noData) || h < minHt) {
                continue;
            }

            double winRadiusM = winA + winB * h;
            int winRadiusCells = std::max(1, static_cast<int>(std::round(winRadiusM / cellSize)));

            bool isMax = true;
            for (int dr = -winRadiusCells; dr <= winRadiusCells && isMax; ++dr) {
                for (int dc = -winRadiusCells; dc <= winRadiusCells; ++dc) {
                    if (dr == 0 && dc == 0) continue;
                    int nr = r + dr;
                    int nc = c + dc;
                    if (nr >= 0 && nr < rows && nc >= 0 && nc < cols) {
                        float nh = chmData[nr * cols + nc];
                        if (nh >= h) {
                            isMax = false;
                            break;
                        }
                    }
                }
            }

            if (isMax) {
                double x = info.minX + (c + 0.5) * cellSize;
                double y = info.maxY - (r + 0.5) * cellSize;
                double crownDiam = winRadiusM * 2.0;
                trees.push_back({ treeId++, x, y, h, crownDiam });
            }
        }
    }

    std::ofstream outFile(outputPath);
    if (!outFile.is_open()) {
        std::cerr << "Error: Failed to create output CSV file: " << outputPath << "\n";
        return 1;
    }

    outFile << "TreeID,X,Y,Height,CrownDiameter\n";
    outFile << std::fixed << std::setprecision(2);
    for (const auto& t : trees) {
        outFile << t.id << "," << t.x << "," << t.y << "," << t.z << "," << t.crownDiameter << "\n";
    }
    outFile.close();

    std::cout << "[CanopyMaxima] Successfully detected " << trees.size() << " individual trees.\n";
    std::cout << "[CanopyMaxima] Output written to: " << outputPath << "\n";

    return 0;
}
