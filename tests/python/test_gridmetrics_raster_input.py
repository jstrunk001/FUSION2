"""Checks gridmetrics raster input: single file, folder of tiles, VRT, /nodata-as-zero.

Builds a synthetic canopy height model (CHM) in four 100 x 100 pixel tiles
with known heights and missing pixels, runs gridmetrics on it three ways,
and compares the outputs with each other and with a direct numpy
calculation of cover and maximum height.

Usage:
    python test_gridmetrics_raster_input.py <path to gridmetrics.exe> <work folder>

The work folder holds only this test's inputs and outputs. It is created if
it does not exist; nothing in it is deleted by this script.
"""
import subprocess
import sys
from pathlib import Path

import numpy as np
import rasterio
from rasterio.transform import from_origin

CELL_SIZE_M = 20
PIXEL_SIZE_M = 1.0
TILE_PIXELS = 100
MIN_HEIGHT_M = 2.0
ORIGIN_X = 500000.0
ORIGIN_Y = 5000200.0


def make_chm_values():
    """Return a 200 x 200 array of heights with NaN holes and a hole of -9999."""
    rows, cols = np.mgrid[0:200, 0:200]
    heights = ((cols * 7 + rows * 13) % 41).astype("float32")
    # some pixels below the minimum height
    heights[(cols % 9 == 0)] = 0.5
    # missing pixels written as NaN
    heights[10:30, 10:30] = np.nan
    # missing pixels written as the declared nodata value, inside tile 4
    heights[150:170, 150:170] = -9999.0
    return heights


def write_tiles(tile_dir, heights):
    tile_dir.mkdir(parents=True, exist_ok=True)
    transform_full = from_origin(ORIGIN_X, ORIGIN_Y, PIXEL_SIZE_M, PIXEL_SIZE_M)
    paths = []
    for tile_row in range(2):
        for tile_col in range(2):
            window = heights[
                tile_row * TILE_PIXELS:(tile_row + 1) * TILE_PIXELS,
                tile_col * TILE_PIXELS:(tile_col + 1) * TILE_PIXELS,
            ]
            tile_origin_x = ORIGIN_X + tile_col * TILE_PIXELS * PIXEL_SIZE_M
            tile_origin_y = ORIGIN_Y - tile_row * TILE_PIXELS * PIXEL_SIZE_M
            path = tile_dir / f"chm_r{tile_row}_c{tile_col}.tif"
            with rasterio.open(
                path, "w", driver="GTiff", height=TILE_PIXELS, width=TILE_PIXELS,
                count=1, dtype="float32", crs="EPSG:32610",
                transform=from_origin(tile_origin_x, tile_origin_y, PIXEL_SIZE_M, PIXEL_SIZE_M),
                nodata=-9999.0,
            ) as dst:
                dst.write(window, 1)
            paths.append(path)
    full_path = tile_dir.parent / "chm_full.tif"
    with rasterio.open(
        full_path, "w", driver="GTiff", height=200, width=200, count=1,
        dtype="float32", crs="EPSG:32610", transform=transform_full, nodata=-9999.0,
    ) as dst:
        dst.write(heights, 1)
    return paths, full_path


def write_mosaic_vrt(vrt_path, tile_paths):
    """Write a VRT that places each 100 x 100 pixel tile in the 200 x 200 mosaic."""
    lines = []
    lines.append('<VRTDataset rasterXSize="200" rasterYSize="200">')
    with rasterio.open(tile_paths[0]) as first:
        wkt = first.crs.to_wkt().replace("&", "&amp;").replace("<", "&lt;")
    lines.append(f"  <SRS>{wkt}</SRS>")
    lines.append(f"  <GeoTransform>{ORIGIN_X}, {PIXEL_SIZE_M}, 0.0, {ORIGIN_Y}, 0.0, {-PIXEL_SIZE_M}</GeoTransform>")
    lines.append('  <VRTRasterBand dataType="Float32" band="1">')
    lines.append("    <NoDataValue>-9999</NoDataValue>")
    for tile_path in tile_paths:
        with rasterio.open(tile_path) as src:
            col_off = int(round((src.transform.c - ORIGIN_X) / PIXEL_SIZE_M))
            row_off = int(round((ORIGIN_Y - src.transform.f) / PIXEL_SIZE_M))
        source_file = Path(tile_path).resolve()
        lines.append("    <SimpleSource>")
        lines.append(f'      <SourceFilename relativeToVRT="0">{source_file}</SourceFilename>')
        lines.append("      <SourceBand>1</SourceBand>")
        lines.append(f'      <SrcRect xOff="0" yOff="0" xSize="{TILE_PIXELS}" ySize="{TILE_PIXELS}"/>')
        lines.append(f'      <DstRect xOff="{col_off}" yOff="{row_off}" xSize="{TILE_PIXELS}" ySize="{TILE_PIXELS}"/>')
        lines.append("    </SimpleSource>")
    lines.append("  </VRTRasterBand>")
    lines.append("</VRTDataset>")
    vrt_path.write_text(chr(10).join(lines) + chr(10), encoding="utf-8")


def run_gridmetrics(exe, args):
    command = [str(exe)] + [str(a) for a in args]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"gridmetrics failed ({result.returncode}): {' '.join(command)}\n"
                           f"{result.stdout}\n{result.stderr}")
    return result.stdout


def read_bands(path):
    with rasterio.open(path) as src:
        names = list(src.descriptions)
        data = src.read().astype("float64")
        # GDAL reports NoData as NaN or a marker; treat both as missing
        nodata = src.nodata
    if nodata is not None and not np.isnan(nodata):
        data[data == nodata] = np.nan
    return names, data


def expected_cover_and_max(heights, nodata_as_zero):
    """Cover (percent of pixels at or above the minimum height) and maximum
    height above the minimum, per 20 m cell, computed directly."""
    values = heights.copy()
    values[values == -9999.0] = np.nan
    if nodata_as_zero:
        values = np.where(np.isnan(values), 0.0, values)
    n_cells = 200 // CELL_SIZE_M
    cover = np.full((n_cells, n_cells), np.nan)
    maximum = np.full((n_cells, n_cells), np.nan)
    for cell_row in range(n_cells):
        for cell_col in range(n_cells):
            block = values[
                cell_row * CELL_SIZE_M:(cell_row + 1) * CELL_SIZE_M,
                cell_col * CELL_SIZE_M:(cell_col + 1) * CELL_SIZE_M,
            ]
            valid = block[~np.isnan(block)]
            if valid.size == 0:
                continue
            cover[cell_row, cell_col] = 100.0 * np.sum(valid >= MIN_HEIGHT_M) / valid.size
            above = valid[valid >= MIN_HEIGHT_M]
            maximum[cell_row, cell_col] = above.max() if above.size else np.nan
    return cover, maximum


def assert_same(label, first, second):
    both_nan = np.isnan(first) & np.isnan(second)
    close = np.isclose(first, second, rtol=1e-4, atol=1e-4)
    if not np.all(both_nan | close):
        n_bad = int(np.sum(~(both_nan | close)))
        raise AssertionError(f"{label}: {n_bad} of {first.size} values differ")
    print(f"PASS  {label}")


def band_index(names, wanted):
    if wanted not in names:
        raise AssertionError(f"band '{wanted}' not found; bands are {names}")
    return names.index(wanted)


def main():
    exe = Path(sys.argv[1])
    work = Path(sys.argv[2])
    work.mkdir(parents=True, exist_ok=True)
    heights = make_chm_values()
    tile_paths, full_path = write_tiles(work / "tiles", heights)
    common = [f"/cellsize:{CELL_SIZE_M}", f"/minht:{MIN_HEIGHT_M}", "/strata:5,10,20"]

    # 1. single file, whole raster
    out_single = work / "out_single"
    run_gridmetrics(exe, [full_path, *common, f"/outdir:{out_single}"])
    names_single, bands_single = read_bands(out_single / "chm_full_gridmetrics.tif")

    # 2. folder of tiles, tiled mode (100 m output tiles matching the source tiles)
    out_folder = work / "out_folder"
    run_gridmetrics(exe, [work / "tiles", *common, "/tilesize:100,100",
                          f"/outdir:{out_folder}", "/threads:2"])
    names_folder, bands_folder = read_bands(out_folder / "project_gridmetrics.vrt")

    # 3. VRT of the tiles, tiled mode (60 m output tiles cut across the source tiles)
    vrt_path = work / "chm_mosaic.vrt"
    write_mosaic_vrt(vrt_path, tile_paths)
    if vrt_path.exists():
        out_vrt = work / "out_vrt"
        run_gridmetrics(exe, [vrt_path, *common, "/tilesize:60,60",
                              f"/outdir:{out_vrt}", "/threads:2"])
        names_vrt, bands_vrt = read_bands(out_vrt / "project_gridmetrics.vrt")
    else:
        raise AssertionError("the test VRT was not written")

    # 4. the outputs agree band by band
    assert names_single == names_folder, "band names differ between single-file and folder runs"
    for band_i, name in enumerate(names_single):
        assert_same(f"folder tiled == single file, band {name}", bands_single[band_i], bands_folder[band_i])
        if bands_vrt is not None:
            assert_same(f"VRT tiled == single file, band {name}", bands_single[band_i], bands_vrt[band_i])

    # 5. cover and maximum height agree with a direct calculation
    expected_cover, expected_max = expected_cover_and_max(heights, nodata_as_zero=False)
    cover_name = next(n for n in names_single if n and "cover" in n.lower())
    max_name = next(n for n in names_single if n and n.lower() in ("elev_max", "elev_maximum"))
    assert_same(f"cover vs numpy ({cover_name})", bands_single[band_index(names_single, cover_name)], expected_cover)
    assert_same(f"max height vs numpy ({max_name})", bands_single[band_index(names_single, max_name)], expected_max)

    # 6. /nodata-as-zero: single file and folder runs agree with numpy
    out_zero_single = work / "out_zero_single"
    run_gridmetrics(exe, [full_path, *common, "/nodata-as-zero", f"/outdir:{out_zero_single}"])
    names_zero, bands_zero_single = read_bands(out_zero_single / "chm_full_gridmetrics.tif")
    out_zero_folder = work / "out_zero_folder"
    run_gridmetrics(exe, [work / "tiles", *common, "/nodata-as-zero", "/tilesize:100,100",
                          f"/outdir:{out_zero_folder}", "/threads:2"])
    _, bands_zero_folder = read_bands(out_zero_folder / "project_gridmetrics.vrt")
    expected_cover_zero, _ = expected_cover_and_max(heights, nodata_as_zero=True)
    cover_i = band_index(names_zero, cover_name)
    assert_same("nodata-as-zero cover vs numpy (single file)", bands_zero_single[cover_i], expected_cover_zero)
    assert_same("nodata-as-zero cover vs numpy (folder)", bands_zero_folder[cover_i], expected_cover_zero)

    # 7. counting missing pixels as zero-height returns lowers mean cover
    if not np.nanmean(bands_zero_single[cover_i]) < np.nanmean(bands_single[cover_i]):
        raise AssertionError("/nodata-as-zero should lower mean cover when missing pixels exist")
    print("PASS  /nodata-as-zero lowers mean cover")
    print("ALL CHECKS PASSED")


if __name__ == "__main__":
    main()
