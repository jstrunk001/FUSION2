```text
Usage: clipdata <input.las/laz or directory> [other /options]
Clips LAS/LAZ point clouds by bounding box or spatial extents

Options:
  /outside	Keep points outside every polygon instead of inside (single-clip mode only)
  /multifile	With /shape, write one output LAS/LAZ per polygon feature instead of one merged output
  /return:<value>	Return numbers to keep: /return:1 or /return:1,2 for an explicit list (ranges like /return:1-2 also work), or the mnemonics /return:first, /return:last, /return:only, /return:intermediate. Omit to keep every return. (default: )
  /class:<value>	Point classifications to keep. Omit for the default (excludes ASPRS noise classes 7 and 18); /class:all or /class:* keeps every class; /class:2,3,4,5 or /class:1-5 whitelists the listed classes; /class:~7,9,18 blacklists them (keeps every other class). (default: )
  /field:<value>	Attribute field used to name each /multifile output (<field-value>.laz); falls back to a zero-padded feature index when omitted or missing on a feature (default: )
  /maxz:<value>	Maximum height above ground or elevation (default: )
  /minz:<value>	Minimum height above ground or elevation (default: )
  /shape:<value>	Polygon shapefile to clip against, in addition to (not instead of) /extent (default: )
  /ground:<value>	Path to ground surface raster (GeoTIFF, ENVI, IMG) for height normalization, or a directory of DTM tiles to mosaic on the fly (default: )
  /output:<value>	Output LAS/LAZ file path (single-clip mode), or output directory (/multifile mode) (default: )
  /extent:<value>	Bounding box LLX,LLY,URX,URY (default: )
```
