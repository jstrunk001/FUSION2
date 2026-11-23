```text
Usage: canopymaxima <input_chm.tif> [other /options]
Individual Tree Top Detection via Variable Window Local Maxima (VLM) on CHM GeoTIFF

Options:
  /zshift:<value>	Add this offset to final elevation/height values after /zmult; height/elevation thresholds apply after this transform (default: 0.0)
  /zmult:<value>	Multiply final elevation/height values by this factor (e.g. 0.3048 for feet to meters); height/elevation thresholds apply after this transform (default: 1.0)
  /window-a:<value>	Window polynomial coefficient A (constant) (default: 0.5)
  /minht:<value>	Minimum tree height threshold (in output vertical units, after /zmult and /zshift) (default: 2.0)
  /window-b:<value>	Window polynomial coefficient B (linear) (default: 0.05)
  /output:<value>	Output CSV file path for tree tops (default: tree_tops.csv)
```
