#include "fusion/raster/ChmSmoothing.h"
#include "fusion/cuda/CanopyModelCuda.h"
#include "test_assert.h"

#include <cmath>
#include <iostream>
#include <random>
#include <vector>

namespace {

constexpr float kNodata = -9999.0f;

// The independent reference implementation the fast separable version is
// checked against: a plain WxW neighborhood scan per cell, O(W^2) per cell,
// deliberately written apart from ChmSmoothing.cpp's own two-pass logic so a
// bug shared by both implementations wouldn't hide behind a passing test.
std::vector<float> BruteForceSmooth(const std::vector<float>& grid, int cols, int rows,
                                     int windowSize, float nodataValue) {
    int half = windowSize / 2;
    std::vector<float> out = grid;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (grid[r * cols + c] == nodataValue) continue;
            double sum = 0.0;
            int count = 0;
            for (int nr = (std::max)(0, r - half); nr <= (std::min)(rows - 1, r + half); ++nr) {
                for (int nc = (std::max)(0, c - half); nc <= (std::min)(cols - 1, c + half); ++nc) {
                    float val = grid[nr * cols + nc];
                    if (val != nodataValue) {
                        sum += val;
                        count++;
                    }
                }
            }
            if (count > 0) out[r * cols + c] = static_cast<float>(sum / count);
        }
    }
    return out;
}

bool GridsMatch(const std::vector<float>& a, const std::vector<float>& b, int& failures) {
    if (a.size() != b.size()) { ++failures; return false; }
    bool ok = true;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::abs(a[i] - b[i]) > 1e-4f) {
            ok = false;
        }
    }
    return ok;
}

} // namespace

// ChmSmoothing::SmoothNodataAwareBox replaced canopymodel's original
// brute-force WxW neighborhood scan with a separable two-pass box sum for
// speed (O(W) instead of O(W^2) per cell). These checks pin down the claim
// the rewrite's commit message makes: that the separable version's output is
// numerically identical to the brute-force average it replaced, across
// window sizes, a scattered-nodata field, and the grid-boundary edge cases
// where a window gets clipped.
int RunChmSmoothingTests() {
    int failures = 0;
    std::cout << "ChmSmoothing tests\n";

    // A uniform grid smooths to itself regardless of window size.
    {
        int cols = 10, rows = 10;
        std::vector<float> grid(cols * rows, 5.0f);
        auto smoothed = fusion::raster::SmoothNodataAwareBox(grid, cols, rows, 5, kNodata);
        for (float v : smoothed) CHECK(std::abs(v - 5.0f) < 1e-5f, failures);
    }

    // A single non-nodata spike in an otherwise-nodata grid: matches the
    // brute-force reference, and stays untouched outside the window radius.
    {
        int cols = 9, rows = 9;
        std::vector<float> grid(cols * rows, kNodata);
        grid[4 * cols + 4] = 100.0f; // center cell
        auto fast = fusion::raster::SmoothNodataAwareBox(grid, cols, rows, 3, kNodata);
        auto brute = BruteForceSmooth(grid, cols, rows, 3, kNodata);
        CHECK(GridsMatch(fast, brute, failures), failures);
        CHECK(fast[0] == kNodata, failures); // corner, untouched (was nodata, stays nodata)
        CHECK(std::abs(fast[4 * cols + 4] - 100.0f) < 1e-4f, failures); // only valid cell in its own window
    }

    // Randomized grid with ~20% scattered nodata cells, checked against the
    // brute-force reference across several window sizes, including window
    // sizes larger than the grid (exercises the clipped-window edge case at
    // every cell, not just the border).
    {
        int cols = 23, rows = 17;
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> valDist(0.0f, 50.0f);
        std::uniform_real_distribution<float> nodataDist(0.0f, 1.0f);
        std::vector<float> grid(cols * rows);
        for (auto& v : grid) {
            v = (nodataDist(rng) < 0.2f) ? kNodata : valDist(rng);
        }

        for (int window : {3, 5, 11, 31}) { // 31 > both grid dimensions
            auto fast = fusion::raster::SmoothNodataAwareBox(grid, cols, rows, window, kNodata);
            auto brute = BruteForceSmooth(grid, cols, rows, window, kNodata);
            CHECK(GridsMatch(fast, brute, failures), failures);
        }
    }

    // An even window size is bumped to the next odd size, same as
    // canopymodel's own /smooth: handling, and still matches the brute-force
    // reference computed with that same bumped-odd size.
    {
        int cols = 12, rows = 12;
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> valDist(0.0f, 10.0f);
        std::vector<float> grid(cols * rows);
        for (auto& v : grid) v = valDist(rng);

        auto fast = fusion::raster::SmoothNodataAwareBox(grid, cols, rows, 4, kNodata);
        auto brute = BruteForceSmooth(grid, cols, rows, 5, kNodata); // 4 -> 5
        CHECK(GridsMatch(fast, brute, failures), failures);
    }

    // An all-nodata grid stays all-nodata -- no divide-by-zero, no cell
    // gains a value it never had.
    {
        int cols = 5, rows = 5;
        std::vector<float> grid(cols * rows, kNodata);
        auto smoothed = fusion::raster::SmoothNodataAwareBox(grid, cols, rows, 3, kNodata);
        for (float v : smoothed) CHECK(v == kNodata, failures);
    }

    // Verify GPU/fallback CHM rasterization interface
    {
        fusion::cuda::CudaChmRasterizationOptions opts;
        opts.cols = 4;
        opts.rows = 4;
        opts.cellSize = 1.0f;
        opts.nodataValue = kNodata;
        std::vector<float> xRel = {0.5f, 1.5f, 0.5f};
        std::vector<float> yRel = {0.5f, 0.5f, 0.5f};
        std::vector<float> z = {10.0f, 15.0f, 20.0f};
        std::vector<float> dtm;
        std::vector<float> outGrid;

        bool ok = fusion::cuda::RasterizeChmPoints(xRel, yRel, z, dtm, opts, outGrid);
        if (ok) {
            CHECK(outGrid[0 * 4 + 0] == 20.0f, failures);
            CHECK(outGrid[0 * 4 + 1] == 15.0f, failures);
            CHECK(outGrid[1 * 4 + 0] == kNodata, failures);
        } else {
            CHECK(outGrid.size() == 16, failures);
            CHECK(outGrid[0] == kNodata, failures);
        }
    }

    std::cout << (failures == 0 ? "  all passed\n" : ("  " + std::to_string(failures) + " failure(s)\n"));
    return failures;
}
