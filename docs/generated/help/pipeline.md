```text
Usage: pipeline <arguments> [other /options]
Multi-tool batch pipeline: tiles/buffers the input once and chains any of the FUSION2 tools per tile

Options:
  /slope	Forwarded to whichever stage(s) accept it
  /retryfailed	Process only tiles with a recorded failed stage
  /rebuild	Ignore recorded state and redo every tile/stage
  /nointensity	Forwarded to whichever stage(s) accept it
  /cleanup	Delete the processing subfolder after a fully successful run
  /merge	Also merge each raster stage's VRT into a single global GeoTIFF
  /first	Forwarded to whichever stage(s) accept it
  /gpu	Forwarded to whichever stage(s) accept it
  /zshift_ground:<value>	Forwarded to whichever stage(s) accept it (default: )
  /minht:<value>	Forwarded to whichever stage(s) accept it (default: )
  /buffer:<value>	Tile buffer distance (default: 50)
  /input:<value>	Input directory of LAS/LAZ files, or a single LAS/LAZ file (default: )
  /window-a:<value>	Forwarded to whichever stage(s) accept it (default: )
  /pipeline:<value>	Comma-separated ordered list of stages to chain per tile, e.g. groundfilter,canopymodel,canopymaxima (default: )
  /tool:<value>	Shorthand for a single-stage /pipeline:<name> (default: )
  /output:<value>	Output directory for finalized rasters/tables (default: )
  /maxz:<value>	Forwarded to whichever stage(s) accept it (default: )
  /extent:<value>	Project extent LLX,LLY,URX,URY (default: )
  /tilesize:<value>	Tile width,height in project units (default: 1000,1000)
  /threads:<value>	Number of parallel worker threads (parallel child processes) (default: 4)
  /processingdir:<value>	Interim-product/state subfolder (default: <output>/_processing) (default: )
  /tiles:<value>	Comma-separated tile names to process (default: all) (default: )
  /zmult_ground:<value>	Forwarded to whichever stage(s) accept it (default: )
  /toolsdir:<value>	Directory containing the sibling tool .exe files (default: this executable's own directory) (default: )
  /cellsize:<value>	Forwarded to whichever stage(s) accept it (default: )
  /heightcut:<value>	Forwarded to whichever stage(s) accept it (default: )
  /zmult:<value>	Forwarded to whichever stage(s) accept it (default: )
  /minz:<value>	Forwarded to whichever stage(s) accept it (default: )
  /outlier:<value>	Forwarded to whichever stage(s) accept it (default: )
  /zshift:<value>	Forwarded to whichever stage(s) accept it (default: )
  /return:<value>	Forwarded to whichever stage(s) accept it (default: )
  /class:<value>	Forwarded to whichever stage(s) accept it (default: )
  /strata:<value>	Forwarded to whichever stage(s) accept it (default: )
  /intstrata:<value>	Forwarded to whichever stage(s) accept it (default: )
  /ground:<value>	Forwarded to whichever stage(s) accept it (default: )
  /window-b:<value>	Forwarded to whichever stage(s) accept it (default: )
  /smooth:<value>	Forwarded to whichever stage(s) accept it (default: )
```
