| Check | Result | Observed | Expected |
|:----------------------------------------|:----------|:-------------------------|:-------------------------|
| gridsurfacestats: exits with status 0 | pass | 0 | 0 |
| surface stats raster written | pass | gridsurfacestats.tif | gridsurfacestats.tif |
| surface stats raster: raster sits on the tile's real coordinates | pass | 1023755.00,​ 1023956.00,​  598873.99,​  59907... | 1023755.00,​ 1023954.98,​  598875.00,​  59907... |
| surface stats raster: coordinate system attached | pass | present | present |
| surface_​area_​ratio >= 1 everywhere | pass | 1.0001 | >= 1 |
| roughness >= 0 everywhere | pass | 0.1754 | >= 0 |
| volume_​diff tracks the surface - reference difference | pass | height difference x cell area | one of the two |
| volume_​diff includes cell area (a true volume) | pass | volume | height difference x cell area |

: Automated checks on the example run of `gridsurfacestats`. {#tbl-checks-gridsurfacestats}

