```text
Usage: gridmetrics <input.las/laz/raster or directory> [optional raster ground path] [other /options]
Computes comprehensive canopy elevation and intensity metrics grid from point clouds

Options:
  /strataraster	With /strata or /intstrata, also append per-stratum bands (density_stratum_NN, stratum_NN_count/proportion/mean/stddev/min/max, intstratum_NN_...) to the multiband output, in single-file and batch mode
  /first	Use only first returns for metric calculations
  /all	Use all returns for canopy cover and metric calculations
  /noraster	Skip writing the GeoTIFF raster(s) -- only valid together with /output-table, since a run must produce at least one output
  /nointensity	Skip computing intensity metrics
  /rgbstrata	With /rgb and /strata both set, also append a mean/stddev/min/max band set (and matching CSV columns) per selected spectral channel within each height-stratum bucket (<channel>_stratum_NN_*).
  /surfstats	Compute surface_area_ratio and roughness bands from the per-cell elevation grid (see /surfstats-source)
  /exp	Compute additional experimental metrics from RSForTools
  /nodata-as-zero	Raster input only: count missing-data pixels (NaN or the raster's NoData value) as zero-height returns instead of skipping them -- for canopy height models where missing pixels are bare ground, so cover and density include them
  /profile	Report high-resolution wall-clock timing breakdown across decompression, binning, metric computation, and I/O
  /gpu	Enable CUDA GPU acceleration for DSM raster zonal metric calculations
  /vrt	Batch/tiled mode: generate a GDAL Virtual Raster (.vrt) across tile rasters (on by default)
  /merge	Batch/tiled mode: merge the VRT into a single global GeoTIFF file
  /ground:<value>	Path to ground surface DEM raster (GeoTIFF, ENVI, IMG), or a directory of DTM tiles to mosaic on the fly (default: )
  /cellsize:<value>	Output grid cell size in project units (default: 10.0)
  /buffer:<value>	Batch/tiled mode: tile buffer distance (default: 50)
  /zshift_ground:<value>	Add this offset to /ground raster elevation values after /zmult_ground, before subtracting from point Z (default: 0.0)
  /minht:<value>	Minimum height above ground for canopy metrics calculation (in output vertical units, after /zmult and /zshift) (default: 2.0)
  /heightcut:<value>	Height cutoff threshold for canopy cover calculations (defaults to minht; in output vertical units) (default: )
  /outlier:<value>	Trim elevation outliers outside min,max values in output vertical units (e.g. -5,150) (default: )
  /zmult:<value>	Multiply final elevation/height values by this factor (e.g. 0.3048 for feet to meters); height/elevation thresholds apply after this transform (default: 1.0)
  /zshift:<value>	Add this offset to final elevation/height values after /zmult; height/elevation thresholds apply after this transform (default: 0.0)
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /zmult_ground:<value>	Multiply /ground raster elevation values by this factor before subtracting from point Z (e.g. 3.28084 for meters to feet) (default: 1.0)
  /strata:<value>	Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0) (default: )
  /intstrata:<value>	Comma-separated intensity strata height thresholds (default: )
  /surfstats-source:<value>	Elevation source for /surfstats: max (typical CHM top-surface use) or mean (ground-DTM-style runs) (default: max)
  /nodata:<value>	Value for cells with zero returns at all: NA, or a number such as 0, -9999, or inf (default: NA)
  /extent:<value>	Batch/tiled mode: project extent LLX,LLY,URX,URY (default: the extent of the input files, snapped to the cell size) (default: )
  /noheight:<value>	Value for height-dependent bands (elev_*, int_*) when a cell has returns but none clear the height cutoff: NA, or a number such as 0, -9999, or inf (default: 0)
  /rgb:<value>	Comma-separated spectral channels to compute a statistic bundle for: R, G, B, N, or all (every channel the input file's LAS point format actually carries) (default: )
  /voxelsize:<value>	3D voxel resolution for voxel volume metrics (same units as the input) (default: 20.0)
  /outroot:<value>	Base root name for output CSV summary metrics tables (default: )
  /outdir:<value>	Output directory for rasters and CSV reports (also the batch/tiled mode output directory) (default: .)
  /output-mode:<value>	Output raster mode: multiband or singleband (default: multiband)
  /output-table:<value>	Write the full per-cell metrics table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /tilesize:<value>	Batch/tiled mode: tile width,height in project units (default: 1000,1000)
  /threads:<value>	Batch/tiled mode: number of parallel worker threads (default: 4)
```
