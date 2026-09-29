| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| cloudmetrics tile: exits with status 0 | pass | 0 | 0 |
| tile metrics CSV written | pass | cloudmetrics_​tile.csv | cloudmetrics_​tile.csv |
| tile: total points matches the tile | pass | 70286 | 70286 |
| tile: elev_​max within the tallest point above ground | pass | 177.83 | <= 181.06 ft |
| tile: canopy points (> 2 ft) match R (within 1%) | pass | 62176 | 62176 |
| cloudmetrics /​shape: exits with status 0 | pass | 0 | 0 |
| per-plot metrics CSV written | pass | cloudmetrics_​plots.csv | cloudmetrics_​plots.csv |
| /​shape: one row per plot,​ labelled from /​field | pass | plotA,​plotB | plotA,​plotB |
| /​shape: plotA point count within 0.5% of R | pass | 4799 | 4799 |
| /​shape: plotB point count within 0.5% of R | pass | 5505 | 5505 |

: Automated checks on the example run of `cloudmetrics`. {#tbl-checks-cloudmetrics}

