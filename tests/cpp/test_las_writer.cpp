#include "fusion/lidar/LASPointCloud.h"
#include "fusion/raster/GDALRaster.h"
#include "test_assert.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

// Reads the 2-byte global encoding field straight from a LAS file's header
// (byte offset 6, little-endian), independent of LASReader.
uint16_t ReadGlobalEncoding(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    unsigned char bytes[8] = {0};
    in.read(reinterpret_cast<char*>(bytes), 8);
    return static_cast<uint16_t>(bytes[6] | (bytes[7] << 8));
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

    // Format 6 (LAS 1.4), compressed .laz: LASzip's format 6-10 compressor
    // corrupts the stream when the legacy classification/flag bits disagree
    // with the extended ones, so every point must decode -- not just the
    // first few dozen -- with varied classes, withheld flags, and returns.
    {
        auto path = ScratchPath("format6.laz");
        auto header = MakeHeader(6, 4);

        std::vector<fusion::lidar::PointRecord> points;
        for (int i = 0; i < 3000; ++i) {
            double t = static_cast<double>(i);
            auto pt = MakePoint(500000.0 + t * 0.03, 4000000.0 + t * 0.02, 40.0 + (i % 37),
                                200000.0 + t * 0.001, static_cast<uint8_t>((i % 3 == 0) ? 2 : (i % 3 == 1) ? 5 : 1), 0, 0, 0);
            pt.returnNumber = static_cast<uint8_t>(1 + (i % 3));
            pt.numberOfReturns = 3;
            pt.withheld = (i % 50 == 0);
            pt.scanAngle = static_cast<int16_t>((i % 21) - 10);
            points.push_back(pt);
        }

        fusion::lidar::LASWriter writer;
        CHECK(writer.Open(path, header), failures);
        for (const auto& pt : points) CHECK(writer.WritePoint(pt), failures);
        writer.Close();

        fusion::lidar::LASReader reader;
        CHECK(reader.Open(path), failures);
        if (reader.IsOpen()) {
            CHECK(reader.GetHeader().pointFormat == 6, failures);
            CHECK(reader.GetPointCount() == points.size(), failures);
            size_t nRead = 0;
            size_t nMismatch = 0;
            fusion::lidar::PointRecord pt;
            while (reader.ReadNextPoint(pt)) {
                const auto& expected = points[nRead];
                if (std::abs(pt.x - expected.x) > 0.01 || std::abs(pt.z - expected.z) > 0.01 ||
                    pt.classification != expected.classification || pt.withheld != expected.withheld ||
                    pt.returnNumber != expected.returnNumber || pt.scanAngle != expected.scanAngle) {
                    nMismatch++;
                }
                nRead++;
            }
            CHECK(nRead == points.size(), failures);
            CHECK(nMismatch == 0, failures);
            reader.Close();
        }
        std::filesystem::remove(path);
    }

    // Coordinate reference system (WKT VLR 2112) propagation test:
    // writing a point cloud with a non-empty projectionWKT must write the
    // "LASF_Projection" VLR so the reader recovers the identical CRS.
    {
        auto path = ScratchPath("projection_roundtrip.laz");
        auto header = MakeHeader(1, 2);
        header.projectionWKT = "PROJCS[\"NAD83 / Washington North\",GEOGCS[\"NAD83\",DATUM[\"North_American_Datum_1983\",SPHEROID[\"GRS 1980\",6378137,298.257222101]],PRIMEM[\"Greenwich\",0],UNIT[\"degree\",0.0174532925199433]],PROJECTION[\"Lambert_Conformal_Conic_2SP\"],PARAMETER[\"standard_parallel_1\",48.73333333333333],PARAMETER[\"standard_parallel_2\",47.5],PARAMETER[\"latitude_of_origin\",47],PARAMETER[\"central_meridian\",-120.8333333333333],PARAMETER[\"false_easting\",500000],PARAMETER[\"false_northing\",0],UNIT[\"metre\",1]]";

        fusion::lidar::LASWriter writer;
        CHECK(writer.Open(path, header), failures);
        writer.WritePoint(MakePoint(500005.0, 4000005.0, 42.0, 999.0, 2, 0, 0, 0));
        writer.Close();

        fusion::lidar::LASReader reader;
        CHECK(reader.Open(path), failures);
        if (reader.IsOpen()) {
            CHECK(reader.GetHeader().projectionWKT == header.projectionWKT, failures);
            reader.Close();
        }
        std::filesystem::remove(path);

        // Raster, VRT, and merged GeoTIFF CRS propagation:
        // GeoTIFF created with projectionWKT must preserve the CRS, and
        // GDALRaster::BuildVRT and MergeVRTToGeoTIFF must carry it through.
        auto rasterPath = ScratchPath("crs_tile.tif");
        auto vrtPath = ScratchPath("crs_mosaic.vrt");
        auto mergedPath = ScratchPath("crs_merged.tif");

        double geotransform[6] = {500000.0, 10.0, 0.0, 4000100.0, 0.0, -10.0};
        std::vector<float> dummyData(10 * 10, 42.0f);

        fusion::raster::GDALRaster outRaster;
        CHECK(outRaster.Create(rasterPath, 10, 10, 1, "Float32", "GTiff", header.projectionWKT, geotransform, -9999.0), failures);
        outRaster.WriteBandData(1, dummyData);
        outRaster.Close();

        fusion::raster::GDALRaster inRaster;
        CHECK(inRaster.Open(rasterPath), failures);
        CHECK(!inRaster.GetInfo().projectionWKT.empty(), failures);
        inRaster.Close();

        CHECK(fusion::raster::GDALRaster::BuildVRT(vrtPath, {rasterPath}), failures);
        fusion::raster::GDALRaster inVrt;
        CHECK(inVrt.Open(vrtPath), failures);
        CHECK(!inVrt.GetInfo().projectionWKT.empty(), failures);
        inVrt.Close();

        CHECK(fusion::raster::GDALRaster::MergeVRTToGeoTIFF(vrtPath, mergedPath), failures);
        fusion::raster::GDALRaster inMerged;
        CHECK(inMerged.Open(mergedPath), failures);
        CHECK(!inMerged.GetInfo().projectionWKT.empty(), failures);
        inMerged.Close();

        std::filesystem::remove(rasterPath);
        std::filesystem::remove(vrtPath);
        std::filesystem::remove(mergedPath);
    }

    // Global encoding propagation: a WKT VLR is only honored by readers such
    // as lidR/rlas when header bit 4 is set, and bit 0 says how to interpret
    // GPS time. The input's bits must reach the output file -- checked on the
    // raw header bytes, since LASReader itself does not need bit 4 to find
    // the WKT VLR.
    {
        auto path = ScratchPath("global_encoding.las");
        auto header = MakeHeader(1, 2);
        header.projectionWKT = "PROJCS[\"test\"]";
        header.globalEncoding = 0x0011; // adjusted standard GPS time + WKT

        fusion::lidar::LASWriter writer;
        CHECK(writer.Open(path, header), failures);
        writer.WritePoint(MakePoint(500005.0, 4000005.0, 42.0, 999.0, 2, 0, 0, 0));
        writer.Close();

        CHECK(ReadGlobalEncoding(path) == 0x0011, failures);

        fusion::lidar::LASReader reader;
        CHECK(reader.Open(path), failures);
        if (reader.IsOpen()) {
            CHECK(reader.GetHeader().globalEncoding == 0x0011, failures);
            reader.Close();
        }
        std::filesystem::remove(path);

        // LAS 1.4 output with WKT must set bit 4 even when the input
        // header did not (the spec requires it for WKT in LAS 1.4).
        auto path14 = ScratchPath("global_encoding_14.las");
        auto header14 = MakeHeader(6, 4);
        header14.projectionWKT = "PROJCS[\"test\"]";
        header14.globalEncoding = 0x0001;

        fusion::lidar::LASWriter writer14;
        CHECK(writer14.Open(path14, header14), failures);
        writer14.WritePoint(MakePoint(500005.0, 4000005.0, 42.0, 999.0, 2, 0, 0, 0));
        writer14.Close();

        CHECK(ReadGlobalEncoding(path14) == 0x0011, failures);
        std::filesystem::remove(path14);
    }

    // GeoTIFF-key coordinate system VLRs (the usual storage in LAS 1.0-1.3
    // files) must round-trip byte for byte rather than being dropped.
    {
        auto path = ScratchPath("geokeys.laz");
        auto header = MakeHeader(1, 2);
        fusion::lidar::LASHeaderInfo::RawVLR keyDirectory;
        keyDirectory.recordID = 34735;
        keyDirectory.description = "GeoKeyDirectoryTag";
        // key directory header (version 1.1.0, 1 key) + ProjectedCSTypeGeoKey = 32610
        keyDirectory.data = {1, 0, 1, 0, 0, 0, 1, 0, 0x00, 0x0C, 0, 0, 1, 0, 0x62, 0x7F};
        header.geoKeyVLRs.push_back(keyDirectory);

        fusion::lidar::LASWriter writer;
        CHECK(writer.Open(path, header), failures);
        writer.WritePoint(MakePoint(500005.0, 4000005.0, 42.0, 999.0, 2, 0, 0, 0));
        writer.Close();

        fusion::lidar::LASReader reader;
        CHECK(reader.Open(path), failures);
        if (reader.IsOpen()) {
            const auto& keys = reader.GetHeader().geoKeyVLRs;
            CHECK(keys.size() == 1, failures);
            if (keys.size() == 1) {
                CHECK(keys[0].recordID == 34735, failures);
                CHECK(keys[0].data == keyDirectory.data, failures);
            }
            reader.Close();
        }
        std::filesystem::remove(path);
    }


    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}
