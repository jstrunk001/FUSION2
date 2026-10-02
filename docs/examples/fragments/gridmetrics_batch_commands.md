```bat
REM gridmetrics batch
gridmetrics out/gridmetrics_batch_input out/ground_dem.tif ^
    /outdir:out/gridmetrics_batch ^
    /tilesize:100,100 ^
    /buffer:20 ^
    /threads:2 ^
    /cellsize:10 ^
    /strata:0.5,2,5,10,20 ^
    /strataraster ^
    /output-table:out/gridmetrics_batch/gridmetrics_batch_table.csv ^
    /merge

REM gridmetrics batch, /extent off the data
gridmetrics out/gridmetrics_batch_input ^
    /outdir:out/gridmetrics_batch_off ^
    /extent:0,0,1000,1000 ^
    /cellsize:10
```

