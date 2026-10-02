| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| gridmetrics batch: exits with status 0 | pass | 0 | 0 |
| gridmetrics batch,​ /​extent off the data: stops with a non-zero status | pass | 1 | > 0 |
| /​extent off the data: error message names the files' extent | pass | message given | message given |
| batch virtual raster written | pass | project_​gridmetrics.vrt | project_​gridmetrics.vrt |
| no /​extent: grid is the input's extent snapped to the cell size | pass | 1023750,​ 1023960,​  598870,​  599080 | 1023750,​ 1023960,​  598870,​  599080 |
| batch virtual raster: coordinate system attached | pass | present | present |
| virtual raster band names match the tile rasters' | pass | elev_​min,​elev_​max,​elev_​mean | elev_​min,​elev_​max,​elev_​mean |
| /​merge GeoTIFF written | pass | project_​gridmetrics_​merged.tif | project_​gridmetrics_​merged.tif |
| /​merge GeoTIFF band names match the tile rasters' | pass | elev_​min,​elev_​max,​elev_​mean | elev_​min,​elev_​max,​elev_​mean |
| returns summed over all cells equal the point count | pass | 70286 | 70286 |
| per-cell return counts match R,​ including along tile seams | pass | 0 | 0 mismatched cells |
| positional ground applied: no elev_​max cell above the tallest point | pass | 0 cells; max 177.83 | 0 cells above 181.06 ft |
| elev_​max matches R's tallest height per cell (median under 0.1 ft) | pass | 0 | < 0.1 ft |
| /​strataraster: one count band per height layer | pass | 6 | 6 |
| /​strataraster: layer counts add to each cell's returns | pass | 0 | 0 mismatched cells |
| batch table written | pass | gridmetrics_​batch_​table.csv | gridmetrics_​batch_​table.csv |
| batch table has one row per cell | pass | 441 | 441 |
| batch table's TotalReturns sum to the point count | pass | 70286 | 70286 |

: Automated checks on the example run of `gridmetrics` batch mode. {#tbl-checks-gridmetrics-batch}

