# Executable Suite & CLI Tools Reference

This user manual documents the command-line interface, syntax, parameters, options, flags, and workflow patterns for all 13 executables in the FUSION suite.

---

## Command-Line Argument Syntax

All tools accept parameters using standard Windows slash syntax (`/option:value` or `/flag`), single-dash syntax (`-option value` or `-flag`), or double-dash syntax (`--option=value` or `--flag`). Help text can be displayed for any tool by passing `/?`, `-h`, or `--help`.

Every tool's required input file (or directory) is a **positional** argument — it is not passed with an `/option:` prefix, just placed on the command line. Everything else is an `/option` or `/flag`.

---

## Executable Manuals

### 1. `gridmetrics.exe`
Calculates statistical elevation, canopy cover, percentile, and experimental metrics across a 2D spatial grid, outputting single-band or multi-band GeoTIFF rasters.

```bash
gridmetrics <input.las/laz> [optional raster ground path] [other /options]
```

The ground surface DEM can be given either as the second positional argument or via `/ground:<path>` — `/ground` takes precedence if both are given.

#### Options & Flags
- `/ground:<path>`: Path to ground surface DEM raster (GeoTIFF, ENVI, IMG).
- `/cellsize:<val>`: Output grid cell size in project units (default: `10.0`).
- `/minht:<val>`: Minimum height above ground for canopy metrics calculation (default: `2.0`).
- `/heightcut:<val>`: Height cutoff threshold for canopy cover calculations (defaults to `/minht`).
- `/outlier:<min,max>`: Trim elevation outliers outside `min,max` values (e.g. `/outlier:-5,150`).
- `/class:<ids>`: Comma-separated point classifications to include (e.g. `/class:2,3,4,5`).
- `/first`: Use only first returns for metric calculations.
- `/all`: Use all returns for canopy cover and metric calculations.
- `/nointensity`: Skip computing intensity metrics.
- `/strata:<h1,h2,...>`: Comma-separated height strata thresholds (e.g. `/strata:0.5,2.0,5.0,10.0,20.0`).
- `/intstrata:<h1,h2,...>`: Comma-separated intensity strata height thresholds.
- `/voxelsize:<val>`: 3D voxel resolution for voxel volume metrics, in meters (default: `20.0`).
- `/exp`: Compute additional experimental metrics from RSForTools.
- `/outroot:<name>`: Base root name for output CSV summary metrics tables.
- `/outdir:<path>`: Output directory for rasters and CSV reports (default: `.`).
- `/output-mode:<mode>`: Output raster mode, `multiband` or `singleband` (default: `multiband`).

---

### 2. `cloudmetrics.exe`
Computes summary canopy, elevation, and intensity metrics for a single point cloud clip.

```bash
cloudmetrics <input.las/laz> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output CSV file path (default: `cloud_metrics.csv`).
- `/minht:<val>`: Minimum height cutoff for canopy metrics, in meters (default: `2.0`).
- `/cellsize:<val>`: Grid cell size for 2D area/volume metrics, in meters (default: `10.0`).
- `/voxelsize:<val>`: 3D voxel resolution for voxel volume metrics, in meters (default: `20.0`).
- `/exp`: Compute additional experimental metrics from RSForTools.

---

### 3. `canopymodel.exe`
Interpolates point cloud data to create a continuous Digital Canopy Height Model (CHM) GeoTIFF raster.

```bash
canopymodel <input.las/laz> [other /options]
```

`/output` is required — the tool errors out if it is not given.

#### Options & Flags
- `/output:<path>`: Output GeoTIFF CHM file path. **Required.**
- `/cellsize:<val>`: Output CHM cell size (default: `1.0`).
- `/ground:<path>`: Path to ground DEM raster for height normalization.
- `/slope`: Normalize heights perpendicular to the local terrain slope plane.
- `/smooth:<n>`: Spatial smoothing window size (e.g. `/smooth:3` for a 3x3 filter).

---

### 4. `canopymaxima.exe`
Detects individual tree tops on CHM rasters using Variable Window Local Maxima (VLM) filtering.

```bash
canopymaxima <input_chm.tif> [other /options]
```

The window size at height `H` is `window_a + window_b * H`.

#### Options & Flags
- `/output:<path>`: Output CSV file path for tree tops (default: `tree_tops.csv`).
- `/minht:<val>`: Minimum tree height threshold, in meters (default: `2.0`).
- `/window_a:<val>`: Window polynomial coefficient A, the constant term (default: `0.5`).
- `/window_b:<val>`: Window polynomial coefficient B, the linear term (default: `0.05`).

---

### 5. `treeseg.exe`
Performs watershed region-growing individual tree crown segmentation on a CHM raster.

```bash
treeseg <input_chm.tif> [other /options]
```

#### Options & Flags
- `/output_grid:<path>`: Output GeoTIFF raster path for tree segments (default: `crown_segments.tif`).
- `/output_csv:<path>`: Output CSV summary table path (default: `crown_summary.csv`).
- `/minht:<val>`: Minimum height cutoff for segmentation, in meters (default: `2.0`).

---

### 6. `groundfilter.exe`
Filters ground points from a LAS/LAZ point cloud and generates a bare-earth GeoTIFF DEM.

```bash
groundfilter <input.las/laz> [other /options]
```

#### Options & Flags
- `/cellsize:<val>`: Output DEM cell size (default: `1.0`).
- `/output-dem:<path>`: Output GeoTIFF ground DEM file path.
- `/output-las:<path>`: Output filtered ground-only LAS/LAZ file path.

---

### 7. `clipdata.exe`
Subsets point cloud data by 2D spatial extent or height range, with optional height normalization.

```bash
clipdata <input.las/laz> [other /options]
```

`/output` is required — the tool errors out if it is not given.

#### Options & Flags
- `/output:<path>`: Output LAS/LAZ file path. **Required.**
- `/extent:<LLX,LLY,URX,URY>`: Bounding box to clip to.
- `/ground:<path>`: Path to ground surface raster (GeoTIFF, ENVI, IMG) for height normalization.
- `/zmin:<val>`: Minimum height above ground (or elevation, if no `/ground` given).
- `/zmax:<val>`: Maximum height above ground (or elevation, if no `/ground` given).

---

### 8. `filterdata.exe`
Filters a point cloud by elevation range or return number.

```bash
filterdata <input.las/laz> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output filtered LAS/LAZ file path (default: `filtered_output.laz`).
- `/minz:<val>`: Minimum Z elevation threshold.
- `/maxz:<val>`: Maximum Z elevation threshold.
- `/return:<n>`: Return number filter (e.g. `/return:1` for first returns only).

---

### 9. `thindata.exe`
Spatially thins a point cloud by keeping a single point per grid cell.

```bash
thindata <input.las/laz> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output thinned LAS/LAZ file path (default: `thinned_output.laz`).
- `/cellsize:<val>`: Grid cell size for 2D spatial thinning, in meters (default: `1.0`).

---

### 10. `returndensity.exe`
Calculates pulse density (pts/m²) and return-type ratio rasters, saved as a multi-band GeoTIFF.

```bash
returndensity <input.las/laz> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output multi-band GeoTIFF raster path (default: `density_metrics.tif`).
- `/cellsize:<val>`: Output grid cell size, in meters (default: `5.0`).

---

### 11. `topometrics.exe`
Computes topographic terrain slope and aspect from an input DEM GeoTIFF, saved as a multi-band GeoTIFF.

```bash
topometrics <input_dem.tif> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output multi-band GeoTIFF raster path (default: `topo_metrics.tif`).

---

### 12. `catalog.exe`
Scans a point cloud file or directory to produce a summary acquisition CSV report and an optional density coverage raster.

```bash
catalog <input.las/laz or directory> [other /options]
```

#### Options & Flags
- `/density:<cellsize>`: Generates a pulse density coverage raster at the given cell size.
- `/output:<path>`: Output CSV summary report file path.

---

### 13. `ltktools.exe`
Automates large-scale tile-based batch processing with spatial buffer management, parallel dispatching, and GDAL VRT aggregation. Takes no positional arguments — input and output directories are named options.

```bash
ltktools [other /options]
```

`/input` and `/output` are both required — the tool errors out if either is missing.

#### Options & Flags
- `/input:<dir>`: Input directory containing LAS/LAZ files. **Required.**
- `/output:<dir>`: Output directory for rasters and metrics. **Required.**
- `/extent:<LLX,LLY,URX,URY>`: Project extent (default: a `0,0,5000,5000` grid if omitted).
- `/tilesize:<w,h>`: Tile width,height in project units (default: `1000,1000`).
- `/buffer:<val>`: Tile buffer distance (default: `50`).
- `/resolution:<val>`: Raster resolution (default: `1.0`).
- `/output-mode:<mode>`: Output raster mode, `multiband` or `singleband` (default: `multiband`).
- `/threads:<N>`: Number of parallel worker threads (default: `4`).
- `/vrt`: Generate a GDAL Virtual Raster (`.vrt`) across tile rasters.
- `/merge`: Merge the VRT into a single global GeoTIFF file.

The following options are forwarded through to the per-tile `gridmetrics` pass:
- `/ground:<path>`: Path to ground surface DEM raster.
- `/minht:<val>`: Minimum height cutoff for canopy metrics (default: `2.0`).
- `/heightcut:<val>`: Height cutoff threshold for canopy cover.
- `/outlier:<min,max>`: Trim elevation outliers outside `min,max` values.
- `/class:<ids>`: Comma-separated point classifications to include.
- `/first`: Use only first returns for metric calculations.
- `/nointensity`: Skip computing intensity metrics.
- `/strata:<h1,h2,...>`: Comma-separated height strata thresholds.
- `/intstrata:<h1,h2,...>`: Comma-separated intensity strata height thresholds.
