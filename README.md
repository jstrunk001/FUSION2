# FUSION Update (GDAL & Native LAS/LAZ Point Cloud Engine)

GDAL-based raster handling & native LAS/LAZ point cloud support update to the **FUSION** suite of forest monitoring focused point cloud processing tools and **LTK** batch toolkit originally prepared by Bob McGaughey ([USDA Forest Service / PNW Research Station](https://research.fs.usda.gov/pnw/products/dataandtools/fusion/ldv-lidar-processing-and-visualization-software-version-440)).

## Key Features

1. **Native GDAL Raster Infrastructure**:
   - Replaces legacy binary `.dtm` raster format with native GDAL dataset reading and writing.
   - Reads any GDAL-supported ground surface DEM (GeoTIFF `.tif`, ERDAS Imagine `.img`, ENVI, AAIGrid, etc.).
   - Outputs single-band or multi-band GeoTIFF rasters with GDAL band descriptions (e.g. `elev_mean`, `elev_p95`, `canopy_cover`, `point_density`).
2. **Direct `.las` and `.laz` Point Cloud I/O**:
   - Built-in reading and writing of standard `.las` (versions 1.0 - 1.4) and compressed `.laz` point cloud formats using `LASlib` / `LASzip`.
3. **Individual Tree Detection & Crown Segmentation**:
   - **`canopymaxima`**: Variable Window Local Maxima (VLM) individual tree top detector on CHM rasters.
   - **`treeseg`**: Watershed region-growing individual tree crown segmentation and per-tree point clipping.
4. **Comprehensive Lidar & Terrain Analytics Suite**:
   - `gridmetrics.exe` generates rasters of point clouds statistics like 90th percentile height and proportion of returns above 2 meters.
   - `cloudmetrics.exe` computes statistical elevation, percentile, canopy cover, canopy relief ratio, and intensity metrics for point cloud files or plot boundaries.
   - `canopymodel.exe` interpolates point clouds to create Canopy Height Models (CHM) saved as GeoTIFF rasters with optional DEM height normalization.
   - `canopymaxima.exe` detects individual tree tops on CHM rasters using Variable Window Local Maxima (VLM) filtering with height-dependent window sizes.
   - `treeseg.exe` performs watershed region-growing individual tree crown segmentation on CHM rasters and outputs crown segment rasters and tabular crown metrics.
   - `groundfilter.exe` filters ground returns from point cloud files and generates a bare-earth ground Digital Elevation Model (DEM) GeoTIFF raster.
   - `clipdata.exe` clips point cloud data by spatial bounding box extents, height thresholds above ground, or elevation ranges.
   - `filterdata.exe` filters point clouds by elevation ranges, return numbers (e.g. first returns), scan angles, or specific point attributes.
   - `thindata.exe` thins and decimates point cloud data by spatially sub-sampling points within a user-defined grid cell size.
   - `returndensity.exe` calculates pulse density (pts/m²) and return type ratio rasters (e.g., proportion of ground or first returns) saved as GeoTIFFs.
   - `topometrics.exe` calculates topographic terrain derivatives (slope and aspect rasters) directly from input DEM GeoTIFFs.
   - `catalog.exe` scans point cloud directories to produce summary reports of point counts, acquisition extents, and spatial density rasters.
   - `ltktools.exe` automates tile-based batch processing with spatial buffer management, parallel execution across CPU cores, status monitoring, and GDAL Virtual Raster (`.vrt`) aggregation.
5. **Batch Processing Toolkit (`ltktools.exe`)**:
   - Configurable tiling engine with spatial buffer management.
   - Parallel multi-process task execution across CPU cores.
   - Real-time status monitor tracking job progress and handling resume/restarts.
   - Automatic GDAL Virtual Raster (`.vrt`) aggregation across tiles.

## Project Structure & Executable Suite

```
fusion_update/
├── CMakeLists.txt              # CMake build configuration
├── build.ps1                   # Configure, compile, bundle (and optionally publish) all tools
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
│   └── tools/                  # Complete CLI executable suite (13 tools)
│       ├── ltktools/           # Batch processor & status monitor
│       ├── gridmetrics/        # Gridded canopy metrics calculator
│       ├── clipdata/           # Point cloud subsetting & spatial clipper
│       ├── groundfilter/       # Ground point filter & DEM creator
│       ├── canopymodel/        # Canopy Height Model (CHM) interpolator
│       ├── catalog/            # Point cloud summarizer & GIS coverage creator
│       ├── canopymaxima/       # Variable Window Local Maxima (VLM) tree top detector
│       ├── treeseg/            # Watershed crown segmentation & tree clipper
│       ├── cloudmetrics/       # Plot & clip point cloud metrics calculator
│       ├── topometrics/        # Topographic terrain derivatives (slope, aspect)
│       ├── filterdata/         # Point cloud elevation & attribute filter
│       ├── thindata/           # Spatial point cloud decimation & thinning
│       └── returndensity/      # Point pulse density & return ratio mapper
└── tests/                      # Evaluation suite, Quarto reports & comparative benchmarks
    ├── R/                      # Quarto benchmark report (.qmd)
    └── docs/                   # Rendered HTML evaluation report
```

## Building from Source

### Prerequisites
- **C++17 Compiler**: Visual Studio 2022 (MSVC), GCC 10+, or Clang 12+
- **CMake**: Version 3.20 or higher
- **Dependencies**: GDAL and LASlib / LASzip (can be managed automatically via `vcpkg` or system package manager)

---

### Quick Build (`build.ps1`)

From inside the `fusion_update` directory, in PowerShell:

```powershell
.\build.ps1
```

This configures CMake, compiles all 13 tools in Release mode, collects the
resulting `.exe` files into `bin/`, and zips them into a versioned bundle at
`dist/fusion_update_tools_v<version>-<timestamp>.zip`. If `dist/` already
holds a bundle from a previous run, that older zip is moved into `archive/`
first, so it isn't lost. Neither `dist/` nor `archive/` is tracked by git --
see "Distributing built tools" below for where the bundle actually goes.

Useful options:
- `.\build.ps1 -VcpkgRoot "C:\path\to\vcpkg"` -- pass a vcpkg toolchain path
  (same as the `-DCMAKE_TOOLCHAIN_FILE` flag below), or set the `VCPKG_ROOT`
  environment variable once and omit the flag.
- `.\build.ps1 -Reconfigure` -- wipe and re-run CMake configure (needed after
  changing `CMakeLists.txt` dependencies, not needed for ordinary code
  changes).
- `.\build.ps1 -Publish` -- after building, publish the bundle as a GitHub
  Release asset via `gh` (requires the GitHub CLI, authenticated with repo
  access) and delete the previous `tools-v*` release. See "Distributing
  built tools" below.

### Manual Build (equivalent CMake commands)

If you'd rather run CMake directly instead of `build.ps1`:

1. **Create and enter the build directory:**
   ```bash
   mkdir build && cd build
   ```
   *(On Windows Command Prompt, run `mkdir build` followed by `cd build` if `&&` is not enabled).*

2. **Generate build configuration with CMake:**
   ```bash
   cmake ..
   ```
   *(Note: If building with `vcpkg` for dependencies, supply the toolchain path: `cmake .. -DCMAKE_TOOLCHAIN_FILE=[path-to-vcpkg]/scripts/buildsystems/vcpkg.cmake`)*

3. **Compile all executables in Release mode:**
   ```bash
   cmake --build . --config Release
   ```

Once compilation finishes, all 13 tool binaries (`gridmetrics.exe`, `clipdata.exe`, `groundfilter.exe`, `canopymodel.exe`, `ltktools.exe`, `canopymaxima.exe`, `treeseg.exe`, `cloudmetrics.exe`, `topometrics.exe`, `filterdata.exe`, `thindata.exe`, `returndensity.exe`, `catalog.exe`) will be available in the `build/` (or `build/Release/`) directory.

---

### Distributing built tools

The compiled executables are large (roughly 160 MB each, ~2 GB for all 13
together), well past GitHub's 100 MB per-file limit for ordinary commits.
Rather than committing the exes or a zip of them into the git tree, built
bundles are published as **GitHub Release assets**:

- `build.ps1 -Publish` zips the current `bin/` contents, uploads the zip to
  a new GitHub Release tagged `tools-v<version>-<timestamp>`, and deletes
  the previous `tools-v*` release (both the release and its git tag) so
  only the latest bundle is published at any time.
- Every bundle `build.ps1` produces locally is still kept, either as the
  current `dist/*.zip` or moved into `archive/*.zip` once superseded --
  only the *published* copy on GitHub is replaced, not your local history
  of them.
- To get a previously built bundle, download the asset from the
  repository's [Releases page](https://github.com/jstrunk001/FUSION2/releases)
  rather than looking for it in the source tree.

