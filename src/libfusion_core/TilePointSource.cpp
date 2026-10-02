#include "fusion/batch/TilePointSource.h"
#include "fusion/lidar/InputResolver.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>

namespace fusion::batch {

namespace {

// Split files hold PointRecord structs exactly as they sit in memory, so a
// point read back from one is identical to the point first decoded from
// the input file. They are written and read by the same running program
// and never outlive it, so the layout never needs to be portable.
static_assert(std::is_trivially_copyable_v<fusion::lidar::PointRecord>,
              "split files store PointRecord as raw bytes");

// Points held per tile before they are appended to its split file. The
// total across one input file's tiles stays near 32 MB.
constexpr size_t kSplitBufferBytes = 32u * 1024u * 1024u;
constexpr size_t kMinBufferPoints = 1024;
constexpr size_t kReadChunkPoints = 8192;

bool PointInBufferedExtent(const fusion::lidar::PointRecord& pt, const TileInfo& tile) {
    return pt.x >= tile.bufferedMinX && pt.x <= tile.bufferedMaxX &&
           pt.y >= tile.bufferedMinY && pt.y <= tile.bufferedMaxY;
}

// One input file to split, and the tiles (indexes into the block) that
// take its points.
struct SplitJob {
    size_t fileIndex{0};
    std::vector<size_t> tileIndexes;
};

// Appends a buffer of points to a split file and empties the buffer.
bool AppendPoints(const std::filesystem::path& path, std::vector<fusion::lidar::PointRecord>& buffer) {
    if (buffer.empty()) return true;
    std::ofstream out(path, std::ios::binary | std::ios::app);
    if (!out.is_open()) return false;
    out.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size() * sizeof(fusion::lidar::PointRecord)));
    bool ok = out.good();
    buffer.clear();
    return ok;
}

// Reads one input file once and writes each point to the split file of
// every job tile whose buffered extent holds it.
bool RunSplitJob(const SplitJob& job, const std::vector<InputFileInfo>& files, const std::vector<TileInfo>& blockTiles) {
    const InputFileInfo& file = files[job.fileIndex];
    size_t numTiles = job.tileIndexes.size();

    //1. find each job tile's split file, and start every one empty
    //  - an existing file from an interrupted earlier run is overwritten
    //  - every split file exists afterwards, so a missing one means failure
    std::vector<std::filesystem::path> splitPaths(numTiles);
    for (size_t t = 0; t < numTiles; ++t) {
        const TileInfo& tile = blockTiles[job.tileIndexes[t]];
        for (const auto& src : tile.inputs) {
            if (src.inputFile == file.path) splitPaths[t] = src.splitPath;
        }
        std::ofstream out(splitPaths[t], std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
    }

    //2. index the job tiles by column and row
    //  - tiles in one column share their x edges and tiles in one row share
    //    their y edges, so a point is matched against the few columns and
    //    rows first rather than against every tile
    std::vector<std::pair<double, double>> colEdges;
    std::vector<std::pair<double, double>> rowEdges;
    for (size_t idx : job.tileIndexes) {
        const TileInfo& tile = blockTiles[idx];
        std::pair<double, double> col{tile.bufferedMinX, tile.bufferedMaxX};
        std::pair<double, double> row{tile.bufferedMinY, tile.bufferedMaxY};
        if (std::find(colEdges.begin(), colEdges.end(), col) == colEdges.end()) colEdges.push_back(col);
        if (std::find(rowEdges.begin(), rowEdges.end(), row) == rowEdges.end()) rowEdges.push_back(row);
    }
    std::vector<int> tileAt(colEdges.size() * rowEdges.size(), -1);
    for (size_t t = 0; t < numTiles; ++t) {
        const TileInfo& tile = blockTiles[job.tileIndexes[t]];
        size_t c = std::find(colEdges.begin(), colEdges.end(), std::make_pair(tile.bufferedMinX, tile.bufferedMaxX)) - colEdges.begin();
        size_t r = std::find(rowEdges.begin(), rowEdges.end(), std::make_pair(tile.bufferedMinY, tile.bufferedMaxY)) - rowEdges.begin();
        tileAt[c * rowEdges.size() + r] = static_cast<int>(t);
    }

    //3. open the input file
    //  - an unreadable file gives no points, as the tile loops have always
    //    skipped a file they cannot open
    fusion::lidar::LASReader reader;
    if (!reader.Open(file.path)) return true;

    //4. read every point once and buffer it for each tile that holds it
    size_t bufferPoints = (std::max)(kMinBufferPoints, kSplitBufferBytes / sizeof(fusion::lidar::PointRecord) / numTiles);
    std::vector<std::vector<fusion::lidar::PointRecord>> buffers(numTiles);
    for (auto& b : buffers) b.reserve(bufferPoints);

    bool ok = true;
    std::vector<size_t> matchedCols;
    fusion::lidar::PointRecord pt;
    while (ok && reader.ReadNextPoint(pt)) {
        matchedCols.clear();
        for (size_t c = 0; c < colEdges.size(); ++c) {
            if (pt.x >= colEdges[c].first && pt.x <= colEdges[c].second) matchedCols.push_back(c);
        }
        if (matchedCols.empty()) continue;
        for (size_t r = 0; r < rowEdges.size(); ++r) {
            if (pt.y < rowEdges[r].first || pt.y > rowEdges[r].second) continue;
            for (size_t c : matchedCols) {
                int t = tileAt[c * rowEdges.size() + r];
                if (t < 0) continue;
                //  - the same inclusive test a direct read applies
                if (!PointInBufferedExtent(pt, blockTiles[job.tileIndexes[t]])) continue;
                buffers[t].push_back(pt);
                if (buffers[t].size() >= bufferPoints && !AppendPoints(splitPaths[t], buffers[t])) ok = false;
            }
        }
    }
    reader.Close();

    //5. write what is left in the buffers
    for (size_t t = 0; t < numTiles && ok; ++t) {
        if (!AppendPoints(splitPaths[t], buffers[t])) ok = false;
    }
    return ok;
}

} // namespace

std::vector<InputFileInfo> ScanInputFiles(const std::filesystem::path& inputPath) {
    std::vector<InputFileInfo> files;
    for (const auto& path : fusion::lidar::ResolveInputFiles({inputPath.string()})) {
        fusion::lidar::LASReader reader;
        if (!reader.Open(path)) continue;
        const auto& h = reader.GetHeader();
        InputFileInfo info;
        info.path = path;
        info.minX = h.minX;
        info.maxX = h.maxX;
        info.minY = h.minY;
        info.maxY = h.maxY;
        info.isCOPC = reader.IsCOPC();
        reader.Close();
        files.push_back(info);
    }
    return files;
}

bool InputOverlapsTile(const InputFileInfo& file, const TileInfo& tile) {
    return !(file.maxX < tile.bufferedMinX || file.minX > tile.bufferedMaxX ||
             file.maxY < tile.bufferedMinY || file.minY > tile.bufferedMaxY);
}

std::vector<std::vector<TileInfo>> GroupTilesIntoBlocks(const std::vector<TileInfo>& tiles, const TileGridSpec& spec,
                                                       const std::vector<InputFileInfo>& files,
                                                       size_t maxTilesPerBlock) {
    //1. widest and tallest plain LAS/LAZ file (COPC files are never split)
    double maxFileWidth = 0.0;
    double maxFileHeight = 0.0;
    for (const auto& f : files) {
        if (f.isCOPC) continue;
        maxFileWidth = (std::max)(maxFileWidth, f.maxX - f.minX);
        maxFileHeight = (std::max)(maxFileHeight, f.maxY - f.minY);
    }
    if (maxFileWidth <= 0.0 && maxFileHeight <= 0.0) return {tiles};
    if (spec.tileSizeX <= 0.0 || spec.tileSizeY <= 0.0) return {tiles};

    //2. block size in tiles: about two file widths, at least two tiles each
    //   way, and no more than maxTilesPerBlock tiles
    long long blockCols = (std::max)(2LL, static_cast<long long>(std::ceil(2.0 * maxFileWidth / spec.tileSizeX)));
    long long blockRows = (std::max)(2LL, static_cast<long long>(std::ceil(2.0 * maxFileHeight / spec.tileSizeY)));
    long long maxTiles = (std::max)(static_cast<long long>(maxTilesPerBlock), 1LL);
    while (blockCols * blockRows > maxTiles) {
        if (blockCols >= blockRows && blockCols > 1) --blockCols;
        else if (blockRows > 1) --blockRows;
        else break;
    }

    //3. give each tile its block from its column and row in the tile grid
    std::map<std::pair<long long, long long>, std::vector<TileInfo>> byBlock;
    for (const auto& tile : tiles) {
        long long col = std::llround((tile.minX - spec.minX) / spec.tileSizeX);
        long long row = std::llround((tile.minY - spec.minY) / spec.tileSizeY);
        byBlock[{col / blockCols, row / blockRows}].push_back(tile);
    }

    //4. blocks in order of their first tile, tiles in tile-ID order
    std::vector<std::vector<TileInfo>> blocks;
    for (auto& [key, blockTiles] : byBlock) {
        std::sort(blockTiles.begin(), blockTiles.end(),
                  [](const TileInfo& a, const TileInfo& b) { return a.tileID < b.tileID; });
        blocks.push_back(std::move(blockTiles));
    }
    std::sort(blocks.begin(), blocks.end(),
              [](const std::vector<TileInfo>& a, const std::vector<TileInfo>& b) { return a.front().tileID < b.front().tileID; });
    return blocks;
}

bool SplitInputsForTiles(std::vector<TileInfo>& blockTiles, const std::vector<InputFileInfo>& files,
                         const std::vector<bool>& needsPoints, const std::filesystem::path& splitDir,
                         int numThreads, std::string& error) {
    //1. start every tile with no inputs
    for (auto& tile : blockTiles) {
        tile.inputs.clear();
        tile.inputsAssigned = true;
        tile.inputsFailed = false;
    }

    //2. for each input file, list the tiles it overlaps, and split it when
    //   it is a plain LAS/LAZ file shared by two or more tiles that read points
    std::vector<SplitJob> jobs;
    for (size_t fi = 0; fi < files.size(); ++fi) {
        const InputFileInfo& file = files[fi];
        std::vector<size_t> overlapping;
        std::vector<size_t> readers;
        for (size_t t = 0; t < blockTiles.size(); ++t) {
            if (!InputOverlapsTile(file, blockTiles[t])) continue;
            overlapping.push_back(t);
            if (t < needsPoints.size() && needsPoints[t]) readers.push_back(t);
        }
        bool split = !file.isCOPC && readers.size() >= 2;

        for (size_t t : overlapping) {
            TileInputSource src;
            src.inputFile = file.path;
            bool tileReads = std::find(readers.begin(), readers.end(), t) != readers.end();
            if (split && tileReads) {
                src.splitPath = splitDir / (blockTiles[t].name + "_" + std::to_string(fi) + ".pts");
            }
            blockTiles[t].inputs.push_back(src);
        }
        if (split) jobs.push_back({fi, readers});
    }
    if (jobs.empty()) return true;

    //3. split the shared files in parallel, one input file per worker at a time
    std::error_code ec;
    std::filesystem::create_directories(splitDir, ec);
    std::vector<char> jobOk(jobs.size(), 0);
    std::atomic<size_t> nextJob{0};
    int workerCount = (std::max)(1, (std::min)(numThreads, static_cast<int>(jobs.size())));
    std::vector<std::thread> workers;
    for (int w = 0; w < workerCount; ++w) {
        workers.emplace_back([&]() {
            while (true) {
                size_t j = nextJob.fetch_add(1);
                if (j >= jobs.size()) break;
                jobOk[j] = RunSplitJob(jobs[j], files, blockTiles) ? 1 : 0;
            }
        });
    }
    for (auto& worker : workers) worker.join();

    //4. mark the tiles of any failed split, so they fail rather than run
    //   on missing points
    bool allOk = true;
    for (size_t j = 0; j < jobs.size(); ++j) {
        if (jobOk[j]) continue;
        allOk = false;
        if (!error.empty()) error += "; ";
        error += "could not write split point files for " + files[jobs[j].fileIndex].path.string() + " in " + splitDir.string();
        for (size_t t : jobs[j].tileIndexes) blockTiles[t].inputsFailed = true;
    }
    return allOk;
}

bool ForEachTilePoint(const TileInfo& tile, const std::filesystem::path& inputPath,
                      const std::function<void(const fusion::lidar::LASHeaderInfo&)>& onInputFile,
                      const std::function<void(const fusion::lidar::PointRecord&)>& onPoint) {
    //1. without assigned inputs, read every overlapping input file directly
    std::vector<TileInputSource> inputs = tile.inputs;
    if (!tile.inputsAssigned) {
        inputs.clear();
        for (const auto& file : ScanInputFiles(inputPath)) {
            if (InputOverlapsTile(file, tile)) inputs.push_back({file.path, {}});
        }
    }
    if (tile.inputsFailed) return false;

    //2. read each input file's points, directly or from its split file
    for (const auto& src : inputs) {
        fusion::lidar::LASReader reader;
        if (!reader.Open(src.inputFile)) continue;
        if (onInputFile) onInputFile(reader.GetHeader());

        //  - COPC files seek to the chunks overlapping the extent; a plain
        //    file is read in full and filtered
        if (src.splitPath.empty()) {
            reader.ReadPointsInExtent(tile.bufferedMinX, tile.bufferedMinY, tile.bufferedMaxX, tile.bufferedMaxY, onPoint);
            reader.Close();
            continue;
        }
        reader.Close();

        //  - a split file already holds only this tile's points, in file order
        std::ifstream in(src.splitPath, std::ios::binary);
        if (!in.is_open()) return false;
        std::vector<fusion::lidar::PointRecord> chunk(kReadChunkPoints);
        while (in) {
            in.read(reinterpret_cast<char*>(chunk.data()),
                    static_cast<std::streamsize>(chunk.size() * sizeof(fusion::lidar::PointRecord)));
            std::streamsize bytes = in.gcount();
            if (bytes % static_cast<std::streamsize>(sizeof(fusion::lidar::PointRecord)) != 0) return false;
            size_t count = static_cast<size_t>(bytes) / sizeof(fusion::lidar::PointRecord);
            for (size_t i = 0; i < count; ++i) onPoint(chunk[i]);
        }
    }
    return true;
}

void RemoveSplitFiles(const std::vector<TileInfo>& blockTiles) {
    for (const auto& tile : blockTiles) {
        for (const auto& src : tile.inputs) {
            if (src.splitPath.empty()) continue;
            std::error_code ec;
            std::filesystem::remove(src.splitPath, ec);
        }
    }
}

} // namespace fusion::batch
