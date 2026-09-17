# Changelog

Notable changes to the FUSION Update tool suite, one section per published
version. Format loosely follows [Keep a Changelog](https://keepachangelog.com/).
Versions match the `VERSION` set in `CMakeLists.txt`'s `project()` call,
which `build.ps1` reads to build each release's tag and title -- see the
README's "Distributing built tools" section.

## [Unreleased]

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
