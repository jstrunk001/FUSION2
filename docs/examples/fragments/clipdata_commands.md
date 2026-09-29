```bat
REM clipdata box
clipdata data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/clipdata/box.las ^
    /extent:1023780,598900,1023880,599000

REM clipdata box + height
clipdata data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/clipdata/box_height_2_200.las ^
    /extent:1023780,598900,1023880,599000 ^
    /ground:out/ground_dem.tif ^
    /minz:2 ^
    /maxz:200

REM clipdata /multifile
clipdata data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/clipdata/per_plot ^
    /shape:out/clipdata/example_plots.shp ^
    /multifile ^
    /field:plot_id

REM clipdata /outside
clipdata data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/clipdata/outside_plots.las ^
    /shape:out/clipdata/example_plots.shp ^
    /outside
```

