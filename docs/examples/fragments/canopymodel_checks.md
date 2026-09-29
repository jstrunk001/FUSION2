| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| canopymodel: exits with status 0 | pass | 0 | 0 |
| canopymodel /​smooth:3: exits with status 0 | pass | 0 | 0 |
| canopymodel /​slope: exits with status 0 | pass | 0 | 0 |
| canopy height model written | pass | chm.tif | chm.tif |
| CHM: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023956.00,​  598873.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| CHM: coordinate system attached | pass | present | present |
| no CHM cell exceeds the tallest point above ground | pass | 0 cells; max 177.83 | 0 cells above 181.06 ft |
| no CHM cell more than 1 ft below ground | pass | -0.17 | >= -1 ft |
| median \|CHM - R reference\| under 0.5 ft (plausible cells) | pass | 0 | < 0.5 ft |
| /​smooth:3 CHM written | pass | chm_​smooth3.tif | chm_​smooth3.tif |
| /​smooth:3 lowers cell-to-cell variance | pass | 1383.46 | < 1599.93 |
| /​slope CHM written | pass | chm_​slope.tif | chm_​slope.tif |
| /​slope changes at least one cell | pass | 2.527 | > 0 |

: Automated checks on the example run of `canopymodel`. {#tbl-checks-canopymodel}

