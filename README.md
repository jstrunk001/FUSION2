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
   - Includes modernized executables for grid metrics, plot clip metrics, topographic derivatives, point filtering, decimation, density mapping, and tile-based batch processing.
5. **Batch Processing Toolkit (`ltktools`)**:
   - Configurable tiling engine with spatial buffer management.
   - Parallel multi-process task execution across CPU cores.
   - Real-time status monitor tracking job progress and handling resume/restarts.
   - Automatic GDAL Virtual Raster (`.vrt`) aggregation across tiles.

## Project Structure & Executable Suite

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

### Step-by-Step Compilation Guide

#### Step 1: Download or Clone the Repository
- **Option A (Download ZIP)**: 
  1. Click **Code** > **Download ZIP** on GitHub.
  2. Extract the downloaded `.zip` file to your target directory.
- **Option B (Git Clone)**:
  ```bash
  git clone https://github.com/username/fusion_update.git
  ```

#### Step 2: Open Command Line and Navigate to the Directory
Open your Command Prompt / PowerShell (Windows) or Terminal (Linux/macOS) and navigate into the extracted project directory:
```bash
cd fusion_update
```

#### Step 3: Run the 3 Compilation Commands
From inside the `fusion_update` directory, run the following 3 standard CMake commands:

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

---

### Output Binaries
Once compilation finishes, all 13 tool binaries (`gridmetrics`, `clipdata`, `groundfilter`, `canopymodel`, `ltktools`, `canopymaxima`, `treeseg`, `cloudmetrics`, `topometrics`, `filterdata`, `thindata`, `returndensity`, `catalog`) will be available in the `build/` (or `build/Release/`) directory.

