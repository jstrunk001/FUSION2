| Tool | Check | Severity | Result | Observed | Expected |
|:-----------------|:--------------------------------|:---------|:----------|:----------------|:----------------|
| reference | reference heights stay within a plausible range | hard | pass | -0.63,​ 176.06 | -10 to 300 ft |
| catalog | catalog: exits with status 0 | hard | pass | 0 | 0 |
| catalog | CSV report written | hard | pass | catalog.csv | catalog.csv |
| catalog | point count matches the tile | hard | pass | 70532 | 70532 |
| catalog | XY bounds match the tile within 0.01 ft | hard | pass | 1023755.00,​ 1023954.98,​  59... | 1023755.00,​ 1023954.98,​  59... |
| catalog | Z range matches the tile within 0.01 ft | hard | pass | 175.38,​ 355.99 | 175.38,​ 355.99 |
| catalog | /​density raster written | hard | pass | 1 | >= 1 .tif |
| catalog | density raster: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023955.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| catalog | density raster: coordinate system attached | soft | pass | present | present |
| catalog | density raster sums to the point count | hard | pass | 70532 | 70532 |
| filterdata | filterdata first_​returns: exits with status 0 | hard | pass | 0 | 0 |
| filterdata | first_​returns: output readable | hard | pass | ok | ok |
| filterdata | first_​returns: kept count matches R | hard | pass | 39120 | 39120 |
| filterdata | first_​returns: every kept point satisfies the filter | hard | pass | 0 | 0 violations |
| filterdata | first_​returns: coordinate system kept | hard | pass | present | present |
| filterdata | first_​returns: withheld points dropped | soft | pass | 0 | 0 |
| filterdata | filterdata ground_​class: exits with status 0 | hard | pass | 0 | 0 |
| filterdata | ground_​class: output readable | hard | pass | ok | ok |
| filterdata | ground_​class: kept count matches R | hard | pass | 3672 | 3672 |
| filterdata | ground_​class: every kept point satisfies the filter | hard | pass | 0 | 0 violations |
| filterdata | ground_​class: coordinate system kept | hard | pass | present | present |
| filterdata | ground_​class: withheld points dropped | soft | pass | 0 | 0 |
| filterdata | filterdata z_​band: exits with status 0 | hard | pass | 0 | 0 |
| filterdata | z_​band: output readable | hard | pass | ok | ok |
| filterdata | z_​band: kept count matches R | hard | pass | 11893 | 11893 |
| filterdata | z_​band: every kept point satisfies the filter | hard | pass | 0 | 0 violations |
| filterdata | z_​band: coordinate system kept | hard | pass | present | present |
| filterdata | z_​band: withheld points dropped | soft | pass | 0 | 0 |
| thindata | thindata: exits with status 0 | hard | pass | 0 | 0 |
| thindata | output readable | hard | pass | ok | ok |
| thindata | no cell keeps more than one point | hard | pass | 0 | 0 duplicate cells |
| thindata | kept count equals occupied cells | hard | pass | 10092 | 10092 |
| thindata | every kept point exists in the tile | hard | pass | 0 | 0 new points |
| groundfilter | groundfilter: exits with status 0 | hard | pass | 0 | 0 |
| groundfilter | ground DEM written | hard | pass | ground_​dem.tif | ground_​dem.tif |
| groundfilter | ground DEM: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023955.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| groundfilter | ground DEM: coordinate system attached | soft | pass | present | present |
| groundfilter | ground DEM has no empty cells | hard | pass | 0 | 0 |
| groundfilter | ground DEM stays within the ground points' elevation rang... | hard | pass | 175.69,​ 179.39 | 175.45,​ 180.76 |
| groundfilter | median \|FUSION2 - lidR\| ground difference under 1 ft | hard | pass | 0.134 | < 1 ft |
| groundfilter | 95th percentile \|FUSION2 - lidR\| ground difference under ... | hard | pass | 0.681 | < 3 ft |
| groundfilter | /​output-points LAS written | hard | pass | present | present |
| groundfilter | /​output-points holds only ground-classed points | hard | pass | 2 | 2 |
| clipdata | clipdata box: exits with status 0 | hard | pass | 0 | 0 |
| clipdata | box: output readable | hard | pass | ok | ok |
| clipdata | box: kept count matches R | hard | pass | 18079 | 18079 |
| clipdata | clipdata box + height: exits with status 0 | hard | pass | 0 | 0 |
| clipdata | box + height: output readable | hard | pass | ok | ok |
| clipdata | box + height: kept count within 10% of R | hard | pass | 16584 | 16584 |
| clipdata | box + height: every kept point is 2-200 ft above ground (... | hard | pass | 2.00,​ 177.83 | 1.5 to 200.5 ft |
| clipdata | clipdata /​multifile: exits with status 0 | hard | pass | 0 | 0 |
| clipdata | /​multifile: plotA file named from /​field | hard | pass | 1 | 1 |
| clipdata | /​multifile: plotA count within 0.5% of R | hard | pass | 4799 | 4799 |
| clipdata | /​multifile: plotB file named from /​field | hard | pass | 1 | 1 |
| clipdata | /​multifile: plotB count within 0.5% of R | hard | pass | 5505 | 5505 |
| clipdata | clipdata /​outside: exits with status 0 | hard | pass | 0 | 0 |
| clipdata | /​outside: output readable | hard | pass | ok | ok |
| clipdata | /​outside: count equals tile minus in-plot points (within ... | hard | pass | 59982 | 59982 |
| canopymodel | canopymodel: exits with status 0 | hard | pass | 0 | 0 |
| canopymodel | canopymodel /​smooth:3: exits with status 0 | hard | pass | 0 | 0 |
| canopymodel | canopymodel /​slope: exits with status 0 | hard | pass | 0 | 0 |
| canopymodel | canopy height model written | hard | pass | chm.tif | chm.tif |
| canopymodel | CHM: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023956.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| canopymodel | CHM: coordinate system attached | soft | pass | present | present |
| canopymodel | no CHM cell exceeds the tallest point above ground | hard | pass | 0 cells; max 177.83 | 0 cells above 181.06 ft |
| canopymodel | no CHM cell more than 1 ft below ground | hard | pass | -0.17 | >= -1 ft |
| canopymodel | median \|CHM - R reference\| under 0.5 ft (plausible cells) | hard | pass | 0 | < 0.5 ft |
| canopymodel | /​smooth:3 CHM written | hard | pass | chm_​smooth3.tif | chm_​smooth3.tif |
| canopymodel | /​smooth:3 lowers cell-to-cell variance | hard | pass | 1383.46 | < 1599.93 |
| canopymodel | /​slope CHM written | hard | pass | chm_​slope.tif | chm_​slope.tif |
| canopymodel | /​slope changes at least one cell | hard | pass | 2.527 | > 0 |
| returndensity | returndensity: exits with status 0 | hard | pass | 0 | 0 |
| returndensity | density raster written | hard | pass | returndensity.tif | returndensity.tif |
| returndensity | density raster: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023955.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| returndensity | density raster: coordinate system attached | soft | pass | present | present |
| returndensity | density x cell area sums to the point count (within 0.5%) | hard | pass | 70286 | 70286 |
| returndensity | per-cell counts match R exactly | hard | pass | 0 | 0 mismatched cells |
| returndensity | first-return percent within 0-100 | hard | pass | 36.96,​ 88.89 | 0 to 100 |
| returndensity | density band's unit label matches the tile's linear units... | soft | pass | point_​density | no m2 label on a feet-based... |
| densitymetrics | densitymetrics: exits with status 0 | hard | pass | 0 | 0 |
| densitymetrics | stratum raster written | hard | pass | dm_​densitymetrics.tif | dm_​densitymetrics.tif |
| densitymetrics | stratum raster: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023955.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| densitymetrics | stratum raster: coordinate system attached | soft | pass | present | present |
| densitymetrics | per-cell CSV table written | hard | pass | present | present |
| densitymetrics | per cell,​ stratum counts add to total returns | hard | pass | 0 | 0 mismatched cells |
| densitymetrics | total returns across cells equals the point count | hard | pass | 70286 | 70286 |
| densitymetrics | top stratum (> 20 ft) count matches R (within 5 points) | hard | pass | 59719 | 59719 |
| gridmetrics | gridmetrics: exits with status 0 | hard | pass | 0 | 0 |
| gridmetrics | /​output-table path honored | soft | pass | written | written |
| gridmetrics | metrics raster written | hard | pass | TC_​1372_​forest200ft_​gridmet... | TC_​1372_​forest200ft_​gridmet... |
| gridmetrics | metrics raster: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023955.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| gridmetrics | metrics raster: coordinate system attached | soft | pass | present | present |
| gridmetrics | no band is empty in every cell | hard | pass | none | none |
| gridmetrics | grid has the expected rows x columns for the tile | hard | pass | 20x20 | 20x20 |
| gridmetrics | per-cell return counts match R | hard | pass | 0 | 0 mismatched cells |
| gridmetrics | no elev_​max cell exceeds the tallest point above ground | hard | pass | 0 cells; max 177.83 | 0 cells above 181.06 ft |
| gridmetrics | CSV table has one row per raster cell | hard | pass | 400 | 400 |
| gridmetrics | CSV elev_​mean matches the raster band | hard | pass | 0 | all cells equal |
| cloudmetrics | cloudmetrics tile: exits with status 0 | hard | pass | 0 | 0 |
| cloudmetrics | tile metrics CSV written | hard | pass | cloudmetrics_​tile.csv | cloudmetrics_​tile.csv |
| cloudmetrics | tile: total points matches the tile | hard | pass | 70286 | 70286 |
| cloudmetrics | tile: elev_​max within the tallest point above ground | hard | pass | 177.83 | <= 181.06 ft |
| cloudmetrics | tile: canopy points (> 2 ft) match R (within 1%) | hard | pass | 62176 | 62176 |
| cloudmetrics | cloudmetrics /​shape: exits with status 0 | hard | pass | 0 | 0 |
| cloudmetrics | per-plot metrics CSV written | hard | pass | cloudmetrics_​plots.csv | cloudmetrics_​plots.csv |
| cloudmetrics | /​shape: one row per plot,​ labelled from /​field | hard | pass | plotA,​plotB | plotA,​plotB |
| cloudmetrics | /​shape: plotA point count within 0.5% of R | hard | pass | 4799 | 4799 |
| cloudmetrics | /​shape: plotB point count within 0.5% of R | hard | pass | 5505 | 5505 |
| topometrics | topometrics ground DEM: exits with status 0 | hard | pass | 0 | 0 |
| topometrics | topometrics ground DEM raster written | hard | pass | topometrics_​ground.tif | topometrics_​ground.tif |
| topometrics | topometrics ground DEM: output on the input DEM's grid | hard | pass | 1023755.00,​ 1023955.00,​  59... | 1023755.00,​ 1023955.00,​  59... |
| topometrics | topometrics ground DEM: keeps the input's coordinate system | soft | pass | present | present |
| topometrics | topometrics ground DEM: interior cells have values | hard | pass | 0 | 0 |
| topometrics | topometrics ground DEM: median \|slope - terra\| under 0.5 ... | hard | pass | 0 | < 0.5 |
| topometrics | topometrics ground DEM: aspect matches terra (compass or ... | hard | pass | compass 0; math 98.14 | < 5 under one convention |
| topometrics | topometrics ground DEM: aspect reported as a compass bear... | soft | pass | compass bearing | compass bearing |
| topometrics | topometrics external DTM: exits with status 0 | hard | pass | 0 | 0 |
| topometrics | topometrics external DTM raster written | hard | pass | topometrics_​external_​dtm.tif | topometrics_​external_​dtm.tif |
| topometrics | topometrics external DTM: output on the input DEM's grid | hard | pass | 1023705,​ 1024005,​  598825,​ ... | 1023705,​ 1024005,​  598825,​ ... |
| topometrics | topometrics external DTM: keeps the input's coordinate sy... | soft | pass | present | present |
| topometrics | topometrics external DTM: interior cells have values | hard | pass | 0 | 0 |
| topometrics | topometrics external DTM: median \|slope - terra\| under 0.... | hard | pass | 0 | < 0.5 |
| topometrics | topometrics external DTM: aspect matches terra (compass o... | hard | pass | compass 0; math 96.12 | < 5 under one convention |
| topometrics | topometrics external DTM: aspect reported as a compass be... | soft | pass | compass bearing | compass bearing |
| gridsurfacestats | gridsurfacestats: exits with status 0 | hard | pass | 0 | 0 |
| gridsurfacestats | surface stats raster written | hard | pass | gridsurfacestats.tif | gridsurfacestats.tif |
| gridsurfacestats | surface stats raster: raster sits on the tile's real coor... | hard | pass | 1023755.00,​ 1023956.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| gridsurfacestats | surface stats raster: coordinate system attached | soft | pass | present | present |
| gridsurfacestats | surface_​area_​ratio >= 1 everywhere | hard | pass | 1.0001 | >= 1 |
| gridsurfacestats | roughness >= 0 everywhere | hard | pass | 0.1754 | >= 0 |
| gridsurfacestats | volume_​diff tracks the surface - reference difference | hard | pass | height difference x cell area | one of the two |
| gridsurfacestats | volume_​diff includes cell area (a true volume) | soft | pass | volume | height difference x cell area |
| canopymaxima | canopymaxima: exits with status 0 | hard | pass | 0 | 0 |
| canopymaxima | tree tops CSV written | hard | pass | tree_​tops.csv | tree_​tops.csv |
| canopymaxima | at least one tree top found | hard | pass | 82 | > 0 |
| canopymaxima | every tree top inside the tile | hard | pass | 0 | 0 outside |
| canopymaxima | every tree top at least /​minht (2 ft) | hard | pass | 2.31 | >= 2 |
| canopymaxima | no tree top exceeds the tallest point above ground | hard | pass | 0 tops; max 177.83 | 0 above 181.06 ft |
| canopymaxima | X/​Y written without scientific-notation rounding | hard | pass | 0 rows,​ e.g. X = 1023789.50 | 0 rows |
| canopymaxima | each top's height equals the CHM at its X/​Y | hard | pass | 0 | 0 mismatched tops |
| treeseg | treeseg: exits with status 0 | hard | pass | 0 | 0 |
| treeseg | segment raster written | hard | pass | crown_​segments.tif | crown_​segments.tif |
| treeseg | crown table written | hard | pass | crown_​summary.csv | crown_​summary.csv |
| treeseg | segment raster: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023956.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| treeseg | segment raster: coordinate system attached | soft | pass | present | present |
| treeseg | same crowns in the table and the raster | hard | pass | 165 | 165 |
| treeseg | PixelCount matches the segment raster | hard | pass | 0 | 0 mismatched crowns |
| treeseg | MaxHeight matches the CHM within each crown | hard | pass | 0 | 0 mismatched crowns |
| treeseg | no crown exceeds the tallest point above ground | hard | pass | 0 crowns; max 177.83 | 0 above 181.06 ft |
| treeseg | crown area = pixel count x cell area | hard | pass | 0 | 0 mismatched crowns |
| treeseg | crown area column's unit label matches the tile's units (... | soft | pass | CrownArea | no m2 label on a feet-based... |
| pipeline | pipeline: exits with status 0 | hard | pass | 0 | 0 |
| pipeline | run state table written | hard | pass | pipeline_​state.csv | pipeline_​state.csv |
| pipeline | no tile x stage failed | hard | pass | done | no failed status |
| pipeline | four tiles processed | hard | pass | 4 | 4 |
| pipeline | mosaicked canopy model written | hard | pass | canopymodel_​merged.tif | a canopymodel .tif or .vrt |
| pipeline | mosaicked CHM: raster sits on the tile's real coordinates | hard | pass | 1023755.00,​ 1023956.00,​  59... | 1023755.00,​ 1023954.98,​  59... |
| pipeline | mosaicked CHM matches single-run CHM (median diff < 0.5 ft) | hard | pass | 0.217 | < 0.5 ft |
| pipeline | no mosaicked CHM cell exceeds the tallest point above ground | hard | pass | 0 | 0 |
| pipeline | combined tree tops table written | hard | pass | 1 | >= 1 |
| pipeline | no duplicate tree tops across tile seams | hard | pass | 0 | 0 |

: Every automated check on the example run, in the order it ran. {#tbl-all-checks}

