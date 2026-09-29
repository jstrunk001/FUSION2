```text
Usage: densitymetrics <input.las/laz or directory> [optional raster ground path] [other /options]
Computes a return-density raster stack across vertical height slices (one band per /strata bucket)

Options:
  /noraster	Skip writing the GeoTIFF raster -- only valid together with /output-table, since a run must produce at least one output
  /output-table:<value>	Write a multicolumn table of per-cell stratum counts (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /nodata:<value>	Value for cells with zero returns at all: NA, or a number such as 0, -9999, or inf. A non-empty cell's stratum bands are always real computed counts, never this sentinel. (default: NA)
  /output:<value>	Base output name (stem) for the raster/CSV files (default: )
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /ground:<value>	Path to ground surface DEM raster (GeoTIFF, ENVI, IMG), or a directory of DTM tiles to mosaic on the fly (default: )
  /outdir:<value>	Output directory for the raster and CSV report (default: .)
  /cellsize:<value>	Output grid cell size in project units (default: 10.0)
  /strata:<value>	Comma-separated height strata thresholds (e.g. 0.5,2.0,5.0,10.0,20.0) -- same syntax as gridmetrics' /strata (default: 0.5,2.0,5.0,10.0,20.0)
```
