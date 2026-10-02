#ifndef FUSION_BATCH_BATCHPIPELINE_H
#define FUSION_BATCH_BATCHPIPELINE_H

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>
#include <limits>
#include <cstdint>
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

// One input file a tile reads points from. splitPath is empty when the
// tile reads the input file itself; otherwise it names a temporary file
// holding the input file's points that fall in the tile's buffered extent,
// written by SplitInputsForTiles (see TilePointSource.h) so that an input
// file shared by several tiles is decompressed once rather than once per
// tile.
struct TileInputSource {
    std::filesystem::path inputFile;
    std::filesystem::path splitPath;
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

    // Set by BatchPipeline before the tile task runs (see ForEachTilePoint).
    // inputs lists every input file whose header extent overlaps the
    // buffered extent, in input-file order. inputsFailed is true when
    // splitting a shared input file for this tile failed, so the tile's
    // points are incomplete and the tile must fail.
    bool inputsAssigned{false};
    bool inputsFailed{false};
    std::vector<TileInputSource> inputs;
};

class TileGridManager {
public:
    static std::vector<TileInfo> GenerateTiles(const TileGridSpec& spec);
};

// Finds the grid cell (col, row) that a point falls in within one batch
// tile, counting columns right from the tile's left edge and rows down
// from its top edge, with cells spec.resolution wide. A tile reads points
// from its buffer as well as its own area, but only points inside its own
// area are given a cell, so every point is counted by exactly one tile.
// A point on a shared edge goes to one tile only: the left and top edges
// belong to this tile, and the right and bottom edges belong to the
// neighbouring tile -- except on the project's outer right and bottom
// edges, which have no neighbour and so stay with this tile. Returns false
// (and leaves col/row unchanged) for a point outside the tile's own area.
bool TileCellForPoint(double x, double y, const TileInfo& tile, const TileGridSpec& spec,
                      int cols, int rows, int& col, int& row);

// Sets the project extent for a batch run over the LAS/LAZ files in
// inputDir, reading only their headers. With hasExtent false, spec's
// extent is set to the combined extent of the files, snapped outward to
// multiples of snapSize, and messageOut describes the extent chosen. With
// hasExtent true, spec's extent is kept as given. Returns false, with
// messageOut holding the error, when the directory holds no readable
// LAS/LAZ file or when none of the files overlap the given extent -- so a
// run over the wrong area stops instead of writing a mosaic of empty tiles.
bool ResolveProjectExtent(const std::filesystem::path& inputDir, bool hasExtent, double snapSize,
                          TileGridSpec& spec, std::string& messageOut);

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

    // Batch/tiled full-metric-parity options -- gridmetrics.exe's tile task
    // reads these to bring its per-tile metric set up to the same column
    // set single-file mode computes (/rgb, /exp, /surfstats, /rgbstrata).
    std::string rgb;
    // Resolved once, from a representative input file, before tiling starts
    // -- every tile's spectral-channel selection (both point ingestion and
    // table/raster column naming) uses this single value, the same way
    // single-file mode resolves one pointFormat from its merged reader,
    // so every tile's table schema matches and can be concatenated.
    uint8_t pointFormat{0};
    bool enableExp{false};
    bool enableSurfStats{false};
    std::string surfStatsSource{"max"};
    double voxelSize{20.0};
    bool enableRgbStrata{false};
    // /strataraster: also write the per-stratum bands into each tile raster
    bool enableStrataRaster{false};

    // When non-empty, the tile task also writes a per-tile metrics table
    // (path extension picked from this option's extension, filename
    // per-tile) -- BatchPipeline concatenates the per-tile tables into one
    // project-level table at this path after all tiles finish, the same
    // way it already mosaics per-tile rasters into a VRT.
    std::filesystem::path outputTablePath;
    bool noRaster{false};

    // OGC WKT coordinate system string resolved from the input point clouds
    // (or ground surface DEM) before tiling starts -- forwarded into each
    // tile task so individual tile GeoTIFFs, and downstream VRT / merged
    // rasters, carry the spatial reference.
    std::string projectionWKT;

    // Folder for the temporary per-tile point files written when an input
    // file is shared by several tiles (see ExecutePipeline). Empty means
    // <outputDir>/_tile_points.
    std::filesystem::path splitDir;
};

class BatchPipeline {
public:
    BatchPipeline(TileGridSpec gridSpec, PipelineJobOptions jobOptions);

    // Runs tileTask on every tile with numThreads workers. Tiles are run in
    // blocks of neighbouring tiles. Before each block runs, every plain
    // LAS/LAZ input file that overlaps two or more of the block's tiles is
    // read once and its points copied into one temporary file per tile, so
    // a tile task reading its points with ForEachTilePoint never
    // decompresses a file that another tile in the block already read.
    // The block's temporary files are removed once its tiles finish.
    // needsPoints, when given, says which tiles will read points at all
    // (pipeline skips tiles that are filtered out or already clipped);
    // other tiles are not given split files.
    bool ExecutePipeline(const std::function<bool(const TileInfo& tile, const PipelineJobOptions& opts)>& tileTask,
                         const std::function<bool(const TileInfo& tile)>& needsPoints = {});

    const std::vector<std::filesystem::path>& GetGeneratedTileRasters() const { return m_tileRasterPaths; }
    std::filesystem::path GetVRTPath() const { return m_vrtPath; }
    const std::vector<std::filesystem::path>& GetGeneratedTileTables() const { return m_tileTablePaths; }
    std::filesystem::path GetMergedTablePath() const { return m_mergedTablePath; }

private:
    TileGridSpec m_gridSpec;
    PipelineJobOptions m_options;
    std::vector<TileInfo> m_tiles;
    std::vector<std::filesystem::path> m_tileRasterPaths;
    std::vector<std::filesystem::path> m_tileTablePaths;
    std::mutex m_mutex;
    std::filesystem::path m_vrtPath;
    std::filesystem::path m_mergedTablePath;
};

} // namespace fusion::batch

#endif // FUSION_BATCH_BATCHPIPELINE_H
