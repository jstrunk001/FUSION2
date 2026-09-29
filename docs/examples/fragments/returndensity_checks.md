| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| returndensity: exits with status 0 | pass | 0 | 0 |
| density raster written | pass | returndensity.tif | returndensity.tif |
| density raster: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023955.00,​  598874.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| density raster: coordinate system attached | pass | present | present |
| density x cell area sums to the point count (within 0.5%) | pass | 70286 | 70286 |
| per-cell counts match R exactly | pass | 0 | 0 mismatched cells |
| first-return percent within 0-100 | pass | 36.96,​ 88.89 | 0 to 100 |
| density band's unit label matches the tile's linear units (feet) | pass | point_​density | no m2 label on a feet-based tile |

: Automated checks on the example run of `returndensity`. {#tbl-checks-returndensity}

