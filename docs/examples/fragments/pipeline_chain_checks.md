| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| pipeline chain,​ 4 threads: exits with status 0 | pass | 0 | 0 |
| pipeline chain,​ 1 thread: exits with status 0 | pass | 0 | 0 |
| chain run state table written | pass | pipeline_​state.csv | pipeline_​state.csv |
| chain accepted: every tile ran canopymodel,​ canopymaxima,​ and treeseg | pass | 27 | 27 |
| canopy model built once per tile | pass | 1 | 1 |
| no tile x stage failed | pass | done | done |
| stitched canopy model written | pass | canopymodel_​merged.tif | canopymodel_​merged.tif |
| joined tree tops table written | pass | canopymaxima_​all.csv | canopymaxima_​all.csv |
| joined crown table written | pass | treeseg_​table_​all.csv | treeseg_​table_​all.csv |
| every crown's MaxHeight is a height in its tile's canopy model | pass | 0 | 0 crowns |
| no crown exceeds the tallest point above ground | pass | 177.83 | <= 181.06 ft |
| no tree top exceeds the tallest point above ground | pass | 177.83 | <= 181.06 ft |
| tree tops table rows are in tile order | pass | 1,​2,​4,​5,​7,​8 | ascending |
| crown table rows are in tile order | pass | 1,​2,​3,​4,​5,​6,​7,​8,​9 | ascending |
| canopymaxima_​all.csv: identical at 4 and 1 threads | pass | e74940ea | e74940ea |
| treeseg_​table_​all.csv: identical at 4 and 1 threads | pass | 62bc0fd7 | 62bc0fd7 |

: Automated checks on the example run of the `pipeline` chain `canopymodel,canopymaxima,treeseg`. {#tbl-checks-pipeline-chain}

