#include "fusion/batch/BatchPipeline.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/table/TableWriter.h"

#include <thread>
#include <future>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <limits>

namespace fusion::batch {

std::vector<TileInfo> TileGridManager::GenerateTiles(const TileGridSpec& spec) {
    std::vector<TileInfo> tiles;
    int tileID = 1;

    for (double x = spec.minX; x < spec.maxX; x += spec.tileSizeX) {
        for (double y = spec.minY; y < spec.maxY; y += spec.tileSizeY) {
            TileInfo info;
            info.tileID = tileID++;
            std::ostringstream ss;
            ss << "tile_" << std::setw(4) << std::setfill('0') << info.tileID;
            info.name = ss.str();

            info.minX = x;
            info.maxX = std::min(x + spec.tileSizeX, spec.maxX);
            info.minY = y;
            info.maxY = std::min(y + spec.tileSizeY, spec.maxY);

            info.bufferedMinX = info.minX - spec.bufferDistance;
            info.bufferedMaxX = info.maxX + spec.bufferDistance;
            info.bufferedMinY = info.minY - spec.bufferDistance;
            info.bufferedMaxY = info.maxY + spec.bufferDistance;

            tiles.push_back(info);
        }
    }
    return tiles;
}

bool TileCellForPoint(double x, double y, const TileInfo& tile, const TileGridSpec& spec,
                      int cols, int rows, int& col, int& row) {
    //1. keep only points inside the tile's own area, not its buffer
    //  - left and top edges belong to this tile, right and bottom edges to
    //    the neighbour, except on the project's outer right/bottom edges
    bool onProjectRightEdge = tile.maxX >= spec.maxX;
    bool onProjectBottomEdge = tile.minY <= spec.minY;
    bool insideX = x >= tile.minX && (x < tile.maxX || (onProjectRightEdge && x == tile.maxX));
    bool insideY = y <= tile.maxY && (y > tile.minY || (onProjectBottomEdge && y == tile.minY));
    if (!insideX || !insideY) return false;

    //2. find the cell, rounding down (not toward zero)
    int colFound = static_cast<int>(std::floor((x - tile.minX) / spec.resolution));
    int rowFound = static_cast<int>(std::floor((tile.maxY - y) / spec.resolution));

    //3. a point exactly on the project's far right or bottom edge lands one
    //   cell past the last one -- pull it back into the last cell
    if (colFound >= cols) colFound = cols - 1;
    if (rowFound >= rows) rowFound = rows - 1;

    col = colFound;
    row = rowFound;
    return true;
}

bool ResolveProjectExtent(const std::filesystem::path& inputDir, bool hasExtent, double snapSize,
                          TileGridSpec& spec, std::string& messageOut) {
    //1. read every LAS/LAZ header: combined extent, and files overlapping spec
    int numFiles = 0;
    int numInExtent = 0;
    double dataMinX = std::numeric_limits<double>::max();
    double dataMinY = std::numeric_limits<double>::max();
    double dataMaxX = std::numeric_limits<double>::lowest();
    double dataMaxY = std::numeric_limits<double>::lowest();
    //  - inputDir may also be a single LAS/LAZ file
    for (const auto& path : fusion::lidar::ResolveInputFiles({inputDir.string()})) {
        fusion::lidar::LASReader reader;
        if (!reader.Open(path)) continue;
        const auto& h = reader.GetHeader();
        ++numFiles;
        dataMinX = (std::min)(dataMinX, h.minX);
        dataMinY = (std::min)(dataMinY, h.minY);
        dataMaxX = (std::max)(dataMaxX, h.maxX);
        dataMaxY = (std::max)(dataMaxY, h.maxY);
        if (h.maxX >= spec.minX && h.minX <= spec.maxX && h.maxY >= spec.minY && h.minY <= spec.maxY) {
            ++numInExtent;
        }
        reader.Close();
    }

    //2. stop when there is nothing to process
    std::ostringstream msg;
    msg << std::fixed << std::setprecision(2);
    if (numFiles == 0) {
        msg << "no readable .las or .laz files found in " << inputDir.string();
        messageOut = msg.str();
        return false;
    }
    if (hasExtent) {
        if (numInExtent == 0) {
            msg << "none of the " << numFiles << " point files in " << inputDir.string()
                << " overlap /extent:" << spec.minX << "," << spec.minY << "," << spec.maxX << "," << spec.maxY
                << " -- the files cover " << dataMinX << "," << dataMinY << "," << dataMaxX << "," << dataMaxY;
            messageOut = msg.str();
            return false;
        }
        messageOut.clear();
        return true;
    }

    //3. no /extent: use the files' combined extent, snapped outward so cell
    //   edges fall on multiples of snapSize
    if (snapSize <= 0.0) snapSize = 1.0;
    spec.minX = std::floor(dataMinX / snapSize) * snapSize;
    spec.minY = std::floor(dataMinY / snapSize) * snapSize;
    spec.maxX = std::ceil(dataMaxX / snapSize) * snapSize;
    spec.maxY = std::ceil(dataMaxY / snapSize) * snapSize;
    msg << "No /extent given -- using the extent of the " << numFiles << " point files, snapped to "
        << snapSize << " units: " << spec.minX << "," << spec.minY << "," << spec.maxX << "," << spec.maxY;
    messageOut = msg.str();
    return true;
}

BatchPipeline::BatchPipeline(TileGridSpec gridSpec, PipelineJobOptions jobOptions)
    : m_gridSpec(gridSpec), m_options(jobOptions) {
    m_tiles = TileGridManager::GenerateTiles(m_gridSpec);
    std::filesystem::create_directories(m_options.outputDir);
}

bool BatchPipeline::ExecutePipeline(const std::function<bool(const TileInfo& tile, const PipelineJobOptions& opts)>& tileTask) {
    std::cout << "[BatchPipeline] Starting batch processing for " << m_tiles.size()
              << " tiles with " << m_options.numThreads << " parallel workers...\n";

    std::atomic<size_t> completedTiles{0};
    std::atomic<size_t> currentIdx{0};
    size_t totalTiles = m_tiles.size();

    std::vector<std::thread> workers;
    int workerCount = std::max(1, m_options.numThreads);

    // (tileID, path) so the table concatenation below can sort into a
    // stable, deterministic order regardless of which worker finishes a
    // given tile first -- completion order across threads is not tile
    // order, but a re-run's merged table must still come out identical.
    std::vector<std::pair<int, std::filesystem::path>> tileTablesByID;
    std::string tableExt = m_options.outputTablePath.extension().string();

    for (int w = 0; w < workerCount; ++w) {
        workers.emplace_back([this, &tileTask, &currentIdx, &completedTiles, totalTiles, &tileTablesByID, &tableExt]() {
            while (true) {
                size_t idx = currentIdx.fetch_add(1);
                if (idx >= totalTiles) break;

                const auto& tile = m_tiles[idx];
                bool success = tileTask(tile, m_options);

                if (success) {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    std::filesystem::path tileRaster = m_options.outputDir / (tile.name + ".tif");
                    if (std::filesystem::exists(tileRaster)) {
                        m_tileRasterPaths.push_back(tileRaster);
                    }
                    if (!tableExt.empty()) {
                        std::filesystem::path tileTable = m_options.outputDir / (tile.name + tableExt);
                        if (std::filesystem::exists(tileTable)) {
                            tileTablesByID.emplace_back(tile.tileID, tileTable);
                        }
                    }
                }

                size_t comp = completedTiles.fetch_add(1) + 1;
                std::cout << "[BatchPipeline] Progress: " << comp << "/" << totalTiles
                          << " (" << (comp * 100 / totalTiles) << "%) completed.\n";
            }
        });
    }

    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    std::cout << "[BatchPipeline] All tile tasks finished. Processed " << m_tileRasterPaths.size() << " rasters.\n";

    // Standardized Strategy A: Build VRT across tile rasters
    if (m_options.generateVRT && !m_tileRasterPaths.empty()) {
        m_vrtPath = m_options.outputDir / "project_gridmetrics.vrt";
        std::cout << "[BatchPipeline] Generating GDAL VRT at: " << m_vrtPath << "...\n";
        bool vrtOk = fusion::raster::GDALRaster::BuildVRT(m_vrtPath, m_tileRasterPaths);

        if (vrtOk) {
            std::cout << "[BatchPipeline] VRT successfully created.\n";
            if (m_options.mergeGeoTIFF) {
                std::filesystem::path mergedTif = m_options.outputDir / "project_gridmetrics_merged.tif";
                std::cout << "[BatchPipeline] Merging VRT to global GeoTIFF: " << mergedTif << "...\n";
                fusion::raster::GDALRaster::MergeVRTToGeoTIFF(m_vrtPath, mergedTif);
            }
        }
    }

    // Concatenate per-tile metrics tables into one project-level table, the
    // same "parallel per-tile write, single-threaded merge" shape as the
    // VRT mosaic above -- table rows are merged in ascending tile-ID order
    // so re-running produces byte-identical output regardless of worker
    // scheduling.
    if (!tableExt.empty() && !tileTablesByID.empty()) {
        std::sort(tileTablesByID.begin(), tileTablesByID.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        m_tileTablePaths.clear();
        for (const auto& [tileID, path] : tileTablesByID) {
            m_tileTablePaths.push_back(path);
        }

        m_mergedTablePath = m_options.outputTablePath;
        std::cout << "[BatchPipeline] Merging " << m_tileTablePaths.size() << " per-tile table(s) into: "
                  << m_mergedTablePath.string() << "...\n";
        if (!fusion::table::ConcatenateGridTables(m_mergedTablePath, m_tileTablePaths)) {
            std::cerr << "Error: failed to merge per-tile tables into '" << m_mergedTablePath.string() << "'.\n";
        }
    }

    return true;
}

} // namespace fusion::batch
