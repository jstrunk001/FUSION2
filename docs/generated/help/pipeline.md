```text
Usage: pipeline <arguments> [other /options]
Multi-tool batch pipeline: tiles/buffers the input once and chains any of the FUSION2 tools per tile

Options:
  /nogpu	Forwarded to whichever stage(s) accept it
  /slope	Forwarded to whichever stage(s) accept it
  /nointensity	Forwarded to whichever stage(s) accept it
  /first	Forwarded to whichever stage(s) accept it
  /gpu	Forwarded to whichever stage(s) accept it
  /cleanup	Delete the processing subfolder after a fully successful run
  /merge	Also merge each raster stage's VRT into a single global GeoTIFF
  /retryfailed	Process only tiles with a recorded failed stage
  /rebuild	Ignore recorded state and redo every tile/stage
  /minz:<value>	Forwarded to whichever stage(s) accept it (default: )
  /maxz:<value>	Forwarded to whichever stage(s) accept it (default: )
  /window-a:<value>	Forwarded to whichever stage(s) accept it (default: )
  /return:<value>	Forwarded to whichever stage(s) accept it (default: )
  /ground:<value>	Forwarded to whichever stage(s) accept it (default: )
  /window-b:<value>	Forwarded to whichever stage(s) accept it (default: )
  /intstrata:<value>	Forwarded to whichever stage(s) accept it (default: )
  /strata:<value>	Forwarded to whichever stage(s) accept it (default: )
  /outlier:<value>	Forwarded to whichever stage(s) accept it (default: )
  /tool:<value>	Shorthand for a single-stage /pipeline:<name> (default: )
  /tilesize:<value>	Tile width,height in project units (default: 1000,1000)
  /class:<value>	Forwarded to whichever stage(s) accept it (default: )
  /input:<value>	Input directory of LAS/LAZ files, or a single LAS/LAZ file (default: )
  /smooth:<value>	Forwarded to whichever stage(s) accept it (default: )
  /toolsdir:<value>	Directory containing the sibling tool .exe files (default: this executable's own directory) (default: )
  /output:<value>	Output directory for finalized rasters/tables (default: )
  /extent:<value>	Project extent LLX,LLY,URX,URY (default: )
  /pipeline:<value>	Comma-separated ordered list of stages to chain per tile, e.g. groundfilter,canopymodel,canopymaxima (default: )
  /buffer:<value>	Tile buffer distance (default: 50)
  /heightcut:<value>	Forwarded to whichever stage(s) accept it (default: )
  /processingdir:<value>	Interim-product/state subfolder (default: <output>/_processing) (default: )
  /cellsize:<value>	Forwarded to whichever stage(s) accept it (default: )
  /threads:<value>	Number of parallel worker threads (parallel child processes) (default: 4)
  /minht:<value>	Forwarded to whichever stage(s) accept it (default: )
  /tiles:<value>	Comma-separated tile names to process (default: all) (default: )
```
