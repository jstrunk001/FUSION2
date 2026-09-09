#ifndef FUSION_BATCH_BATCHPIPELINE_H
#define FUSION_BATCH_BATCHPIPELINE_H

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>
#include <limits>
#include "fusion/raster/GDALRaster.h"

namespace fusion::batch {

struct TileGridSpec {
    double minX{0.0};
    double maxX{0.0};
    double minY{0.0};
    double maxY{0.0};
    double tileSizeX{1000.0};
    double tileSizeY{1000.0};
    double bufferDistance{50.0};
    double resolution{1.0};
};

struct TileInfo {
    int tileID{0};
    std::string name;
    double minX{0.0};
    double maxX{0.0};
    double minY{0.0};
    double maxY{0.0};
    double bufferedMinX{0.0};
    double bufferedMaxX{0.0};
    double bufferedMinY{0.0};
    double bufferedMaxY{0.0};
};

class TileGridManager {
public:
    static std::vector<TileInfo> GenerateTiles(const TileGridSpec& spec);
};

struct PipelineJobOptions {
    std::filesystem::path inputPointCloudDir;
    std::filesystem::path outputDir;
    std::string outputMode{"multiband"}; // "multiband" or "singleband"
    int numThreads{4};
    bool generateVRT{true};
    bool mergeGeoTIFF{false};
    std::string groundPath;
    double minHt{2.0};
    double heightCut{2.0};
    std::string outlier;
    std::string pointClass;
    bool firstOnly{false};
    bool noIntensity{false};
    std::string strata;
    std::string intStrata;

    // Resolved /nodata,/noheight sentinel values (see SentinelPolicy.h) --
    // plain floats rather than a SentinelPolicy member so this header
    // doesn't need to depend on fusion/metrics/SentinelPolicy.h; the
    // gridmetrics batch/tiled tile task applies the same two-tier rule
    // (nodataValue for a fully empty cell, noheightValue for a cell with
    // returns but none clearing the height cutoff) as single-file mode.
    float nodataValue{std::numeric_limits<float>::quiet_NaN()};
    float noheightValue{0.0f};
};

class BatchPipeline {
public:
    BatchPipeline(TileGridSpec gridSpec, PipelineJobOptions jobOptions);

    bool ExecutePipeline(const std::function<bool(const TileInfo& tile, const PipelineJobOptions& opts)>& tileTask);

    const std::vector<std::filesystem::path>& GetGeneratedTileRasters() const { return m_tileRasterPaths; }
    std::filesystem::path GetVRTPath() const { return m_vrtPath; }

private:
    TileGridSpec m_gridSpec;
    PipelineJobOptions m_options;
    std::vector<TileInfo> m_tiles;
    std::vector<std::filesystem::path> m_tileRasterPaths;
    std::mutex m_mutex;
    std::filesystem::path m_vrtPath;
};

} // namespace fusion::batch

#endif // FUSION_BATCH_BATCHPIPELINE_H
