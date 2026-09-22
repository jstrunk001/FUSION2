#ifndef FUSION_RASTER_CHMSMOOTHING_H
#define FUSION_RASTER_CHMSMOOTHING_H

#include <vector>

namespace fusion::raster {

// Nodata-aware mean smoothing over a cols x rows grid, windowSize x windowSize
// (windowSize is forced odd -- an even value is incremented by 1, matching
// canopymodel's own /smooth: handling). A cell equal to nodataValue is never
// itself replaced, and never contributes to a neighbor's average; a cell with
// no valid neighbors in its window is left at its original value.
//
// Implemented as a horizontal 1D box sum followed by a vertical 1D box sum
// (2*windowSize additions per cell) rather than the brute-force windowSize^2
// neighborhood scan -- see the comment above the two-pass loop in the .cpp
// file for why the two give the identical result.
std::vector<float> SmoothNodataAwareBox(const std::vector<float>& grid, int cols, int rows,
                                         int windowSize, float nodataValue);

} // namespace fusion::raster

#endif // FUSION_RASTER_CHMSMOOTHING_H
