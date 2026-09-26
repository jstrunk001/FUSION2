// treeseg.cpp : Modernized TreeSeg Executable for FUSION2
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <fstream>
#include <iomanip>
#include <vector>
#include <queue>
#include <filesystem>
#include <cmath>
#include <algorithm>

struct PixelNode {
    int r;
    int c;
    float h;
    bool operator<(const PixelNode& other) const {
        return h < other.h; // Max-heap based on height
    }
};

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("treeseg", "Individual Tree Crown Watershed Segmentation on CHM GeoTIFF");
    parser.SetPositionalArgsUsage("<input_chm.tif>");
    parser.AddOption("output-raster", "Output GeoTIFF raster path for tree segments", "crown_segments.tif");
    parser.AddOption("output-table", "Output CSV summary table path", "crown_summary.csv");
    parser.AddOption("minht", "Minimum height cutoff for segmentation (m)", "2.0");

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
    std::string outGridPath = parser.GetOption("output-raster").value_or("crown_segments.tif");
    std::string outCsvPath = parser.GetOption("output-table").value_or("crown_summary.csv");
    double minHt = std::stod(parser.GetOption("minht").value_or("2.0"));

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

    std::cout << "[TreeSeg] Performing watershed segmentation on CHM (" << cols << "x" << rows << ")... Height cutoff: " << minHt << "m\n";

    std::vector<int32_t> segmentGrid(cols * rows, 0);
    std::priority_queue<PixelNode> pq;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float h = chmData[r * cols + c];
            if (h != static_cast<float>(noData) && h >= minHt) {
                pq.push({ r, c, h });
            }
        }
    }

    int currentTreeId = 0;
    std::vector<int> dr = {-1, 1, 0, 0, -1, -1, 1, 1};
    std::vector<int> dc = {0, 0, -1, 1, -1, 1, -1, 1};

    while (!pq.empty()) {
        PixelNode curr = pq.top();
        pq.pop();

        size_t idx = curr.r * cols + curr.c;
        if (segmentGrid[idx] != 0) continue;

        // Check 8-neighbors for existing assigned segment ID
        int assignedId = 0;
        float maxNeighborH = -1.0f;
        for (int i = 0; i < 8; ++i) {
            int nr = curr.r + dr[i];
            int nc = curr.c + dc[i];
            if (nr >= 0 && nr < rows && nc >= 0 && nc < cols) {
                size_t nIdx = nr * cols + nc;
                if (segmentGrid[nIdx] != 0 && chmData[nIdx] > maxNeighborH) {
                    maxNeighborH = chmData[nIdx];
                    assignedId = segmentGrid[nIdx];
                }
            }
        }

        if (assignedId == 0) {
            // New local peak -> start new segment ID
            segmentGrid[idx] = ++currentTreeId;
        } else {
            segmentGrid[idx] = assignedId;
        }
    }

    // Write GeoTIFF segment grid
    double geotransform[6] = { info.minX, cellSize, 0.0, info.maxY, 0.0, -cellSize };
    fusion::raster::GDALRaster segRaster;
    std::vector<float> segFloatBuffer(cols * rows, 0.0f);
    for (size_t i = 0; i < segmentGrid.size(); ++i) {
        segFloatBuffer[i] = static_cast<float>(segmentGrid[i]);
    }

    if (segRaster.Create(outGridPath, cols, rows, 1, "Float32", "GTiff", info.projectionWKT, geotransform, 0.0)) {
        segRaster.SetBandDescription(1, "tree_segment_id");
        segRaster.WriteBandData(1, segFloatBuffer);
        segRaster.Close();
        std::cout << "[TreeSeg] Output segment GeoTIFF: " << outGridPath << "\n";
    }

    // Write summary CSV table
    std::vector<int> crownPixelCounts(currentTreeId + 1, 0);
    std::vector<float> maxHeights(currentTreeId + 1, 0.0f);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int id = segmentGrid[r * cols + c];
            if (id > 0) {
                crownPixelCounts[id]++;
                maxHeights[id] = std::max(maxHeights[id], chmData[r * cols + c]);
            }
        }
    }

    std::ofstream csvFile(outCsvPath);
    if (csvFile.is_open()) {
        csvFile << "TreeID,MaxHeight,CrownArea,PixelCount\n";
        csvFile << std::fixed << std::setprecision(2);
        double pixelArea = cellSize * cellSize;
        for (int id = 1; id <= currentTreeId; ++id) {
            if (crownPixelCounts[id] > 0) {
                csvFile << id << "," << maxHeights[id] << "," << (crownPixelCounts[id] * pixelArea) << "," << crownPixelCounts[id] << "\n";
            }
        }
        csvFile.close();
        std::cout << "[TreeSeg] Output crown summary CSV: " << outCsvPath << "\n";
    }

    std::cout << "[TreeSeg] Successfully segmented " << currentTreeId << " individual tree crowns.\n";
    return 0;
}
