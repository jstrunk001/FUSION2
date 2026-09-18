// Manual verification tool -- not wired into the CMake build or CTest.
// Lists a GeoTIFF's band descriptions (optionally filtered by a name
// substring) and, given a cell column/row, prints that cell's value on each
// matching band. Useful for spot-checking a gridmetrics/cloudmetrics raster
// output against its companion CSV during manual QA.
//
// This file is not built by any CMakeLists.txt target -- add one temporarily
// to build it:
//
//   add_executable(verify_raster_bands tests/cpp/verify_raster_bands.cpp)
//   target_link_libraries(verify_raster_bands PRIVATE fusion_core)
//
// then reconfigure and build:
//
//   cmake -S . -B build
//   cmake --build build --config Release --target verify_raster_bands
//   ./build/verify_raster_bands.exe <path.tif> [name_filter] [col row]
//
// Remove the two temporary CMakeLists.txt lines again once done -- this tool
// is meant to be compiled on demand for a manual check, not to become a
// permanent build target.

#include "fusion/raster/GDALRaster.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "usage: verify_raster_bands <path.tif> [name_filter] [col row]\n";
        return 1;
    }

    fusion::raster::GDALRaster raster;
    if (!raster.Open(argv[1])) {
        std::cerr << "Failed to open " << argv[1] << "\n";
        return 1;
    }

    std::string nameFilter = (argc >= 3) ? argv[2] : "";
    bool haveCell = (argc >= 5);
    int col = haveCell ? std::stoi(argv[3]) : -1;
    int row = haveCell ? std::stoi(argv[4]) : -1;

    int numBands = raster.GetNumBands();
    std::cout << "Bands: " << numBands << ", size: " << raster.GetWidth() << "x" << raster.GetHeight() << "\n";

    for (int band = 1; band <= numBands; ++band) {
        std::string name = raster.GetBandDescription(band);
        if (!nameFilter.empty() && name.find(nameFilter) == std::string::npos) {
            continue;
        }
        std::cout << band << ": " << name;
        if (haveCell) {
            auto value = raster.GetCellValue(col, row, band);
            std::cout << " -> value at (col=" << col << ",row=" << row << ") = "
                      << (value ? std::to_string(*value) : std::string("<none>"));
        }
        std::cout << "\n";
    }
    return 0;
}
