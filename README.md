# FUSION2 - Forest Monitoring Point Cloud Processing Tools (GDAL & Native LAS/LAZ Point Cloud Engine)

Command line tools for processing Lidar point clouds for forest analyses. GDAL-based raster handling & native LAS/LAZ/COPC point cloud suite of forest monitoring focused point cloud processing tools. These tools are derived from the stand-alone tools provided in the original FUSION package, prepared by Bob McGaughey ([USDA Forest Service / PNW Research Station](https://research.fs.usda.gov/pnw/products/dataandtools/fusion/ldv-lidar-processing-and-visualization-software-version-440)). This fork replaces the legacy binary `.dtm` raster format with native GDAL dataset (tiff only) reading and writing and native LAS/LAZ point cloud I/O, with accelerated COPC read support in `gridmetrics`'s batch mode.

See the companion [`FUSION2-examples`](https://github.com/jstrunk001/FUSION2-examples) repository for example workflows and small sample datasets that exercise these tools end to end.

## Key Features

1. **Native GDAL Raster Infrastructure & Dynamic DTM Mosaics**:
   - Replaces legacy binary `.dtm` raster format with native GDAL dataset reading and writing.
   - Reads any GDAL-supported ground surface DEM (GeoTIFF `.tif`, ERDAS Imagine `.img`, ENVI, AAIGrid, etc.).
   - Automatically mosaics directories of ground DTM raster tiles into in-memory virtual rasters (`.vrt`) on the fly across tools accepting ground DEM inputs (`cloudmetrics`, `canopymodel`, `gridmetrics`, `clipdata`, `pipeline`).
   - Outputs single-band or multi-band GeoTIFF rasters with GDAL band descriptions (e.g. `elev_mean`, `elev_p95`, `canopy_cover`, `point_density`).
   - Every raster-producing tool (`gridmetrics`, `densitymetrics`, `canopymodel`, `groundfilter`, `returndensity`, `topometrics`, `gridsurfacestats`) can also write its per-cell values as a CSV or SQLite table via `/output-table:<path.csv|.sqlite>`; `/noraster` skips the GeoTIFF entirely and must be paired with `/output-table`, since a run needs at least one output.
2. **Direct `.las` and `.laz` Point Cloud I/O with Directory Streaming**:
   - Built-in reading and writing of standard `.las` (versions 1.0 - 1.4) and compressed `.laz` point cloud formats using static `LASzip`.
   - All point cloud tools (`cloudmetrics`, `canopymodel`, `gridmetrics`, `clipdata`, `groundfilter`, `returndensity`, `thindata`, `filterdata`, `catalog`, `pipeline`) accept individual `.las`/`.laz` files OR directories of point clouds with seamless multi-file streaming.
   - COPC files are read-only (no COPC output format) -- since COPC is backward-compatible LAZ, any tool above can read one sequentially like a plain `.laz` file, but only `gridmetrics`'s batch/tiled mode uses the file's own COPC chunk index to seek directly to the chunks overlapping each tile instead of reading the whole file.
3. **Individual Tree Detection & Crown Segmentation**:
   - **`canopymaxima`**: Variable Window Local Maxima (VLM) individual tree top detector on CHM rasters.
   - **`treeseg`**: Watershed region-growing individual tree crown segmentation and per-tree point clipping.
4. **Comprehensive Lidar & Terrain Analytics Suite**:
   - `gridmetrics.exe` generates rasters of point clouds statistics like 90th percentile height and proportion of returns above 2 meters -- either for a single file, or tiled/buffered/mosaicked across a whole directory (see its batch/tiled mode below). Single-file and batch/tiled mode compute the identical full metric set (elevation stats, `/rgb`, `/strata`/`/intstrata`, `/rgbstrata`, `/surfstats`, `/exp`) and can both export it as a per-cell `/output-table` CSV or SQLite table alongside (or, with `/noraster`, instead of) the raster.
   - `cloudmetrics.exe` computes statistical elevation, percentile, canopy cover, canopy relief ratio, and intensity metrics for point cloud files or plot boundaries.
   - `canopymodel.exe` interpolates point clouds to create Canopy Height Models (CHM) saved as GeoTIFF rasters with optional DEM height normalization.
   - `canopymaxima.exe` detects individual tree tops on CHM rasters using Variable Window Local Maxima (VLM) filtering with height-dependent window sizes.
   - `treeseg.exe` performs watershed region-growing individual tree crown segmentation on CHM rasters and outputs crown segment rasters and tabular crown metrics.
   - `groundfilter.exe` filters ground returns from point cloud files and generates a bare-earth ground Digital Elevation Model (DEM) GeoTIFF raster.
   - `clipdata.exe` clips point cloud data by spatial bounding box extents, height thresholds above ground, or elevation ranges.
   - `filterdata.exe` filters point clouds by elevation ranges, return numbers (e.g. first returns), classification, scan angles, or specific point attributes.
   - `thindata.exe` thins and decimates point cloud data by spatially sub-sampling points within a user-defined grid cell size.
   - `returndensity.exe` calculates pulse density (pts/m²) and return type ratio rasters (e.g., proportion of ground or first returns) saved as GeoTIFFs.
   - `topometrics.exe` calculates topographic terrain derivatives (slope and aspect rasters) directly from input DEM GeoTIFFs.
   - `catalog.exe` scans point cloud directories to produce summary reports of point counts, acquisition extents, and spatial density rasters.
   - `pipeline.exe` chains any combination of the tools above per tile (e.g. ground filter -> canopy model -> tree tops) across a tiled, buffered, multithreaded batch run -- see below and [`docs/PIPELINE_GUIDE.md`](docs/PIPELINE_GUIDE.md).
5. **Multi-Tool Batch Pipeline (`pipeline.exe`)**:
   - Configurable tiling engine with spatial buffer management, shared with `gridmetrics.exe`'s own batch/tiled mode.
   - Chains multiple tools per tile as child processes (e.g. `groundfilter,canopymodel,canopymaxima`), auto-wiring a `groundfilter` stage's DEM into any later stage's `/ground` option.
   - Interim per-tile products (and the run's resumable state) live in an `_processing/` subfolder under the output directory.
   - A CSV state manifest tracks tile x stage status, so re-runs skip finished work and `/retryfailed`/`/tiles:` can target a subset of tiles.
   - Per-stage finalization: raster stages are mosaicked into a `.vrt` (optionally merged into one GeoTIFF), table stages are concatenated across tiles. `gridmetrics.exe`'s own internal batch/tiled mode does the same concatenation independently for its own per-tile `/output-table` output, without going through `pipeline.exe` at all.

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
│       ├── table/TableWriter.h
│       └── batch/
│           ├── BatchPipeline.h
│           └── StatusMessenger.h
├── src/
│   ├── libfusion_core/         # Core engine implementation
│   └── tools/                  # Complete CLI executable suite (13 tools)
│       ├── gridmetrics/        # Gridded canopy metrics calculator (single-file + batch/tiled modes)
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
│       ├── returndensity/      # Point pulse density & return ratio mapper
│       └── pipeline/           # Multi-tool batch pipeline orchestrator
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

### Build Profiles: Minimal (Default) vs Full GDAL

FUSION Update provides two build configurations:

1. **Minimal Standalone Profile (Default - Recommended for Distribution)**:
   - Built against a tailored, minimal static GDAL with only essential remote sensing formats: **GeoTIFF**, **Cloud Optimized GeoTIFF (COG)**, and **VRT (Virtual Raster)**.
   - Enables internal Deflate and LERC compression (ideal for floating-point Canopy Height Models and DEMs).
   - Links static PROJ + SQLite3 for complete EPSG and coordinate reference system support. (This is GDAL/PROJ's own internal SQLite3, used only for its EPSG database -- unrelated to the separate SQLite amalgamation vendored directly under `deps/sqlite3_amalgamation/`, which backs `/output-table`'s `.sqlite` output and isn't part of the GDAL build at all.)
   - Omits heavy, unneeded dependencies (OpenBLAS, Poppler, MySQL, PostgreSQL, NetCDF, HDF5).
   - Produces compact, self-contained executables (**~8–15 MB each**) with **zero external DLL dependencies**.
   - Builds automatically when running `.\build.ps1` (or via `.\build_gdal_minimal.ps1`).

2. **Full / Extended Multi-Format Profile**:
   - Links a complete system or Rtools static GDAL installation with all 150+ raster/vector formats enabled (HDF5, NetCDF, PostGIS, etc.).
   - Useful for specialized workflows requiring non-TIFF scientific raster formats.
   - Produces larger standalone binaries (~108 MB stripped).
   - Enabled by passing `.\build.ps1 -FullGDAL` or passing `-DGDAL_DIR=<path-to-full-gdal>` to CMake.

---

### Quick Build (`build.ps1`)

From inside the `fusion_update` directory, in PowerShell:

```powershell
.\build.ps1
```

This ensures the minimal static GDAL is ready (compiling it if not already present), configures CMake in Release mode, compiles all 13 tools, strips debug symbols, collects the resulting `.exe` files into `bin/`, and zips them into a versioned bundle at `dist/fusion_update_tools_v<version>-<timestamp>.zip`. If `dist/` already holds a bundle from a previous run, that older zip is moved into `archive/` first.

Useful options:
- `.\build.ps1` -- build minimal, lightweight standalone tools (~8–15 MB each).
- `.\build.ps1 -FullGDAL` -- build using full multi-format static GDAL (NetCDF, HDF, etc.).
- `.\build.ps1 -GDALDir "C:\path\to\cmake\gdal"` -- use a custom user-provided GDAL CMake directory.
- `.\build.ps1 -VcpkgRoot "C:\path\to\vcpkg"` -- pass a vcpkg toolchain path.
- `.\build.ps1 -Reconfigure` -- wipe and re-run CMake configure.
- `.\build.ps1 -Publish` -- after building, publish the bundle as a GitHub Release asset via `gh` and delete the previous `tools-v*` release.

### Manual Build (equivalent CMake commands)

If building manually with CMake:

1. **Build minimal GDAL (once):**
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\build_gdal_minimal.ps1
   ```

2. **Generate build configuration with CMake pointing to minimal GDAL:**
   ```bash
   cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DGDAL_DIR=deps/gdal_minimal/lib/cmake/gdal
   ```

3. **Compile all executables in Release mode:**
   ```bash
   cmake --build build --config Release --parallel
   ```

Once compilation finishes, all 13 tool binaries will be available in `build/` (or `bin/` if using `build.ps1`).

---

### Distributing built tools

With the minimal static profile and symbol stripping, each tool executable is compact (~8–15 MB each, ~120–150 MB total for all 13 tools combined), with zero external DLL dependencies.

Built bundles are published as **GitHub Release assets**:
- `build.ps1 -Publish` zips the current `bin/` contents, uploads the zip to a new GitHub Release tagged `tools-v<version>-<timestamp>`, and cleans up the previous release tag.
- To download pre-built binaries, visit the repository's [Releases page](https://github.com/jstrunk001/FUSION2/releases).

### Version bumps

The release `<version>` above comes from `VERSION` in `CMakeLists.txt`'s `project()` call -- a manual, single source of truth read by `build.ps1`. Before publishing a release with real functional changes:
1. Bump `VERSION` in `CMakeLists.txt` (semantic versioning: `MAJOR.MINOR.PATCH`).
2. Add a matching `## [<version>] - <date>` entry to [`CHANGELOG.md`](CHANGELOG.md) describing what changed.

`build.ps1 -Publish` checks this automatically before compiling: if the current version was already published, it warns and asks for confirmation before continuing; if `CHANGELOG.md` has no entry for the current version, it warns but does not block.

