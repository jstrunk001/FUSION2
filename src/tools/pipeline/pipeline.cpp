// pipeline.cpp : Multi-tool batch pipeline orchestrator for FUSION2
//
// Tiles/buffers the input point cloud once, then -- per tile -- chains any
// of the registered tools (see fusion/batch/StageRegistry.h) by spawning
// each one's already-built .exe as a child process. Each stage's per-tile
// output lands in a _processing/<tile>/ subfolder, which doubles as both
// the interim product and the resumable state; a CSV manifest
// (_processing/pipeline_state.csv) tracks tile x stage status so re-runs
// skip finished work.
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/cli/ParseUtil.h"
#include "fusion/batch/BatchPipeline.h"
#include "fusion/batch/TilePointSource.h"
#include "fusion/batch/StatusMessenger.h"
#include "fusion/batch/ProcessRunner.h"
#include "fusion/batch/StageRegistry.h"
#include "fusion/batch/PipelineState.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/LASPointCloud.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <ctime>
#include <optional>
#include <filesystem>
#include <mutex>

using fusion::batch::ArtifactKind;
using fusion::batch::StageSpec;
using fusion::batch::StageStatus;
using fusion::batch::StageResult;

// Splits a comma-separated option value into a trimmed list, e.g. the
// /pipeline or /tiles argument.
static std::vector<std::string> SplitCommaList(const std::string& str) {
    std::vector<std::string> values;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) values.push_back(item);
    }
    return values;
}

static std::string NowTimestamp() {
    auto t = std::time(nullptr);
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    std::ostringstream ss;
    ss << std::put_time(&tmBuf, "%Y-%m-%dT%H:%M:%S");
    return ss.str();
}

// Every option/flag name pipeline.exe forwards verbatim into whichever
// stage's own main() understands it -- see StageRegistry's per-stage
// BuildArgs for which subset each tool actually reads.
static const std::vector<std::string> kForwardableOptions = {
    "cellsize", "minht", "heightcut", "outlier", "class", "strata", "intstrata",
    "ground", "window-a", "window-b", "smooth", "minz", "maxz", "return"
};
static const std::vector<std::string> kForwardableFlags = {
    "first", "nointensity", "slope", "gpu", "nogpu"
};

static std::unordered_map<std::string, std::string> BuildForwardedOptions(const fusion::cli::ArgumentParser& parser) {
    std::unordered_map<std::string, std::string> forwarded;
    for (const auto& name : kForwardableOptions) {
        if (auto val = parser.GetOption(name)) {
            forwarded[name] = *val;
        }
    }
    for (const auto& name : kForwardableFlags) {
        if (parser.HasFlag(name)) {
            forwarded[name] = "true";
        }
    }
    return forwarded;
}

// Buffered spatial clip of every overlapping LAS/LAZ file under inputPath
// (a directory of tiles, or a single point cloud file) into one per-tile
// point cloud (Stage 0). Every point-cloud-input stage in the pipeline
// reads from this file (or a later filterdata/thindata stage's output)
// rather than re-scanning the whole input itself. The clip takes the
// header of the first overlapping input file. Points come from
// ForEachTilePoint, which reads an input file shared by several tiles once
// for all of them instead of once per tile. Returns false when no input
// file overlaps the tile or its points cannot be read or written.
static bool MaterializeTileClip(const std::filesystem::path& inputPath,
                                 const fusion::batch::TileInfo& tile,
                                 const std::filesystem::path& outPath) {
    fusion::lidar::LASWriter writer;
    bool haveHeader = false;
    bool writerOk = true;

    bool pointsRead = fusion::batch::ForEachTilePoint(tile, inputPath,
        [&](const fusion::lidar::LASHeaderInfo& header) {
            if (haveHeader) return;
            haveHeader = true;
            writerOk = writer.Open(outPath, header);
        },
        [&](const fusion::lidar::PointRecord& pt) {
            if (writerOk) writer.WritePoint(pt);
        });
    writer.Close();

    if (!haveHeader) {
        return false; // No source file overlapped this tile.
    }
    return pointsRead && writerOk;
}

// Rewrites canopymaxima's tree-top CSV (TreeID,X,Y,Height,CrownDiameter) in
// place: drops rows whose (X,Y) falls in the tile's buffer zone, and
// prefixes TreeID with the tile name so IDs stay unique once every tile's
// table is concatenated.
static void FilterAndPrefixTreeTopTable(const std::filesystem::path& path, const fusion::batch::TileInfo& tile) {
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string header;
    std::getline(in, header);

    std::vector<std::string> keptLines;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        std::stringstream ss(line);
        std::string idStr, xStr, yStr, rest;
        std::getline(ss, idStr, ',');
        std::getline(ss, xStr, ',');
        std::getline(ss, yStr, ',');
        std::getline(ss, rest);

        double x = std::stod(xStr);
        double y = std::stod(yStr);
        if (x < tile.minX || x > tile.maxX || y < tile.minY || y > tile.maxY) continue;

        keptLines.push_back(tile.name + "_" + idStr + "," + xStr + "," + yStr + "," + rest);
    }
    in.close();

    std::ofstream out(path, std::ios::trunc);
    out << header << "\n";
    for (const auto& l : keptLines) out << l << "\n";
}

// Rewrites treeseg's crown-summary CSV (TreeID,MaxHeight,CrownArea_m2,
// PixelCount) in place: prefixes TreeID with the tile name. treeseg reports
// no X/Y per crown, so unlike canopymaxima's table this cannot be filtered
// to the tile's core extent -- rows for crowns centered in the buffer zone
// may appear in more than one tile's table. The crown-segment *raster* is
// still cropped to core extent, so the mosaicked raster has no overlap;
// only this table carries the limitation.
static void PrefixTreeIdColumn(const std::filesystem::path& path, const std::string& tileName) {
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::string header;
    std::getline(in, header);

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        size_t comma = line.find(',');
        if (comma == std::string::npos) continue;
        lines.push_back(tileName + "_" + line.substr(0, comma) + line.substr(comma));
    }
    in.close();

    std::ofstream out(path, std::ios::trunc);
    out << header << "\n";
    for (const auto& l : lines) out << l << "\n";
}

// treeseg's crown-summary table is written next to its (buffered) raster
// output with the same stem -- this derives that path the same way in both
// the per-tile run and finalization, so the two never disagree on the name.
static std::filesystem::path TreesegTablePath(const std::filesystem::path& subprocessOutputPath) {
    std::filesystem::path tablePath = subprocessOutputPath;
    tablePath.replace_extension(".csv");
    return tablePath;
}

static void ConcatenateCsvFiles(const std::vector<std::filesystem::path>& paths, const std::filesystem::path& outPath) {
    std::ofstream out(outPath, std::ios::trunc);
    bool wroteHeader = false;
    for (const auto& path : paths) {
        std::ifstream in(path);
        if (!in.is_open()) continue;
        std::string line;
        bool first = true;
        while (std::getline(in, line)) {
            if (first) {
                first = false;
                if (!wroteHeader) {
                    out << line << "\n";
                    wroteHeader = true;
                }
                continue;
            }
            if (!line.empty()) out << line << "\n";
        }
    }
}

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("pipeline", "Multi-tool batch pipeline: tiles/buffers the input once and chains any of the FUSION2 tools per tile");
    parser.AddOption("pipeline", "Comma-separated ordered list of stages to chain per tile, e.g. groundfilter,canopymodel,canopymaxima");
    parser.AddOption("tool", "Shorthand for a single-stage /pipeline:<name>");
    parser.AddOption("input", "Input directory of LAS/LAZ files, or a single LAS/LAZ file");
    parser.AddOption("output", "Output directory for finalized rasters/tables");
    parser.AddOption("extent", "Project extent LLX,LLY,URX,URY");
    parser.AddOption("tilesize", "Tile width,height in project units", "1000,1000");
    parser.AddOption("buffer", "Tile buffer distance", "50");
    parser.AddOption("threads", "Number of parallel worker threads (parallel child processes)", "4");
    parser.AddOption("processingdir", "Interim-product/state subfolder (default: <output>/_processing)");
    parser.AddFlag("rebuild", "Ignore recorded state and redo every tile/stage");
    parser.AddOption("tiles", "Comma-separated tile names to process (default: all)");
    parser.AddFlag("retryfailed", "Process only tiles with a recorded failed stage");
    parser.AddOption("toolsdir", "Directory containing the sibling tool .exe files (default: this executable's own directory)");
    parser.AddFlag("cleanup", "Delete the processing subfolder after a fully successful run");
    parser.AddFlag("merge", "Also merge each raster stage's VRT into a single global GeoTIFF");

    // Options forwarded verbatim into whichever stage understands them.
    for (const auto& name : kForwardableOptions) parser.AddOption(name, "Forwarded to whichever stage(s) accept it");
    for (const auto& name : kForwardableFlags) parser.AddFlag(name, "Forwarded to whichever stage(s) accept it");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    auto optInput = parser.GetOption("input");
    auto optOutput = parser.GetOption("output");
    if (!optInput || !optOutput) {
        std::cerr << "Error: /input and /output directory parameters are required.\n";
        parser.PrintHelp();
        return 1;
    }
    std::filesystem::path inputPath = *optInput;
    std::filesystem::path outputDir = *optOutput;

    // 1. Resolve and validate the requested stage chain.
    std::vector<std::string> stageNames;
    if (auto p = parser.GetOption("pipeline")) {
        stageNames = SplitCommaList(*p);
    } else if (auto t = parser.GetOption("tool")) {
        stageNames = {*t};
    } else {
        std::cerr << "Error: /pipeline:<stage1,stage2,...> or /tool:<name> is required.\n";
        return 1;
    }

    std::string validationError;
    if (!fusion::batch::ValidateStageChain(stageNames, validationError)) {
        std::cerr << "Error: " << validationError << "\n";
        return 1;
    }

    // 2. Resolve run-level paths and options.
    std::filesystem::path processingDir = parser.GetOption("processingdir").value_or((outputDir / "_processing").string());
    std::filesystem::path toolsDir = parser.GetOption("toolsdir") ? std::filesystem::path(*parser.GetOption("toolsdir")) : fusion::batch::GetExecutableDir();
    bool resume = !parser.HasFlag("rebuild");
    bool cleanupAfter = parser.HasFlag("cleanup");
    bool alsoMerge = parser.HasFlag("merge");
    std::vector<std::string> tileFilter;
    if (auto t = parser.GetOption("tiles")) tileFilter = SplitCommaList(*t);

    std::filesystem::create_directories(outputDir);
    std::filesystem::create_directories(processingDir);

    auto forwardedOptions = BuildForwardedOptions(parser);

    fusion::batch::PipelineState state;
    state.Load(processingDir / "pipeline_state.csv");

    if (parser.HasFlag("retryfailed")) {
        tileFilter = state.TilesWithStatus(StageStatus::Failed);
        if (tileFilter.empty()) {
            std::cout << "[Pipeline] /retryfailed: no failed tiles recorded in " << (processingDir / "pipeline_state.csv") << ". Nothing to do.\n";
            return 0;
        }
    }

    // 3. Build the tile grid and hand tile dispatch to BatchPipeline, exactly
    //    as gridmetrics's own batch mode does -- just with generateVRT off,
    //    since finalization here is per-stage, not one mosaic.
    //  - without /extent, the extent of the input files is used, snapped to
    //    /cellsize (or to whole units when no /cellsize is forwarded)
    fusion::batch::TileGridSpec gridSpec;
    bool hasExtent = false;
    if (auto ext = parser.GetOption("extent")) {
        std::stringstream ss(*ext);
        char ch;
        ss >> gridSpec.minX >> ch >> gridSpec.minY >> ch >> gridSpec.maxX >> ch >> gridSpec.maxY;
        hasExtent = true;
    }
    double extentSnap = std::stod(parser.GetOption("cellsize").value_or("1.0"));
    std::string extentMessage;
    if (!fusion::batch::ResolveProjectExtent(inputPath, hasExtent, extentSnap, gridSpec, extentMessage)) {
        std::cerr << "Error: " << extentMessage << "\n";
        return 1;
    }
    if (!extentMessage.empty()) std::cout << "[Pipeline] " << extentMessage << "\n";
    if (auto ts = parser.GetOption("tilesize")) {
        std::stringstream ss(*ts);
        char ch;
        ss >> gridSpec.tileSizeX >> ch >> gridSpec.tileSizeY;
    }
    if (auto buf = parser.GetOption("buffer")) {
        gridSpec.bufferDistance = std::stod(*buf);
    }

    fusion::batch::PipelineJobOptions jobOpts;
    jobOpts.inputPointCloudDir = inputPath;
    jobOpts.outputDir = outputDir;
    jobOpts.numThreads = std::stoi(parser.GetOption("threads").value_or("4"));
    jobOpts.generateVRT = false;
    jobOpts.splitDir = processingDir / "_tile_points";

    fusion::batch::StatusMessenger::Instance().SetLogFile(processingDir / "pipeline.log");
    fusion::batch::StatusMessenger::Instance().SendStatus("Starting pipeline: " + parser.GetOption("pipeline").value_or(parser.GetOption("tool").value_or("")));

    fusion::batch::BatchPipeline batch(gridSpec, jobOpts);

    // 4. Per-tile task: Stage 0 clip, then chain the requested stages.
    //  - a tile reads input points only when it is in the requested subset
    //    and its Stage 0 clip is not already done; BatchPipeline splits
    //    shared input files only for those tiles
    auto tileSelected = [&](const fusion::batch::TileInfo& tile) -> bool {
        return tileFilter.empty() || std::find(tileFilter.begin(), tileFilter.end(), tile.name) != tileFilter.end();
    };
    auto clipDone = [&](const fusion::batch::TileInfo& tile) -> bool {
        std::filesystem::path clipPath = processingDir / tile.name / "clip.laz";
        const StageResult* clipState = state.Find(tile.name, "clip");
        return resume && clipState && clipState->status == StageStatus::Done && std::filesystem::exists(clipPath);
    };
    auto needsPoints = [&](const fusion::batch::TileInfo& tile) -> bool {
        return tileSelected(tile) && !clipDone(tile);
    };

    auto tileTask = [&](const fusion::batch::TileInfo& tile, const fusion::batch::PipelineJobOptions&) -> bool {
        if (!tileSelected(tile)) {
            return true; // Not in the requested subset -- skip entirely.
        }

        std::filesystem::path tileDir = processingDir / tile.name;
        std::filesystem::create_directories(tileDir);
        std::filesystem::path logPath = tileDir / "log.txt";

        // Stage 0: buffered clip, resumable exactly like every other stage.
        std::filesystem::path clipPath = tileDir / "clip.laz";
        if (!clipDone(tile)) {
            std::string startedAt = NowTimestamp();
            bool ok = MaterializeTileClip(inputPath, tile, clipPath);
            StageResult result;
            result.tile = tile.name;
            result.stage = "clip";
            result.status = ok ? StageStatus::Done : StageStatus::Failed;
            result.outputPath = clipPath.string();
            result.exitCode = ok ? 0 : 1;
            result.startedAt = startedAt;
            result.finishedAt = NowTimestamp();
            state.Record(result);
            if (!ok) {
                fusion::batch::StatusMessenger::Instance().SendStatus("[" + tile.name + "] no overlapping input data -- skipping tile.");
                return false;
            }
        }

        std::filesystem::path currentPointCloudPath = clipPath;
        std::optional<std::filesystem::path> lastRasterBufferedPath;
        std::optional<std::filesystem::path> groundDemBufferedPath;
        bool tileOk = true;

        for (const auto& stageName : stageNames) {
            const StageSpec* stage = fusion::batch::FindStage(stageName);

            std::filesystem::path primaryInput = (stage->inputKind == ArtifactKind::PointCloud)
                ? currentPointCloudPath
                : *lastRasterBufferedPath;

            std::filesystem::path canonicalOutputPath = tileDir / (stageName + stage->outputExt);
            std::filesystem::path subprocessOutputPath = (stage->outputKind == ArtifactKind::Raster)
                ? tileDir / (stageName + "_buffered" + stage->outputExt)
                : canonicalOutputPath;

            const StageResult* prior = state.Find(tile.name, stageName);
            bool alreadyDone = resume && prior && prior->status == StageStatus::Done && std::filesystem::exists(canonicalOutputPath);

            if (!alreadyDone) {
                std::string startedAt = NowTimestamp();
                auto args = stage->buildArgs(
                    primaryInput,
                    (stage->acceptsGround ? groundDemBufferedPath : std::nullopt),
                    subprocessOutputPath,
                    forwardedOptions);

                // If a stage requests GPU acceleration, throttle concurrent GPU stages
                // to avoid device VRAM contention across worker threads.
                static std::mutex s_gpuMutex;
                bool isGpuStage = (stageName == "canopymodel" || stageName == "gridmetrics") &&
                                  (forwardedOptions.find("gpu") != forwardedOptions.end() && forwardedOptions.at("gpu") == "true");

                std::unique_lock<std::mutex> gpuLock(s_gpuMutex, std::defer_lock);
                if (isGpuStage) {
                    gpuLock.lock();
                }

                auto procResult = fusion::batch::RunProcess(toolsDir / stage->exeName, args, logPath);
                bool ok = procResult.launched && procResult.exitCode == 0;

                if (ok && stage->outputKind == ArtifactKind::Raster) {
                    std::filesystem::path actualWritten = stage->resolveOutputPath(primaryInput, subprocessOutputPath);
                    ok = fusion::raster::GDALRaster::CropToExtent(actualWritten, canonicalOutputPath, tile.minX, tile.minY, tile.maxX, tile.maxY);
                    if (ok) lastRasterBufferedPath = actualWritten;
                } else if (ok && stage->outputKind == ArtifactKind::PointCloud) {
                    currentPointCloudPath = canonicalOutputPath;
                } else if (ok && stage->outputKind == ArtifactKind::Table) {
                    FilterAndPrefixTreeTopTable(canonicalOutputPath, tile);
                }

                if (ok && stageName == "treeseg") {
                    PrefixTreeIdColumn(TreesegTablePath(subprocessOutputPath), tile.name);
                }
                if (ok && stageName == "groundfilter") {
                    groundDemBufferedPath = lastRasterBufferedPath;
                }

                StageResult result;
                result.tile = tile.name;
                result.stage = stageName;
                result.status = ok ? StageStatus::Done : StageStatus::Failed;
                result.outputPath = canonicalOutputPath.string();
                result.exitCode = procResult.exitCode;
                result.startedAt = startedAt;
                result.finishedAt = NowTimestamp();
                state.Record(result);

                if (!ok) {
                    fusion::batch::StatusMessenger::Instance().SendStatus("[" + tile.name + "] stage \"" + stageName + "\" failed (exit " + std::to_string(procResult.exitCode) + ") -- see " + logPath.string());
                    tileOk = false;
                    break;
                }
            } else {
                // Resumed: still need to reconstruct the chaining state a fresh run would have set.
                if (stage->outputKind == ArtifactKind::Raster) {
                    lastRasterBufferedPath = tileDir / (stageName + "_buffered" + stage->outputExt);
                    if (stageName == "groundfilter") groundDemBufferedPath = lastRasterBufferedPath;
                } else if (stage->outputKind == ArtifactKind::PointCloud) {
                    currentPointCloudPath = canonicalOutputPath;
                }
            }
        }

        return tileOk;
    };

    batch.ExecutePipeline(tileTask, needsPoints);

    // 5. Finalize each stage: mosaic rasters, concatenate tables, leave
    //    point-cloud outputs where they are.
    for (const auto& stageName : stageNames) {
        const StageSpec* stage = fusion::batch::FindStage(stageName);
        auto results = state.ResultsForStage(stageName);

        std::vector<std::filesystem::path> donePaths;
        for (const auto& r : results) {
            if (r.status == StageStatus::Done) donePaths.push_back(r.outputPath);
        }
        if (donePaths.empty()) continue;

        if (stage->outputKind == ArtifactKind::Raster) {
            std::filesystem::path vrtPath = outputDir / (stageName + ".vrt");
            if (fusion::raster::GDALRaster::BuildVRT(vrtPath, donePaths)) {
                std::cout << "[Pipeline] " << stageName << ": mosaicked " << donePaths.size() << " tile(s) -> " << vrtPath << "\n";
                if (alsoMerge) {
                    std::filesystem::path mergedPath = outputDir / (stageName + "_merged.tif");
                    fusion::raster::GDALRaster::MergeVRTToGeoTIFF(vrtPath, mergedPath);
                    std::cout << "[Pipeline] " << stageName << ": merged -> " << mergedPath << "\n";
                }
            }
            if (stageName == "treeseg") {
                std::vector<std::filesystem::path> tablePaths;
                for (const auto& r : results) {
                    if (r.status != StageStatus::Done) continue;
                    std::filesystem::path t = TreesegTablePath(processingDir / r.tile / (stageName + "_buffered" + stage->outputExt));
                    if (std::filesystem::exists(t)) tablePaths.push_back(t);
                }
                if (!tablePaths.empty()) {
                    std::filesystem::path outCsv = outputDir / "treeseg_table_all.csv";
                    ConcatenateCsvFiles(tablePaths, outCsv);
                    std::cout << "[Pipeline] treeseg: concatenated " << tablePaths.size() << " crown-summary table(s) -> " << outCsv << "\n";
                }
            }
        } else if (stage->outputKind == ArtifactKind::Table) {
            std::filesystem::path outCsv = outputDir / (stageName + "_all.csv");
            ConcatenateCsvFiles(donePaths, outCsv);
            std::cout << "[Pipeline] " << stageName << ": concatenated " << donePaths.size() << " table(s) -> " << outCsv << "\n";
        } else {
            std::cout << "[Pipeline] " << stageName << ": " << donePaths.size() << " per-tile point cloud file(s) left in " << processingDir << "\n";
        }
    }

    bool anyFailed = !state.TilesWithStatus(StageStatus::Failed).empty();
    if (cleanupAfter && !anyFailed) {
        std::error_code ec;
        std::filesystem::remove_all(processingDir, ec);
        std::cout << "[Pipeline] /cleanup: removed " << processingDir << "\n";
    } else if (cleanupAfter && anyFailed) {
        std::cout << "[Pipeline] /cleanup requested but at least one tile/stage failed -- keeping " << processingDir << " for /retryfailed.\n";
    }

    fusion::batch::StatusMessenger::Instance().SendStatus(anyFailed ? "Pipeline completed with failures." : "Pipeline completed successfully.");
    return anyFailed ? 1 : 0;
}
