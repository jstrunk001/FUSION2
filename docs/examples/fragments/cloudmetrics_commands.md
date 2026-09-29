```bat
REM cloudmetrics tile
cloudmetrics data/raw/lidar/TC_1372_forest200ft.laz ^
    /ground:out/ground_dem.tif ^
    /output:out/cloudmetrics_tile.csv ^
    /minht:2 ^
    /strata:0.5,2,5,10,20

REM cloudmetrics /shape
cloudmetrics data/raw/lidar/TC_1372_forest200ft.laz ^
    /ground:out/ground_dem.tif ^
    /output:out/cloudmetrics_plots.csv ^
    /shape:out/clipdata/example_plots.shp ^
    /field:plot_id
```

