// GridMetricsCuda.cpp : Host wrapper and 33-band metrics container for CUDA raster zonal engine
//
#include "fusion/cuda/GridMetricsCuda.h"
#include <cmath>
#include <algorithm>

#if defined(FUSION_HAS_CUDA)
extern "C" bool LaunchCudaRasterGridMetrics(
    const float* h_dsmGrid,
    const float* h_dtmGrid,
    int fineCols, int fineRows,
    float fineCellSize,
    float coarseCellSize,
    int coarseCols, int coarseRows,
    float minHt,
    float heightCut,
    float nodataValue,
    float* h_outBands
);
#endif

namespace fusion::cuda {

std::vector<std::string> GetCudaRasterMetricBandNames() {
    return {
        "elev_mean",
        "elev_stddev",
        "elev_variance",
        "elev_cv",
        "elev_min",
        "elev_max",
        "elev_crr",
        "elev_p01",
        "elev_p05",
        "elev_p10",
        "elev_p20",
        "elev_p25",
        "elev_p30",
        "elev_p40",
        "elev_p50",
        "elev_p60",
        "elev_p70",
        "elev_p75",
        "elev_p80",
        "elev_p90",
        "elev_p95",
        "elev_p99",
        "elev_iqr",
        "elev_mode",
        "elev_aad",
        "elev_mad_median",
        "elev_mad_mode",
        "elev_skewness",
        "elev_kurtosis",
        "elev_quadratic_mean",
        "elev_cubic_mean",
        "canopy_cover",
        "point_density"
    };
}

bool ComputeRasterGridMetrics(
    const float* dsmGrid,
    const float* dtmGrid,
    const CudaGridMetricsOptions& options,
    CudaRasterMetricsBundle& outMetrics
) {
    if (dsmGrid == nullptr || options.fineCols <= 0 || options.fineRows <= 0 ||
        options.fineCellSize <= 0.0f || options.coarseCellSize <= 0.0f) {
        return false;
    }

    int coarseCols = static_cast<int>(std::ceil(options.fineCols * options.fineCellSize / options.coarseCellSize));
    int coarseRows = static_cast<int>(std::ceil(options.fineRows * options.fineCellSize / options.coarseCellSize));
    if (coarseCols <= 0) coarseCols = 1;
    if (coarseRows <= 0) coarseRows = 1;

    outMetrics.outCols = coarseCols;
    outMetrics.outRows = coarseRows;
    outMetrics.bandNames = GetCudaRasterMetricBandNames();
    size_t numBands = outMetrics.bandNames.size();
    size_t numCells = static_cast<size_t>(coarseCols) * coarseRows;

    outMetrics.bands.assign(numBands, std::vector<float>(numCells, options.nodataValue));

#if defined(FUSION_HAS_CUDA)
    std::vector<float> flatOutput(numBands * numCells, options.nodataValue);

    bool ok = LaunchCudaRasterGridMetrics(
        dsmGrid,
        dtmGrid,
        options.fineCols,
        options.fineRows,
        options.fineCellSize,
        options.coarseCellSize,
        coarseCols,
        coarseRows,
        options.minHt,
        options.heightCut,
        options.nodataValue,
        flatOutput.data()
    );

    if (ok) {
        for (size_t b = 0; b < numBands; ++b) {
            std::copy(
                flatOutput.begin() + b * numCells,
                flatOutput.begin() + (b + 1) * numCells,
                outMetrics.bands[b].begin()
            );
        }
        return true;
    }
#endif

    return false;
}

} // namespace fusion::cuda
