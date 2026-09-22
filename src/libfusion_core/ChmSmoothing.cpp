#include "fusion/raster/ChmSmoothing.h"

#include <algorithm>

namespace fusion::raster {

std::vector<float> SmoothNodataAwareBox(const std::vector<float>& grid, int cols, int rows,
                                         int windowSize, float nodataValue) {
    if (windowSize % 2 == 0) windowSize += 1;
    int half = windowSize / 2;

    // A WxW nodata-aware mean is separable into a horizontal 1D box sum
    // followed by a vertical 1D box sum (2*W additions per cell instead of
    // W*W). Nodata cells contribute 0 to both the value sum and the
    // valid-neighbor count in each pass, so the two-pass result matches the
    // original brute-force nodata-aware average exactly: summing a WxW
    // neighborhood's valid values row-by-row-then-column-by-column gives the
    // same total as summing it in one pass, since addition is associative
    // and commutative and nodata cells are excluded identically either way.
    std::vector<double> horizSum(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0.0);
    std::vector<int> horizCount(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            double sum = 0.0;
            int count = 0;
            int cLo = (std::max)(0, c - half);
            int cHi = (std::min)(cols - 1, c + half);
            for (int nc = cLo; nc <= cHi; ++nc) {
                float val = grid[r * cols + nc];
                if (val != nodataValue) {
                    sum += val;
                    count++;
                }
            }
            horizSum[r * cols + c] = sum;
            horizCount[r * cols + c] = count;
        }
    }

    std::vector<float> smoothedGrid = grid;
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            if (grid[r * cols + c] == nodataValue) continue;

            double sum = 0.0;
            int count = 0;
            int rLo = (std::max)(0, r - half);
            int rHi = (std::min)(rows - 1, r + half);
            for (int nr = rLo; nr <= rHi; ++nr) {
                sum += horizSum[nr * cols + c];
                count += horizCount[nr * cols + c];
            }
            if (count > 0) {
                smoothedGrid[r * cols + c] = static_cast<float>(sum / count);
            }
        }
    }

    return smoothedGrid;
}

} // namespace fusion::raster
