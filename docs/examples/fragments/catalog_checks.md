| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| catalog: exits with status 0 | pass | 0 | 0 |
| CSV report written | pass | catalog.csv | catalog.csv |
| point count matches the tile | pass | 70532 | 70532 |
| XY bounds match the tile within 0.01 ft | pass | 1023755.00,​ 1023954.98,​  598875.00,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| Z range matches the tile within 0.01 ft | pass | 175.38,​ 355.99 | 175.38,​ 355.99 |
| /​density raster written | pass | 1 | >= 1 .tif |
| density raster: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023955.00,​  598874.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| density raster: coordinate system attached | pass | present | present |
| density raster sums to the point count | pass | 70532 | 70532 |

: Automated checks on the example run of `catalog`. {#tbl-checks-catalog}

