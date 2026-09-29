```bat
REM canopymodel
canopymodel data/raw/lidar/TC_1372_forest200ft.laz ^
    /ground:out/ground_dem.tif ^
    /cellsize:3 ^
    /output:out/chm.tif

REM canopymodel /smooth:3
canopymodel data/raw/lidar/TC_1372_forest200ft.laz ^
    /ground:out/ground_dem.tif ^
    /cellsize:3 ^
    /output:out/chm_smooth3.tif ^
    /smooth:3

REM canopymodel /slope
canopymodel data/raw/lidar/TC_1372_forest200ft.laz ^
    /ground:out/ground_dem.tif ^
    /cellsize:3 ^
    /output:out/chm_slope.tif ^
    /slope
```

