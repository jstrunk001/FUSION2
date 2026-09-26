# Changelog

Notable changes to the FUSION2 tool suite, one section per published
version. Format loosely follows [Keep a Changelog](https://keepachangelog.com/).
Versions match the `VERSION` set in `CMakeLists.txt`'s `project()` call,
which `build.ps1` reads to build each release's tag and title -- see the
README's "Distributing built tools" section.

## [Unreleased]

- `catalog /density` now writes its point-count GeoTIFF on the input's
  real coordinates, with the input's coordinate system attached. It
  previously read both from the point reader after closing it, which
  reset them, so the raster landed at (0, 0) with no coordinate system.
  Points lying exactly on the input's maximum-X or minimum-Y edge are now
  counted in the last column or row instead of being dropped. The raster
  code moved to `fusion::lidar::WritePointCountRaster`, with tests.

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
