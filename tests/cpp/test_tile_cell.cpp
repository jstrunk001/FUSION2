#include "fusion/batch/BatchPipeline.h"
#include "fusion/lidar/LASPointCloud.h"
#include "test_assert.h"

#include <cmath>
#include <filesystem>
#include <string>
#include <iostream>
#include <vector>

namespace {

// Counts how many tiles give a point a cell. Every point inside the
// project must be counted by exactly one tile.
int TilesClaimingPoint(double x, double y, const std::vector<fusion::batch::TileInfo>& tiles,
                       const fusion::batch::TileGridSpec& spec) {
    int claims = 0;
    for (const auto& tile : tiles) {
        int cols = static_cast<int>(std::ceil((tile.maxX - tile.minX) / spec.resolution));
        int rows = static_cast<int>(std::ceil((tile.maxY - tile.minY) / spec.resolution));
        int col = -1;
        int row = -1;
        if (fusion::batch::TileCellForPoint(x, y, tile, spec, cols, rows, col, row)) {
            ++claims;
        }
    }
    return claims;
}

} // namespace

// gridmetrics batch mode reads each tile's points with a buffer around it.
// Its cell lookup once rounded toward zero, so buffer points up to one cell
// left of or above a tile were counted in the tile's first column or top
// row, as well as in the neighbouring tile -- about 3.8% extra returns on a
// 1000 m tile run with 20 m cells. These checks confirm buffer points are
// never given a cell and every point is counted by exactly one tile.
int RunTileCellTests() {
    int failures = 0;
    std::cout << "TileCellForPoint tests\n";

    //1. a 2 x 2 grid of 100 m tiles with 20 m cells and a 50 m buffer
    fusion::batch::TileGridSpec spec;
    spec.minX = 1000.0;
    spec.minY = 2000.0;
    spec.maxX = 1200.0;
    spec.maxY = 2200.0;
    spec.tileSizeX = 100.0;
    spec.tileSizeY = 100.0;
    spec.bufferDistance = 50.0;
    spec.resolution = 20.0;
    auto tiles = fusion::batch::TileGridManager::GenerateTiles(spec);
    CHECK(tiles.size() == 4, failures);

    //2. the lower-left tile, and the cell lookup for points inside it
    const fusion::batch::TileInfo* lowerLeft = nullptr;
    for (const auto& tile : tiles) {
        if (tile.minX == 1000.0 && tile.minY == 2000.0) lowerLeft = &tile;
    }
    CHECK(lowerLeft != nullptr, failures);
    if (lowerLeft) {
        int col = -1;
        int row = -1;
        CHECK(fusion::batch::TileCellForPoint(1005.0, 2095.0, *lowerLeft, spec, 5, 5, col, row), failures);
        CHECK(col == 0 && row == 0, failures);
        CHECK(fusion::batch::TileCellForPoint(1095.0, 2005.0, *lowerLeft, spec, 5, 5, col, row), failures);
        CHECK(col == 4 && row == 4, failures);

        //3. buffer points just past each of the four edges get no cell
        //  - 5 m past the edge, less than one cell width: the case that
        //    rounding toward zero used to put in column 0 or row 0
        col = -1;
        row = -1;
        CHECK(!fusion::batch::TileCellForPoint(995.0, 2050.0, *lowerLeft, spec, 5, 5, col, row), failures);
        CHECK(!fusion::batch::TileCellForPoint(1050.0, 2105.0, *lowerLeft, spec, 5, 5, col, row), failures);
        CHECK(!fusion::batch::TileCellForPoint(1105.0, 2050.0, *lowerLeft, spec, 5, 5, col, row), failures);
        CHECK(!fusion::batch::TileCellForPoint(1050.0, 1995.0, *lowerLeft, spec, 5, 5, col, row), failures);
        CHECK(col == -1 && row == -1, failures);
    }

    //4. every point across the project, including points on the internal
    //   seams and on all four outer edges, is counted by exactly one tile
    std::vector<double> xs = {1000.0, 1000.5, 1019.99, 1020.0, 1099.99, 1100.0, 1100.01, 1180.0, 1199.99, 1200.0};
    std::vector<double> ys = {2000.0, 2000.5, 2019.99, 2020.0, 2099.99, 2100.0, 2100.01, 2180.0, 2199.99, 2200.0};
    int pointsNotCountedOnce = 0;
    for (double x : xs) {
        for (double y : ys) {
            if (TilesClaimingPoint(x, y, tiles, spec) != 1) ++pointsNotCountedOnce;
        }
    }
    CHECK(pointsNotCountedOnce == 0, failures);

    //5. points outside the project are counted by no tile
    CHECK(TilesClaimingPoint(999.0, 2100.0, tiles, spec) == 0, failures);
    CHECK(TilesClaimingPoint(1201.0, 2100.0, tiles, spec) == 0, failures);
    CHECK(TilesClaimingPoint(1100.0, 1999.0, tiles, spec) == 0, failures);
    CHECK(TilesClaimingPoint(1100.0, 2201.0, tiles, spec) == 0, failures);

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}

namespace {

// Writes a two-point LAS file whose points sit at (minX, minY) and
// (maxX, maxY), so its header extent is exactly that box.
bool WriteTwoPointLas(const std::filesystem::path& path, double minX, double minY, double maxX, double maxY) {
    fusion::lidar::LASHeaderInfo header;
    header.pointFormat = 1;
    header.versionMajor = 1;
    header.versionMinor = 2;
    header.xScaleFactor = 0.01;
    header.yScaleFactor = 0.01;
    header.zScaleFactor = 0.01;
    header.xOffset = minX;
    header.yOffset = minY;
    header.minX = minX;
    header.maxX = maxX;
    header.minY = minY;
    header.maxY = maxY;
    header.minZ = 0.0;
    header.maxZ = 10.0;
    fusion::lidar::LASWriter writer;
    if (!writer.Open(path, header)) return false;
    fusion::lidar::PointRecord pt;
    pt.returnNumber = 1;
    pt.numberOfReturns = 1;
    pt.x = minX;
    pt.y = minY;
    pt.z = 1.0;
    writer.WritePoint(pt);
    pt.x = maxX;
    pt.y = maxY;
    pt.z = 9.0;
    writer.WritePoint(pt);
    writer.Close();
    return true;
}

} // namespace

// gridmetrics and pipeline batch modes once used a fixed (0,0)-(5000,5000)
// project extent when /extent was left out, so data anywhere else produced
// a mosaic of empty tiles and a "completed successfully" message. These
// checks confirm the default is now the input files' combined extent,
// snapped outward to the cell size, and that a run stops with an error when
// there are no files or none overlap the given extent.
int RunProjectExtentTests() {
    int failures = 0;
    std::cout << "ResolveProjectExtent tests\n";

    //1. two made-up LAS files far from (0, 0), in their own folder
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "fusion_tests_project_extent";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    CHECK(WriteTwoPointLas(dir / "a.las", -90112.0, 165888.0, -89100.5, 166900.0), failures);
    CHECK(WriteTwoPointLas(dir / "b.las", -89100.5, 166900.0, -88064.0, 167936.0), failures);

    //2. no /extent: combined extent of both files, snapped outward to 20 m
    fusion::batch::TileGridSpec spec;
    std::string message;
    CHECK(fusion::batch::ResolveProjectExtent(dir, false, 20.0, spec, message), failures);
    CHECK(spec.minX == -90120.0 && spec.minY == 165880.0, failures);
    CHECK(spec.maxX == -88060.0 && spec.maxY == 167940.0, failures);
    CHECK(!message.empty(), failures);

    //3. a given /extent that overlaps the files is kept unchanged
    fusion::batch::TileGridSpec given;
    given.minX = -90000.0;
    given.minY = 166000.0;
    given.maxX = -89000.0;
    given.maxY = 167000.0;
    CHECK(fusion::batch::ResolveProjectExtent(dir, true, 20.0, given, message), failures);
    CHECK(given.minX == -90000.0 && given.maxY == 167000.0, failures);

    //4. a given /extent that misses every file is an error
    fusion::batch::TileGridSpec missing;
    missing.minX = 0.0;
    missing.minY = 0.0;
    missing.maxX = 5000.0;
    missing.maxY = 5000.0;
    CHECK(!fusion::batch::ResolveProjectExtent(dir, true, 20.0, missing, message), failures);
    CHECK(message.find("overlap") != std::string::npos, failures);

    //5. a folder with no LAS/LAZ files is an error
    std::filesystem::path emptyDir = dir / "empty";
    std::filesystem::create_directories(emptyDir);
    fusion::batch::TileGridSpec none;
    CHECK(!fusion::batch::ResolveProjectExtent(emptyDir, false, 20.0, none, message), failures);

    std::filesystem::remove_all(dir);
    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}
