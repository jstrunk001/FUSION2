```text
Usage: groundfilter <input.las/laz or directory> [other /options]
Classifies ground points with the Kraus & Pfeifer iterative filter and generates a GeoTIFF ground DEM

Options:
  /noraster	Skip writing the ground DEM GeoTIFF -- only valid together with /output-table, since a run must produce at least one output
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /cellsize:<value>	Output DEM cell size (default: 1.0)
  /filtercell:<value>	Cell size of the filter's intermediate surfaces (horizontal units); each surface cell averages its 3 x 3 neighbourhood (default: 10.0)
  /gparam:<value>	Kraus & Pfeifer g: residual at or below which a point gets full weight (vertical units) (default: -2.0)
  /output-table:<value>	Also write a one-band multicolumn table alongside the ground DEM raster (path ending in .csv or .sqlite) -- omit to skip table output entirely (default: )
  /tolerance:<value>	Classify as ground every point within this distance of the final surface (default: every point with residual <= g + w) (default: )
  /wparam:<value>	Kraus & Pfeifer w: width above g over which a point's weight falls to 0 (vertical units) (default: 2.5)
  /aparam:<value>	Kraus & Pfeifer a: steepness of the weight function (default: 1.0)
  /iterations:<value>	Number of surface/weight passes (default: 5)
  /output-raster:<value>	Output GeoTIFF ground DEM file path (default: )
  /coarsecell:<value>	Cell size of a first, coarse filter stage; points more than /coarsecut above its surface are dropped before the fine passes (default 3 x /filtercell; 0 disables) (default: )
  /bparam:<value>	Kraus & Pfeifer b: exponent of the weight function (default: 4.0)
  /coarsecut:<value>	Height above the coarse surface beyond which a point cannot be ground (vertical units; default 4 x /wparam) (default: )
  /output-points:<value>	Output LAS/LAZ of the points classified as ground (written with classification 2) (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
```
