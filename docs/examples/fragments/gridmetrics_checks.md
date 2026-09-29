| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| gridmetrics: exits with status 0 | pass | 0 | 0 |
| /​output-table path honored | pass | written | written |
| metrics raster written | pass | TC_​1372_​forest200ft_​gridmetrics.tif | TC_​1372_​forest200ft_​gridmetrics.tif |
| metrics raster: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023955.00,​  598874.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| metrics raster: coordinate system attached | pass | present | present |
| no band is empty in every cell | pass | none | none |
| grid has the expected rows x columns for the tile | pass | 20x20 | 20x20 |
| per-cell return counts match R | pass | 0 | 0 mismatched cells |
| no elev_​max cell exceeds the tallest point above ground | pass | 0 cells; max 177.83 | 0 cells above 181.06 ft |
| CSV table has one row per raster cell | pass | 400 | 400 |
| CSV elev_​mean matches the raster band | pass | 0 | all cells equal |

: Automated checks on the example run of `gridmetrics`. {#tbl-checks-gridmetrics}

