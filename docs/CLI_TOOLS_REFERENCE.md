# Executable Suite & CLI Tools Reference

This user manual documents the command-line interface, syntax, parameters, options, flags, and workflow patterns for all 13 executables in the FUSION suite.

`ltktools.exe` from earlier versions of this suite has been folded into `gridmetrics.exe` (see its batch/tiled mode below) and replaced as the multi-tool orchestrator by the new `pipeline.exe` (see [`PIPELINE_GUIDE.md`](PIPELINE_GUIDE.md) for worked pipeline examples). Several option names changed in this pass to stay consistent across tools — see the "Renamed in this release" callouts below.

---

## Command-Line Argument Syntax

All tools accept parameters using standard Windows slash syntax (`/option:value` or `/flag`), single-dash syntax (`-option value` or `-flag`), or double-dash syntax (`--option=value` or `--flag`). Help text can be displayed for any tool by passing `/?`, `-h`, or `--help`.

Every tool's required input file (or directory) is a **positional** argument — it is not passed with an `/option:` prefix, just placed on the command line. Everything else is an `/option` or `/flag`.

---

## Executable Manuals

### 1. `gridmetrics.exe`
Calculates statistical elevation, canopy cover, percentile, and experimental metrics across a 2D spatial grid, outputting single-band or multi-band GeoTIFF rasters. Has two modes, chosen by whether the positional input is a file or a directory.

#### Single-file mode
```bash
gridmetrics <input.las/laz> [optional raster ground path] [other /options]
```
Processes one point cloud file, writing its raster(s) and CSV summary directly into `/outdir`. The ground surface DEM can be given either as the second positional argument or via `/ground:<path>` — `/ground` takes precedence if both are given.

#### Batch/tiled mode
```bash
gridmetrics <input_directory> /outdir:<dir> [/tilesize:...] [/buffer:...] [/threads:...] [other /options]
```
Tiles and buffers every LAS/LAZ file in the input directory, computes the same grid metrics per tile in parallel (multithreaded, in-process — no child processes), and mosaics the tile rasters into a `.vrt` in `/outdir`. This is what `ltktools.exe` used to do as a separate executable; it's the same behavior, just reached by pointing `gridmetrics` at a directory instead of a file. Batch mode always writes one multiband raster per tile (`/output-mode` is ignored — singleband mode would write many files per tile, which doesn't fit the one-raster-per-tile mosaicking model) and skips the CSV summary/experimental-metrics options that only apply to single-file mode.

- `/extent:<LLX,LLY,URX,URY>`: Batch mode only. Project extent (default: a `0,0,5000,5000` grid if omitted).
- `/tilesize:<w,h>`: Batch mode only. Tile width,height in project units (default: `1000,1000`).
- `/buffer:<val>`: Batch mode only. Tile buffer distance (default: `50`).
- `/threads:<N>`: Batch mode only. Number of parallel worker threads (default: `4`).
- `/vrt`: Batch mode only. Generate a GDAL Virtual Raster (`.vrt`) across tile rasters (on by default).
- `/merge`: Batch mode only. Merge the VRT into a single global GeoTIFF file.

#### Options & Flags (both modes, except where noted)
- `/ground:<path>`: Path to ground surface DEM raster (GeoTIFF, ENVI, IMG).
- `/cellsize:<val>`: Output grid cell size in project units (default: `10.0`). Batch mode's former separate `/resolution` option has been dropped in favor of this — one cell-size option everywhere.
- `/minht:<val>`: Minimum height above ground for canopy metrics calculation (default: `2.0`).
- `/heightcut:<val>`: Height cutoff threshold for canopy cover calculations (defaults to `/minht`).
- `/outlier:<min,max>`: Trim elevation outliers outside `min,max` values (e.g. `/outlier:-5,150`).
- `/class:<ids>`: Comma-separated point classifications to include (e.g. `/class:2,3,4,5`).
- `/first`: Use only first returns for metric calculations.
- `/all`: Single-file mode only. Use all returns for canopy cover and metric calculations.
- `/nointensity`: Skip computing intensity metrics.
- `/strata:<h1,h2,...>`: Comma-separated height strata thresholds (e.g. `/strata:0.5,2.0,5.0,10.0,20.0`).
- `/intstrata:<h1,h2,...>`: Comma-separated intensity strata height thresholds.
- `/voxelsize:<val>`: Single-file mode only. 3D voxel resolution for voxel volume metrics, in meters (default: `20.0`).
- `/exp`: Single-file mode only. Compute additional experimental metrics from RSForTools.
- `/outroot:<name>`: Single-file mode only. Base root name for output CSV summary metrics tables.
- `/outdir:<path>`: Output directory for rasters and CSV reports (single-file mode) or for tile rasters and the mosaicked VRT (batch mode). Default: `.`.
- `/output-mode:<mode>`: Single-file mode only. Output raster mode, `multiband` or `singleband` (default: `multiband`).

---

### 2. `cloudmetrics.exe`
Computes summary canopy, elevation, and intensity metrics for a single point cloud file or an entire directory of point clouds. Supports height normalization via a ground DTM raster file or a directory of DTM tiles.

```bash
cloudmetrics <input.las/laz or directory> [optional ground DTM file or directory] [other /options]
```

#### Options & Flags
- `/output:<path>`: Output CSV file path (default: `cloud_metrics.csv`).
- `/ground:<path>`: Path to ground DEM raster for height normalization (GeoTIFF, ENVI, IMG), or a directory of DTM tiles to mosaic on the fly via an in-memory VRT. May also be supplied as an unflagged second positional argument.
- `/minht:<val>`: Minimum height cutoff for canopy metrics, in meters (default: `2.0`).
- `/cellsize:<val>`: Grid cell size for 2D area/volume metrics, in meters (default: `10.0`).
- `/voxelsize:<val>`: 3D voxel resolution for voxel volume metrics, in meters (default: `20.0`).
- `/exp`: Compute additional experimental metrics from RSForTools.

---

### 3. `canopymodel.exe`
Interpolates point cloud data to create a continuous Digital Canopy Height Model (CHM) GeoTIFF raster. Accepts a single `.las`/`.laz` file or an entire directory of point clouds.

```bash
canopymodel <input.las/laz or directory> [other /options]
```

`/output` is required — the tool errors out if it is not given.

#### Options & Flags
- `/output:<path>`: Output GeoTIFF CHM file path. **Required.**
- `/cellsize:<val>`: Output CHM cell size (default: `1.0`).
- `/ground:<path>`: Path to ground DEM raster file for height normalization, or a directory of DTM tiles to mosaic on the fly.
- `/slope`: Normalize heights perpendicular to the local terrain slope plane.
- `/smooth:<n>`: Spatial smoothing window size (e.g. `/smooth:3` for a 3x3 filter).

---

### 4. `canopymaxima.exe`
Detects individual tree tops on CHM rasters using Variable Window Local Maxima (VLM) filtering.

```bash
canopymaxima <input_chm.tif> [other /options]
```

The window size at height `H` is `window-a + window-b * H`.

#### Options & Flags
- `/output:<path>`: Output CSV file path for tree tops (default: `tree_tops.csv`).
- `/minht:<val>`: Minimum tree height threshold, in meters (default: `2.0`).
- `/window-a:<val>`: Window polynomial coefficient A, the constant term (default: `0.5`). Renamed from `/window_a`.
- `/window-b:<val>`: Window polynomial coefficient B, the linear term (default: `0.05`). Renamed from `/window_b`.

---

### 5. `treeseg.exe`
Performs watershed region-growing individual tree crown segmentation on a CHM raster.

```bash
treeseg <input_chm.tif> [other /options]
```

#### Options & Flags
- `/output-raster:<path>`: Output GeoTIFF raster path for tree segments (default: `crown_segments.tif`). Renamed from `/output_grid`.
- `/output-table:<path>`: Output CSV summary table path (default: `crown_summary.csv`). Renamed from `/output_csv`.
- `/minht:<val>`: Minimum height cutoff for segmentation, in meters (default: `2.0`).

---

### 6. `groundfilter.exe`
Filters ground points from a LAS/LAZ point cloud file or directory and generates a bare-earth GeoTIFF DEM.

```bash
groundfilter <input.las/laz or directory> [other /options]
```

#### Options & Flags
- `/cellsize:<val>`: Output DEM cell size (default: `1.0`).
- `/output-raster:<path>`: Output GeoTIFF ground DEM file path. Renamed from `/output-dem`.
- `/output-points:<path>`: Output filtered ground-only LAS/LAZ file path. Renamed from `/output-las`. Not yet implemented -- declared but currently a no-op regardless of the name used.

---

### 7. `clipdata.exe`
Subsets point cloud data from a file or directory by 2D spatial extent or height range, with optional height normalization.

```bash
clipdata <input.las/laz or directory> [other /options]
```

`/output` is required — the tool errors out if it is not given.

#### Options & Flags
- `/output:<path>`: Output LAS/LAZ file path. **Required.**
- `/extent:<LLX,LLY,URX,URY>`: Bounding box to clip to.
- `/ground:<path>`: Path to ground surface raster (GeoTIFF, ENVI, IMG) or directory of DTM tiles for height normalization.
- `/minz:<val>`: Minimum height above ground (or elevation, if no `/ground` given). Renamed from `/zmin`.
- `/maxz:<val>`: Maximum height above ground (or elevation, if no `/ground` given). Renamed from `/zmax`.

---

### 8. `filterdata.exe`
Filters a point cloud file or directory by elevation range, return number, or classification.

```bash
filterdata <input.las/laz or directory> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output filtered LAS/LAZ file path (default: `filtered_output.laz`).
- `/minz:<val>`: Minimum Z elevation threshold.
- `/maxz:<val>`: Maximum Z elevation threshold.
- `/return:<n>`: Return number filter (e.g. `/return:1` for first returns only).
- `/class:<ids>`: Comma-separated point classifications to keep (e.g. `/class:2,3,4,5`). New in this release, matching `gridmetrics`'s `/class`.

---

### 9. `thindata.exe`
Spatially thins a point cloud file or directory by keeping a single point per grid cell.

```bash
thindata <input.las/laz or directory> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output thinned LAS/LAZ file path (default: `thinned_output.laz`).
- `/cellsize:<val>`: Grid cell size for 2D spatial thinning, in meters (default: `1.0`).

---

### 10. `returndensity.exe`
Calculates pulse density (pts/m²) and return-type ratio rasters from a point cloud file or directory, saved as a multi-band GeoTIFF.

```bash
returndensity <input.las/laz or directory> [other /options]
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

### 13. `pipeline.exe`
Multi-tool batch pipeline orchestrator. Tiles and buffers the input point cloud once, then -- per tile -- chains any of 9 registered tools (`gridmetrics`, `canopymodel`, `groundfilter`, `returndensity`, `filterdata`, `thindata`, `canopymaxima`, `topometrics`, `treeseg`) by running each one's own `.exe` as a child process, in order. This replaces the old `ltktools.exe`, which only ever ran one hardcoded `gridmetrics`-style pass per tile -- that specific behavior now lives in `gridmetrics.exe`'s own batch/tiled mode (see above); `pipeline.exe` is for chaining *multiple* tools. See [`PIPELINE_GUIDE.md`](PIPELINE_GUIDE.md) for worked end-to-end examples, the valid-chaining rule, and the state/resume model.

```bash
pipeline /pipeline:<stage1,stage2,...> /input:<dir> /output:<dir> [other /options]
```

`/input` and `/output` are both required. Either `/pipeline:<stage1,stage2,...>` (a chain) or `/tool:<name>` (a single stage) is required.

#### Options & Flags
- `/pipeline:<stage1,stage2,...>`: Ordered list of stages to chain per tile, e.g. `/pipeline:groundfilter,canopymodel,canopymaxima`.
- `/tool:<name>`: Shorthand for a single-stage `/pipeline:<name>`.
- `/input:<dir>`: Input directory containing LAS/LAZ files. **Required.**
- `/output:<dir>`: Output directory for each stage's finalized (mosaicked/concatenated) result. **Required.**
- `/extent:<LLX,LLY,URX,URY>`: Project extent (default: a `0,0,5000,5000` grid if omitted).
- `/tilesize:<w,h>`: Tile width,height in project units (default: `1000,1000`).
- `/buffer:<val>`: Tile buffer distance (default: `50`).
- `/threads:<N>`: Number of parallel worker threads -- each one runs one tile's stage chain at a time, so this is also how many child processes run concurrently (default: `4`).
- `/processingdir:<path>`: Interim-product and state subfolder (default: `<output>/_processing`). Holds every stage's per-tile output plus `pipeline_state.csv`, the tile x stage status manifest.
- `/rebuild`: Ignore recorded state and redo every tile and stage, instead of the default resume-from-state behavior.
- `/tiles:<name1,name2,...>`: Process only the named tiles instead of the whole extent.
- `/retryfailed`: Process only tiles with a recorded failed stage in the state manifest.
- `/toolsdir:<path>`: Directory containing the sibling tool `.exe` files (default: the directory `pipeline.exe` itself is running from).
- `/cleanup`: Delete the processing subfolder after a fully successful run (skipped if any tile/stage failed, so `/retryfailed` still has something to resume from).
- `/merge`: Also merge each raster stage's VRT into a single global GeoTIFF.

The following options are forwarded to whichever stage(s) in the chain accept them (see each stage's own section above for what it does):
- `/cellsize`, `/minht`, `/heightcut`, `/outlier`, `/class`, `/strata`, `/intstrata`, `/ground`, `/window-a`, `/window-b`, `/smooth`, `/minz`, `/maxz`, `/return`
- `/first`, `/nointensity`, `/slope`

If `groundfilter` runs earlier in the chain, its DEM output is passed automatically as `/ground` to any later stage that accepts it (`canopymodel`, `gridmetrics`) -- an explicit `/ground:<path>` is only needed when no `groundfilter` stage precedes it.
