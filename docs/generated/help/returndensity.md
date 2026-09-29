```text
Usage: returndensity <input.las/laz or directory> [other /options]
Generates Point Density and Return Ratio GeoTIFF Rasters

Options:
  /noraster	Skip writing the GeoTIFF raster -- only valid together with /output-table, since a run must produce at least one output
  /output-table:<value>	Also write a multicolumn table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /cellsize:<value>	Output grid cell size (default: 5.0)
  /output:<value>	Output multi-band GeoTIFF raster path (default: density_metrics.tif)
```
