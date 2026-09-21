#include "fusion/metrics/SurfaceStats.h"

#include <cmath>
#include <cstdint>

namespace fusion::metrics {

SurfaceStatsGrid ComputeSurfaceStatsGrid(
        const std::vector<float>& elevation, int cols, int rows,
        double cellSize, float noData,
        const std::vector<float>* reference) {
    SurfaceStatsGrid grid;
    size_t numCells = static_cast<size_t>(cols) * static_cast<size_t>(rows);
    grid.surfaceAreaRatio.assign(numCells, noData);
    grid.roughness.assign(numCells, noData);

    // A missing neighbor -- off the grid entirely, or itself noData -- is
    // treated as "locally flat": substitute the center cell's own value
    // rather than abort the whole cell. Real canopy/ground data is commonly
    // patchy (a stand with 80%+ bare/low-vegetation cells is a realistic
    // input, not a corner case), and requiring a full, unbroken 3x3 block of
    // valid cells meant surfstats came back entirely NA on exactly that kind
    // of input -- every cell missing at least one of its 8 neighbors, with
    // no partial/degraded answer available. This also lets every cell,
    // including the grid's own border, get a value (there is no
    // off-grid neighbor to be missing that isn't already handled the same
    // way as a noData one) -- SummarizeSurfaceStats' own "assume flat" rule
    // for a cell with no computed ratio already reflects the same
    // philosophy, just one level up.
    auto sampleOrCenter = [&](int rr, int cc, float center) -> float {
        if (rr < 0 || rr >= rows || cc < 0 || cc >= cols) return center;
        float v = elevation[static_cast<size_t>(rr) * cols + cc];
        return (v == noData) ? center : v;
    };

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            float z5 = elevation[r * cols + c];
            if (z5 == noData) continue;

            float z1 = sampleOrCenter(r - 1, c - 1, z5);
            float z2 = sampleOrCenter(r - 1, c, z5);
            float z3 = sampleOrCenter(r - 1, c + 1, z5);
            float z4 = sampleOrCenter(r, c - 1, z5);
            float z6 = sampleOrCenter(r, c + 1, z5);
            float z7 = sampleOrCenter(r + 1, c - 1, z5);
            float z8 = sampleOrCenter(r + 1, c, z5);
            float z9 = sampleOrCenter(r + 1, c + 1, z5);

            // Horn's method for partial derivatives (same as topometrics.cpp)
            double dz_dx = ((z3 + 2 * z6 + z9) - (z1 + 2 * z4 + z7)) / (8.0 * cellSize);
            double dz_dy = ((z7 + 2 * z8 + z9) - (z1 + 2 * z2 + z3)) / (8.0 * cellSize);
            double slopeRad = std::atan(std::sqrt(dz_dx * dz_dx + dz_dy * dz_dy));

            size_t idx = static_cast<size_t>(r) * cols + c;
            grid.surfaceAreaRatio[idx] = static_cast<float>(1.0 / std::cos(slopeRad));

            double windowMean = (z1 + z2 + z3 + z4 + z5 + z6 + z7 + z8 + z9) / 9.0;
            double sqSum = (z1 - windowMean) * (z1 - windowMean)
                         + (z2 - windowMean) * (z2 - windowMean)
                         + (z3 - windowMean) * (z3 - windowMean)
                         + (z4 - windowMean) * (z4 - windowMean)
                         + (z5 - windowMean) * (z5 - windowMean)
                         + (z6 - windowMean) * (z6 - windowMean)
                         + (z7 - windowMean) * (z7 - windowMean)
                         + (z8 - windowMean) * (z8 - windowMean)
                         + (z9 - windowMean) * (z9 - windowMean);
            grid.roughness[idx] = static_cast<float>(std::sqrt(sqSum / 9.0));
        }
    }

    if (reference) {
        grid.volumeDiff.assign(numCells, noData);
        for (size_t i = 0; i < numCells; ++i) {
            float elev = elevation[i];
            float ref = (*reference)[i];
            if (elev == noData || ref == noData) continue;
            grid.volumeDiff[i] = elev - ref;
        }
    }

    return grid;
}

SurfaceStatsSummary SummarizeSurfaceStats(const SurfaceStatsGrid& grid,
                                           const std::vector<float>& elevation,
                                           int cols, int rows, double cellSize, float noData) {
    SurfaceStatsSummary summary;
    size_t numCells = static_cast<size_t>(cols) * static_cast<size_t>(rows);
    double cellArea = cellSize * cellSize;

    uint64_t validElevCount = 0;
    double surfaceArea3D = 0.0;
    double ratioSum = 0.0;
    uint64_t ratioCount = 0;
    double roughnessSum = 0.0;
    uint64_t roughnessCount = 0;
    double volumeDiffSum = 0.0;

    for (size_t i = 0; i < numCells; ++i) {
        if (elevation[i] == noData) continue;
        validElevCount++;

        float ratio = grid.surfaceAreaRatio[i];
        if (ratio != noData) {
            surfaceArea3D += cellArea * ratio;
            ratioSum += ratio;
            ratioCount++;
        } else {
            // Defensive backstop only -- ComputeSurfaceStatsGrid computes a
            // ratio for every cell with valid elevation now, border cells
            // included, so this path shouldn't normally be reached. Assume
            // flat if it ever is.
            surfaceArea3D += cellArea;
        }

        float rough = grid.roughness[i];
        if (rough != noData) {
            roughnessSum += rough;
            roughnessCount++;
        }

        if (!grid.volumeDiff.empty() && grid.volumeDiff[i] != noData) {
            volumeDiffSum += grid.volumeDiff[i] * cellArea;
        }
    }

    summary.planimetricArea = static_cast<double>(validElevCount) * cellArea;
    summary.surfaceArea3D = surfaceArea3D;
    summary.surfaceAreaRatioMean = (ratioCount > 0) ? (ratioSum / ratioCount) : 0.0;
    summary.roughnessMean = (roughnessCount > 0) ? (roughnessSum / roughnessCount) : 0.0;
    summary.volumeDiffTotal = volumeDiffSum;

    return summary;
}

} // namespace fusion::metrics
