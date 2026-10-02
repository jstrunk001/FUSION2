#include "fusion/batch/BatchPipeline.h"
#include "fusion/batch/TilePointSource.h"
#include "fusion/lidar/LASPointCloud.h"
#include "test_assert.h"

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

// gridmetrics batch mode and pipeline once decompressed each plain LAS/LAZ
// file once per processing tile it overlapped -- nine times for a 2,048 m
// file under 1,000 m tiles. A file shared by several tiles is now read once
// and its points copied into one temporary file per tile. These checks
// confirm every tile receives exactly the points, in exactly the order, a
// direct read of the input files gives it, and that blocks and temporary
// files are handled as described in TilePointSource.h.

namespace {

std::filesystem::path ScratchDir() {
    return std::filesystem::temp_directory_path() / "fusion_tests_tile_point_source";
}

// Writes a point file covering [minX,maxX] x [minY,maxY] with a point every
// `step` units. Each point's GPS time is its own serial number, so a point
// can be identified after it is read back.
bool WriteGridFile(const std::filesystem::path& path, double minX, double maxX, double minY, double maxY,
                   double step, double& serial) {
    fusion::lidar::LASHeaderInfo header;
    header.pointFormat = 1;
    header.versionMajor = 1;
    header.versionMinor = 2;
    header.xScaleFactor = 0.01;
    header.yScaleFactor = 0.01;
    header.zScaleFactor = 0.01;
    header.minX = minX;
    header.maxX = maxX;
    header.minY = minY;
    header.maxY = maxY;
    header.minZ = 0.0;
    header.maxZ = 50.0;

    fusion::lidar::LASWriter writer;
    if (!writer.Open(path, header)) return false;
    for (double x = minX; x <= maxX; x += step) {
        for (double y = minY; y <= maxY; y += step) {
            fusion::lidar::PointRecord pt;
            pt.x = x;
            pt.y = y;
            pt.z = 10.0 + 0.01 * x;
            pt.gpsTime = serial;
            serial += 1.0;
            writer.WritePoint(pt);
        }
    }
    writer.Close();
    return true;
}

// Collects a tile's points, as read by ForEachTilePoint.
std::vector<fusion::lidar::PointRecord> TilePoints(const fusion::batch::TileInfo& tile, const std::filesystem::path& inputDir,
                                                   bool& ok) {
    std::vector<fusion::lidar::PointRecord> points;
    ok = fusion::batch::ForEachTilePoint(tile, inputDir, nullptr,
                                         [&](const fusion::lidar::PointRecord& pt) { points.push_back(pt); });
    return points;
}

bool SamePoints(const std::vector<fusion::lidar::PointRecord>& a, const std::vector<fusion::lidar::PointRecord>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y || a[i].z != b[i].z || a[i].gpsTime != b[i].gpsTime) return false;
    }
    return true;
}

} // namespace

int RunTilePointSourceTests() {
    int failures = 0;
    std::cout << "Tile point source tests\n";

    //1. two input files under a 3 x 2 grid of 100 m tiles with 10 m buffers
    //  - a.laz covers x 0-200, which reaches into the right column's
    //    buffer (x 190-310), so it overlaps all six tiles
    //  - b.las covers x 200-300 and overlaps the middle and right columns
    //  - the 2.5 m point spacing puts points exactly on buffered edges
    std::filesystem::path dir = ScratchDir();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir / "input");
    double serial = 0.0;
    CHECK(WriteGridFile(dir / "input" / "a.laz", 0.0, 200.0, 0.0, 200.0, 2.5, serial), failures);
    CHECK(WriteGridFile(dir / "input" / "b.las", 200.0, 300.0, 0.0, 200.0, 2.5, serial), failures);

    fusion::batch::TileGridSpec spec;
    spec.minX = 0.0;
    spec.maxX = 300.0;
    spec.minY = 0.0;
    spec.maxY = 200.0;
    spec.tileSizeX = 100.0;
    spec.tileSizeY = 100.0;
    spec.bufferDistance = 10.0;
    spec.resolution = 1.0;
    std::vector<fusion::batch::TileInfo> tiles = fusion::batch::TileGridManager::GenerateTiles(spec);
    CHECK(tiles.size() == 6, failures);

    std::vector<fusion::batch::InputFileInfo> files = fusion::batch::ScanInputFiles(dir / "input");
    CHECK(files.size() == 2, failures);

    //2. points read directly, the way the tile loops always read them
    std::vector<std::vector<fusion::lidar::PointRecord>> direct;
    for (const auto& tile : tiles) {
        bool ok = false;
        direct.push_back(TilePoints(tile, dir / "input", ok));
        CHECK(ok, failures);
        CHECK(!direct.back().empty(), failures);
    }

    //3. split both files for all tiles, then read again: same points, same order
    {
        std::vector<fusion::batch::TileInfo> block = tiles;
        std::vector<bool> needs(block.size(), true);
        std::string error;
        CHECK(fusion::batch::SplitInputsForTiles(block, files, needs, dir / "split", 4, error), failures);
        CHECK(error.empty(), failures);

        size_t splitCount = 0;
        for (size_t t = 0; t < block.size(); ++t) {
            for (const auto& src : block[t].inputs) {
                if (src.splitPath.empty()) continue;
                ++splitCount;
                CHECK(std::filesystem::exists(src.splitPath), failures);
            }
            bool ok = false;
            std::vector<fusion::lidar::PointRecord> fromSplit = TilePoints(block[t], dir / "input", ok);
            CHECK(ok, failures);
            CHECK(SamePoints(fromSplit, direct[t]), failures);
        }
        //  - a.laz overlaps 6 tiles, b.las 4: every overlap is split
        CHECK(splitCount == 10, failures);

        //  - the split files are removed afterwards
        fusion::batch::RemoveSplitFiles(block);
        for (const auto& tile : block) {
            for (const auto& src : tile.inputs) {
                if (!src.splitPath.empty()) CHECK(!std::filesystem::exists(src.splitPath), failures);
            }
        }
    }

    //4. a file read by only one tile is not split, and tiles that read no
    //   points are given no split files
    {
        std::vector<fusion::batch::TileInfo> block = tiles;
        std::vector<bool> needs(block.size(), false);
        needs[0] = true;
        std::string error;
        CHECK(fusion::batch::SplitInputsForTiles(block, files, needs, dir / "split", 2, error), failures);
        for (const auto& tile : block) {
            CHECK(tile.inputsAssigned, failures);
            for (const auto& src : tile.inputs) CHECK(src.splitPath.empty(), failures);
        }
        bool ok = false;
        CHECK(SamePoints(TilePoints(block[0], dir / "input", ok), direct[0]), failures);
        CHECK(ok, failures);
    }

    //5. a tile whose split failed reports failure instead of reading
    //   incomplete points
    {
        fusion::batch::TileInfo failed = tiles[0];
        failed.inputsAssigned = true;
        failed.inputsFailed = true;
        bool ok = true;
        TilePoints(failed, dir / "input", ok);
        CHECK(!ok, failures);
    }

    //6. blocks hold every tile once, at most the requested number per block,
    //   in tile-ID order
    {
        auto blocks = fusion::batch::GroupTilesIntoBlocks(tiles, spec, files, 2);
        std::set<int> seen;
        int lastFirstID = 0;
        for (const auto& block : blocks) {
            CHECK(!block.empty() && block.size() <= 2, failures);
            CHECK(block.front().tileID > lastFirstID, failures);
            lastFirstID = block.front().tileID;
            for (size_t i = 0; i < block.size(); ++i) {
                CHECK(seen.insert(block[i].tileID).second, failures);
                if (i > 0) CHECK(block[i].tileID > block[i - 1].tileID, failures);
            }
        }
        CHECK(seen.size() == tiles.size(), failures);

        //  - with the default limit, 200 m files and 100 m tiles give blocks
        //    of 4 x 4 tiles, so this whole 3 x 2 grid is one block
        CHECK(fusion::batch::GroupTilesIntoBlocks(tiles, spec, files).size() == 1, failures);
    }

    std::filesystem::remove_all(dir, ec);
    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}
