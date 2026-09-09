# Functional Comparison: Legacy FUSION (v4.40) vs. Modernized `fusion_update`

This document inventories the functionality present in the original USDA Forest Service FUSION suite (Version 4.40 and LiDAR Toolkit / LTK) that is not currently implemented in the modernized `fusion_update` codebase.

Per guidance, format-conversion programs dedicated strictly to legacy PLANS `.dtm`, binary `.lda`, and ASCII raster formats (such as `ASCII2DTM`, `DTM2ASCII`, `DTM2ENVI`, `DTM2TIF`, `DTM2XYZ`, `XYZ2DTM`, `ClipDTM`, `MergeDTM`, `SplitDTM`, `RepairGridDTM`, `DTMDescribe`, `LDA2LAS`, `LDAtoASCII`, `LDAConvert`, `ASC2ASC`, `ASCII3D`, and `ASCImport`) are excluded from this comparison.

---

## 1. Summary of Current `fusion_update` Toolset

The `fusion_update` suite focuses on replacing the legacy `.dtm` raster format with native GDAL GeoTIFF support, streaming direct `.las` and `.laz` point cloud input and output, and multithreading batch execution. It provides 13 command-line tools:

1. `gridmetrics` (gridded canopy and elevation metric rasters, single-file and batch/tiled mode)
2. `cloudmetrics` (point cloud summary metrics)
3. `canopymodel` (canopy height model / CHM interpolation)
4. `canopymaxima` (variable-window local maxima tree top detection)
5. `treeseg` (watershed crown segmentation)
6. `groundfilter` (iterative bare-earth ground filtering and DEM generation)
7. `clipdata` (bounding-box spatial subsetting and elevation slicing)
8. `filterdata` (elevation, return number, and classification filtering)
9. `thindata` (spatial point cloud decimation per grid cell)
10. `returndensity` (pulse density and return ratio mapping)
11. `topometrics` (topographic slope and aspect calculation from DEMs)
12. `catalog` (acquisition coverage reports and point density summary)
13. `pipeline` (multi-stage tile and buffer batch orchestrator)

---

## 2. Functionality in Original FUSION Missing from `fusion_update`

### A. Interactive Graphical Environments

1. **`FUSION.exe` (Interactive 2D Desktop GIS & Workstation)**:
   - **Multi-layer GIS Display**: Visual overlay of LiDAR point coverage, georeferenced aerial imagery (DOQs, ortho-imagery, TIFF/JPEG/MrSID/ECW), bare-earth DEMs, canopy height models, and vector GIS layers (ESRI shapefiles of stands, roads, plot centers, and streams).
   - **Interactive Sampling & Plot Extraction**: Interactive drawing of sample plots (circles, rectangles, polygons) to clip points and immediately send them to visualization or metric calculators.
   - **2D/3D Profile Tool**: Interactive profile line drawing to inspect cross-sections of vegetation structure and ground surfaces.
   - **Interactive Tree Measurement**: On-screen stem mapping and manual digitizing of individual tree heights, crown widths, and lean.
   - **GUI Control Center**: Dialog-driven interface to parameterize and execute command-line LTK tools without manual shell scripting.

2. **`LDV.exe` (3D LiDAR Data Viewer)**:
   - **Hardware-Accelerated 3D OpenGL Visualization**: High-throughput rendering of raw point clouds with real-time rotation, pan, zoom, and fly-through modes.
   - **Flexible Color Modes**: Dynamic coloring of points by elevation, return intensity, return number, classification code, RGB color attributes, GPS timestamp, or user attributes.
   - **Dynamic Slicing Planes**: Interactive clipping boxes and slicing planes (X, Y, and Z axes) to strip away upper canopy layers and inspect understory or ground returns.
   - **3D Tree Crown & Stem Rendering**: Displaying 3D geometric tree models (conifer and hardwood crown shapes, stem locations, DBH, and lean) rendered directly in the point cloud scene over the terrain surface.
   - **3D Measurement**: Direct point-to-point distance, tree height, crown base height, and canopy dimension measurement tools.
   - **Animation & Trajectory Playback**: Flight-line playback from trajectory files and camera animation sequence recording.

---

### B. Vector & GIS Spatial Subsetting

1. **`PolyClipData` (Shapefile Polygon Subsetting)**:
   - Clips point clouds using polygon geometries from ESRI shapefiles.
   - **Multi-file Clipping (`/multifile`)**: Automatically iterates over features in a shapefile (such as forest inventory plots, harvest units, or stand boundaries) and writes a distinct, properly attributed LAS/LAZ file for each polygon, named using a designated attribute column (via `/shape:field`).
   - Supports inverted clipping (`/outside`) to extract points outside polygon boundaries.
   - *Status in `fusion_update`*: `clipdata` only supports rectangular bounding boxes (`/extent:minx,miny,maxx,maxy`). Shapefile polygon clipping is entirely absent.

---

### C. Surface Interpolation, Triangulation (TIN), and Quality Control

1. **`SurfaceCreate` / `TINSurfaceCreate`**:
   - Creates bare-earth or canopy surface models using Delaunay Triangulation (TIN) directly from point data, followed by interpolation onto a grid.
   - Preserves exact point elevations at sample locations without the smoothing or cell-binning artifacts common to raster grid binning.
   - Supports return-filtering (`/return:`) and classification filtering (`/class:`).
2. **`GridSurfaceCreate`**:
   - Creates gridded elevation surface models directly from point data with configurable cell operations (minimum, maximum, median, mean) and explicit multi-cell void filling (`/filldist:`).
3. **`SurfaceFilter` / `Filter2`**:
   - Specialized surface filtering tools to despike, median filter, or smooth existing surface models to eliminate noise or vegetation remnants.
4. **`fillsurfacevoids`**:
   - Algorithmic void and hole filling for surface models containing gaps due to water bodies, cloud shadows, or sparse returns.

---

### D. Surface Analysis & Raster Map Algebra

1. **`GridSurfaceStats` & `SurfaceStats`**:
   - **3D Surface Area vs. Planimetric Area**: Calculates the true 3D surface area of complex terrain (accounting for slope and micro-topography) compared to nominal 2D planimetric area.
   - **Volume & Cut/Fill**: Calculates volume between a surface and a reference datum or bare-earth ground model.
   - **Roughness & Topographic Complexity**: Computes surface roughness and terrain texture metrics across user-specified sample factors.
2. **`GridSample` & `SurfaceSample`**:
   - Extracts surface model elevations at specific point coordinates provided in a CSV or text table (such as field inventory plot centers), with optional window neighborhood sampling.
3. **`ModelMath` & `SRSGridMath`**:
   - Surface model map algebra operations (addition, subtraction, absolute difference, scaling, multiplication).
   - Used to subtract an earlier surface model from a later one to compute canopy growth, vegetation loss from disturbances, or terrain cut-and-fill.
4. **`diffimage`**:
   - Raster differencing tool to compare two surface models and produce difference maps flagging changes exceeding a threshold.
5. **`SurfaceColor`**:
   - Produces georeferenced color images from surface models based on user-defined elevation/height color ramp rules.

---

### E. Point Cloud Processing & Derivative Products

1. **`IntensityImage` (Ortho-intensity Image Generation)**:
   - Generates georeferenced grayscale or color raster images based on point return intensities.
   - Features histogram percentile contrast stretching (`/minint`, `/maxint`), pixel jittering/anti-aliasing, and return-type selection (`/allreturns`, `/lowest`).
   - Serves as a surrogate aerial photograph in areas where optical imagery is unavailable or misaligned with LiDAR data.
2. **`imagecreate` & `TiledImageMap`**:
   - Generates tiled image pyramids and interactive HTML image map index pages linking master overviews to high-resolution tiles.
3. **`FirstLastReturn`**:
   - Extracts first and last returns into separate files.
   - Includes the `/lastnotfirst` switch, which isolates true penetrated returns (last returns from pulses that had multiple returns) from single-return pulses.
4. **`DensityMetrics`**:
   - Computes return density across vertical height slices (elevation bands above ground) on a spatial grid, evaluating vertical distribution and foliage layer density.
5. **`Cover`**:
   - Dedicated canopy closure tool calculating cover and penetration ratios across multiple height thresholds.
6. **`VegMask`**:
   - Generates binary vegetation masks based on point height variance, standard deviation, and cover thresholds within grid cells.
7. **`MergeData`**:
   - Standalone utility to merge multiple point cloud files into a single LAS/LAZ file.
8. **`Classify`**:
   - Rule-based point reclassification tool based on height above ground, intensity, or spatial criteria.

---

### F. Forestry-Specific Stem Modeling & Tree Inventory Utilities

1. **`TreePoints` & `treebase`**:
   - Converts tabular tree lists (with coordinates, heights, crown ratios, and species) into synthetic 3D point clouds representing stems and crown envelopes.
   - Identifies tree base ground elevations from bare-earth models.
2. **Stem Mapping with Lean & DBH**:
   - In legacy FUSION 4.40, tree data structures incorporated explicit support for DBH, lean angle from vertical, and lean azimuth, with visual representation in LDV.
3. **`TreeSeg` Individual Tree Point Extraction**:
   - Legacy `TreeSeg` supported clipping and exporting point clouds for each individual segmented tree crown basin into separate files. In `fusion_update`, `treeseg` produces the crown raster and tabular metrics, but point clipping for individual segments is not yet enabled.

---

### G. Trajectory & Full-Waveform Processing

1. **`FWFProcess`**:
   - Decomposes digitized full-waveform LiDAR data into discrete return points and Gaussian waveform components.
2. **`SBETPrep` & `ProcessRange`**:
   - Parses Smooth Best Estimate of Trajectory (SBET) airborne trajectory files to compute aircraft positions, scan ranges, and incidence angles for each pulse.

---

### H. Data Management & Workflow Utilities

1. **`JoinDB`**:
   - Relational table joining utility to combine columns from two data tables (e.g., merging LiDAR metric CSVs with field-measured plot inventory tables).
2. **`SRSPlotSummary`, `SRSAggregate`, `SRSClassify`, `SRSSummarize`, `SRSAdjustIntensity`**:
   - Tools developed for regional forest inventory analysis, automating plot metric aggregation, swath intensity normalization, and metric-based forest type classification.
3. **`Identify`**:
   - Queries metadata stored in output files to recover the specific FUSION program version, execution timestamp, and command-line parameters used to generate them.

---

## 3. Option and Feature Gaps Within Ported Tools

Beyond entirely omitted tools, several tools that were ported to `fusion_update` omit specific options or capabilities found in their legacy equivalents:

| Ported Tool | Missing Legacy Feature or Parameter | Practical Impact |
| :--- | :--- | :--- |
| **`clipdata`** | Missing `/shape:`, `/anglemin`/`/anglemax`, `/timemin`/`/timemax`, `/biaselev`. | Cannot clip by shapefile polygons; cannot filter by scan angle or GPS timestamp. |
| **`cloudmetrics`** | Missing direct plot-coordinate list processing (`/id`, radius). | Cannot compute metrics directly for arbitrary plot locations from a master point cloud without prior clipping. |
| **`groundfilter`** | `/output-points` (saving classified ground returns to LAS/LAZ) is declared but currently a no-op; Kraus & Pfeifer weight tuning (`/gparam`, `/wparam`, `/aparam`, `/bparam`) is omitted. | Cannot output classified LAS point clouds from ground filtering; tuning algorithm weights is restricted. |
| **`canopymodel`** | Omission of Delaunay TIN interpolation (`/tin`), peak preservation (`/peaks`), and texture metrics (`/texture`). | CHM creation relies solely on grid binning and raster smoothing rather than facet interpolation. |
| **`thindata`** | Omission of targeted point selection algorithms (lowest, highest, closest to center) and density-based thinning (`/density`). | Thins strictly to one point per grid cell rather than decimating to a uniform pulse density. |
| **`treeseg`** | Individual tree point cloud clipping is not yet implemented. | Crown segments are output as rasters and summary tables, but individual tree LAS point clips are not written. |
| **`returndensity`** | Grid alignment options (`/align`, `/gridxy`, `/extent`) are simplified. | Mosaicking return density rasters across adjacent custom tiles requires post-processing with GDAL. |

---

## 4. Architectural Summary

The modernized `fusion_update` toolset provides substantial performance gains (4.5x to 6.6x throughput improvement, streaming point cloud I/O, multi-threading, and native GeoTIFF raster support). However, the modernization focused on the core automated forest modeling pipeline:

$$\text{Point Cloud} \longrightarrow \text{Ground Filter} \longrightarrow \text{CHM} \longrightarrow \text{Tree Detection / GridMetrics}$$

The broader capabilities of original FUSION—specifically the **interactive 2D/3D visualization workstation** (`FUSION.exe` and `LDV.exe`), **vector polygon clipping** (`PolyClipData`), **Delaunay TIN surface generation** (`TINSurfaceCreate`), **surface geometry analysis** (`SurfaceStats`), **ortho-intensity raster imaging** (`IntensityImage`), and **first/last return pulse splitting** (`FirstLastReturn`)—remain exclusive to the legacy codebase.
