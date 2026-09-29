| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| treeseg: exits with status 0 | pass | 0 | 0 |
| segment raster written | pass | crown_​segments.tif | crown_​segments.tif |
| crown table written | pass | crown_​summary.csv | crown_​summary.csv |
| segment raster: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023956.00,​  598873.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| segment raster: coordinate system attached | pass | present | present |
| same crowns in the table and the raster | pass | 165 | 165 |
| PixelCount matches the segment raster | pass | 0 | 0 mismatched crowns |
| MaxHeight matches the CHM within each crown | pass | 0 | 0 mismatched crowns |
| no crown exceeds the tallest point above ground | pass | 0 crowns; max 177.83 | 0 above 181.06 ft |
| crown area = pixel count x cell area | pass | 0 | 0 mismatched crowns |
| crown area column's unit label matches the tile's units (feet) | pass | CrownArea | no m2 label on a feet-based tile |

: Automated checks on the example run of `treeseg`. {#tbl-checks-treeseg}

