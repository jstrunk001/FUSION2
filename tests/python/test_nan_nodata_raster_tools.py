"""Checks that the raster-input tools treat NaN missing pixels as missing.

Writes a small canopy height model whose missing pixels are NaN and whose
declared NoData value is NaN, runs canopymaxima and treeseg on it, and
confirms that no tree top or crown has a NaN height.

Usage:
    python test_nan_nodata_raster_tools.py <folder holding the built .exe files> <work folder>
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import rasterio
from rasterio.transform import from_origin


def write_chm(path, missing_as_nan=True):
    heights = np.full((60, 60), 0.0, dtype="float32")
    # three isolated crowns, 10 m tall, on a 1 m grid
    for crown_row, crown_col in [(10, 10), (30, 30), (45, 15)]:
        heights[crown_row - 2:crown_row + 3, crown_col - 2:crown_col + 3] = 6.0
        heights[crown_row, crown_col] = 10.0
    # a block of missing pixels
    heights[50:58, 40:58] = np.nan if missing_as_nan else 0.0
    with rasterio.open(
        path, "w", driver="GTiff", height=60, width=60, count=1, dtype="float32",
        crs="EPSG:32610", transform=from_origin(500000.0, 5000060.0, 1.0, 1.0), nodata=np.nan if missing_as_nan else None,
    ) as dst:
        dst.write(heights, 1)


def main():
    exe_dir = Path(sys.argv[1])
    work = Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    chm_path = work / "chm_nan.tif"
    write_chm(chm_path)

    # canopymaxima: three real tree tops, none with a NaN height
    tops_path = work / "tops.csv"
    subprocess.run([str(exe_dir / "canopymaxima.exe"), str(chm_path), f"/output:{tops_path}", "/minht:2"],
                   check=True, capture_output=True)
    lines = tops_path.read_text().strip().splitlines()[1:]
    heights = [row.split(",")[3] for row in lines]
    assert len(lines) == 3, f"expected 3 tree tops, got {len(lines)}"
    assert all(h.lower() != "nan" for h in heights), f"a tree top has a NaN height: {heights}"
    print("PASS  canopymaxima: 3 tree tops, none with a NaN height")

    # treeseg: no NaN values, and the same crowns as when the missing block is 0 m
    chm_zero_path = work / "chm_zero.tif"
    write_chm(chm_zero_path, missing_as_nan=False)
    crown_counts = {}
    for label, raster_path in [("nan", chm_path), ("zero", chm_zero_path)]:
        crowns_path = work / f"crowns_{label}.csv"
        subprocess.run([str(exe_dir / "treeseg.exe"), str(raster_path),
                        f"/output-raster:{work / ('segments_' + label + '.tif')}",
                        f"/output-table:{crowns_path}", "/minht:2"],
                       check=True, capture_output=True)
        crown_text = crowns_path.read_text().lower()
        assert "nan" not in crown_text, f"treeseg wrote a NaN value ({label})"
        crown_counts[label] = len(crown_text.strip().splitlines()) - 1
    assert crown_counts["nan"] == crown_counts["zero"], f"crown counts differ: {crown_counts}"
    print(f"PASS  treeseg: {crown_counts['nan']} crowns, same as with the missing block set to 0, no NaN values")
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
