| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| filterdata first_​returns: exits with status 0 | pass | 0 | 0 |
| first_​returns: output readable | pass | ok | ok |
| first_​returns: kept count matches R | pass | 39120 | 39120 |
| first_​returns: every kept point satisfies the filter | pass | 0 | 0 violations |
| first_​returns: coordinate system kept | pass | present | present |
| first_​returns: withheld points dropped | pass | 0 | 0 |
| filterdata ground_​class: exits with status 0 | pass | 0 | 0 |
| ground_​class: output readable | pass | ok | ok |
| ground_​class: kept count matches R | pass | 3672 | 3672 |
| ground_​class: every kept point satisfies the filter | pass | 0 | 0 violations |
| ground_​class: coordinate system kept | pass | present | present |
| ground_​class: withheld points dropped | pass | 0 | 0 |
| filterdata z_​band: exits with status 0 | pass | 0 | 0 |
| z_​band: output readable | pass | ok | ok |
| z_​band: kept count matches R | pass | 11893 | 11893 |
| z_​band: every kept point satisfies the filter | pass | 0 | 0 violations |
| z_​band: coordinate system kept | pass | present | present |
| z_​band: withheld points dropped | pass | 0 | 0 |

: Automated checks on the example run of `filterdata`. {#tbl-checks-filterdata}

