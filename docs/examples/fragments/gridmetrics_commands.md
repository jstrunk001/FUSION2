```bat
REM gridmetrics
gridmetrics data/raw/lidar/TC_1372_forest200ft.laz out/ground_dem.tif ^
    /cellsize:10 ^
    /outdir:out/gridmetrics ^
    /strata:0.5,2,5,10,20 ^
    /strataraster ^
    /surfstats ^
    /output-table:out/gridmetrics/gridmetrics_table.csv
```

