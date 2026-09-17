# Executable Suite & CLI Tools Reference

This user manual documents the command-line interface, syntax, parameters, options, flags, and workflow patterns for all 15 executables in the FUSION suite.

`ltktools.exe` from earlier versions of this suite has been folded into `gridmetrics.exe` (see its batch/tiled mode below) and replaced as the multi-tool orchestrator by the new `pipeline.exe` (see [`PIPELINE_GUIDE.md`](PIPELINE_GUIDE.md) for worked pipeline examples). Several option names changed in this pass to stay consistent across tools — see the "Renamed in this release" callouts below.

**Changed default behavior in this release:** `gridmetrics.exe` and `cloudmetrics.exe` now write `NA` by default, not a silently-computed value or `-9999`, for any cell/cloud with zero returns at all -- this includes `point_density` and `canopy_cover`/`CanopyCoverPct`, and (for gridmetrics) even the raw `TotalReturns`/`FirstReturns` counts in the CSV export. A separate `/noheight` sentinel (default `0`) covers the different case of a cell/cloud that has returns but none clearing the height cutoff. See "NA vs. 0 -- the /nodata and /noheight sentinel convention" near the top of [`METRICS_REFERENCE.md`](METRICS_REFERENCE.md) for the full rule and why the two cases are now distinguished.

---

## Command-Line Argument Syntax

All tools accept parameters using standard Windows slash syntax (`/option:value` or `/flag`), single-dash syntax (`-option value` or `-flag`), or double-dash syntax (`--option=value` or `--flag`). Help text can be displayed for any tool by passing `/?`, `-h`, or `--help`.

Every tool's required input file (or directory) is a **positional** argument — it is not passed with an `/option:` prefix, just placed on the command line. Everything else is an `/option` or `/flag`.

---

## Point Filtering: `/class` and `/return` (all point-cloud tools)

Every tool that reads point cloud data directly (`gridmetrics`, `cloudmetrics`, `canopymodel`, `groundfilter`, `clipdata`, `filterdata`, `thindata`, `densitymetrics`, and any point-cloud stage run through `pipeline`) shares one centralized point-filtering implementation and CLI syntax, replacing each tool's previous ad hoc classification handling (a bare whitelist, with no default noise exclusion and no shared return-number support):

- `/class:<spec>`: Point classifications to keep.
  - Omitted: **default** — excludes ASPRS noise classes 7 (Low Point) and 18 (High Noise); every other class is kept.
  - `/class:all` or `/class:*`: disables the default noise exclusion, keeping every classification code, including 7 and 18.
  - `/class:2,3,4,5` or `/class:1-5`: whitelist — keeps only the listed classes (a comma list, inclusive ranges, or both together).
  - `/class:~7,9,18`: blacklist — keeps every class except the listed ones, replacing the default 7/18 exclusion with your own list.
- `/return:<spec>`: Return numbers to keep.
  - Omitted: keeps every return.
  - `/return:1` or `/return:1,2` (ranges like `/return:1-2` also work): explicit return-number whitelist.
  - `/return:first`, `/return:last`, `/return:only`, `/return:intermediate`: legacy FUSION mnemonics (first return, last return, the only return on a single-return pulse, or a return that is neither first nor last).
- Withheld points (the LAS "discard this point" flag) are always excluded, independent of `/class` — there is no option to keep them.

`gridmetrics`' separate `/first` flag (selecting first returns for the metric-calculation set) is unchanged and independent of `/return` — the two can be combined.

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

A COPC (Cloud Optimized Point Cloud) file in the input directory is detected automatically (no separate flag) -- its own chunk index is used to seek directly to the chunks overlapping each tile's buffered extent, instead of reading the whole file sequentially per tile. A plain (non-indexed) LAS/LAZ file falls back to today's sequential read, unchanged. This mainly pays off when the input directory holds one very large regional COPC file rather than pre-tiled LAS/LAZ -- pre-tiled input is already read efficiently without it.

- `/extent:<LLX,LLY,URX,URY>`: Batch mode only. Project extent (default: a `0,0,5000,5000` grid if omitted).
- `/tilesize:<w,h>`: Batch mode only. Tile width,height in project units (default: `1000,1000`).
- `/buffer:<val>`: Batch mode only. Tile buffer distance (default: `50`).
- `/threads:<N>`: Batch mode only. Number of parallel worker threads (default: `4`).
- `/vrt`: Batch mode only. Generate a GDAL Virtual Raster (`.vrt`) across tile rasters (on by default).
- `/merge`: Batch mode only. Merge the VRT into a single global GeoTIFF file.

Single-file mode's `elev_*`/`int_*` bands are now the full statistic bundle (min/max/mean/stddev/variance/cv/skewness/kurtosis/crr/mode/median/iqr/percentiles/AAD/MAD-median/MAD-mode/L-moments/quadratic and cubic mean, plus `elev_profile_area`) -- see [`METRICS_REFERENCE.md`](METRICS_REFERENCE.md) Section 1 for the full column list. It also always emits `r1count`...`r9count` (per-return-number counts) and `allcover`/`afcover`/`allabovemean`/`allabovemode`/`afabovemean`/`afabovemode` (cover-variant bands), all computed directly from the unfiltered return set. Batch/tiled mode keeps the smaller, original band set (no full bundle, no return-number/cover-variant bands) -- see the codebase's own `TileCellAccumulator` comment for why.

#### Options & Flags (both modes, except where noted)
- `/ground:<path>`: Path to ground surface DEM raster (GeoTIFF, ENVI, IMG).
- `/cellsize:<val>`: Output grid cell size in project units (default: `10.0`). Batch mode's former separate `/resolution` option has been dropped in favor of this — one cell-size option everywhere.
- `/minht:<val>`: Minimum height above ground for canopy metrics calculation (default: `2.0`).
- `/heightcut:<val>`: Height cutoff threshold for canopy cover calculations (defaults to `/minht`).
- `/outlier:<min,max>`: Trim elevation outliers outside `min,max` values (e.g. `/outlier:-5,150`).
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools.
- `/first`: Use only first returns for metric calculations.
- `/all`: Single-file mode only. Use all returns for canopy cover and metric calculations.
- `/nointensity`: Skip computing intensity metrics.
- `/strata:<h1,h2,...>`: Comma-separated height strata thresholds (e.g. `/strata:0.5,2.0,5.0,10.0,20.0`). Each stratum bucket gets the simplified 6-metric summary as CSV columns -- `stratum_N_count`, `stratum_N_proportion`, `stratum_N_mean`, `stratum_N_stddev`, `stratum_N_min`, `stratum_N_max` (see [`METRICS_REFERENCE.md`](METRICS_REFERENCE.md)) -- for that bucket's points. A bucket with zero points in an otherwise non-empty cell gets `/noheight` for its mean/stddev/min/max columns and a real `0` for its own count/proportion.
- `/intstrata:<h1,h2,...>`: Comma-separated intensity strata height thresholds (defaults to `/strata`'s thresholds if omitted, currently accumulated but not yet surfaced in output -- see the codebase's own `strataIntSums`/`strataIntCounts` fields).
- `/rgb:<spec>`: Comma-separated spectral channels to compute a full statistic bundle for -- `R`, `G`, `B`, `N` (near-infrared), or `all` (every channel the input file's LAS point format actually carries). RGB is populated in point formats 2, 3, 5, 7, 8, 10; NIR only in formats 8 and 10 -- a requested channel the file's format doesn't carry prints a warning naming the channel and the point format, and is simply omitted (never a hard failure). Emits `red_*`/`green_*`/`blue_*`/`nir_*` bands, one full bundle per selected, available channel, gated by the same `/minht` cutoff and `/noheight`/`/nodata` rule as `int_*`.
- `/rgbstrata`: With `/rgb` and `/strata` both set. Also reports a mean/stddev/min/max summary per selected spectral channel within each height-stratum bucket -- `<channel>_stratum_N_mean`/`stddev`/`min`/`max` (e.g. `red_stratum_00_mean`), as both CSV columns and (with `/strataraster`) raster bands. Count/proportion aren't repeated here since the elevation `/strata` columns/bands sharing the same bucket boundaries already report them. Ignored (with a warning) if `/rgb` or `/strata` is missing.
- `/voxelsize:<val>`: Single-file mode only. 3D voxel resolution for voxel volume metrics, in meters (default: `20.0`).
- `/exp`: Single-file mode only. Compute additional experimental metrics from RSForTools.
- `/outroot:<name>`: Single-file mode only. Base root name for output CSV summary metrics tables.
- `/outdir:<path>`: Output directory for rasters and CSV reports (single-file mode) or for tile rasters and the mosaicked VRT (batch mode). Default: `.`.
- `/output-mode:<mode>`: Single-file mode only. Output raster mode, `multiband` or `singleband` (default: `multiband`).
- `/surfstats`: Single-file mode only. Compute `surface_area_ratio` and `roughness` bands from the per-cell elevation grid gridmetrics already holds in memory (no second raster round-trip). See `/surfstats-source` and [`METRICS_REFERENCE.md`](METRICS_REFERENCE.md) for the underlying math.
- `/surfstats-source:<max|mean>`: With `/surfstats`. Which per-cell elevation to feed the surface-stats math -- `max` (default) suits typical CHM top-surface use, `mean` suits ground-DTM-style runs.
- `/strataraster`: With `/strata`. Also append, per stratum bucket, a return-density band (`density_stratum_00`, ...), a count band (`stratum_00_count`, ...), a proportion band (`stratum_00_proportion`, ...), and the simplified mean/stddev/min/max band set (`stratum_00_mean`, `stratum_00_stddev`, `stratum_00_min`, `stratum_00_max`) to the multiband output -- the same values `/strata` already writes to CSV, just also as raster bands. With `/rgbstrata`, also appends that per-channel mean/stddev/min/max band set.
- `/nodata:<NA|number>`: Value for cells with zero returns at all (default: `NA`). Applies to every band and, in the CSV export, every column including `TotalReturns`/`FirstReturns`.
- `/noheight:<NA|number>`: Value for height-dependent bands (`elev_*`, `int_*`, and `/strataraster`'s/`/rgbstrata`'s mean/stddev/min/max bands) when a cell (or stratum bucket) has returns but none clear `/minht`/land in that bucket (default: `0`). Never applied to `canopy_cover`, `point_density`, or `/strataraster`'s own density/count/proportion bands -- those are well-defined directly from the unfiltered return count. If both `/nodata` and `/noheight` are explicitly set to different, non-`NA` values, gridmetrics prints a warning and uses `/nodata`'s value as the GeoTIFF's registered NoData value (GDAL supports only one per file); `/noheight`'s value is still written as an ordinary pixel, just not flagged as NoData by the file header.

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
- `/surfstats`: Compute `SurfaceAreaRatioMean`, `RoughnessMean`, `PlanimetricArea`, and `SurfaceArea3D` from a `/cellsize` elevation grid (max height per cell across the point cloud). Not available together with `/shape`.
- `/shape:<path.shp>`: Compute one metrics row per polygon feature instead of one row for the whole cloud -- streams the full, uncropped point cloud exactly once, routing each point into whichever feature's polygon contains it. Replaces the legacy two-step `PolyClipData`-then-`CloudMetrics` workflow (no intermediate per-plot LAS files written or reread).
- `/field:<name>`: With `/shape`. Attribute field used to label each output row (`Label` column); falls back to a zero-padded feature index when omitted or missing on a feature.
- `/nodata:<NA|number>`: Value for a cloud/feature with zero points at all (default: `NA`). Applies to every CSV column, including `TotalPoints`/`CanopyPoints`.
- `/noheight:<NA|number>`: Value for height-dependent columns (the full `elev_*`/`int_*` statistic bundle -- see below) when points exist but none clear `/minht` (default: `0`). Never applied to `CanopyCoverPct`, which is well-defined directly from the unfiltered point count.
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools.
- `/strata:<h1,h2,...>`: Comma-separated height strata thresholds -- same syntax as `gridmetrics`' `/strata`. Each bucket gets the simplified 6-metric summary as CSV columns: `stratum_N_count`, `stratum_N_proportion`, `stratum_N_mean`, `stratum_N_stddev`, `stratum_N_min`, `stratum_N_max` (see [`METRICS_REFERENCE.md`](METRICS_REFERENCE.md)).
- `/intstrata:<h1,h2,...>`: Comma-separated height thresholds bucketing points the same way as `/strata` (defaults to `/strata`'s thresholds if omitted), but reporting an `intstratum_N_*` **intensity** summary per bucket instead of elevation.
- `/rgb:<spec>`: Comma-separated spectral channels to compute a full statistic bundle for -- `R`, `G`, `B`, `N`, or `all` -- same semantics as `gridmetrics`' `/rgb` (see above), emitting `red_*`/`green_*`/`blue_*`/`nir_*` CSV columns.
- `/rgbstrata`: With `/rgb` and `/strata` both set. Also reports a mean/stddev/min/max summary per selected spectral channel within each height-stratum bucket as CSV columns -- `<channel>_stratum_N_mean`/`stddev`/`min`/`max` (e.g. `red_stratum_00_mean`). Ignored (with a warning) if `/rgb` or `/strata` is missing.

`cloudmetrics`' output CSV now carries the same full `elev_*`/`int_*` statistic bundle (plus `elev_profile_area`) that `gridmetrics` writes per cell -- see [`METRICS_REFERENCE.md`](METRICS_REFERENCE.md) Section 1 for the column list. The `/strata`/`/intstrata`/`/rgbstrata` per-bucket columns use the simplified 6-metric (or 4-metric, for `/rgbstrata`) summary instead, not this full bundle.

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
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools.

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
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools. Applied before computing each cell's minimum elevation, so noise/withheld returns don't pull the ground surface up or down.

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
- `/shape:<path.shp>`: Polygon shapefile to clip against, in addition to (not instead of) `/extent` -- both apply if both are given.
- `/multifile`: With `/shape`. Write one output LAS/LAZ per polygon feature instead of one merged output, into the directory given by `/output`. Each feature's writer is opened lazily on its first matched point, so polygons with no returns don't create empty files.
- `/field:<name>`: With `/multifile`. Attribute field used to name each output file (`<field-value>.laz`); falls back to a zero-padded feature index when omitted or missing on a feature.
- `/outside`: Keep points outside every polygon instead of inside. Single-clip mode only (no `/multifile`).
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering, applied before the spatial/height clip -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools.

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
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools. `/return` now accepts the same lists/ranges/mnemonics as every other tool, not just a single return number.

---

### 9. `thindata.exe`
Spatially thins a point cloud file or directory by keeping a single point per grid cell.

```bash
thindata <input.las/laz or directory> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output thinned LAS/LAZ file path (default: `thinned_output.laz`).
- `/cellsize:<val>`: Grid cell size for 2D spatial thinning, in meters (default: `1.0`).
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering, applied before thinning -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools.

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

---

### 14. `gridsurfacestats.exe`
Computes surface area ratio and roughness from a DEM/CHM GeoTIFF (a standalone raster-in/raster-out tool -- the same math is also available inline from `gridmetrics`/`cloudmetrics` via `/surfstats`, see above). With `/reference`, also computes per-cell cut/fill volume against a second, independently produced surface.

```bash
gridsurfacestats <input_surface.tif> [other /options]
```

#### Options & Flags
- `/output:<path>`: Output multi-band GeoTIFF raster path (default: `surface_stats.tif`).
- `/reference:<path>`: A second surface GeoTIFF, the same cols x rows as the input, to diff against for cut/fill `volume_diff`. Omit to skip cut/fill entirely (2-band output instead of 3).

The output raster inherits its NoData value directly from the input surface's own registered NoData -- there is no separate `/nodata` option, since a cell already flagged NoData in the source DEM/CHM has no basis for a surface-stats value either.

---

### 15. `densitymetrics.exe`
Computes a return-density raster stack across vertical height slices -- one band per `/strata` bucket (`density_stratum_00`, `density_stratum_01`, ...), each cell holding that bucket's return count per unit area. This is `gridmetrics`' `/strataraster` add-on (see above) as its own dedicated tool: reuses the same grid-binning and strata-bucket-assignment logic, matches the legacy `DensityMetrics` tool name for anyone porting old batch scripts, and is the natural place for a future `pipeline.exe` stage.

```bash
densitymetrics <input.las/laz or directory> [optional raster ground path] [other /options]
```

#### Options & Flags
- `/strata:<h1,h2,...>`: Comma-separated height strata thresholds, same syntax as `gridmetrics`' `/strata` (default: `0.5,2.0,5.0,10.0,20.0`).
- `/cellsize:<val>`: Output grid cell size in project units (default: `10.0`).
- `/ground:<path>`: Path to ground surface DEM raster (GeoTIFF, ENVI, IMG), or a directory of DTM tiles to mosaic on the fly. May also be supplied as an unflagged second positional argument.
- `/class:<spec>` / `/return:<spec>`: Point classification and return-number filtering -- see "Point Filtering" above for the shared syntax and defaults across all point-cloud tools.
- `/output:<name>`: Base output name (stem) for the raster/CSV files (default: derived from the input filename).
- `/outdir:<path>`: Output directory for the raster and CSV report (default: `.`).
- `/nodata:<NA|number>`: Value for cells with zero returns at all (default: `NA`). Applies to every stratum band and, in the CSV export, every column including `TotalReturns`. A non-empty cell's stratum bands are always real computed counts (including a legitimate `0` for an empty bucket) -- `densitymetrics` has no `/noheight` option, since nothing it computes is height-filtered the way `elev_*`/`int_*` bands are.
