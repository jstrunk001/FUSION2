#ifndef FUSION_METRICS_SURFACESTATS_H
#define FUSION_METRICS_SURFACESTATS_H

#include <vector>

namespace fusion::metrics {

struct SurfaceStatsGrid {
    std::vector<float> surfaceAreaRatio; // 3D area / 2D planimetric area, per cell
    std::vector<float> roughness;        // local elevation stddev, per cell
    std::vector<float> volumeDiff;       // elevation - reference, per cell; empty unless a reference grid was supplied
};

// Generalizes topometrics.cpp's 3x3-neighbor Horn's-method slope calculation
// (no Delaunay/TIN needed): surfaceAreaRatio is the direct closed-form
// 1/cos(slope) derivation, roughness is the local elevation standard
// deviation in the same 3x3 window. A value is computed for every cell that
// itself has valid (non-noData) elevation, including the grid's own
// border -- a missing neighbor (off the grid edge, or itself noData) is
// treated as locally flat by substituting the cell's own value in that
// neighbor's place, rather than leaving the whole cell noData. This
// degrades gracefully on sparse/patchy real data (e.g. mostly bare ground
// with scattered canopy) instead of requiring every cell to sit inside an
// unbroken 3x3 block of valid neighbors, which real canopy data routinely
// fails to provide anywhere in a small or sparse tile. Only a cell whose
// own elevation is noData is left noData in the output. volumeDiff needs no
// neighborhood -- it's a direct per-cell elevation-minus-reference
// difference, computed for every cell where both grids have valid data,
// when reference is supplied (same cols x rows as elevation; nullptr skips
// volumeDiff entirely, leaving that vector empty).
SurfaceStatsGrid ComputeSurfaceStatsGrid(
    const std::vector<float>& elevation, int cols, int rows,
    double cellSize, float noData,
    const std::vector<float>* reference = nullptr);

struct SurfaceStatsSummary {
    double planimetricArea{0.0};
    double surfaceArea3D{0.0};
    double surfaceAreaRatioMean{0.0};
    double roughnessMean{0.0};
    double volumeDiffTotal{0.0}; // only meaningful if a reference was supplied
};

// Same per-cell math, reduced to scalars for a one-row CSV consumer
// (cloudmetrics' /surfstats). ComputeSurfaceStatsGrid now computes a ratio
// for every cell with valid elevation (see its own comment), so the "assume
// flat" fallback below is a defensive backstop rather than the common case
// it used to be -- kept in case a caller ever hands this function a grid
// built some other way.
SurfaceStatsSummary SummarizeSurfaceStats(const SurfaceStatsGrid& grid,
                                           const std::vector<float>& elevation,
                                           int cols, int rows, double cellSize, float noData);

} // namespace fusion::metrics

#endif // FUSION_METRICS_SURFACESTATS_H
