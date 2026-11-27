```text
Usage: treeseg <input_chm.tif> [other /options]
Individual Tree Crown Watershed Segmentation on CHM GeoTIFF

Options:
  /output-raster:<value>	Output GeoTIFF raster path for tree segments (default: crown_segments.tif)
  /output-table:<value>	Output CSV summary table path (default: crown_summary.csv)
  /minht:<value>	Minimum height cutoff for segmentation (in output vertical units, after /zmult and /zshift) (default: 2.0)
  /zshift:<value>	Add this offset to final elevation/height values after /zmult; height/elevation thresholds apply after this transform (default: 0.0)
  /zmult:<value>	Multiply final elevation/height values by this factor (e.g. 0.3048 for feet to meters); height/elevation thresholds apply after this transform (default: 1.0)
```
