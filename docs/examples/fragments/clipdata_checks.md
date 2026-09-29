| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| clipdata box: exits with status 0 | pass | 0 | 0 |
| box: output readable | pass | ok | ok |
| box: kept count matches R | pass | 18079 | 18079 |
| clipdata box + height: exits with status 0 | pass | 0 | 0 |
| box + height: output readable | pass | ok | ok |
| box + height: kept count within 10% of R | pass | 16584 | 16584 |
| box + height: every kept point is 2-200 ft above ground (+/​- 0.5 ft) | pass | 2.00,​ 177.83 | 1.5 to 200.5 ft |
| clipdata /​multifile: exits with status 0 | pass | 0 | 0 |
| /​multifile: plotA file named from /​field | pass | 1 | 1 |
| /​multifile: plotA count within 0.5% of R | pass | 4799 | 4799 |
| /​multifile: plotB file named from /​field | pass | 1 | 1 |
| /​multifile: plotB count within 0.5% of R | pass | 5505 | 5505 |
| clipdata /​outside: exits with status 0 | pass | 0 | 0 |
| /​outside: output readable | pass | ok | ok |
| /​outside: count equals tile minus in-plot points (within 0.5%) | pass | 59982 | 59982 |

: Automated checks on the example run of `clipdata`. {#tbl-checks-clipdata}

