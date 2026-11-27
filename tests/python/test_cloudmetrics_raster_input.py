"""Checks cloudmetrics on a raster input, for the whole raster and for /shape polygons.

Builds a 200 x 200 pixel canopy height model with known heights and missing
pixels, writes two rectangular polygons as a shapefile, runs cloudmetrics on
the raster, and compares the point counts, canopy cover, and mean canopy
height with a direct numpy calculation. Also runs a folder of raster tiles,
and /nodata-as-zero.

Usage:
    python test_cloudmetrics_raster_input.py <path to cloudmetrics.exe> <work folder>

The work folder holds only this test's inputs and outputs. It is created if
it does not exist; nothing in it is deleted by this script.
"""
import csv
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
import rasterio
from rasterio.transform import from_origin

ORIGIN_X = 500000.0
ORIGIN_Y = 5000200.0
MIN_HEIGHT_M = 2.0
TILE_PIXELS = 100

# polygons as (min x, min y, max x, max y); edges sit on whole units so no pixel center falls on an edge
POLYGONS = {"north_west": (10.0, 110.0, 90.0, 190.0), "south_east": (100.0, 10.0, 190.0, 100.0)}


def make_heights():
    rows, cols = np.mgrid[0:200, 0:200]
    heights = ((cols * 7 + rows * 13) % 41).astype("float32")
    heights[cols % 9 == 0] = 0.5
    heights[10:30, 10:30] = np.nan
    heights[150:170, 150:170] = -9999.0
    return heights


def write_rasters(work, heights):
    tile_dir = work / "tiles"
    tile_dir.mkdir(parents=True, exist_ok=True)
    for tile_row in range(2):
        for tile_col in range(2):
            window = heights[tile_row * TILE_PIXELS:(tile_row + 1) * TILE_PIXELS,
                             tile_col * TILE_PIXELS:(tile_col + 1) * TILE_PIXELS]
            with rasterio.open(
                tile_dir / f"chm_r{tile_row}_c{tile_col}.tif", "w", driver="GTiff",
                height=TILE_PIXELS, width=TILE_PIXELS, count=1, dtype="float32", crs="EPSG:32610",
                transform=from_origin(ORIGIN_X + tile_col * TILE_PIXELS, ORIGIN_Y - tile_row * TILE_PIXELS, 1.0, 1.0),
                nodata=-9999.0,
            ) as dst:
                dst.write(window, 1)
    full_path = work / "chm_full.tif"
    with rasterio.open(
        full_path, "w", driver="GTiff", height=200, width=200, count=1, dtype="float32",
        crs="EPSG:32610", transform=from_origin(ORIGIN_X, ORIGIN_Y, 1.0, 1.0), nodata=-9999.0,
    ) as dst:
        dst.write(heights, 1)
    return tile_dir, full_path


def write_polygon_shapefile(path):
    """Write the rectangles in POLYGONS as a polygon shapefile with a 'name' text field."""
    boxes = [(ORIGIN_X + x0, ORIGIN_Y - 200.0 + y0, ORIGIN_X + x1, ORIGIN_Y - 200.0 + y1)
             for x0, y0, x1, y1 in POLYGONS.values()]
    names = list(POLYGONS.keys())

    records = b""
    offsets = []
    position = 100  # the main file header is 100 bytes
    for box_i, (x0, y0, x1, y1) in enumerate(boxes):
        # outer ring runs clockwise, closed
        ring = [(x0, y0), (x0, y1), (x1, y1), (x1, y0), (x0, y0)]
        content = struct.pack("<i4d2i", 5, x0, y0, x1, y1, 1, 5) + struct.pack("<i", 0)
        for ring_x, ring_y in ring:
            content += struct.pack("<2d", ring_x, ring_y)
        record = struct.pack(">2i", box_i + 1, len(content) // 2) + content
        offsets.append((position, len(content) // 2))
        records += record
        position += len(record)

    all_x = [b[0] for b in boxes] + [b[2] for b in boxes]
    all_y = [b[1] for b in boxes] + [b[3] for b in boxes]
    bounds = struct.pack("<8d", min(all_x), min(all_y), max(all_x), max(all_y), 0, 0, 0, 0)

    def header(total_bytes):
        return struct.pack(">7i", 9994, 0, 0, 0, 0, 0, total_bytes // 2) + struct.pack("<2i", 1000, 5) + bounds

    path.with_suffix(".shp").write_bytes(header(100 + len(records)) + records)
    index = b"".join(struct.pack(">2i", off // 2, length) for off, length in offsets)
    path.with_suffix(".shx").write_bytes(header(100 + len(index)) + index)

    # attribute table: one 20 character text field
    field_length = 20
    record_length = 1 + field_length
    dbf = struct.pack("<4BIHH20x", 3, 126, 1, 1, len(names), 32 + 32 + 1, record_length)
    dbf += b"name".ljust(11, b"\x00") + b"C" + b"\x00" * 4 + struct.pack("<2B", field_length, 0) + b"\x00" * 14
    dbf += b"\x0d"
    for name in names:
        dbf += b" " + name.encode().ljust(field_length)
    dbf += b"\x1a"
    path.with_suffix(".dbf").write_bytes(dbf)
    path.with_suffix(".prj").write_text(
        'PROJCS["WGS 84 / UTM zone 10N",GEOGCS["WGS 84",DATUM["WGS_1984",SPHEROID["WGS 84",6378137,298.257223563]],'
        'PRIMEM["Greenwich",0],UNIT["degree",0.0174532925199433]],PROJECTION["Transverse_Mercator"],'
        'PARAMETER["latitude_of_origin",0],PARAMETER["central_meridian",-123],PARAMETER["scale_factor",0.9996],'
        'PARAMETER["false_easting",500000],PARAMETER["false_northing",0],UNIT["metre",1]]')


def run(exe, args):
    command = [str(exe)] + [str(a) for a in args]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"cloudmetrics failed ({result.returncode}): {' '.join(command)}\n{result.stdout}\n{result.stderr}")
    return result.stdout


def read_rows(csv_path):
    with open(csv_path, newline="") as handle:
        return list(csv.DictReader(handle))


def expected_stats(heights, box, nodata_as_zero):
    """Point count, canopy point count, cover percent, and mean canopy height of the
    pixels whose centers are inside box (x0, y0, x1, y1 measured from the raster's lower-left)."""
    values = heights.copy()
    values[values == -9999.0] = np.nan
    if nodata_as_zero:
        values = np.where(np.isnan(values), 0.0, values)
    x0, y0, x1, y1 = box
    centers_x = np.arange(200) + 0.5
    centers_y = 200.0 - (np.arange(200) + 0.5)
    inside_col = (centers_x >= x0) & (centers_x <= x1)
    inside_row = (centers_y >= y0) & (centers_y <= y1)
    window = values[np.ix_(inside_row, inside_col)].ravel()
    window = window[~np.isnan(window)]
    canopy = window[window >= MIN_HEIGHT_M]
    return len(window), len(canopy), 100.0 * len(canopy) / len(window), float(canopy.mean())


def check_close(label, found, expected, tolerance=1e-3):
    if abs(float(found) - expected) > tolerance:
        raise AssertionError(f"{label}: found {found}, expected {expected}")
    print(f"PASS  {label}")


def main():
    exe = Path(sys.argv[1])
    work = Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    heights = make_heights()
    tile_dir, full_path = write_rasters(work, heights)
    shape_path = work / "polygons.shp"
    write_polygon_shapefile(shape_path)
    common = [f"/minht:{MIN_HEIGHT_M}"]

    for label, nodata_flag in [("", []), ("with /nodata-as-zero, ", ["/nodata-as-zero"])]:
        nodata_as_zero = bool(nodata_flag)
        tag = "zero" if nodata_as_zero else "skip"

        # 1. whole raster, single file
        out_whole = work / f"whole_{tag}.csv"
        run(exe, [full_path, *common, *nodata_flag, f"/output:{out_whole}"])
        row = read_rows(out_whole)[0]
        total, canopy, cover, mean_height = expected_stats(heights, (0.0, 0.0, 200.0, 200.0), nodata_as_zero)
        check_close(f"{label}whole raster TotalPoints", row["TotalPoints"], total)
        check_close(f"{label}whole raster CanopyCoverPct", row["CanopyCoverPct"], cover)
        check_close(f"{label}whole raster elev_mean", row["elev_mean"], mean_height)

        # 2. a folder of tiles gives the same row
        out_folder = work / f"folder_{tag}.csv"
        run(exe, [tile_dir, *common, *nodata_flag, f"/output:{out_folder}"])
        row_folder = read_rows(out_folder)[0]
        check_close(f"{label}folder TotalPoints", row_folder["TotalPoints"], total)
        check_close(f"{label}folder elev_mean", row_folder["elev_mean"], mean_height)

        # 3. /shape: one row per polygon
        out_shape = work / f"shape_{tag}.csv"
        run(exe, [full_path, *common, *nodata_flag, f"/shape:{shape_path}", "/field:name", f"/output:{out_shape}"])
        rows_shape = {r["Label"]: r for r in read_rows(out_shape)}
        assert set(rows_shape) == set(POLYGONS), f"polygon labels found: {sorted(rows_shape)}"
        for name, box in POLYGONS.items():
            total, canopy, cover, mean_height = expected_stats(heights, box, nodata_as_zero)
            check_close(f"{label}polygon {name} TotalPoints", rows_shape[name]["TotalPoints"], total)
            check_close(f"{label}polygon {name} CanopyPoints", rows_shape[name]["CanopyPoints"], canopy)
            check_close(f"{label}polygon {name} CanopyCoverPct", rows_shape[name]["CanopyCoverPct"], cover)
            check_close(f"{label}polygon {name} elev_mean", rows_shape[name]["elev_mean"], mean_height)
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
