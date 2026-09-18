# Quick-look PNG previews of selected bands from a gridmetrics multiband
# GeoTIFF, for manual visual verification during development -- not part of
# the automated build or test suite. Rerun by hand:
#   Rscript tests/R/visualize_gridmetrics_bands.R <path_to_gridmetrics.tif> [out_dir]

#1. Resolve script arguments: path to a gridmetrics multiband GeoTIFF, and
#   an optional output directory for the preview PNGs (defaults to the
#   raster's own directory).
args_in = commandArgs(trailingOnly = TRUE)
if (length(args_in) < 1) {
    stop("Usage: Rscript visualize_gridmetrics_bands.R <path_to_gridmetrics.tif> [out_dir]")
}
path_tif = args_in[1]
dir_out = if (length(args_in) >= 2) args_in[2] else dirname(path_tif)

#2. Open the raster and pick which bands to preview -- the core elevation/
#   cover summary bands when present, plus every per-stratum mean band
#   (elevation "stratum_NN_mean" or intensity "intstratum_NN_mean"),
#   auto-detected so this script works for any /strata or /intstrata run
#   without hardcoding a specific threshold list.
r_all = terra::rast(path_tif)
cat("Opened", path_tif, "--", terra::nlyr(r_all), "bands,",
    terra::ncol(r_all), "x", terra::nrow(r_all), "cells\n")

nms_core = intersect(c("elev_mean", "canopy_cover", "point_density"), names(r_all))
nms_strata_mean = grep("^(stratum|intstratum)_[0-9]+_mean$", names(r_all), value = TRUE)
nms_wanted = c(nms_core, nms_strata_mean)
if (length(nms_wanted) == 0) {
    stop("No matching bands found in ", path_tif)
}
r_sel = r_all[[nms_wanted]]

#3. Render each selected band as its own PNG.
dir.create(dir_out, recursive = TRUE, showWarnings = FALSE)
for (nm_i in names(r_sel)) {
    path_png = file.path(dir_out, paste0("preview_", nm_i, ".png"))
    grDevices::png(path_png, width = 900, height = 900, res = 150)
    terra::plot(r_sel[[nm_i]], main = nm_i, col = grDevices::terrain.colors(100))
    grDevices::dev.off()
    cat("Wrote:", path_png, "\n")
}

#4. Render a combined panel figure for a side-by-side comparison.
path_panel = file.path(dir_out, "preview_panel.png")
grDevices::png(path_panel, width = 1500, height = 1000, res = 150)
terra::plot(r_sel, col = grDevices::terrain.colors(100))
grDevices::dev.off()
cat("Wrote:", path_panel, "\n")
