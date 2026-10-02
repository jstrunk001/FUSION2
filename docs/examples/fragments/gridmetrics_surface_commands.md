```bat
REM gridmetrics DSM raster
gridmetrics data/raw/dsm_source_examples/site_grays_harbor/naip3d_dsm.tif data/raw/lidar_dtm_examples/site_grays_harbor/usgs_lidar_dtm.tif ^
    /cellsize:20 ^
    /minht:2 ^
    /outdir:out/gridmetrics_dsm ^
    /output-table:out/gridmetrics_dsm/naip3d_dsm_table.csv ^
    /profile

REM gridmetrics CHM raster
gridmetrics data/raw/dsm_source_examples/site_grays_harbor/meta_chm.tif ^
    /cellsize:20 ^
    /minht:2 ^
    /outdir:out/gridmetrics_dsm ^
    /output-table:out/gridmetrics_dsm/meta_chm_table.csv
```

