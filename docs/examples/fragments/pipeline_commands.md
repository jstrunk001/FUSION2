```bat
REM pipeline
pipeline ^
    /pipeline:groundfilter,canopymodel,canopymaxima ^
    /input:data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/pipeline ^
    /extent:1023755,598875,1023954.98,599074.99 ^
    /tilesize:100,100 ^
    /buffer:20 ^
    /cellsize:3 ^
    /minht:2 ^
    /merge
```

