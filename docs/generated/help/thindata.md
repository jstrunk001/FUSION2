```text
Usage: thindata <input.las/laz or directory> [other /options]
Spatial Point Cloud Thinning / Decimation Tool

Options:
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /cellsize:<value>	Grid cell size for 2D spatial thinning (same units as the input) (default: 1.0)
  /output:<value>	Output thinned LAS/LAZ file path (default: thinned_output.laz)
```
