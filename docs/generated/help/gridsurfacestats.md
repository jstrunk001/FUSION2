```text
Usage: gridsurfacestats <input_surface.tif> [other /options]
Computes surface area ratio, roughness, and (with /reference) cut/fill volume from a DEM/CHM GeoTIFF

Options:
  /noraster	Skip writing the GeoTIFF raster -- only valid together with /output-table, since a run must produce at least one output
  /output:<value>	Output multi-band GeoTIFF raster path (default: surface_stats.tif)
  /reference:<value>	Second surface GeoTIFF (same cols x rows) to diff against for cut/fill volume_diff (default: )
  /output-table:<value>	Also write a multicolumn table alongside the raster (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /zshift:<value>	Add this offset to final elevation/height values after /zmult; height/elevation thresholds apply after this transform (default: 0.0)
  /zmult:<value>	Multiply final elevation/height values by this factor (e.g. 0.3048 for feet to meters); height/elevation thresholds apply after this transform (default: 1.0)
```
