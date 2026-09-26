// groundfilter.cpp : Modernized GroundFilter Executable for FUSION2
//
#include "fusion/cli/ArgumentParser.h"
#include "fusion/raster/GDALRaster.h"
#include "fusion/lidar/InputResolver.h"
#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/PointFilter.h"
#include "fusion/lidar/GroundFilter.h"
#include "fusion/table/TableWriter.h"

#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>
#include <limits>
#include <algorithm>

int main(int argc, char* argv[]) {
    fusion::cli::ArgumentParser parser("groundfilter", "Classifies ground points with the Kraus & Pfeifer iterative filter and generates a GeoTIFF ground DEM");
    parser.SetPositionalArgsUsage("<input.las/laz or directory>");
    parser.AddOption("cellsize", "Output DEM cell size", "1.0");
    parser.AddOption("filtercell", "Cell size of the filter's intermediate surfaces (horizontal units); each surface cell averages its 3 x 3 neighbourhood", "10.0");
    parser.AddOption("gparam", "Kraus & Pfeifer g: residual at or below which a point gets full weight (vertical units)", "-2.0");
    parser.AddOption("wparam", "Kraus & Pfeifer w: width above g over which a point's weight falls to 0 (vertical units)", "2.5");
    parser.AddOption("aparam", "Kraus & Pfeifer a: steepness of the weight function", "1.0");
    parser.AddOption("bparam", "Kraus & Pfeifer b: exponent of the weight function", "4.0");
    parser.AddOption("iterations", "Number of surface/weight passes", "5");
    parser.AddOption("coarsecell", "Cell size of a first, coarse filter stage; points more than /coarsecut above its surface are dropped before the fine passes (default 3 x /filtercell; 0 disables)");
    parser.AddOption("coarsecut", "Height above the coarse surface beyond which a point cannot be ground (vertical units; default 4 x /wparam)");
    parser.AddOption("tolerance", "Classify as ground every point within this distance of the final surface (default: every point with residual <= g + w)");
    parser.AddOption("output-raster", "Output GeoTIFF ground DEM file path");
    parser.AddOption("output-points", "Output LAS/LAZ of the points classified as ground (written with classification 2)");
    fusion::lidar::PointFilter::RegisterOptions(parser);
    parser.AddOption("output-table", "Also write a one-band multicolumn table alongside the ground DEM raster (path ending in .csv or .sqlite) -- omit to skip table output entirely");
    parser.AddFlag("noraster", "Skip writing the ground DEM GeoTIFF -- only valid together with /output-table, since a run must produce at least one output");

    if (!parser.Parse(argc, argv)) {
        return 0;
    }

    const auto& posArgs = parser.GetPositionalArgs();
    if (posArgs.empty()) {
        std::cerr << "Error: Input LAS/LAZ point cloud file or directory is required.\n";
        parser.PrintHelp();
        return 1;
    }

    bool noRaster = parser.HasFlag("noraster");
    bool wantTable = parser.WasExplicit("output-table");
    if (noRaster && !wantTable) {
        std::cerr << "Error: /noraster requires /output-table:<path> -- a run must produce at least one output.\n";
        return 1;
    }

    auto inputFiles = fusion::lidar::ResolveInputFiles(posArgs);
    if (inputFiles.empty()) {
        std::cerr << "Error: No valid .las or .laz files found from input arguments.\n";
        return 1;
    }

    double cellSize = std::stod(parser.GetOption("cellsize").value_or("1.0"));

    fusion::lidar::MergedPointCloudReader reader;
    if (!reader.Open(inputFiles)) {
        std::cerr << "Error: Failed to open input point cloud(s).\n";
        return 1;
    }

    //1. read the filter parameters (legacy FUSION GroundFilter switch names)
    fusion::lidar::KrausPfeiferParams kpParams;
    kpParams.cellSize = std::stod(parser.GetOption("filtercell").value_or("10.0"));
    kpParams.g = std::stod(parser.GetOption("gparam").value_or("-2.0"));
    kpParams.w = std::stod(parser.GetOption("wparam").value_or("2.5"));
    kpParams.a = std::stod(parser.GetOption("aparam").value_or("1.0"));
    kpParams.b = std::stod(parser.GetOption("bparam").value_or("4.0"));
    kpParams.iterations = std::stoi(parser.GetOption("iterations").value_or("5"));
    if (auto tol = parser.GetOption("tolerance")) kpParams.tolerance = std::stod(*tol);
    kpParams.coarseCellSize = 3.0 * kpParams.cellSize;
    if (auto coarse = parser.GetOption("coarsecell")) kpParams.coarseCellSize = std::stod(*coarse);
    if (auto cut = parser.GetOption("coarsecut")) kpParams.coarseCut = std::stod(*cut);
    if (kpParams.cellSize <= 0.0 || kpParams.w <= 0.0 || kpParams.iterations < 1) {
        std::cerr << "Error: /filtercell and /wparam must be positive and /iterations at least 1.\n";
        return 1;
    }

    const auto header = reader.GetHeader();
    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    //2. load the coordinates of every point that passes /class and /return
    //  - noise classes 7/18 and withheld points are already excluded by
    //    PointFilter's defaults, so low noise cannot drag the surface down
    fusion::lidar::PointFilter pointFilter = fusion::lidar::PointFilter::FromParser(parser);
    std::vector<double> xs, ys, zs;
    xs.reserve(header.pointCount);
    ys.reserve(header.pointCount);
    zs.reserve(header.pointCount);
    fusion::lidar::PointRecord pt;
    while (reader.ReadNextPoint(pt)) {
        if (!pointFilter.Keep(pt)) continue;
        xs.push_back(pt.x);
        ys.push_back(pt.y);
        zs.push_back(pt.z);
    }

    //3. classify ground with the Kraus & Pfeifer filter
    std::vector<uint8_t> isGround = fusion::lidar::ClassifyGroundKrausPfeifer(
        xs, ys, zs, header.minX, header.minY, header.maxX, header.maxY, kpParams);
    size_t nGround = 0;
    for (uint8_t flag : isGround) nGround += flag;
    std::cout << "[GroundFilter] Classified " << nGround << " of " << zs.size() << " points as ground.\n";
    if (nGround == 0) {
        std::cerr << "Error: No points were classified as ground -- check /gparam, /wparam, and /filtercell.\n";
        return 1;
    }

    //4. grid the ground points' mean elevation onto the DEM, then fill
    //   cells that hold no ground point from their neighbours
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> sumZ(static_cast<size_t>(cols) * rows, 0.0);
    std::vector<int> countZ(static_cast<size_t>(cols) * rows, 0);
    for (size_t i = 0; i < zs.size(); ++i) {
        if (!isGround[i]) continue;
        int col = static_cast<int>((xs[i] - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - ys[i]) / cellSize);
        col = std::clamp(col, 0, cols - 1);
        row = std::clamp(row, 0, rows - 1);
        size_t idx = static_cast<size_t>(row) * cols + col;
        sumZ[idx] += zs[i];
        countZ[idx]++;
    }
    std::vector<double> demGrid(sumZ.size(), nan);
    for (size_t i = 0; i < demGrid.size(); ++i) {
        if (countZ[i] > 0) demGrid[i] = sumZ[i] / countZ[i];
    }
    fusion::lidar::FillEmptyCells(demGrid, cols, rows);
    std::vector<float> minElevGrid(demGrid.size());
    for (size_t i = 0; i < demGrid.size(); ++i) {
        minElevGrid[i] = std::isnan(demGrid[i]) ? -9999.0f : static_cast<float>(demGrid[i]);
    }

    //5. /output-points: re-read the input and write the points classified
    //   as ground, with classification set to 2
    //  - a second pass over the input avoids holding every full point
    //    record in memory; the same PointFilter visits points in the same
    //    order, so the i-th kept point matches isGround[i]
    if (parser.WasExplicit("output-points")) {
        fusion::lidar::LASWriter pointWriter;
        // Copy the whole input header (not field by field) so the
        // coordinate system VLRs and global encoding bits come along too.
        fusion::lidar::LASHeaderInfo writeHeader = header;
        if (!pointWriter.Open(*parser.GetOption("output-points"), writeHeader)) {
            std::cerr << "Error: Failed to open ground point output file: " << *parser.GetOption("output-points") << "\n";
            return 1;
        }
        reader.Rewind();
        size_t keptIndex = 0;
        while (reader.ReadNextPoint(pt)) {
            if (!pointFilter.Keep(pt)) continue;
            if (keptIndex < isGround.size() && isGround[keptIndex]) {
                pt.classification = 2;
                pointWriter.WritePoint(pt);
            }
            keptIndex++;
        }
        pointWriter.Close();
        std::cout << "[GroundFilter] Successfully output ground points: " << *parser.GetOption("output-points") << "\n";
    }
    reader.Close();

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    if (!noRaster) {
        if (auto outDem = parser.GetOption("output-raster")) {
            fusion::raster::GDALRaster demRaster;
            if (demRaster.Create(*outDem, cols, rows, 1, "Float32", "GTiff", header.projectionWKT, geotransform, -9999.0)) {
                demRaster.SetBandDescription(1, "ground_elevation");
                demRaster.WriteBandData(1, minElevGrid);
                demRaster.Close();
                std::cout << "[GroundFilter] Successfully output ground DEM GeoTIFF: " << *outDem << "\n";
            }
        }
    }

    if (wantTable) {
        std::vector<fusion::table::BandDef> bandDefs = {{"ground_elevation", &minElevGrid}};
        std::filesystem::path tablePath = *parser.GetOption("output-table");
        if (fusion::table::WriteGridTable(tablePath, cols, rows, geotransform, bandDefs, -9999.0f)) {
            std::cout << "[GroundFilter] Successfully output table: " << tablePath.string() << "\n";
        }
    }

    return 0;
}
