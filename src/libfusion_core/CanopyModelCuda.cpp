// CanopyModelCuda.cpp : Host wrapper and fallback logic for GPU CHM rasterization
//
#include "fusion/cuda/CanopyModelCuda.h"

#if defined(FUSION_HAS_CUDA)
extern "C" bool LaunchCudaChmRasterization(
    const float* h_xRel,
    const float* h_yRel,
    const float* h_z,
    int numPoints,
    const float* h_dtmGrid,
    float cellSize,
    int cols,
    int rows,
    float nodataValue,
    int smoothWidth,
    float* h_outChmGrid
);
#endif

namespace fusion::cuda {

bool IsCudaAvailable() {
#if defined(FUSION_HAS_CUDA)
    return true;
#else
    return false;
#endif
}

bool RasterizeChmPoints(
    const std::vector<float>& xRel,
    const std::vector<float>& yRel,
    const std::vector<float>& z,
    const std::vector<float>& dtmGrid,
    const CudaChmRasterizationOptions& options,
    std::vector<float>& outChmGrid
) {
    if (options.cols <= 0 || options.rows <= 0 || xRel.empty()) {
        return false;
    }

    outChmGrid.assign(static_cast<size_t>(options.cols) * options.rows, options.nodataValue);

#if defined(FUSION_HAS_CUDA)
    const float* dtmPtr = (!dtmGrid.empty() && dtmGrid.size() == static_cast<size_t>(options.cols) * options.rows)
        ? dtmGrid.data()
        : nullptr;

    return LaunchCudaChmRasterization(
        xRel.data(),
        yRel.data(),
        z.data(),
        static_cast<int>(xRel.size()),
        dtmPtr,
        options.cellSize,
        options.cols,
        options.rows,
        options.nodataValue,
        options.smoothWidth,
        outChmGrid.data()
    );
#else
    return false;
#endif
}

} // namespace fusion::cuda
