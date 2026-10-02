```text
Usage: gridmetrics <input.las/laz/raster or directory> [optional raster ground path] [other /options]
Computes comprehensive canopy elevation and intensity metrics grid from point clouds

Options:
  /merge	Batch/tiled mode: merge the VRT into a single global GeoTIFF file
  /vrt	Batch/tiled mode: generate a GDAL Virtual Raster (.vrt) across tile rasters (on by default)
  /nogpu	Disable CUDA GPU acceleration (force CPU scanline processing)
  /gpu	Enable CUDA GPU acceleration for DSM raster zonal metric calculations
  /profile	Report high-resolution wall-clock timing breakdown across decompression, binning, metric computation, and I/O
  /noraster	Skip writing the GeoTIFF raster(s) -- only valid together with /output-table, since a run must produce at least one output
  /exp	Compute additional experimental metrics from RSForTools
  /strataraster	With /strata or /intstrata, also append per-stratum bands (density_stratum_NN, stratum_NN_count/proportion/mean/stddev/min/max, intstratum_NN_...) to the multiband output, in single-file and batch mode
  /surfstats	Compute surface_area_ratio and roughness bands from the per-cell elevation grid (see /surfstats-source)
  /nointensity	Skip computing intensity metrics
  /all	Use all returns for canopy cover and metric calculations
  /rgbstrata	With /rgb and /strata both set, also append a mean/stddev/min/max band set (and matching CSV columns) per selected spectral channel within each height-stratum bucket (<channel>_stratum_NN_*).
  /first	Use only first returns for metric calculations
  /buffer:<value>	Batch/tiled mode: tile buffer distance (default: 50)
  /tilesize:<value>	Batch/tiled mode: tile width,height in project units (default: 1000,1000)
  /extent:<value>	Batch/tiled mode: project extent LLX,LLY,URX,URY (default: the extent of the input files, snapped to the cell size) (default: )
  /output-table:<value>	Write the full per-cell metrics table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /output-mode:<value>	Output raster mode: multiband or singleband (default: multiband)
  /outdir:<value>	Output directory for rasters and CSV reports (also the batch/tiled mode output directory) (default: .)
  /outroot:<value>	Base root name for output CSV summary metrics tables (default: )
  /surfstats-source:<value>	Elevation source for /surfstats: max (typical CHM top-surface use) or mean (ground-DTM-style runs) (default: max)
  /rgb:<value>	Comma-separated spectral channels to compute a statistic bundle for: R, G, B, N, or all (every channel the input file's LAS point format actually carries) (default: )
  /cellsize:<value>	Output grid cell size in project units (default: 10.0)
  /strata:<value>	Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0) (default: )
  /outlier:<value>	Trim elevation outliers outside min,max values (e.g. -5,150) (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /ground:<value>	Path to ground surface DEM raster (GeoTIFF, ENVI, IMG), or a directory of DTM tiles to mosaic on the fly (default: )
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /heightcut:<value>	Height cutoff threshold for canopy cover calculations (defaults to minht) (default: )
  /noheight:<value>	Value for height-dependent bands (elev_*, int_*) when a cell has returns but none clear the height cutoff: NA, or a number such as 0, -9999, or inf (default: 0)
  /intstrata:<value>	Comma-separated intensity strata height thresholds (default: )
  /threads:<value>	Batch/tiled mode: number of parallel worker threads (default: 4)
  /minht:<value>	Minimum height above ground for canopy metrics calculation (default: 2.0)
  /voxelsize:<value>	3D voxel resolution for voxel volume metrics (same units as the input) (default: 20.0)
  /nodata:<value>	Value for cells with zero returns at all: NA, or a number such as 0, -9999, or inf (default: NA)
```
