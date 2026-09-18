#include "fusion/batch/BatchPipeline.h"
#include "fusion/table/TableWriter.h"

#include <thread>
#include <future>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

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
