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

    for (int r = 1; r < rows - 1; ++r) {
        for (int c = 1; c < cols - 1; ++c) {
            float z5 = elevation[r * cols + c];
            if (z5 == noData) continue;

            float z1 = elevation[(r - 1) * cols + (c - 1)];
            float z2 = elevation[(r - 1) * cols + c];
            float z3 = elevation[(r - 1) * cols + (c + 1)];
            float z4 = elevation[r * cols + (c - 1)];
            float z6 = elevation[r * cols + (c + 1)];
            float z7 = elevation[(r + 1) * cols + (c - 1)];
            float z8 = elevation[(r + 1) * cols + c];
            float z9 = elevation[(r + 1) * cols + (c + 1)];

            if (z1 == noData || z2 == noData || z3 == noData || z4 == noData ||
                z6 == noData || z7 == noData || z8 == noData || z9 == noData) {
                continue;
            }

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
            // Border/edge cell with no computed ratio -- assume flat.
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
