```text
Usage: cloudmetrics <input.las/laz or directory> [optional ground DTM path] [other /options]
Computes Summary Metrics for Point Cloud Clips

Options:
  /rgbstrata	With /rgb and /strata both set, also report a mean/stddev/min/max bundle per selected spectral channel within each height-stratum bucket (<channel>_stratum_NN_*).
  /surfstats	Compute surface area ratio, roughness, planimetric area, and 3D surface area from a /cellsize elevation grid (top-of-cloud max per cell). Not available with /shape.
  /exp	Compute additional experimental metrics from RSForTools
  /output:<value>	Output CSV file path (default: cloud_metrics.csv)
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /ground:<value>	Path to ground DEM raster for height normalization, or a directory of DTM tiles to mosaic on the fly (default: )
  /rgb:<value>	Comma-separated spectral channels to compute a statistic bundle for: R, G, B, N, or all (every channel the input file's LAS point format actually carries) (default: )
  /cellsize:<value>	Grid cell size for 2D area/volume metrics (same units as the input) (default: 10.0)
  /shape:<value>	Polygon shapefile -- compute one metrics row per polygon feature instead of one row for the whole cloud (default: )
  /strata:<value>	Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0) -- same syntax as gridmetrics' /strata. Appends stratum_N_count/proportion plus a full elevation statistic bundle per bucket. (default: )
  /field:<value>	Attribute field used to label each /shape polygon's output row; falls back to a zero-padded feature index when omitted or missing on a feature (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /minht:<value>	Minimum height cutoff for canopy metrics (same units as the input) (default: 2.0)
  /voxelsize:<value>	3D voxel resolution for voxel volume metrics (same units as the input) (default: 20.0)
  /nodata:<value>	Value for a cloud/feature with zero points at all: NA, or a number such as 0, -9999, or inf (default: NA)
  /intstrata:<value>	Comma-separated height thresholds bucketing points the same way as /strata, but reporting an intensity statistic bundle per bucket instead of elevation (defaults to /strata's thresholds if omitted). (default: )
  /noheight:<value>	Value for height-dependent columns when points exist but none clear the height cutoff: NA, or a number such as 0, -9999, or inf (default: 0)
```
