| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| pipeline: exits with status 0 | pass | 0 | 0 |
| run state table written | pass | pipeline_​state.csv | pipeline_​state.csv |
| no tile x stage failed | pass | done | no failed status |
| four tiles processed | pass | 4 | 4 |
| mosaicked canopy model written | pass | canopymodel_​merged.tif | a canopymodel .tif or .vrt |
| mosaicked CHM: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023956.00,​  598873.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| mosaicked CHM matches single-run CHM (median diff < 0.5 ft) | pass | 0.217 | < 0.5 ft |
| no mosaicked CHM cell exceeds the tallest point above ground | pass | 0 | 0 |
| combined tree tops table written | pass | 1 | >= 1 |
| no duplicate tree tops across tile seams | pass | 0 | 0 |

: Automated checks on the example run of `pipeline`. {#tbl-checks-pipeline}

