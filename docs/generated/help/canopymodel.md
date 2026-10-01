```text
Usage: canopymodel <input.las/laz or directory> [other /options]
Generates Canopy Height Model (CHM) GeoTIFF from point cloud

Options:
  /noraster	Skip writing the GeoTIFF raster -- only valid together with /output-table, since a run must produce at least one output
  /nogpu	Disable CUDA GPU acceleration (force CPU rasterization)
  /gpu	Enable CUDA GPU acceleration for CHM rasterization
  /slope	Normalize heights perpendicular to local terrain slope plane
  /output-table:<value>	Also write a one-band multicolumn table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /smooth:<value>	Spatial smoothing window size (e.g. 3 for 3x3 filter) (default: )
  /output:<value>	Output GeoTIFF CHM file path (default: )
  /ground:<value>	Path to ground DEM raster for height normalization, or a directory of DTM tiles to mosaic on the fly (default: )
  /cellsize:<value>	Output CHM cell size (default: 1.0)
```
