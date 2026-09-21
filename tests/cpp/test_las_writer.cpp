#include "fusion/lidar/LASPointCloud.h"
#include "test_assert.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <vector>

// Round-trips small point sets through LASWriter/LASReader. This exercises
// the rewrite that replaced a hand-rolled raw-struct writer (which stamped
// the header's declared pointDataFormat while always writing the 20-byte
// Format-0 record layout underneath, and never compressed .laz output) with
// one that writes every point through the vendored laszip API.

namespace {

std::filesystem::path ScratchPath(const std::string& name) {
    return std::filesystem::temp_directory_path() / ("fusion_tests_" + name);
}

fusion::lidar::PointRecord MakePoint(double x, double y, double z, double gpsTime,
                                      uint8_t classification, uint16_t red, uint16_t green, uint16_t blue) {
    fusion::lidar::PointRecord pt;
    pt.x = x;
    pt.y = y;
    pt.z = z;
    pt.gpsTime = gpsTime;
    pt.classification = classification;
    pt.red = red;
    pt.green = green;
    pt.blue = blue;
    pt.intensity = 123;
    pt.returnNumber = 1;
    pt.numberOfReturns = 1;
    pt.pointSourceID = 7;
    return pt;
}

fusion::lidar::LASHeaderInfo MakeHeader(uint8_t pointFormat, uint8_t versionMinor) {
    fusion::lidar::LASHeaderInfo header;
    header.pointFormat = pointFormat;
    header.versionMajor = 1;
    header.versionMinor = versionMinor;
    header.xScaleFactor = 0.001;
    header.yScaleFactor = 0.001;
    header.zScaleFactor = 0.001;
    header.xOffset = 500000.0;
    header.yOffset = 4000000.0;
    header.zOffset = 0.0;
    header.minX = 500000.0;
    header.maxX = 500100.0;
    header.minY = 4000000.0;
    header.maxY = 4000100.0;
    header.minZ = 0.0;
    header.maxZ = 100.0;
    return header;
}

} // namespace

int RunLASWriterTests() {
    int failures = 0;
    std::cout << "LASWriter/LASReader round-trip tests\n";

    // Format 1 (base + GPS time), uncompressed .las: the record content
    // must actually carry GPS time, and the header must keep reporting
    // format 1 rather than silently downgrading to format 0.
    {
        auto path = ScratchPath("format1.las");
        auto header = MakeHeader(1, 2);

        fusion::lidar::LASWriter writer;
        CHECK(writer.Open(path, header), failures);
        writer.WritePoint(MakePoint(500010.123, 4000020.456, 55.5, 123456.789, 2, 0, 0, 0));
        writer.WritePoint(MakePoint(500020.111, 4000030.222, 60.25, 123457.0, 5, 0, 0, 0));
        writer.Close();

        fusion::lidar::LASReader reader;
        CHECK(reader.Open(path), failures);
        if (reader.IsOpen()) {
            CHECK(reader.GetHeader().pointFormat == 1, failures);
            CHECK(reader.GetPointCount() == 2, failures);

            fusion::lidar::PointRecord pt;
            CHECK(reader.ReadNextPoint(pt), failures);
            CHECK(std::abs(pt.x - 500010.123) < 0.01, failures);
            CHECK(std::abs(pt.y - 4000020.456) < 0.01, failures);
            CHECK(std::abs(pt.z - 55.5) < 0.01, failures);
            CHECK(std::abs(pt.gpsTime - 123456.789) < 0.001, failures);
            CHECK(pt.classification == 2, failures);
            reader.Close();
        }
        std::filesystem::remove(path);
    }

    // Format 3 (base + GPS time + RGB), compressed .laz: content must round
    // trip (including color) and the file must actually be compressed, not
    // raw bytes wearing a .laz extension.
    {
        auto lazPath = ScratchPath("format3.laz");
        auto lasPath = ScratchPath("format3_uncompressed.las");
        auto header = MakeHeader(3, 2);

        // Enough regularly-varying points that real LASzip compression has
        // something to compress -- a handful of points wouldn't show a
        // reliable size difference against fixed per-file overhead.
        std::vector<fusion::lidar::PointRecord> points;
        for (int i = 0; i < 500; ++i) {
            double t = static_cast<double>(i);
            points.push_back(MakePoint(
                500000.0 + t * 0.5, 4000000.0 + t * 0.25, 50.0 + (i % 20),
                100000.0 + t, static_cast<uint8_t>(2 + (i % 3)),
                static_cast<uint16_t>(1000 + i), static_cast<uint16_t>(2000 + i), static_cast<uint16_t>(3000 + i)));
        }

        {
            fusion::lidar::LASWriter lazWriter;
            CHECK(lazWriter.Open(lazPath, header), failures);
            for (const auto& pt : points) lazWriter.WritePoint(pt);
            lazWriter.Close();

            fusion::lidar::LASWriter lasWriter;
            CHECK(lasWriter.Open(lasPath, header), failures);
            for (const auto& pt : points) lasWriter.WritePoint(pt);
            lasWriter.Close();
        }

        auto lazSize = std::filesystem::exists(lazPath) ? std::filesystem::file_size(lazPath) : 0;
        auto lasSize = std::filesystem::exists(lasPath) ? std::filesystem::file_size(lasPath) : 0;
        CHECK(lazSize > 0 && lasSize > 0, failures);
        CHECK(lazSize < lasSize, failures); // real compression, not raw bytes under a .laz name

        fusion::lidar::LASReader reader;
        CHECK(reader.Open(lazPath), failures);
        if (reader.IsOpen()) {
            CHECK(reader.GetHeader().pointFormat == 3, failures);
            CHECK(reader.GetPointCount() == points.size(), failures);

            fusion::lidar::PointRecord pt;
            CHECK(reader.ReadNextPoint(pt), failures);
            CHECK(std::abs(pt.x - points[0].x) < 0.01, failures);
            CHECK(std::abs(pt.gpsTime - points[0].gpsTime) < 0.001, failures);
            CHECK(pt.red == points[0].red, failures);
            CHECK(pt.green == points[0].green, failures);
            CHECK(pt.blue == points[0].blue, failures);
            reader.Close();
        }
        std::filesystem::remove(lazPath);
        std::filesystem::remove(lasPath);
    }

    // Format 7 (LAS 1.4 extended base + GPS + RGB) -- the extended-format
    // path must also carry RGB through, not just the pre-existing format
    // 6 baseline.
    {
        auto path = ScratchPath("format7.las");
        auto header = MakeHeader(7, 4);

        fusion::lidar::LASWriter writer;
        CHECK(writer.Open(path, header), failures);
        writer.WritePoint(MakePoint(500005.0, 4000005.0, 42.0, 999.0, 2, 500, 600, 700));
        writer.Close();

        fusion::lidar::LASReader reader;
        CHECK(reader.Open(path), failures);
        if (reader.IsOpen()) {
            CHECK(reader.GetHeader().pointFormat == 7, failures);
            fusion::lidar::PointRecord pt;
            CHECK(reader.ReadNextPoint(pt), failures);
            CHECK(pt.red == 500, failures);
            CHECK(pt.green == 600, failures);
            CHECK(pt.blue == 700, failures);
            reader.Close();
        }
        std::filesystem::remove(path);
    }

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}
