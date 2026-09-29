| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| densitymetrics: exits with status 0 | pass | 0 | 0 |
| stratum raster written | pass | dm_​densitymetrics.tif | dm_​densitymetrics.tif |
| stratum raster: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023955.00,​  598874.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| stratum raster: coordinate system attached | pass | present | present |
| per-cell CSV table written | pass | present | present |
| per cell,​ stratum counts add to total returns | pass | 0 | 0 mismatched cells |
| total returns across cells equals the point count | pass | 70286 | 70286 |
| top stratum (> 20 ft) count matches R (within 5 points) | pass | 59719 | 59719 |

: Automated checks on the example run of `densitymetrics`. {#tbl-checks-densitymetrics}

