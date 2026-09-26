#include "fusion/metrics/SurfaceStats.h"
#include "test_assert.h"

#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr float kNodata = -9999.0f;

bool NearlyEqual(double a, double b, double tolerance = 1e-4) {
    return std::abs(a - b) < tolerance;
}

} // namespace

// ComputeSurfaceStatsGrid/SummarizeSurfaceStats back gridsurfacestats and
// cloudmetrics' /surfstats. Two fixes are pinned down here:
// - volumeDiff is a volume (height difference x cell area) in each cell, so
//   the per-cell raster band and the summed total agree in units; the total
//   must not multiply by the cell area a second time.
// - patchy or sparse elevation grids still get a value in every cell that
//   has data. The original version needed an unbroken 3 x 3 block of valid
//   cells around each cell and returned an all-nodata result on patchy
//   canopy surfaces.
int RunSurfaceStatsTests() {
    int failures = 0;
    std::cout << "SurfaceStats tests\n";

    {
        //1. flat 3 x 3 surface 6 units above a flat reference, 2-unit cells:
        //   each cell's volume is 6 x (2 x 2) = 24, and the total is 9 x 24
        const int cols = 3;
        const int rows = 3;
        const double cellSize = 2.0;
        std::vector<float> elevation(cols * rows, 10.0f);
        std::vector<float> reference(cols * rows, 4.0f);

        auto grid = fusion::metrics::ComputeSurfaceStatsGrid(elevation, cols, rows, cellSize, kNodata, &reference);
        CHECK(grid.volumeDiff.size() == elevation.size(), failures);
        bool every_cell_24 = true;
        for (float v : grid.volumeDiff) {
            if (!NearlyEqual(v, 24.0)) every_cell_24 = false;
        }
        CHECK(every_cell_24, failures);

        auto summary = fusion::metrics::SummarizeSurfaceStats(grid, elevation, cols, rows, cellSize, kNodata);
        CHECK(NearlyEqual(summary.volumeDiffTotal, 216.0), failures);
        CHECK(NearlyEqual(summary.planimetricArea, 36.0), failures);

        //2. a nodata reference cell leaves that cell's volume as nodata and
        //   drops it from the total
        reference[4] = kNodata;
        auto grid_gap = fusion::metrics::ComputeSurfaceStatsGrid(elevation, cols, rows, cellSize, kNodata, &reference);
        CHECK(grid_gap.volumeDiff[4] == kNodata, failures);
        auto summary_gap = fusion::metrics::SummarizeSurfaceStats(grid_gap, elevation, cols, rows, cellSize, kNodata);
        CHECK(NearlyEqual(summary_gap.volumeDiffTotal, 192.0), failures);

        //3. no reference grid leaves volumeDiff empty
        auto grid_noref = fusion::metrics::ComputeSurfaceStatsGrid(elevation, cols, rows, cellSize, kNodata);
        CHECK(grid_noref.volumeDiff.empty(), failures);
    }

    {
        //4. a plane rising 0.75 units per unit of x: Horn's slope at an
        //   interior cell is exactly 0.75, so the 3D-to-flat area ratio is
        //   sqrt(1 + 0.75^2) = 1.25
        const int cols = 5;
        const int rows = 5;
        const double cellSize = 2.0;
        std::vector<float> elevation(cols * rows);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                elevation[r * cols + c] = static_cast<float>(0.75 * c * cellSize);
            }
        }
        auto grid = fusion::metrics::ComputeSurfaceStatsGrid(elevation, cols, rows, cellSize, kNodata);
        CHECK(NearlyEqual(grid.surfaceAreaRatio[2 * cols + 2], 1.25), failures);
    }

    {
        //5. a checkerboard 5 x 5 grid, every other cell nodata: no valid cell
        //   has a complete 3 x 3 neighbourhood, yet every valid cell -- border
        //   cells included -- still gets a finite area ratio and roughness,
        //   and every nodata cell stays nodata
        const int cols = 5;
        const int rows = 5;
        const double cellSize = 1.0;
        std::vector<float> elevation(cols * rows, kNodata);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                if ((r + c) % 2 == 0) elevation[r * cols + c] = static_cast<float>(5.0 + r + 0.5 * c);
            }
        }
        auto grid = fusion::metrics::ComputeSurfaceStatsGrid(elevation, cols, rows, cellSize, kNodata);

        bool valid_cells_filled = true;
        bool nodata_cells_empty = true;
        for (size_t i = 0; i < elevation.size(); ++i) {
            if (elevation[i] == kNodata) {
                if (grid.surfaceAreaRatio[i] != kNodata || grid.roughness[i] != kNodata) nodata_cells_empty = false;
            } else {
                bool ratio_ok = grid.surfaceAreaRatio[i] != kNodata && std::isfinite(grid.surfaceAreaRatio[i]) && grid.surfaceAreaRatio[i] >= 1.0f;
                bool rough_ok = grid.roughness[i] != kNodata && std::isfinite(grid.roughness[i]) && grid.roughness[i] >= 0.0f;
                if (!ratio_ok || !rough_ok) valid_cells_filled = false;
            }
        }
        CHECK(valid_cells_filled, failures);
        CHECK(nodata_cells_empty, failures);

        //6. the summary counts only the 13 valid cells as flat area and
        //   reports a positive mean ratio
        auto summary = fusion::metrics::SummarizeSurfaceStats(grid, elevation, cols, rows, cellSize, kNodata);
        CHECK(NearlyEqual(summary.planimetricArea, 13.0), failures);
        CHECK(summary.surfaceAreaRatioMean >= 1.0, failures);
    }

    if (failures == 0) std::cout << "  all passed\n";
    return failures;
}
