```text
Usage: topometrics <input_dem.tif> [other /options]
Computes Topographic Terrain Metrics (Slope, Aspect) from DEM GeoTIFF

Options:
  /noraster	Skip writing the GeoTIFF raster -- only valid together with /output-table, since a run must produce at least one output
  /zshift:<value>	Add this offset to final elevation/height values after /zmult; height/elevation thresholds apply after this transform (default: 0.0)
  /zmult:<value>	Multiply final elevation/height values by this factor (e.g. 0.3048 for feet to meters); height/elevation thresholds apply after this transform (default: 1.0)
  /output-table:<value>	Also write a multicolumn table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /output:<value>	Output multi-band GeoTIFF raster path (default: topo_metrics.tif)
```
