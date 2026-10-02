
Notable changes to the FUSION2 tool suite, one section per published
version. Format loosely follows [Keep a Changelog](https://keepachangelog.com/).
Versions match the `VERSION` set in `CMakeLists.txt`'s `project()` call,
which `build.ps1` reads to build each release's tag and title -- see the
README's "Distributing built tools" section.

## [0.0.3] - 2026-10-02

- `gridmetrics` accepts a surface raster (a photogrammetric surface model
  or a canopy height model, as GeoTIFF or another GDAL raster) in place of
  a point cloud. Each pixel counts as one return at its centre, with the
  ground raster subtracted when one is given.
- `gridmetrics /profile` prints how long each part of a single-file run
  took: reading the input, assigning points to cells, computing metrics,
  and writing outputs.
- `gridmetrics` batch mode and `pipeline` no longer default to a fixed
  (0,0)-(5000,5000) area when `/extent` is left out. They use the input
  files' combined extent, snapped outward to the cell size, and stop with
  an error when an `/extent` overlaps no input file. Data elsewhere
  previously produced a mosaic of empty tiles and a success message.
- `gridmetrics` batch mode applies a ground raster given as the second
  positional argument. It previously read the ground only from
  `/ground:`, so heights stayed raw elevations. A ground raster that
  cannot be opened now stops the run.
- `gridmetrics` batch mode counts each point in one tile only. Points in a
  tile's buffer up to one cell left of or above it were also counted by
  that tile, adding 3.8% of returns along every seam on a 17-million-point
  test tile.
- `gridmetrics` batch mode writes `/strataraster` bands, as single-file
  mode does, and warns when `/strata` or `/intstrata` is given with no
  raster or table output to hold them.
- Virtual rasters built by `gridmetrics` batch mode and `pipeline`, and
  GeoTIFFs merged from them, carry the tile rasters' band names instead
  of `project_gridmetrics_1`, `_2`, and so on.
- `pipeline` accepts a raster-reading stage after a table stage, as in
  `canopymodel,canopymaxima,treeseg`, and feeds it the most recent
  raster in the chain, so the canopy model is built once for both.
- `pipeline` joins each table stage's per-tile tables in tile order
  rather than the order tiles finished, so repeated runs and runs at
  different `/threads` give identical tables.
- `pipeline` starts its child tools without a console window when their
  output goes to a log file, so a hidden or scheduled run no longer opens
  a window per tile and stage.
- `gridmetrics` batch mode and `pipeline` read each plain LAS/LAZ input
  file once per block of neighbouring processing tiles, instead of once
  for every tile it overlaps. A 2,048 m file under 1,000 m tiles was
  previously decompressed nine times. Each file's points are copied into
  temporary per-tile files (`_tile_points` under the output folder, or
  under `_processing` for `pipeline`), which are removed as each block
  finishes. COPC files still seek to the parts each tile needs.
- The user manual has worked examples, with automated checks, for
  `gridmetrics` on surface rasters, `gridmetrics` batch mode, and the
  `canopymodel,canopymaxima,treeseg` chain.
- `catalog /density` now writes its point-count GeoTIFF on the input's
  real coordinates, with the input's coordinate system attached. It
  previously read both from the point reader after closing it, which
  reset them, so the raster landed at (0, 0) with no coordinate system.
  Points lying exactly on the input's maximum-X or minimum-Y edge are now
  counted in the last column or row instead of being dropped. The raster
  code moved to `fusion::lidar::WritePointCountRaster`, with tests.
- Options written as `--name=value` now work. The argument parser kept
  one of the two dashes in the option name, so every tool ignored
  `--output=...`, `--cellsize=...`, and the like, used its defaults, and
  still reported success. `/name:value` and `-name value` were
  unaffected.
- Help text and console messages no longer label heights, cell sizes, and
  voxel sizes as meters; the tools use the input's own units.
  `pipeline`'s description names the FUSION2 tools.
- The PDF manual is now the FUSION2 User Manual (`FUSION2_Manual.pdf`), a
  Quarto book covering installation, a quick start, the conventions every
  tool shares, a worked example for each of the 15 tools (commands,
  options, and figures from one example tile), the option reference with
  each tool's built-in help, metric definitions, the unit tests, and the
  automated checks on the example tile. It replaces
  `FUSION_Documentation.pdf`, which held only the overview page.
- `build.ps1` stops if compilation or any unit test fails, and saves the
  unit-test results and each tool's help text for the manual.

## [0.0.2] - 2026-09-26

- Ground-height lookups (`GDALRaster::GetElevation`, used by every tool that
  reads a ground DEM) now give a point up to one cell past the right or
  bottom edge of the DEM the edge value, as they already did past the left
  and top edges. Such points previously got no ground height.
- Added tests for ground-height lookups near raster edges and nodata cells,
  surface-statistics volume units and patchy grids, and the `pipeline`
  stage list's `densitymetrics` entry.
- `groundfilter` now classifies ground with the Kraus & Pfeifer iterative
  robust filter (legacy FUSION's `/gparam`, `/wparam`, `/aparam`,
  `/bparam`, `/iterations`, `/tolerance`, plus `/filtercell`,
  `/coarsecell`, `/coarsecut`) and fills DEM cells that hold no ground
  point. It previously took the lowest return in each cell, which put
  canopy into the DEM wherever a cell had no ground return and left empty
  cells as nodata. `/output-points` now writes the points the filter
  classified as ground (as class 2) rather than points already classed 2
  in the input.
- LAZ output in LAS 1.4 point formats 6-10 was corrupt (readers decoded
  only the first few dozen points); uncompressed output and LAS 1.2
  formats were unaffected.

- Renamed the project from "FUSION Update" (`fusion_update`) to FUSION2,
  matching the GitHub repository name: CMake project name, `vcpkg.json`
  name, LAS header generating-software field, docs, and source headers.
  `build.ps1` now builds under `%LOCALAPPDATA%\FUSION2_build` and names
  bundles `FUSION2_tools_v<version>.zip`. `speed_benchmark.qmd` records
  the new tools as `fusion2`; `fusion_evaluation_report.qmd` still reads
  earlier results labeled `fusion_update`.
- Point cloud outputs (`filterdata`, `thindata`, `clipdata`,
  `groundfilter /output-points`, `pipeline` tile clips) now keep the
  input's header global encoding bits (GPS time type; WKT flag) and its
  GeoTIFF-key coordinate system VLRs. Without the WKT flag, lidR/rlas
  treated outputs as having no coordinate system; files that stored
  their coordinate system as GeoTIFF keys lost it entirely.
- `groundfilter /output-points` now carries the input's coordinate system
  (its header was previously built field by field and omitted it).
- Build links under Rtools 4.5: nghttp2, psl, brotli, and deflate are added
  to the static link group when the toolchain provides them.
- `build_gdal_minimal.ps1` replaces a vendored `proj.db` that no longer
  matches the newest Rtools' PROJ, instead of keeping a stale copy.
- Added a `gridmetrics /intstrata` output (CSV columns and raster bands),
  opt-in `/output-table` (CSV or SQLite) and `/noraster` switches across
  the raster tools, and `clipdata /shape`, `/multifile`, `/outside`.

## [0.0.1] - 2026-09-09

First draft release of the tool bundle as a GitHub Release asset
(`tools-v0.0.1-20260909-124109`).

- Added a shared `PointFilter` module (elevation, return-number,
  classification, and scan-angle filtering) used across the point cloud
  tools.
- Added a lightweight, assert-based test harness under `tests/cpp/`,
  registered with CTest -- no GoogleTest/Catch2 dependency.
- Corrected the project version (previously `5.0.0`, a placeholder) so
  release tags and titles reflect this being the first draft release.
- Linked the `FUSION2-examples` companion repository from the README.
