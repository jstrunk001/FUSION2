| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| gridmetrics DSM raster: exits with status 0 | pass | 0 | 0 |
| gridmetrics CHM raster: exits with status 0 | pass | 0 | 0 |
| DSM raster: metrics raster written | pass | naip3d_​dsm_​gridmetrics.tif | naip3d_​dsm_​gridmetrics.tif |
| DSM raster: metrics table written | pass | naip3d_​dsm_​table.csv | naip3d_​dsm_​table.csv |
| DSM raster: grid covers the raster's extent | pass | 425750,​  426250,​ 5180750,​ 5181250 | 425750,​  426250,​ 5180750,​ 5181250 |
| DSM raster: grid has the expected rows x columns | pass | 25x25 | 25x25 |
| DSM raster metrics raster: coordinate system attached | pass | present | present |
| DSM raster: no intensity bands (a raster has no intensity) | pass | 0 | 0 |
| DSM raster: per-cell pixel counts match R | pass | 0 | 0 mismatched cells |
| DSM raster: elev_​max matches R in every cell (within 0.01 m) | pass | 0.0001 | < 0.01 |
| DSM raster: elev_​mean matches R in every cell (within 0.01 m) | pass | 0.0001 | < 0.01 |
| CHM raster: metrics raster written | pass | meta_​chm_​gridmetrics.tif | meta_​chm_​gridmetrics.tif |
| CHM raster: metrics table written | pass | meta_​chm_​table.csv | meta_​chm_​table.csv |
| CHM raster: grid covers the raster's extent | pass | 425750,​  426250,​ 5180750,​ 5181250 | 425750,​  426250,​ 5180750,​ 5181250 |
| CHM raster: grid has the expected rows x columns | pass | 25x25 | 25x25 |
| CHM raster metrics raster: coordinate system attached | pass | present | present |
| CHM raster: no intensity bands (a raster has no intensity) | pass | 0 | 0 |
| CHM raster: per-cell pixel counts match R | pass | 0 | 0 mismatched cells |
| CHM raster: elev_​max matches R in every cell (within 0.001 m) | pass | 0 | < 0.001 |
| CHM raster: elev_​mean matches R in every cell (within 0.001 m) | pass | 0 | < 0.001 |
| /​profile: timing table lists every section | pass | 6 | 6 |
| /​profile: sections add up to the total (within 0.003 s) | pass | 0.353 | 0.352 |

: Automated checks on the example run of `gridmetrics` on a surface raster. {#tbl-checks-gridmetrics-surface}

