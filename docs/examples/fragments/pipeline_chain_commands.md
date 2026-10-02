```bat
REM pipeline chain, 4 threads
pipeline ^
    /pipeline:canopymodel,canopymaxima,treeseg ^
    /input:data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/pipeline_chain_t4 ^
    /ground:out/ground_dem.tif ^
    /tilesize:100,100 ^
    /buffer:20 ^
    /cellsize:3 ^
    /minht:2 ^
    /threads:4 ^
    /merge

REM pipeline chain, 1 thread
pipeline ^
    /pipeline:canopymodel,canopymaxima,treeseg ^
    /input:data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/pipeline_chain_t1 ^
    /ground:out/ground_dem.tif ^
    /tilesize:100,100 ^
    /buffer:20 ^
    /cellsize:3 ^
    /minht:2 ^
    /threads:1 ^
    /merge
```

