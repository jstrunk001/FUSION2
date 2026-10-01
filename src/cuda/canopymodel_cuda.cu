// canopymodel_cuda.cu : CUDA-accelerated Canopy Height Model (CHM) rasterization
//
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <vector>
#include <climits>
#include <cmath>

namespace {

// Kernel: Updates cell maximum height using atomicMax on positive IEEE-754 bit representations.
// Positive floats share the exact same binary ordering as signed 32-bit integers.
__global__ void RasterizeCHMKernel(const float* __restrict__ xRel,
                                   const float* __restrict__ yRel,
                                   const float* __restrict__ z,
                                   const float* __restrict__ dtmGrid,
                                   int* __restrict__ chmIntGrid,
                                   int numPoints,
                                   float cellSize,
                                   int cols,
                                   int rows) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= numPoints) return;

    int c = static_cast<int>(xRel[idx] / cellSize);
    int r = static_cast<int>(yRel[idx] / cellSize);

    if (c >= 0 && c < cols && r >= 0 && r < rows) {
        int cellIndex = r * cols + c;
        float ground = (dtmGrid != nullptr) ? dtmGrid[cellIndex] : 0.0f;
        float h = z[idx] - ground;
        if (h >= 0.0f) {
            atomicMax(&chmIntGrid[cellIndex], __float_as_int(h));
        }
    }
}

// Kernel: Converts integer bit representations back to float, mapping INT_MIN sentinels to nodataValue.
__global__ void FinalizeCHMKernel(const int* __restrict__ chmIntGrid,
                                  float* __restrict__ chmOutGrid,
                                  int numCells,
                                  float nodataValue) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= numCells) return;

    int valInt = chmIntGrid[idx];
    if (valInt < 0) {
        chmOutGrid[idx] = nodataValue;
    } else {
        chmOutGrid[idx] = __int_as_float(valInt);
    }
}

// Kernel: 1D horizontal pass of separable nodata-aware box smoothing filter.
__global__ void SmoothBoxHorizontalKernel(const float* __restrict__ inGrid,
                                          float* __restrict__ outSum,
                                          int* __restrict__ outCount,
                                          int cols, int rows,
                                          int halfWindow,
                                          float nodataValue) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = cols * rows;
    if (idx >= total) return;

    int r = idx / cols;
    int c = idx % cols;

    float sum = 0.0f;
    int count = 0;
    int cLo = (c - halfWindow > 0) ? (c - halfWindow) : 0;
    int cHi = (c + halfWindow < cols - 1) ? (c + halfWindow) : (cols - 1);

    for (int nc = cLo; nc <= cHi; ++nc) {
        float v = inGrid[r * cols + nc];
        if (v != nodataValue) {
            sum += v;
            count++;
        }
    }

    outSum[idx] = sum;
    outCount[idx] = count;
}

// Kernel: 1D vertical pass completing the separable nodata-aware box smoothing filter.
__global__ void SmoothBoxVerticalKernel(const float* __restrict__ inGrid,
                                        const float* __restrict__ inSum,
                                        const int* __restrict__ inCount,
                                        float* __restrict__ outGrid,
                                        int cols, int rows,
                                        int halfWindow,
                                        float nodataValue) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = cols * rows;
    if (idx >= total) return;

    int r = idx / cols;
    int c = idx % cols;

    if (inGrid[idx] == nodataValue) {
        outGrid[idx] = nodataValue;
        return;
    }

    float sum = 0.0f;
    int count = 0;
    int rLo = (r - halfWindow > 0) ? (r - halfWindow) : 0;
    int rHi = (r + halfWindow < rows - 1) ? (r + halfWindow) : (rows - 1);

    for (int nr = rLo; nr <= rHi; ++nr) {
        sum += inSum[nr * cols + c];
        count += inCount[nr * cols + c];
    }

    if (count > 0) {
        outGrid[idx] = sum / static_cast<float>(count);
    } else {
        outGrid[idx] = nodataValue;
    }
}

} // namespace

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
) {
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount <= 0) {
        return false;
    }

    int numCells = cols * rows;
    if (numCells <= 0 || numPoints <= 0) {
        return false;
    }

    // Allocate device buffers
    float *d_xRel = nullptr, *d_yRel = nullptr, *d_z = nullptr;
    float *d_dtmGrid = nullptr;
    int *d_chmIntGrid = nullptr;
    float *d_chmOutGrid = nullptr;

    size_t ptsSize = static_cast<size_t>(numPoints) * sizeof(float);
    size_t gridIntSize = static_cast<size_t>(numCells) * sizeof(int);
    size_t gridFloatSize = static_cast<size_t>(numCells) * sizeof(float);

    bool success = true;
    if (cudaMalloc(&d_xRel, ptsSize) != cudaSuccess ||
        cudaMalloc(&d_yRel, ptsSize) != cudaSuccess ||
        cudaMalloc(&d_z, ptsSize) != cudaSuccess ||
        cudaMalloc(&d_chmIntGrid, gridIntSize) != cudaSuccess ||
        cudaMalloc(&d_chmOutGrid, gridFloatSize) != cudaSuccess) {
        success = false;
    }

    if (success && h_dtmGrid != nullptr) {
        if (cudaMalloc(&d_dtmGrid, gridFloatSize) != cudaSuccess ||
            cudaMemcpy(d_dtmGrid, h_dtmGrid, gridFloatSize, cudaMemcpyHostToDevice) != cudaSuccess) {
            success = false;
        }
    }

    if (success) {
        // Initialize destination grid to -1 (0xFFFFFFFF in signed int is -1, strictly less than any __float_as_int(h >= 0))
        cudaMemset(d_chmIntGrid, 0xFF, gridIntSize);

        // Upload point arrays
        cudaMemcpy(d_xRel, h_xRel, ptsSize, cudaMemcpyHostToDevice);
        cudaMemcpy(d_yRel, h_yRel, ptsSize, cudaMemcpyHostToDevice);
        cudaMemcpy(d_z, h_z, ptsSize, cudaMemcpyHostToDevice);

        // Launch rasterization kernel
        int threadsPerBlock = 256;
        int blocksForPoints = (numPoints + threadsPerBlock - 1) / threadsPerBlock;
        RasterizeCHMKernel<<<blocksForPoints, threadsPerBlock>>>(
            d_xRel, d_yRel, d_z, d_dtmGrid, d_chmIntGrid, numPoints, cellSize, cols, rows
        );

        // Launch finalization kernel
        int blocksForCells = (numCells + threadsPerBlock - 1) / threadsPerBlock;
        FinalizeCHMKernel<<<blocksForCells, threadsPerBlock>>>(
            d_chmIntGrid, d_chmOutGrid, numCells, nodataValue
        );

        // Optional spatial smoothing on device
        if (smoothWidth >= 3) {
            if (smoothWidth % 2 == 0) smoothWidth += 1;
            int halfWindow = smoothWidth / 2;

            float* d_smoothSum = nullptr;
            int* d_smoothCount = nullptr;
            float* d_smoothedGrid = nullptr;

            if (cudaMalloc(&d_smoothSum, gridFloatSize) == cudaSuccess &&
                cudaMalloc(&d_smoothCount, gridIntSize) == cudaSuccess &&
                cudaMalloc(&d_smoothedGrid, gridFloatSize) == cudaSuccess) {

                SmoothBoxHorizontalKernel<<<blocksForCells, threadsPerBlock>>>(
                    d_chmOutGrid, d_smoothSum, d_smoothCount, cols, rows, halfWindow, nodataValue
                );

                SmoothBoxVerticalKernel<<<blocksForCells, threadsPerBlock>>>(
                    d_chmOutGrid, d_smoothSum, d_smoothCount, d_smoothedGrid, cols, rows, halfWindow, nodataValue
                );

                cudaMemcpy(h_outChmGrid, d_smoothedGrid, gridFloatSize, cudaMemcpyDeviceToHost);

                cudaFree(d_smoothSum);
                cudaFree(d_smoothCount);
                cudaFree(d_smoothedGrid);
            } else {
                cudaMemcpy(h_outChmGrid, d_chmOutGrid, gridFloatSize, cudaMemcpyDeviceToHost);
            }
        } else {
            cudaMemcpy(h_outChmGrid, d_chmOutGrid, gridFloatSize, cudaMemcpyDeviceToHost);
        }

        cudaDeviceSynchronize();
    }

    if (d_xRel) cudaFree(d_xRel);
    if (d_yRel) cudaFree(d_yRel);
    if (d_z) cudaFree(d_z);
    if (d_dtmGrid) cudaFree(d_dtmGrid);
    if (d_chmIntGrid) cudaFree(d_chmIntGrid);
    if (d_chmOutGrid) cudaFree(d_chmOutGrid);

    return success;
}
