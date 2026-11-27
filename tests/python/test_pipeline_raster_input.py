"""Checks that pipeline can start from a raster (a folder of tiles) instead of a point cloud.

Builds a 200 x 200 pixel canopy height model with isolated 10 m tall trees,
some of them close to the output tile edges, in four 100 x 100 pixel tiles.
Runs pipeline on the folder with the stages canopymaxima,treeseg and with
small output tiles, then runs canopymaxima on the whole raster, and confirms
that the tree tops found by pipeline are the same set of trees. Also confirms
that pipeline refuses a raster input with a stage that needs a point cloud.

Usage:
    python test_pipeline_raster_input.py <folder holding the built .exe files> <work folder>

The work folder holds only this test's inputs and outputs. It is created if
it does not exist; nothing in it is deleted by this script.
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import rasterio
from rasterio.transform import from_origin

ORIGIN_X = 500000.0
ORIGIN_Y = 5000200.0
TILE_PIXELS = 100
# (row, column) of each tree top, chosen so some lie within 3 pixels of the 60 m output tile edges
TREE_PIXELS = [(10, 10), (58, 58), (62, 120), (100, 100), (141, 30), (180, 180), (30, 61)]


def make_heights():
    heights = np.zeros((200, 200), dtype="float32")
    for tree_row, tree_col in TREE_PIXELS:
        heights[tree_row - 2:tree_row + 3, tree_col - 2:tree_col + 3] = 6.0
        heights[tree_row, tree_col] = 10.0
    return heights


def write_tiles(tile_dir, heights):
    tile_dir.mkdir(parents=True, exist_ok=True)
    for tile_row in range(2):
        for tile_col in range(2):
            window = heights[tile_row * TILE_PIXELS:(tile_row + 1) * TILE_PIXELS,
                             tile_col * TILE_PIXELS:(tile_col + 1) * TILE_PIXELS]
            tile_x = ORIGIN_X + tile_col * TILE_PIXELS
            tile_y = ORIGIN_Y - tile_row * TILE_PIXELS
            with rasterio.open(
                tile_dir / f"chm_r{tile_row}_c{tile_col}.tif", "w", driver="GTiff",
                height=TILE_PIXELS, width=TILE_PIXELS, count=1, dtype="float32",
                crs="EPSG:32610", transform=from_origin(tile_x, tile_y, 1.0, 1.0), nodata=-9999.0,
            ) as dst:
                dst.write(window, 1)
    full_path = tile_dir.parent / "chm_full.tif"
    with rasterio.open(
        full_path, "w", driver="GTiff", height=200, width=200, count=1, dtype="float32",
        crs="EPSG:32610", transform=from_origin(ORIGIN_X, ORIGIN_Y, 1.0, 1.0), nodata=-9999.0,
    ) as dst:
        dst.write(heights, 1)
    return full_path


def read_tree_positions(csv_path):
    """Return the sorted (x, y) positions of the tree tops in a canopymaxima table, to 0.01."""
    positions = []
    for row in csv_path.read_text().strip().splitlines()[1:]:
        fields = row.split(",")
        positions.append((round(float(fields[1]), 2), round(float(fields[2]), 2)))
    return sorted(positions)


def main():
    exe_dir = Path(sys.argv[1])
    work = Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    heights = make_heights()
    full_path = write_tiles(work / "tiles", heights)

    # 1. tree tops from the whole raster
    whole_csv = work / "tops_whole.csv"
    subprocess.run([str(exe_dir / "canopymaxima.exe"), str(full_path), f"/output:{whole_csv}", "/minht:2"],
                   check=True, capture_output=True)
    whole_positions = read_tree_positions(whole_csv)
    assert len(whole_positions) == len(TREE_PIXELS), f"whole raster found {len(whole_positions)} trees"

    # 2. pipeline over the folder of tiles, with output tiles that cut across the source tiles
    out_dir = work / "pipeline_out"
    result = subprocess.run(
        [str(exe_dir / "pipeline.exe"), f"/input:{work / 'tiles'}", f"/output:{out_dir}",
         "/pipeline:canopymaxima,treeseg", "/tilesize:60,60", "/buffer:10", "/minht:2", "/threads:2"],
        capture_output=True, text=True)
    assert result.returncode == 0, f"pipeline failed:\n{result.stdout}\n{result.stderr}"
    tops_files = list(out_dir.glob("canopymaxima*.csv"))
    assert tops_files, f"no tree top table in {out_dir}: {sorted(p.name for p in out_dir.iterdir())}"
    pipeline_positions = read_tree_positions(tops_files[0])

    # 3. the same trees, each counted once
    assert pipeline_positions == whole_positions, (
        f"tree tops differ: pipeline {len(pipeline_positions)} vs whole raster {len(whole_positions)}")
    print(f"PASS  pipeline on a raster folder finds the same {len(whole_positions)} trees as the whole raster")

    # 4. the crown raster mosaic exists and covers the input
    segments_vrt = out_dir / "treeseg.vrt"
    assert segments_vrt.exists(), "treeseg.vrt was not written"
    with rasterio.open(segments_vrt) as segments:
        assert (segments.width, segments.height) == (200, 200), f"treeseg mosaic is {segments.width}x{segments.height}"
    print("PASS  treeseg mosaic covers the 200 x 200 pixel input")

    # 5. a stage that needs a point cloud is refused for a raster input
    refused = subprocess.run(
        [str(exe_dir / "pipeline.exe"), f"/input:{work / 'tiles'}", f"/output:{work / 'refused_out'}",
         "/pipeline:canopymodel"], capture_output=True, text=True)
    assert refused.returncode != 0, "pipeline accepted a point cloud stage on a raster input"
    print("PASS  pipeline refuses canopymodel on a raster input")
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
