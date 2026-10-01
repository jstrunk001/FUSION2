#ifndef FUSION_CUDA_GRIDMETRICS_CUDA_H
#define FUSION_CUDA_GRIDMETRICS_CUDA_H

#include <vector>
#include <string>

namespace fusion::cuda {

// Options for GPU-accelerated raster zonal metric calculation
struct CudaGridMetricsOptions {
    int fineCols = 0;
    int fineRows = 0;
    float fineCellSize = 1.0f;
    float coarseCellSize = 30.0f; // Output zone resolution in project units
    float minHt = 2.0f;
    float heightCut = 2.0f;
    float nodataValue = -9999.0f;
};

// 33-band metric output container produced by the CUDA raster zonal kernel
struct CudaRasterMetricsBundle {
    int outCols = 0;
    int outRows = 0;
    std::vector<std::string> bandNames;
    std::vector<std::vector<float>> bands; // [bandIndex][cellIndex]
};

// Returns standard 33-band metric names in execution order
std::vector<std::string> GetCudaRasterMetricBandNames();

// Compute 33-band metrics across zones from high-resolution DSM (and optional DTM) rasters on GPU.
// Returns true on success, or false if GPU is unavailable or device execution fails.
bool ComputeRasterGridMetrics(
    const float* dsmGrid,
    const float* dtmGrid,
    const CudaGridMetricsOptions& options,
    CudaRasterMetricsBundle& outMetrics
);

} // namespace fusion::cuda

#endif // FUSION_CUDA_GRIDMETRICS_CUDA_H
