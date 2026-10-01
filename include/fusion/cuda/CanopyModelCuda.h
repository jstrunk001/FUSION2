#ifndef FUSION_CUDA_CANOPYMODEL_CUDA_H
#define FUSION_CUDA_CANOPYMODEL_CUDA_H

#include <vector>
#include <cstddef>

namespace fusion::cuda {

// Check whether CUDA device execution is compiled and available at runtime
bool IsCudaAvailable();

// Configuration parameters for GPU CHM rasterization
struct CudaChmRasterizationOptions {
    float cellSize = 1.0f;
    int cols = 0;
    int rows = 0;
    float nodataValue = -9999.0f;
    int smoothWidth = 0; // If >= 3, executes separable GPU box smoothing
};

// Rasterize points into a 2.5D Canopy Height Model (CHM) on GPU.
// Points are provided in tile-origin normalized coordinates (xRel, yRel, z)
// to maintain single-precision floating-point accuracy.
// If dtmGrid is non-empty (size cols * rows), it is subtracted per cell.
// Returns true on successful GPU rasterization, or false if GPU failed/unavailable.
bool RasterizeChmPoints(
    const std::vector<float>& xRel,
    const std::vector<float>& yRel,
    const std::vector<float>& z,
    const std::vector<float>& dtmGrid,
    const CudaChmRasterizationOptions& options,
    std::vector<float>& outChmGrid
);

} // namespace fusion::cuda

#endif // FUSION_CUDA_CANOPYMODEL_CUDA_H
