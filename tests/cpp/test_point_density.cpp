#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/PointDensity.h"
#include "fusion/raster/GDALRaster.h"
#include "test_assert.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// Checks the point-count raster that catalog's /density option writes.
// catalog previously read the raster's origin and coordinate system from a
// reference to the point reader's header after closing the reader, which
// resets that header -- so the raster came out at (0, 0) with no
// coordinate system even though its cell counts were right. It also
// dropped points lying exactly on the maximum-X or minimum-Y edge.

namespace {

std::filesystem::path ScratchPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("fusion_tests_" + name);
}

fusion::lidar::PointRecord MakePoint(double x, double y) {
    fusion::lidar::PointRecord pt;
    pt.x = x;
    pt.y = y;
    pt.z = 100.0;
    pt.returnNumber = 1;
    pt.numberOfReturns = 1;
    pt.classification = 1;
    return pt;
}

} // namespace

int RunPointDensityTests() {
    int failures = 0;
    std::cout << "Point-count raster (catalog /density) tests\n";

    //1. write a 100 x 100 unit tile with a coordinate system and seven
    //   points: one in each corner cell, including one exactly on the
    //   maximum-X / minimum-Y corner, plus three in one interior cell
    auto lasPath = ScratchPath("point_density.las");
    auto tifPath = ScratchPath("point_density.tif");

    fusion::lidar::LASHeaderInfo header;
    header.pointFormat = 1;
    header.versionMajor = 1;
    header.versionMinor = 2;
    header.xScaleFactor = 0.01;
    header.yScaleFactor = 0.01;
    header.zScaleFactor = 0.01;
    header.xOffset = 500000.0;
    header.yOffset = 4000000.0;
    header.minX = 500000.0;
    header.maxX = 500100.0;
    header.minY = 4000000.0;
    header.maxY = 4000100.0;
    header.minZ = 100.0;
    header.maxZ = 100.0;
    header.projectionWKT = "PROJCS[\"NAD83 / Washington North\",GEOGCS[\"NAD83\",DATUM[\"North_American_Datum_1983\",SPHEROID[\"GRS 1980\",6378137,298.257222101]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Lambert_Conformal_Conic_2SP\"],PARAMETER[\"standard_parallel_1\",48.73333333333333],PARAMETER[\"standard_parallel_2\",47.5],PARAMETER[\"latitude_of_origin\",47],PARAMETER[\"central_meridian\",-120.8333333333333],PARAMETER[\"false_easting\",500000],PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]";

    std::vector<fusion::lidar::PointRecord> points = {
        MakePoint(500000.0, 4000100.0),   // top-left corner
        MakePoint(500100.0, 4000100.0),   // top-right corner, on max X
        MakePoint(500000.0, 4000000.0),   // bottom-left corner, on min Y
        MakePoint(500100.0, 4000000.0),   // bottom-right corner, on max X and min Y
        MakePoint(500055.0, 4000045.0),   // three points in one interior cell
        MakePoint(500056.0, 4000046.0),
        MakePoint(500057.0, 4000047.0),
    };

    fusion::lidar::LASWriter writer;
    CHECK(writer.Open(lasPath, header), failures);
    for (const auto& pt : points) writer.WritePoint(pt);
    writer.Close();

    //2. build the raster at 10-unit cells
    CHECK(fusion::lidar::WritePointCountRaster({lasPath}, 10.0, tifPath), failures);

    //3. the raster must sit on the tile and carry its coordinate system
    fusion::raster::GDALRaster raster;
    CHECK(raster.Open(tifPath), failures);
    if (raster.IsOpen()) {
        const auto& info = raster.GetInfo();
        CHECK(info.width == 10, failures);
        CHECK(info.height == 10, failures);
        CHECK(std::abs(info.originX - 500000.0) < 1e-6, failures);
        CHECK(std::abs(info.originY - 4000100.0) < 1e-6, failures);
        CHECK(std::abs(info.pixelWidth - 10.0) < 1e-9, failures);
        CHECK(!info.projectionWKT.empty(), failures);

        //4. every point must be counted, including those on the max-X and
        //   min-Y edges, and each count must land in the right cell
        std::vector<float> counts;
        CHECK(raster.ReadBandData(1, counts), failures);
        double total = 0.0;
        for (float c : counts) total += c;
        CHECK(std::abs(total - static_cast<double>(points.size())) < 1e-6, failures);
        if (counts.size() == 100) {
            CHECK(counts[0 * 10 + 0] == 1.0f, failures);   // top-left
            CHECK(counts[0 * 10 + 9] == 1.0f, failures);   // top-right
            CHECK(counts[9 * 10 + 0] == 1.0f, failures);   // bottom-left
            CHECK(counts[9 * 10 + 9] == 1.0f, failures);   // bottom-right
            CHECK(counts[5 * 10 + 5] == 3.0f, failures);   // interior cell
        }
        raster.Close();
    }

    std::filesystem::remove(lasPath);
    std::filesystem::remove(tifPath);

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}
