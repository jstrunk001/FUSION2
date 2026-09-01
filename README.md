# FUSION Update (GDAL & Native LAS/LAZ Point Cloud Engine)

Modernized, high-performance C++ reimplementation of the **FUSION** point cloud processing suite and **LTK** batch toolkit (USDA Forest Service / PNW Research Station).

## Key Features

1. **Native GDAL Raster Infrastructure**:
   - Replaces legacy binary `.dtm` raster format with native GDAL dataset reading and writing.
   - Reads any GDAL-supported ground surface DEM (GeoTIFF `.tif`, ERDAS Imagine `.img`, ENVI, AAIGrid, etc.).
   - Outputs single-band or multi-band GeoTIFF rasters with GDAL band descriptions (e.g. `elev_mean`, `elev_p95`, `canopy_cover`, `point_density`).
2. **Direct `.las` and `.laz` Point Cloud I/O**:
   - Built-in reading and writing of standard `.las` (versions 1.0 - 1.4) and compressed `.laz` point cloud formats using `LASlib` / `LASzip`.
3. **Batch Processing Toolkit (`ltktools`)**:
   - Configurable tiling engine with spatial buffer management.
   - Parallel multi-process task execution across CPU cores.
   - Real-time status monitor (`LTKStatusMessenger`) tracking job progress and handling resume/restarts.
   - Automatic GDAL Virtual Raster (`.vrt`) aggregation across tiles (Strategy A), with optional single-pass GeoTIFF merging.
4. **Modern C++ Architecture (C++17 / C++20)**:
   - Clean RAII wrappers, `std::filesystem::path`, type safety, exception handling, modular core engine static library (`libfusion_core`), and CMake build support.

## Project Structure

```
fusion_update/
├── CMakeLists.txt              # CMake build configuration
├── vcpkg.json                  # C++ dependencies (GDAL, LASlib, LASzip)
├── include/
│   └── fusion/
│       ├── raster/GDALRaster.h
│       ├── lidar/LASPointCloud.h
│       ├── cli/ArgumentParser.h
│       └── batch/
│           ├── BatchPipeline.h
│           └── StatusMessenger.h
├── src/
│   ├── libfusion_core/         # Core engine implementation
│   └── tools/                  # Point cloud executables
│       ├── ltktools/           # Batch processor & status monitor
│       ├── gridmetrics/        # Gridded metrics calculator
│       ├── clipdata/           # Point cloud clipper
│       ├── groundfilter/       # Ground point filter & DEM creator
│       ├── canopymodel/        # Canopy height model interpolator
│       └── catalog/            # Point cloud summarizer & GIS coverage creator
```

## Building

Requires C++17 compiler (Visual Studio 2022 / GCC 10+ / Clang 12+), CMake 3.20+, and GDAL / LASlib.

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```
