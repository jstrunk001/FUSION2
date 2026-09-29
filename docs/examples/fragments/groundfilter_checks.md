| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| groundfilter: exits with status 0 | pass | 0 | 0 |
| ground DEM written | pass | ground_​dem.tif | ground_​dem.tif |
| ground DEM: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023955.00,​  598874.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| ground DEM: coordinate system attached | pass | present | present |
| ground DEM has no empty cells | pass | 0 | 0 |
| ground DEM stays within the ground points' elevation range (+/​- 1 ft) | pass | 175.69,​ 179.39 | 175.45,​ 180.76 |
| median \|FUSION2 - lidR\| ground difference under 1 ft | pass | 0.134 | < 1 ft |
| 95th percentile \|FUSION2 - lidR\| ground difference under 3 ft | pass | 0.681 | < 3 ft |
| /​output-points LAS written | pass | present | present |
| /​output-points holds only ground-classed points | pass | 2 | 2 |

: Automated checks on the example run of `groundfilter`. {#tbl-checks-groundfilter}

