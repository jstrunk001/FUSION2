```bat
REM topometrics ground DEM
topometrics out/ground_dem.tif ^
    / ^
    /output:out/topometrics_ground.tif

REM topometrics external DTM
topometrics data/raw/lidar_dtm_examples/site_tc_1372_forest/usgs_3dep_dtm.tif ^
    / ^
    /output:out/topometrics_external_dtm.tif
```

