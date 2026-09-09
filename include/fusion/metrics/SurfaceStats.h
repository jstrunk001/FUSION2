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
// deviation in the same 3x3 window. Both are only computable for interior
// cells with a full, noData-free 3x3 neighborhood -- border cells and cells
// missing a neighbor get noData in the output grid, matching topometrics'
// own convention. volumeDiff needs no neighborhood -- it's a direct per-cell
// elevation-minus-reference difference, computed for every cell where both
// grids have valid data, when reference is supplied (same cols x rows as
// elevation; nullptr skips volumeDiff entirely, leaving that vector empty).
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
// (cloudmetrics' /surfstats). Cells without a computed surfaceAreaRatio
// (border cells) are assumed flat (ratio 1.0) when accumulating
// surfaceArea3D, so the total still covers the whole valid-data extent
// rather than silently excluding its edge.
SurfaceStatsSummary SummarizeSurfaceStats(const SurfaceStatsGrid& grid,
                                           const std::vector<float>& elevation,
                                           int cols, int rows, double cellSize, float noData);

} // namespace fusion::metrics

#endif // FUSION_METRICS_SURFACESTATS_H
