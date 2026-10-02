#ifndef FUSION_BATCH_TILEPOINTSOURCE_H
#define FUSION_BATCH_TILEPOINTSOURCE_H

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "fusion/batch/BatchPipeline.h"
#include "fusion/lidar/LASPointCloud.h"

namespace fusion::batch {

// Header facts about one input LAS/LAZ file, read once before tiling.
struct InputFileInfo {
    std::filesystem::path path;
    double minX{0.0};
    double maxX{0.0};
    double minY{0.0};
    double maxY{0.0};
    bool isCOPC{false};
};

// Reads the header of every LAS/LAZ file under inputPath (a directory, or
// one file), in the sorted order ResolveInputFiles gives. Files that cannot
// be opened are left out, as the tile loops have always skipped them.
std::vector<InputFileInfo> ScanInputFiles(const std::filesystem::path& inputPath);

// True when the file's header extent overlaps the tile's buffered extent.
bool InputOverlapsTile(const InputFileInfo& file, const TileInfo& tile);

// Groups tiles into blocks of neighbouring tiles for ExecutePipeline. A
// block spans about two input-file widths in each direction (at least two
// tiles each way) and holds at most maxTilesPerBlock tiles, so each input
// file is read about once per block while the temporary split files for
// one block stay a few input files in size. Blocks, and tiles inside each
// block, come out in tile-ID order. Returns one block of all tiles when
// files is empty.
std::vector<std::vector<TileInfo>> GroupTilesIntoBlocks(const std::vector<TileInfo>& tiles, const TileGridSpec& spec,
                                                       const std::vector<InputFileInfo>& files,
                                                       size_t maxTilesPerBlock = 256);

// Fills in each tile's inputs (see TileInfo) and writes the split files
// for one block. A plain LAS/LAZ file that overlaps two or more tiles for
// which needsPoints is true is read once, and each of its points is
// appended to the split file of every such tile whose buffered extent
// holds it; those tiles then read the split file instead of the input
// file. COPC files, and files overlapping only one such tile, are read
// directly by the tile. Input files are split in parallel with numThreads
// workers, each writing only its own files. Split files are named
// <splitDir>/<tile name>_<input index>.pts. Returns false, with error set
// and the affected tiles marked inputsFailed, when a split file cannot be
// written.
bool SplitInputsForTiles(std::vector<TileInfo>& blockTiles, const std::vector<InputFileInfo>& files,
                         const std::vector<bool>& needsPoints, const std::filesystem::path& splitDir,
                         int numThreads, std::string& error);

// Calls onPoint for every point in the tile's buffered extent, reading its
// input files in input-file order and each file's points in file order --
// the same points, in the same order, whether a file is read directly or
// from its split file. onInputFile, when given, is called with each
// overlapping input file's header before that file's points. A tile whose
// inputs were not assigned by BatchPipeline reads every overlapping file
// under inputPath directly. Returns false when the tile's inputs are
// marked failed or a split file cannot be read.
bool ForEachTilePoint(const TileInfo& tile, const std::filesystem::path& inputPath,
                      const std::function<void(const fusion::lidar::LASHeaderInfo&)>& onInputFile,
                      const std::function<void(const fusion::lidar::PointRecord&)>& onPoint);

// Removes the block's split files (temporary files this library wrote).
void RemoveSplitFiles(const std::vector<TileInfo>& blockTiles);

} // namespace fusion::batch

#endif // FUSION_BATCH_TILEPOINTSOURCE_H
