| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| thindata: exits with status 0 | pass | 0 | 0 |
| output readable | pass | ok | ok |
| no cell keeps more than one point | pass | 0 | 0 duplicate cells |
| kept count equals occupied cells | pass | 10092 | 10092 |
| every kept point exists in the tile | pass | 0 | 0 new points |

: Automated checks on the example run of `thindata`. {#tbl-checks-thindata}

