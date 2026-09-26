// clipdata.cpp : Modernized ClipData Executable for FUSION2
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/PointFilter.h"
#include "fusion/geom/SpatialMask.h"
#include "fusion/geom/PolygonFeatureSet.h"

#include <iostream>
#include <filesystem>
#include <sstream>
#include <vector>
#include <memory>
#include <algorithm>

// /multifile mode: streams the merged input once, routing each matched point
// to a lazily-opened LASWriter for whichever polygon feature contains it.
// Extracted into its own function so main()'s single-clip path (the common
// case, unchanged from before /shape existed) stays simple to read.
static int RunMultiFileClip(
        fusion::lidar::MergedPointCloudReader& reader,
        const fusion::geom::PolygonFeatureSet& featureSet,
        const std::filesystem::path& outDir,
        double minX, double minY, double maxX, double maxY,
        double zMin, double zMax,
        bool hasGround, fusion::raster::GDALRaster& groundRaster,
        const fusion::lidar::PointFilter& pointFilter) {
    std::filesystem::create_directories(outDir);

    const auto& header = reader.GetHeader();
    const size_t featureCount = featureSet.FeatureCount();

    // Windows' default CRT open-file-handle limit (~512) makes one
    // concurrently-open writer per polygon feature unsafe once a shapefile
    // has more than a few hundred features -- LASWriter::Open starts
    // silently failing partway through and every later polygon's points get
    // dropped. LAZ output cannot be safely appended after Close(), so
    // instead of an LRU cache that would need to reopen-and-append evicted
    // writers, features are processed in bounded-size batches: each batch
    // re-streams the whole input once, writing only to the small, bounded
    // set of writers open for that batch. With featureCount <=
    // kMaxOpenWriters (the common case) this is exactly one pass, identical
    // to before; only pathologically large polygon counts pay for extra
    // input passes, in exchange for never silently dropping a polygon.
    constexpr size_t kMaxOpenWriters = 256;

    uint64_t totalWritten = 0;
    uint64_t filesWritten = 0;

    for (size_t batchStart = 0; batchStart < featureCount; batchStart += kMaxOpenWriters) {
        size_t batchEnd = (std::min)(batchStart + kMaxOpenWriters, featureCount);

        std::vector<std::unique_ptr<fusion::lidar::LASWriter>> writers(batchEnd - batchStart);
        std::vector<uint64_t> writtenCounts(batchEnd - batchStart, 0);

        reader.Rewind();
        fusion::lidar::PointRecord pt;
        while (reader.ReadNextPoint(pt)) {
            if (!pointFilter.Keep(pt)) {
                continue;
            }
            if (pt.x < minX || pt.x > maxX || pt.y < minY || pt.y > maxY) {
                continue;
            }

            double elevation = pt.z;
            if (hasGround) {
                if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                    elevation -= *gz;
                }
            }
            if (elevation < zMin || elevation > zMax) {
                continue;
            }

            size_t featureIdx = featureSet.FindContaining(pt.x, pt.y);
            if (featureIdx == fusion::geom::PolygonFeatureSet::npos) {
                continue;
            }
            if (featureIdx < batchStart || featureIdx >= batchEnd) {
                continue;
            }

            size_t localIdx = featureIdx - batchStart;
            auto& writer = writers[localIdx];
            if (!writer) {
                writer = std::make_unique<fusion::lidar::LASWriter>();
                std::filesystem::path outPath = outDir / (featureSet.Label(featureIdx) + ".laz");
                if (!writer->Open(outPath, header)) {
                    std::cerr << "Error: Failed to create output point cloud: " << outPath << "\n";
                    writer.reset();
                    continue;
                }
            }

            writer->WritePoint(pt);
            writtenCounts[localIdx]++;
            totalWritten++;
        }

        for (size_t i = 0; i < writers.size(); ++i) {
            if (writers[i]) {
                writers[i]->Close();
                filesWritten++;
            }
        }
    }

    std::cout << "[ClipData] Wrote " << totalWritten << " point(s) across " << filesWritten
              << " polygon output file(s) (of " << featureCount << " feature(s)) to " << outDir << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("clipdata", "Clips LAS/LAZ point clouds by bounding box or spatial extents");
    parser.SetPositionalArgsUsage("<input.las/laz or directory>");
    parser.AddOption("extent", "Bounding box LLX,LLY,URX,URY");
    parser.AddOption("ground", "Path to ground surface raster (GeoTIFF, ENVI, IMG) for height normalization, or a directory of DTM tiles to mosaic on the fly");
    parser.AddOption("minz", "Minimum height above ground or elevation");
    parser.AddOption("maxz", "Maximum height above ground or elevation");
    parser.AddOption("output", "Output LAS/LAZ file path (single-clip mode), or output directory (/multifile mode)");
    parser.AddOption("shape", "Polygon shapefile to clip against, in addition to (not instead of) /extent");
    parser.AddFlag("multifile", "With /shape, write one output LAS/LAZ per polygon feature instead of one merged output");
    parser.AddOption("field", "Attribute field used to name each /multifile output (<field-value>.laz); falls back to a zero-padded feature index when omitted or missing on a feature");
    parser.AddFlag("outside", "Keep points outside every polygon instead of inside (single-clip mode only)");
    fusion::lidar::PointFilter::RegisterOptions(parser);

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    auto optOutput = parser.GetOption("output");
    if (!optOutput) {
        std::cerr << "Error: /output file parameter is required.\n";
        return 1;
    }
    std::filesystem::path outputPath = *optOutput;

    bool multiFile = parser.HasFlag("multifile");
    bool outsideFlag = parser.HasFlag("outside");
    std::string fieldName = parser.GetOption("field").value_or("");

    fusion::geom::SpatialMask mask;
    fusion::geom::PolygonFeatureSet featureSet;
    bool haveShape = false;
    if (auto shapePath = parser.GetOption("shape")) {
        if (multiFile) {
            if (!featureSet.LoadShapefile(*shapePath, fieldName)) {
                std::cerr << "Error: Failed to load polygon shapefile: " << *shapePath << "\n";
                return 1;
            }
        } else {
            if (!mask.LoadShapefile(*shapePath)) {
                std::cerr << "Error: Failed to load polygon shapefile: " << *shapePath << "\n";
                return 1;
            }
        }
        haveShape = true;
    }

    if (multiFile && !haveShape) {
        std::cerr << "Error: /multifile requires /shape.\n";
        return 1;
    }

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    double minX = -1e9, minY = -1e9, maxX = 1e9, maxY = 1e9;
    if (auto ext = parser.GetOption("extent")) {
        std::stringstream ss(*ext);
        char ch;
        ss >> minX >> ch >> minY >> ch >> maxX >> ch >> maxY;
    }

    double zMin = -1e9, zMax = 1e9;
    if (auto zm = parser.GetOption("minz")) zMin = std::stod(*zm);
    if (auto zm = parser.GetOption("maxz")) zMax = std::stod(*zm);

    fusion::raster::GDALRaster groundRaster;
    bool hasGround = false;
    if (auto groundPath = parser.GetOption("ground")) {
        hasGround = groundRaster.Open(*groundPath);
    }

    fusion::lidar::MergedPointCloudReader reader;
    if (!reader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open input point cloud(s).\n";
        return 1;
    }

    fusion::lidar::PointFilter pointFilter = fusion::lidar::PointFilter::FromParser(parser);

    if (multiFile) {
        std::cout << "[ClipData] Clipping " << inputFiles.size() << " point cloud file(s) against "
                  << featureSet.FeatureCount() << " polygon feature(s) -> " << outputPath << "...\n";
        int rc = RunMultiFileClip(reader, featureSet, outputPath, minX, minY, maxX, maxY, zMin, zMax, hasGround, groundRaster, pointFilter);
        reader.Close();
        return rc;
    }

    fusion::lidar::LASWriter writer;
    if (!writer.Open(outputPath, reader.GetHeader())) {
        std::cerr << "Error: Failed to create output point cloud: " << outputPath << "\n";
        return 1;
    }

    std::cout << "[ClipData] Clipping " << inputFiles.size() << " point cloud file(s) -> " << outputPath.filename().string() << "...\n";

    fusion::lidar::PointRecord pt;
    uint64_t clippedCount = 0;
    while (reader.ReadNextPoint(pt)) {
        if (!pointFilter.Keep(pt)) continue;
        if (pt.x >= minX && pt.x <= maxX && pt.y >= minY && pt.y <= maxY) {
            bool inShape = !haveShape || (mask.Contains(pt.x, pt.y) != outsideFlag);
            if (!inShape) {
                continue;
            }

            double elevation = pt.z;
            if (hasGround) {
                if (auto gz = groundRaster.GetElevation(pt.x, pt.y)) {
                    elevation -= *gz;
                }
            }

            if (elevation >= zMin && elevation <= zMax) {
                writer.WritePoint(pt);
                clippedCount++;
            }
        }
    }

    reader.Close();
    writer.Close();

    std::cout << "[ClipData] Successfully wrote " << clippedCount << " clipped points to " << outputPath << "\n";
    return 0;
}
