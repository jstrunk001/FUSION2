#include "fusion/raster/GDALRaster.h"
#include "fusion/cuda/GridMetricsCuda.h"
#include "test_assert.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <vector>

namespace {

constexpr double kNodata = -9999.0;
constexpr double kTolerance = 1e-4;

// A 4 x 3 grid of 10-unit cells covering x 1000-1040, y 2000-2030, with
// every cell's value taken from one tilted plane evaluated at the cell's
// centre. Bilinear interpolation between cell centres reproduces a plane
// exactly, so the expected answer anywhere between centres is simply the
// plane itself -- computed here independently of GDALRaster.cpp.
constexpr int kCols = 4;
constexpr int kRows = 3;
constexpr double kCell = 10.0;
constexpr double kOriginX = 1000.0;
constexpr double kTopY = 2030.0;

double Plane(double x, double y) {
    return 100.0 + 0.5 * (x - kOriginX) + 0.2 * (y - 2000.0);
}

double CellCentreX(int col) { return kOriginX + (col + 0.5) * kCell; }
double CellCentreY(int row) { return kTopY - (row + 0.5) * kCell; }

bool Near(const std::optional<double>& value, double expected) {
    return value.has_value() && std::abs(*value - expected) < kTolerance;
}

} // namespace

// GDALRaster::GetElevation is how every tool looks up the ground height
// under a point (and canopymodel/gridmetrics/densitymetrics all depend on
// it). Points near the raster's edge used to get no value at all: a point
// in the outer half-cell has no cell centre on one side, so bilinear
// sampling asked for a cell off the grid, got nothing, and fell back to a
// rounded cell index that could itself be off the grid. Heights then came
// back missing along tile edges. These checks pin down the fixed behaviour:
// edge points are clamped to the edge cell, points up to one cell outside
// the raster still get the edge value, points further out get nothing, and
// a nodata neighbour falls back to a valid neighbour instead of nothing.
int RunRasterSamplingTests() {
    int failures = 0;
    std::cout << "GDALRaster::GetElevation tests\n";

    //1. write the plane grid to a scratch GeoTIFF
    //  - cell (2, 1) is set to nodata for the nodata-neighbour check; none
    //    of the other checks sample next to it
    std::vector<float> values(kCols * kRows);
    for (int r = 0; r < kRows; ++r) {
        for (int c = 0; c < kCols; ++c) {
            values[r * kCols + c] = static_cast<float>(Plane(CellCentreX(c), CellCentreY(r)));
        }
    }
    values[1 * kCols + 2] = static_cast<float>(kNodata);

    auto path = std::filesystem::temp_directory_path() / "fusion_tests_raster_sampling.tif";
    double geotransform[6] = {kOriginX, kCell, 0.0, kTopY, 0.0, -kCell};
    {
        fusion::raster::GDALRaster out;
        CHECK(out.Create(path, kCols, kRows, 1, "Float32", "GTiff", "", geotransform, kNodata), failures);
        CHECK(out.WriteBandData(1, values), failures);
        out.Close();
    }

    fusion::raster::GDALRaster raster;
    CHECK(raster.Open(path), failures);
    using fusion::raster::SampleMethod;

    //2. a point exactly on a cell centre returns that cell's value
    CHECK(Near(raster.GetElevation(CellCentreX(1), CellCentreY(0)), Plane(CellCentreX(1), CellCentreY(0))), failures);

    //3. a point between four cell centres returns the plane's value there
    CHECK(Near(raster.GetElevation(1012.0, 2018.0), Plane(1012.0, 2018.0)), failures);

    //4. a point in the outer half-cell along the left edge is clamped to
    //   the edge column: flat in x (column 0's centre), still interpolated in y
    double expected_edge = Plane(CellCentreX(0), 2018.0);
    CHECK(Near(raster.GetElevation(1001.0, 2018.0), expected_edge), failures);

    //5. a point half a cell outside the raster still gets the edge value
    CHECK(Near(raster.GetElevation(995.0, 2018.0), expected_edge), failures);

    //6. a corner point outside both edges clamps to the corner cell
    CHECK(Near(raster.GetElevation(1043.0, 1997.0), Plane(CellCentreX(3), CellCentreY(2))), failures);

    //7. points more than one cell outside the raster get no value
    CHECK(!raster.GetElevation(980.0, 2018.0).has_value(), failures);
    CHECK(!raster.GetElevation(1020.0, 2045.0).has_value(), failures);

    //8. nearest-cell sampling also clamps a just-outside point to the edge
    CHECK(Near(raster.GetElevation(995.0, 2018.0, SampleMethod::Nearest), Plane(CellCentreX(0), CellCentreY(1))), failures);

    //9. a point whose four neighbours include the nodata cell (2, 1), and
    //   whose nearest cell is that nodata cell, still returns one of the
    //   three valid neighbours rather than nothing
    auto near_nodata = raster.GetElevation(1024.0, 2016.0);
    bool is_valid_neighbour = Near(near_nodata, Plane(CellCentreX(1), CellCentreY(0)))
                           || Near(near_nodata, Plane(CellCentreX(2), CellCentreY(0)))
                           || Near(near_nodata, Plane(CellCentreX(1), CellCentreY(1)));
    CHECK(is_valid_neighbour, failures);

    //10. the nodata cell's own centre returns nothing under nearest sampling
    CHECK(!raster.GetElevation(CellCentreX(2), CellCentreY(1), SampleMethod::Nearest).has_value(), failures);

    //11. ReadBandWindow reads sub-windows accurately
    std::vector<float> win;
    CHECK(raster.ReadBandWindow(1, 1, 0, 2, 2, win), failures);
    CHECK(win.size() == 4, failures);
    if (win.size() == 4) {
        CHECK(std::abs(win[0] - values[0 * kCols + 1]) < kTolerance, failures);
        CHECK(std::abs(win[1] - values[0 * kCols + 2]) < kTolerance, failures);
        CHECK(std::abs(win[2] - values[1 * kCols + 1]) < kTolerance, failures);
        CHECK(std::abs(win[3] - values[1 * kCols + 2]) < kTolerance, failures);
    }

    //12. ReadBandWindow rejects out-of-bounds windows
    std::vector<float> badWin;
    CHECK(!raster.ReadBandWindow(1, -1, 0, 2, 2, badWin), failures);
    CHECK(!raster.ReadBandWindow(1, 0, 0, kCols + 1, 1, badWin), failures);
    CHECK(!raster.ReadBandWindow(1, 0, 0, 1, kRows + 1, badWin), failures);
    CHECK(!raster.ReadBandWindow(2, 0, 0, 1, 1, badWin), failures); // band 2 doesn't exist

    //13. Verify CUDA 33-band raster metrics metadata and execution container
    {
        auto bandNames = fusion::cuda::GetCudaRasterMetricBandNames();
        CHECK(bandNames.size() == 33, failures);
        CHECK(bandNames.front() == "elev_mean", failures);
        CHECK(bandNames.back() == "point_density", failures);

        fusion::cuda::CudaGridMetricsOptions opts;
        opts.fineCols = kCols;
        opts.fineRows = kRows;
        opts.fineCellSize = 1.0f;
        opts.coarseCellSize = 2.0f;
        opts.minHt = 2.0f;
        opts.heightCut = 2.0f;
        opts.nodataValue = -9999.0f;

        fusion::cuda::CudaRasterMetricsBundle bundle;
        bool ok = fusion::cuda::ComputeRasterGridMetrics(values.data(), nullptr, opts, bundle);
        CHECK(bundle.bandNames.size() == 33, failures);
        CHECK(bundle.bands.size() == 33, failures);
        CHECK(bundle.outCols == 2, failures);
        CHECK(bundle.outRows == 2, failures);
        // If CUDA device is available, ok is true; otherwise ok is false (clean CPU fallback)
        (void)ok;
    }

    raster.Close();
    std::filesystem::remove(path);

    if (failures == 0) std::cout << "  all passed\n";
    return failures;
}
