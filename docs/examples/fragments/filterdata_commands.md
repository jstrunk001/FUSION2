```bat
REM filterdata first_returns
filterdata data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/filterdata/first_returns.las ^
    /return:1

REM filterdata ground_class
filterdata data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/filterdata/ground_class.las ^
    /class:2

REM filterdata z_band
filterdata data/raw/lidar/TC_1372_forest200ft.laz ^
    /output:out/filterdata/z_band.las ^
    /minz:253 ^
    /maxz:273
```

