// ltktools.cpp : Modern C++ Batch Processing Utility for FUSION Update
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/batch/BatchPipeline.h"
#include "fusion/batch/StatusMessenger.h"
#include <iostream>
#include <sstream>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("ltktools", "FUSION Batch Processing & Tile Pipeline Utility");
    parser.AddOption("extent", "Project extent LLX,LLY,URX,URY");
    parser.AddOption("tilesize", "Tile width,height in project units", "1000,1000");
    parser.AddOption("buffer", "Tile buffer distance", "50");
    parser.AddOption("resolution", "Raster resolution", "1.0");
    parser.AddOption("input", "Input directory containing LAS/LAZ files");
    parser.AddOption("output", "Output directory for rasters and metrics");
    parser.AddOption("output-mode", "Output raster mode: multiband or singleband", "multiband");
    parser.AddOption("threads", "Number of parallel worker threads", "4");
    parser.AddFlag("vrt", "Generate GDAL Virtual Raster (.vrt) across tile rasters (default: true)");
    parser.AddFlag("merge", "Merge VRT into a single global GeoTIFF file");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    auto optExtent = parser.GetOption("extent");
    auto optInput = parser.GetOption("input");
    auto optOutput = parser.GetOption("output");

    if (!optInput || !optOutput) {
        std::cerr << "Error: /input and /output directory parameters are required.\n";
        parser.PrintHelp();
        return 1;
    }

    fusion::batch::TileGridSpec gridSpec;
    if (optExtent) {
        std::stringstream ss(*optExtent);
        char ch;
        ss >> gridSpec.minX >> ch >> gridSpec.minY >> ch >> gridSpec.maxX >> ch >> gridSpec.maxY;
    } else {
        gridSpec.minX = 0; gridSpec.minY = 0; gridSpec.maxX = 5000; gridSpec.maxY = 5000;
    }

    if (auto ts = parser.GetOption("tilesize")) {
        std::stringstream ss(*ts);
        char ch;
        ss >> gridSpec.tileSizeX >> ch >> gridSpec.tileSizeY;
    }

    if (auto buf = parser.GetOption("buffer")) {
        gridSpec.bufferDistance = std::stod(*buf);
    }

    if (auto res = parser.GetOption("resolution")) {
        gridSpec.resolution = std::stod(*res);
    }

    fusion::batch::PipelineJobOptions jobOpts;
    jobOpts.inputPointCloudDir = *optInput;
    jobOpts.outputDir = *optOutput;
    jobOpts.outputMode = parser.GetOption("output-mode").value_or("multiband");
    jobOpts.numThreads = std::stoi(parser.GetOption("threads").value_or("4"));
    jobOpts.generateVRT = true;
    jobOpts.mergeGeoTIFF = parser.HasFlag("merge");

    fusion::batch::StatusMessenger::Instance().SetLogFile(jobOpts.outputDir / "ltktools_batch.log");
    fusion::batch::StatusMessenger::Instance().SendStatus("Initializing ltktools batch pipeline...");

    fusion::batch::BatchPipeline pipeline(gridSpec, jobOpts);

    auto tileTask = [](const fusion::batch::TileInfo& tile, const fusion::batch::PipelineJobOptions& opts) -> bool {
        std::filesystem::path outTif = opts.outputDir / (tile.name + ".tif");

        fusion::raster::GDALRaster raster;
        int numBands = (opts.outputMode == "multiband") ? 5 : 1;
        int cols = static_cast<int>((tile.maxX - tile.minX) / 1.0);
        int rows = static_cast<int>((tile.maxY - tile.minY) / 1.0);

        double gt[6] = { tile.minX, 1.0, 0.0, tile.maxY, 0.0, -1.0 };
        if (!raster.Create(outTif, cols, rows, numBands, "Float32", "GTiff", "", gt, -9999.0)) {
            return false;
        }

        if (opts.outputMode == "multiband") {
            raster.SetBandDescription(1, "elev_mean");
            raster.SetBandDescription(2, "elev_stddev");
            raster.SetBandDescription(3, "elev_p95");
            raster.SetBandDescription(4, "canopy_cover");
            raster.SetBandDescription(5, "point_density");
        } else {
            raster.SetBandDescription(1, "elev_mean");
        }

        raster.Close();
        return true;
    };

    bool ok = pipeline.ExecutePipeline(tileTask);
    if (ok) {
        fusion::batch::StatusMessenger::Instance().SendStatus("ltktools batch processing completed successfully.");
    } else {
        fusion::batch::StatusMessenger::Instance().SendStatus("ltktools batch processing encountered errors.");
    }

    return ok ? 0 : 1;
}
