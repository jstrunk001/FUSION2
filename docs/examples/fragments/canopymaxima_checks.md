| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| canopymaxima: exits with status 0 | pass | 0 | 0 |
| tree tops CSV written | pass | tree_​tops.csv | tree_​tops.csv |
| at least one tree top found | pass | 82 | > 0 |
| every tree top inside the tile | pass | 0 | 0 outside |
| every tree top at least /​minht (2 ft) | pass | 2.31 | >= 2 |
| no tree top exceeds the tallest point above ground | pass | 0 tops; max 177.83 | 0 above 181.06 ft |
| X/​Y written without scientific-notation rounding | pass | 0 rows,​ e.g. X = 1023789.50 | 0 rows |
| each top's height equals the CHM at its X/​Y | pass | 0 | 0 mismatched tops |

: Automated checks on the example run of `canopymaxima`. {#tbl-checks-canopymaxima}

