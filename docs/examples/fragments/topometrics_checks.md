| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| topometrics ground DEM: exits with status 0 | pass | 0 | 0 |
| topometrics ground DEM raster written | pass | topometrics_​ground.tif | topometrics_​ground.tif |
| topometrics ground DEM: output on the input DEM's grid | pass | 1023755.00,​ 1023955.00,​  598874.99,​  59907... | 1023755.00,​ 1023955.00,​  598874.99,​  59907... |
| topometrics ground DEM: keeps the input's coordinate system | pass | present | present |
| topometrics ground DEM: interior cells have values | pass | 0 | 0 |
| topometrics ground DEM: median \|slope - terra\| under 0.5 degrees | pass | 0 | < 0.5 |
| topometrics ground DEM: aspect matches terra (compass or math conve... | pass | compass 0; math 98.14 | < 5 under one convention |
| topometrics ground DEM: aspect reported as a compass bearing (0 = n... | pass | compass bearing | compass bearing |
| topometrics external DTM: exits with status 0 | pass | 0 | 0 |
| topometrics external DTM raster written | pass | topometrics_​external_​dtm.tif | topometrics_​external_​dtm.tif |
| topometrics external DTM: output on the input DEM's grid | pass | 1023705,​ 1024005,​  598825,​  599125 | 1023705,​ 1024005,​  598825,​  599125 |
| topometrics external DTM: keeps the input's coordinate system | pass | present | present |
| topometrics external DTM: interior cells have values | pass | 0 | 0 |
| topometrics external DTM: median \|slope - terra\| under 0.5 degrees | pass | 0 | < 0.5 |
| topometrics external DTM: aspect matches terra (compass or math con... | pass | compass 0; math 96.12 | < 5 under one convention |
| topometrics external DTM: aspect reported as a compass bearing (0 =... | pass | compass bearing | compass bearing |

: Automated checks on the example run of `topometrics`. {#tbl-checks-topometrics}

