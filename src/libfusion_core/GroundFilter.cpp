#include "fusion/lidar/GroundFilter.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace fusion::lidar {

namespace {

// Grid geometry shared by the surface passes: cell (col, row) covers
// [minX + col*cellSize, minX + (col+1)*cellSize) in x, and likewise in y
// counted up from minY. Rows run south to north here (internal only).
struct SurfaceGrid {
    double minX{0.0};
    double minY{0.0};
    double cellSize{1.0};
    int cols{1};
    int rows{1};

    int ColOf(double x) const {
        int col = static_cast<int>(std::floor((x - minX) / cellSize));
        return std::clamp(col, 0, cols - 1);
    }
    int RowOf(double y) const {
        int row = static_cast<int>(std::floor((y - minY) / cellSize));
        return std::clamp(row, 0, rows - 1);
    }
    double CentreX(int col) const { return minX + (col + 0.5) * cellSize; }
    double CentreY(int row) const { return minY + (row + 0.5) * cellSize; }
};

// Weighted sums of one cell's points, with x and y measured from that
// cell's own centre (keeps the sums small and well conditioned).
struct CellMoments {
    double w{0}, wx{0}, wy{0}, wz{0};
    double wxx{0}, wxy{0}, wyy{0}, wxz{0}, wyz{0};
};

// A local surface z = a + bx * dx + by * dy, where dx, dy are measured
// from the cell centre. a is NaN when no weighted point reached the cell.
struct CellPlane {
    double a{std::numeric_limits<double>::quiet_NaN()};
    double bx{0.0};
    double by{0.0};
};

// Fits one pass's surface: for each cell, a weighted least-squares plane
// through the points in it and its 8 neighbours. A plane (not a weighted
// mean) is what keeps the filter stable on slopes: once weights vary
// across a neighbourhood, a weighted mean is pulled toward wherever the
// weight is concentrated and misses sloped ground by up to slope x cell
// size, which pushes true ground points past g + w and makes the surface
// sink further every pass. A plane fits planar ground exactly whatever
// the weights. Neighbourhoods too small or too collinear to fix a plane
// fall back to the weighted mean; cells with no weight at all take the
// mean of their neighbours' levels, as a flat plane.
std::vector<CellPlane> FitWeightedPlanes(
        const SurfaceGrid& grid,
        const std::vector<int>& pointCell,
        const std::vector<double>& x,
        const std::vector<double>& y,
        const std::vector<double>& z,
        const std::vector<double>& weight) {
    const size_t nCells = static_cast<size_t>(grid.cols) * grid.rows;

    //1. accumulate each cell's weighted moments about its own centre
    std::vector<CellMoments> moments(nCells);
    for (size_t i = 0; i < z.size(); ++i) {
        const double wi = weight[i];
        if (wi <= 0.0) continue;
        const int cell = pointCell[i];
        const int col = cell % grid.cols;
        const int row = cell / grid.cols;
        const double dx = x[i] - grid.CentreX(col);
        const double dy = y[i] - grid.CentreY(row);
        CellMoments& m = moments[cell];
        m.w += wi;
        m.wx += wi * dx;
        m.wy += wi * dy;
        m.wz += wi * z[i];
        m.wxx += wi * dx * dx;
        m.wxy += wi * dx * dy;
        m.wyy += wi * dy * dy;
        m.wxz += wi * dx * z[i];
        m.wyz += wi * dy * z[i];
    }

    //2. per cell, combine the 3 x 3 neighbourhood's moments about this
    //   cell's centre and solve the weighted normal equations
    //  - a neighbour's coordinates are its own dx, dy plus the offset
    //    (ox, oy) between the two centres, so its sums shift exactly
    std::vector<CellPlane> planes(nCells);
    for (int row = 0; row < grid.rows; ++row) {
        for (int col = 0; col < grid.cols; ++col) {
            CellMoments s;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    const int r = row + dr;
                    const int c = col + dc;
                    if (r < 0 || r >= grid.rows || c < 0 || c >= grid.cols) continue;
                    const CellMoments& m = moments[static_cast<size_t>(r) * grid.cols + c];
                    if (m.w <= 0.0) continue;
                    const double ox = dc * grid.cellSize;
                    const double oy = dr * grid.cellSize;
                    s.w += m.w;
                    s.wx += m.wx + ox * m.w;
                    s.wy += m.wy + oy * m.w;
                    s.wz += m.wz;
                    s.wxx += m.wxx + 2.0 * ox * m.wx + ox * ox * m.w;
                    s.wxy += m.wxy + ox * m.wy + oy * m.wx + ox * oy * m.w;
                    s.wyy += m.wyy + 2.0 * oy * m.wy + oy * oy * m.w;
                    s.wxz += m.wxz + ox * m.wz;
                    s.wyz += m.wyz + oy * m.wz;
                }
            }
            if (s.w <= 0.0) continue;

            CellPlane& plane = planes[static_cast<size_t>(row) * grid.cols + col];
            plane.a = s.wz / s.w; // weighted-mean fallback

            // Normal equations  [w  wx  wy ] [a ]   [wz ]
            //                   [wx wxx wxy] [bx] = [wxz]
            //                   [wy wxy wyy] [by]   [wyz]
            const double det =
                s.w * (s.wxx * s.wyy - s.wxy * s.wxy) -
                s.wx * (s.wx * s.wyy - s.wxy * s.wy) +
                s.wy * (s.wx * s.wxy - s.wxx * s.wy);
            const double scale = s.w * s.w * s.w * std::pow(grid.cellSize, 4);
            if (std::abs(det) <= 1e-9 * scale) continue;

            const double detA =
                s.wz * (s.wxx * s.wyy - s.wxy * s.wxy) -
                s.wx * (s.wxz * s.wyy - s.wxy * s.wyz) +
                s.wy * (s.wxz * s.wxy - s.wxx * s.wyz);
            const double detBx =
                s.w * (s.wxz * s.wyy - s.wxy * s.wyz) -
                s.wz * (s.wx * s.wyy - s.wxy * s.wy) +
                s.wy * (s.wx * s.wyz - s.wxz * s.wy);
            const double detBy =
                s.w * (s.wxx * s.wyz - s.wxz * s.wxy) -
                s.wx * (s.wx * s.wyz - s.wxz * s.wy) +
                s.wz * (s.wx * s.wxy - s.wxx * s.wy);
            plane.a = detA / det;
            plane.bx = detBx / det;
            plane.by = detBy / det;
        }
    }

    //3. give cells no weighted point reached a flat plane at their
    //   neighbours' level
    std::vector<double> level(nCells);
    for (size_t i = 0; i < nCells; ++i) level[i] = planes[i].a;
    FillEmptyCells(level, grid.cols, grid.rows);
    for (size_t i = 0; i < nCells; ++i) {
        if (std::isnan(planes[i].a)) {
            planes[i].a = level[i];
            planes[i].bx = 0.0;
            planes[i].by = 0.0;
        }
    }
    return planes;
}

// Surface value for point i from its own cell's plane.
double SurfaceAt(const SurfaceGrid& grid, const std::vector<CellPlane>& planes,
                 int cell, double x, double y) {
    const CellPlane& p = planes[cell];
    const int col = cell % grid.cols;
    const int row = cell / grid.cols;
    return p.a + p.bx * (x - grid.CentreX(col)) + p.by * (y - grid.CentreY(row));
}

} // namespace

double KrausPfeiferWeight(double v, const KrausPfeiferParams& params) {
    if (v <= params.g) return 1.0;
    if (v > params.g + params.w) return 0.0;
    return 1.0 / (1.0 + std::pow(params.a * (v - params.g), params.b));
}

void FillEmptyCells(std::vector<double>& grid, int cols, int rows) {
    //1. nothing to fill from if every cell is empty
    bool anyFilled = std::any_of(grid.begin(), grid.end(), [](double v) { return !std::isnan(v); });
    if (!anyFilled) return;

    //2. grow filled values outward one ring per sweep
    //  - each sweep reads from a snapshot, so a cell filled in this sweep
    //    does not feed its neighbours until the next one
    bool anyEmpty = true;
    while (anyEmpty) {
        std::vector<double> snapshot = grid;
        anyEmpty = false;
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                size_t idx = static_cast<size_t>(row) * cols + col;
                if (!std::isnan(snapshot[idx])) continue;
                double sum = 0.0;
                int n = 0;
                for (int dr = -1; dr <= 1; ++dr) {
                    for (int dc = -1; dc <= 1; ++dc) {
                        int r = row + dr;
                        int c = col + dc;
                        if (r < 0 || r >= rows || c < 0 || c >= cols) continue;
                        double v = snapshot[static_cast<size_t>(r) * cols + c];
                        if (!std::isnan(v)) {
                            sum += v;
                            n++;
                        }
                    }
                }
                if (n > 0) {
                    grid[idx] = sum / n;
                } else {
                    anyEmpty = true;
                }
            }
        }
    }
}

namespace {

// Runs the Kraus & Pfeifer passes on one grid and returns every point's
// residual against the surface fitted with the final weights. Points with
// excluded[i] set keep weight 0 throughout, so they never shape the surface.
std::vector<double> RunKrausPfeiferPasses(
        const std::vector<double>& x,
        const std::vector<double>& y,
        const std::vector<double>& z,
        const std::vector<uint8_t>& excluded,
        double minX, double minY, double maxX, double maxY,
        double cellSize,
        const KrausPfeiferParams& params) {
    const size_t n = z.size();

    //1. lay out the surface grid and index each point's cell
    SurfaceGrid grid;
    grid.minX = minX;
    grid.minY = minY;
    grid.cellSize = cellSize;
    grid.cols = (std::max)(1, static_cast<int>(std::ceil((maxX - minX) / cellSize)));
    grid.rows = (std::max)(1, static_cast<int>(std::ceil((maxY - minY) / cellSize)));
    std::vector<int> pointCell(n);
    for (size_t i = 0; i < n; ++i) {
        pointCell[i] = grid.RowOf(y[i]) * grid.cols + grid.ColOf(x[i]);
    }

    //2. iterate: surface from current weights, then new weights from residuals
    //  - the first surface uses equal weights, as in Kraus & Pfeifer
    std::vector<double> weight(n);
    for (size_t i = 0; i < n; ++i) weight[i] = excluded[i] ? 0.0 : 1.0;
    for (int pass = 0; pass < params.iterations; ++pass) {
        std::vector<CellPlane> planes = FitWeightedPlanes(grid, pointCell, x, y, z, weight);
        for (size_t i = 0; i < n; ++i) {
            if (excluded[i]) continue;
            double residual = z[i] - SurfaceAt(grid, planes, pointCell[i], x[i], y[i]);
            weight[i] = KrausPfeiferWeight(residual, params);
        }
    }

    //3. residuals against the surface fitted with the final weights
    std::vector<CellPlane> finalPlanes = FitWeightedPlanes(grid, pointCell, x, y, z, weight);
    std::vector<double> residual(n);
    for (size_t i = 0; i < n; ++i) {
        residual[i] = z[i] - SurfaceAt(grid, finalPlanes, pointCell[i], x[i], y[i]);
    }
    return residual;
}

} // namespace

std::vector<uint8_t> ClassifyGroundKrausPfeifer(
        const std::vector<double>& x,
        const std::vector<double>& y,
        const std::vector<double>& z,
        double minX, double minY, double maxX, double maxY,
        const KrausPfeiferParams& params) {
    const size_t n = z.size();
    std::vector<uint8_t> isGround(n, 0);
    if (n == 0 || x.size() != n || y.size() != n || params.cellSize <= 0.0) return isGround;

    //1. coarse stage: exclude points far above a coarse ground surface
    std::vector<uint8_t> excluded(n, 0);
    const double coarseCut = params.coarseCut.value_or(4.0 * params.w);
    if (params.coarseCellSize > params.cellSize && coarseCut > 0.0) {
        std::vector<double> coarseResidual = RunKrausPfeiferPasses(
            x, y, z, excluded, minX, minY, maxX, maxY, params.coarseCellSize, params);
        for (size_t i = 0; i < n; ++i) {
            excluded[i] = coarseResidual[i] > coarseCut ? 1 : 0;
        }
    }

    //2. fine stage on the remaining points
    std::vector<double> residual = RunKrausPfeiferPasses(
        x, y, z, excluded, minX, minY, maxX, maxY, params.cellSize, params);

    //3. classify against the fine surface
    for (size_t i = 0; i < n; ++i) {
        if (excluded[i]) continue;
        bool ground = params.tolerance
            ? std::abs(residual[i]) <= *params.tolerance
            : residual[i] <= params.g + params.w;
        isGround[i] = ground ? 1 : 0;
    }
    return isGround;
}

} // namespace fusion::lidar
