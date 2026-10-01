// gridmetrics_raster_cuda.cu : CUDA fused raster zonal analytics kernel for FUSION2
//
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <vector>
#include <string>
#include <cmath>
#include <cfloat>

namespace {

constexpr int kNumBins = 2048;
constexpr int kNumBands = 33;
constexpr float kEpsilon = 1e-7f;

// 33 standard non-prohibitive metric band indices
enum MetricIndex {
    M_MEAN = 0,
    M_STDDEV,
    M_VARIANCE,
    M_CV,
    M_MIN,
    M_MAX,
    M_CRR,
    M_P01,
    M_P05,
    M_P10,
    M_P20,
    M_P25,
    M_P30,
    M_P40,
    M_P50,
    M_P60,
    M_P70,
    M_P75,
    M_P80,
    M_P90,
    M_P95,
    M_P99,
    M_IQR,
    M_MODE,
    M_AAD,
    M_MAD_MEDIAN,
    M_MAD_MODE,
    M_SKEWNESS,
    M_KURTOSIS,
    M_QUADRATIC_MEAN,
    M_CUBIC_MEAN,
    M_CANOPY_COVER,
    M_POINT_DENSITY
};

// Warp-level reduction helpers
__device__ inline float WarpReduceSum(float val) {
    for (int offset = 16; offset > 0; offset /= 2) {
        val += __shfl_down_sync(0xFFFFFFFF, val, offset);
    }
    return val;
}

__device__ inline float WarpReduceMin(float val) {
    for (int offset = 16; offset > 0; offset /= 2) {
        val = fminf(val, __shfl_down_sync(0xFFFFFFFF, val, offset));
    }
    return val;
}

__device__ inline float WarpReduceMax(float val) {
    for (int offset = 16; offset > 0; offset /= 2) {
        val = fmaxf(val, __shfl_down_sync(0xFFFFFFFF, val, offset));
    }
    return val;
}

// Bitonic sort for small N (< 256) in shared memory
__device__ void BitonicSortShared(float* sharedArray, int n) {
    int powerOfTwo = 1;
    while (powerOfTwo < n) powerOfTwo <<= 1;

    for (int k = 2; k <= powerOfTwo; k <<= 1) {
        for (int j = k >> 1; j > 0; j >>= 1) {
            for (int i = threadIdx.x; i < powerOfTwo; i += blockDim.x) {
                int ixj = i ^ j;
                if (ixj > i) {
                    if ((i & k) == 0) {
                        if (i < n && ixj < n && sharedArray[i] > sharedArray[ixj]) {
                            float tmp = sharedArray[i];
                            sharedArray[i] = sharedArray[ixj];
                            sharedArray[ixj] = tmp;
                        }
                    } else {
                        if (i < n && ixj < n && sharedArray[i] < sharedArray[ixj]) {
                            float tmp = sharedArray[i];
                            sharedArray[i] = sharedArray[ixj];
                            sharedArray[ixj] = tmp;
                        }
                    }
                }
            }
            __syncthreads();
        }
    }
}

// Kernel: One thread block per coarse zone, evaluating all fine pixels inside
__global__ void FusedZonalMetricsKernel(
    const float* __restrict__ dsmGrid,
    const float* __restrict__ dtmGrid,
    int fineCols, int fineRows,
    float fineCellSize,
    float coarseCellSize,
    int coarseCols, int coarseRows,
    float minHt,
    float heightCut,
    float nodataValue,
    float* __restrict__ outBands // size: kNumBands * (coarseCols * coarseRows)
) {
    int zoneCol = blockIdx.x;
    int zoneRow = blockIdx.y;
    if (zoneCol >= coarseCols || zoneRow >= coarseRows) return;

    int zoneIdx = zoneRow * coarseCols + zoneCol;
    int tid = threadIdx.x;
    int blockSize = blockDim.x;

    // Pixel bounds of this coarse zone in fine grid coordinates
    float zoneStartX = zoneCol * coarseCellSize;
    float zoneEndX = zoneStartX + coarseCellSize;
    float zoneStartY = zoneRow * coarseCellSize;
    float zoneEndY = zoneStartY + coarseCellSize;

    int minFineCol = static_cast<int>(zoneStartX / fineCellSize);
    int maxFineCol = static_cast<int>(zoneEndX / fineCellSize);
    int minFineRow = static_cast<int>(zoneStartY / fineCellSize);
    int maxFineRow = static_cast<int>(zoneEndY / fineCellSize);

    if (minFineCol < 0) minFineCol = 0;
    if (maxFineCol > fineCols) maxFineCol = fineCols;
    if (minFineRow < 0) minFineRow = 0;
    if (maxFineRow > fineRows) maxFineRow = fineRows;

    int totalZonePixels = (maxFineCol - minFineCol) * (maxFineRow - minFineRow);

    __shared__ int s_hist[kNumBins];
    __shared__ int s_cdf[kNumBins];
    __shared__ float s_smallArray[256];
    __shared__ int s_totalValid;
    __shared__ int s_aboveCut;
    __shared__ int s_aboveMin;
    __shared__ float s_zMin;
    __shared__ float s_zMax;
    __shared__ float s_sum;
    __shared__ float s_sumSq;
    __shared__ float s_sumCb;

    // Clear histogram
    for (int b = tid; b < kNumBins; b += blockSize) {
        s_hist[b] = 0;
        s_cdf[b] = 0;
    }
    if (tid == 0) {
        s_totalValid = 0;
        s_aboveCut = 0;
        s_aboveMin = 0;
        s_zMin = FLT_MAX;
        s_zMax = -FLT_MAX;
        s_sum = 0.0f;
        s_sumSq = 0.0f;
        s_sumCb = 0.0f;
    }
    __syncthreads();

    // First reduction pass: evaluate total returns, extremes, and raw moments
    int localValid = 0;
    int localAboveCut = 0;
    int localAboveMin = 0;
    float localZMin = FLT_MAX;
    float localZMax = -FLT_MAX;
    float localSum = 0.0f;
    float localSumSq = 0.0f;
    float localSumCb = 0.0f;

    for (int i = tid; i < totalZonePixels; i += blockSize) {
        int r = minFineRow + (i / (maxFineCol - minFineCol));
        int c = minFineCol + (i % (maxFineCol - minFineCol));
        if (r < fineRows && c < fineCols) {
            int fineIdx = r * fineCols + c;
            float rawZ = dsmGrid[fineIdx];
            if (rawZ != nodataValue && !isnan(rawZ)) {
                float ground = (dtmGrid != nullptr) ? dtmGrid[fineIdx] : 0.0f;
                float h = rawZ - ground;
                localValid++;
                if (h >= heightCut) localAboveCut++;
                if (h >= minHt) {
                    localAboveMin++;
                    localZMin = fminf(localZMin, h);
                    localZMax = fmaxf(localZMax, h);
                    localSum += h;
                    localSumSq += (h * h);
                    localSumCb += (h * h * h);
                }
            }
        }
    }

    atomicAdd(&s_totalValid, localValid);
    atomicAdd(&s_aboveCut, localAboveCut);
    atomicAdd(&s_aboveMin, localAboveMin);
    atomicMin(reinterpret_cast<int*>(&s_zMin), __float_as_int(fmaxf(0.0f, localZMin)));
    atomicMax(reinterpret_cast<int*>(&s_zMax), __float_as_int(fmaxf(0.0f, localZMax)));
    atomicAdd(&s_sum, localSum);
    atomicAdd(&s_sumSq, localSumSq);
    atomicAdd(&s_sumCb, localSumCb);
    __syncthreads();

    int nValid = s_totalValid;
    int nCut = s_aboveCut;
    int nMin = s_aboveMin;

    if (nValid == 0) {
        if (tid < kNumBands) {
            outBands[tid * (coarseCols * coarseRows) + zoneIdx] = nodataValue;
        }
        return;
    }

    float zMin = (nMin > 0) ? s_zMin : 0.0f;
    float zMax = (nMin > 0) ? s_zMax : 0.0f;
    float mean = (nMin > 0) ? (s_sum / static_cast<float>(nMin)) : 0.0f;
    float variance = (nMin > 1) ? (fmaxf(0.0f, (s_sumSq - (s_sum * s_sum) / nMin) / (nMin - 1))) : 0.0f;
    float stddev = sqrtf(variance);
    float cv = (mean > kEpsilon) ? (stddev / mean * 100.0f) : 0.0f;
    float crr = (zMax - zMin > kEpsilon) ? ((mean - zMin) / (zMax - zMin)) : 0.0f;
    float quadMean = (nMin > 0) ? sqrtf(s_sumSq / nMin) : 0.0f;
    float cubicMean = (nMin > 0) ? cbrtf(s_sumCb / nMin) : 0.0f;
    float cover = (nValid > 0) ? (static_cast<float>(nCut) / static_cast<float>(nValid) * 100.0f) : 0.0f;
    float pointDensity = static_cast<float>(nValid) / (coarseCellSize * coarseCellSize);

    // Second pass: Small N (< 256) branch via bitonic sort vs large N histogram CDF
    if (nMin > 0 && nMin <= 256) {
        // Collect points into shared small array
        __shared__ int s_insertIdx;
        if (tid == 0) s_insertIdx = 0;
        __syncthreads();

        for (int i = tid; i < totalZonePixels; i += blockSize) {
            int r = minFineRow + (i / (maxFineCol - minFineCol));
            int c = minFineCol + (i % (maxFineCol - minFineCol));
            if (r < fineRows && c < fineCols) {
                float rawZ = dsmGrid[r * fineCols + c];
                if (rawZ != nodataValue && !isnan(rawZ)) {
                    float ground = (dtmGrid != nullptr) ? dtmGrid[r * fineCols + c] : 0.0f;
                    float h = rawZ - ground;
                    if (h >= minHt) {
                        int pos = atomicAdd(&s_insertIdx, 1);
                        if (pos < 256) s_smallArray[pos] = h;
                    }
                }
            }
        }
        __syncthreads();

        BitonicSortShared(s_smallArray, nMin);

        if (tid == 0) {
            auto q = [&](float rank) -> float {
                int idx = static_cast<int>(roundf(rank * (nMin - 1)));
                if (idx < 0) idx = 0;
                if (idx >= nMin) idx = nMin - 1;
                return s_smallArray[idx];
            };

            float p01 = q(0.01f), p05 = q(0.05f), p10 = q(0.10f), p20 = q(0.20f), p25 = q(0.25f);
            float p30 = q(0.30f), p40 = q(0.40f), p50 = q(0.50f), p60 = q(0.60f), p70 = q(0.70f);
            float p75 = q(0.75f), p80 = q(0.80f), p90 = q(0.90f), p95 = q(0.95f), p99 = q(0.99f);
            float iqr = p75 - p25;

            // Single-pass differences for AAD, MAD, skew, kurtosis
            float aadSum = 0.0f, madMedSum = 0.0f, m3Sum = 0.0f, m4Sum = 0.0f;
            for (int k = 0; k < nMin; ++k) {
                float v = s_smallArray[k];
                float dev = v - mean;
                aadSum += fabsf(dev);
                madMedSum += fabsf(v - p50);
                m3Sum += (dev * dev * dev);
                m4Sum += (dev * dev * dev * dev);
            }
            float aad = aadSum / nMin;
            float madMedian = madMedSum / nMin;
            float skewness = (stddev > kEpsilon && nMin > 2) ? ((m3Sum / nMin) / (stddev * stddev * stddev)) : 0.0f;
            float kurtosis = (stddev > kEpsilon && nMin > 3) ? ((m4Sum / nMin) / (variance * variance) - 3.0f) : 0.0f;

            int totalZones = coarseCols * coarseRows;
            outBands[M_MEAN * totalZones + zoneIdx] = mean;
            outBands[M_STDDEV * totalZones + zoneIdx] = stddev;
            outBands[M_VARIANCE * totalZones + zoneIdx] = variance;
            outBands[M_CV * totalZones + zoneIdx] = cv;
            outBands[M_MIN * totalZones + zoneIdx] = zMin;
            outBands[M_MAX * totalZones + zoneIdx] = zMax;
            outBands[M_CRR * totalZones + zoneIdx] = crr;
            outBands[M_P01 * totalZones + zoneIdx] = p01;
            outBands[M_P05 * totalZones + zoneIdx] = p05;
            outBands[M_P10 * totalZones + zoneIdx] = p10;
            outBands[M_P20 * totalZones + zoneIdx] = p20;
            outBands[M_P25 * totalZones + zoneIdx] = p25;
            outBands[M_P30 * totalZones + zoneIdx] = p30;
            outBands[M_P40 * totalZones + zoneIdx] = p40;
            outBands[M_P50 * totalZones + zoneIdx] = p50;
            outBands[M_P60 * totalZones + zoneIdx] = p60;
            outBands[M_P70 * totalZones + zoneIdx] = p70;
            outBands[M_P75 * totalZones + zoneIdx] = p75;
            outBands[M_P80 * totalZones + zoneIdx] = p80;
            outBands[M_P90 * totalZones + zoneIdx] = p90;
            outBands[M_P95 * totalZones + zoneIdx] = p95;
            outBands[M_P99 * totalZones + zoneIdx] = p99;
            outBands[M_IQR * totalZones + zoneIdx] = iqr;
            outBands[M_MODE * totalZones + zoneIdx] = p50; // Mode proxy for small sample
            outBands[M_AAD * totalZones + zoneIdx] = aad;
            outBands[M_MAD_MEDIAN * totalZones + zoneIdx] = madMedian;
            outBands[M_MAD_MODE * totalZones + zoneIdx] = madMedian;
            outBands[M_SKEWNESS * totalZones + zoneIdx] = skewness;
            outBands[M_KURTOSIS * totalZones + zoneIdx] = kurtosis;
            outBands[M_QUADRATIC_MEAN * totalZones + zoneIdx] = quadMean;
            outBands[M_CUBIC_MEAN * totalZones + zoneIdx] = cubicMean;
            outBands[M_CANOPY_COVER * totalZones + zoneIdx] = cover;
            outBands[M_POINT_DENSITY * totalZones + zoneIdx] = pointDensity;
        }
    } else if (nMin > 256) {
        // Dynamic histogram pass
        float binWidth = (zMax - zMin > kEpsilon) ? ((zMax - zMin) / kNumBins) : 1.0f;
        for (int i = tid; i < totalZonePixels; i += blockSize) {
            int r = minFineRow + (i / (maxFineCol - minFineCol));
            int c = minFineCol + (i % (maxFineCol - minFineCol));
            if (r < fineRows && c < fineCols) {
                float rawZ = dsmGrid[r * fineCols + c];
                if (rawZ != nodataValue && !isnan(rawZ)) {
                    float ground = (dtmGrid != nullptr) ? dtmGrid[r * fineCols + c] : 0.0f;
                    float h = rawZ - ground;
                    if (h >= minHt) {
                        int b = static_cast<int>((h - zMin) / binWidth);
                        if (b < 0) b = 0;
                        if (b >= kNumBins) b = kNumBins - 1;
                        atomicAdd(&s_hist[b], 1);
                    }
                }
            }
        }
        __syncthreads();

        // Single-thread CDF construction and percentile lookup
        if (tid == 0) {
            int runningSum = 0;
            int maxCount = -1;
            int modeBin = 0;
            for (int b = 0; b < kNumBins; ++b) {
                int c = s_hist[b];
                runningSum += c;
                s_cdf[b] = runningSum;
                if (c > maxCount) {
                    maxCount = c;
                    modeBin = b;
                }
            }

            auto getPercentile = [&](float pct) -> float {
                int target = static_cast<int>(ceilf(pct * nMin));
                for (int b = 0; b < kNumBins; ++b) {
                    if (s_cdf[b] >= target) {
                        return zMin + (b + 0.5f) * binWidth;
                    }
                }
                return zMax;
            };

            float p01 = getPercentile(0.01f), p05 = getPercentile(0.05f), p10 = getPercentile(0.10f);
            float p20 = getPercentile(0.20f), p25 = getPercentile(0.25f), p30 = getPercentile(0.30f);
            float p40 = getPercentile(0.40f), p50 = getPercentile(0.50f), p60 = getPercentile(0.60f);
            float p70 = getPercentile(0.70f), p75 = getPercentile(0.75f), p80 = getPercentile(0.80f);
            float p90 = getPercentile(0.90f), p95 = getPercentile(0.95f), p99 = getPercentile(0.99f);
            float iqr = p75 - p25;
            float mode = zMin + (modeBin + 0.5f) * binWidth;

            // Compute higher moments using bin centers
            float aadSum = 0.0f, madMedSum = 0.0f, madModeSum = 0.0f;
            float m3Sum = 0.0f, m4Sum = 0.0f;
            for (int b = 0; b < kNumBins; ++b) {
                int cnt = s_hist[b];
                if (cnt > 0) {
                    float val = zMin + (b + 0.5f) * binWidth;
                    float dev = val - mean;
                    aadSum += cnt * fabsf(dev);
                    madMedSum += cnt * fabsf(val - p50);
                    madModeSum += cnt * fabsf(val - mode);
                    m3Sum += cnt * (dev * dev * dev);
                    m4Sum += cnt * (dev * dev * dev * dev);
                }
            }
            float aad = aadSum / nMin;
            float madMedian = madMedSum / nMin;
            float madMode = madModeSum / nMin;
            float skewness = (stddev > kEpsilon) ? ((m3Sum / nMin) / (stddev * stddev * stddev)) : 0.0f;
            float kurtosis = (stddev > kEpsilon) ? ((m4Sum / nMin) / (variance * variance) - 3.0f) : 0.0f;

            int totalZones = coarseCols * coarseRows;
            outBands[M_MEAN * totalZones + zoneIdx] = mean;
            outBands[M_STDDEV * totalZones + zoneIdx] = stddev;
            outBands[M_VARIANCE * totalZones + zoneIdx] = variance;
            outBands[M_CV * totalZones + zoneIdx] = cv;
            outBands[M_MIN * totalZones + zoneIdx] = zMin;
            outBands[M_MAX * totalZones + zoneIdx] = zMax;
            outBands[M_CRR * totalZones + zoneIdx] = crr;
            outBands[M_P01 * totalZones + zoneIdx] = p01;
            outBands[M_P05 * totalZones + zoneIdx] = p05;
            outBands[M_P10 * totalZones + zoneIdx] = p10;
            outBands[M_P20 * totalZones + zoneIdx] = p20;
            outBands[M_P25 * totalZones + zoneIdx] = p25;
            outBands[M_P30 * totalZones + zoneIdx] = p30;
            outBands[M_P40 * totalZones + zoneIdx] = p40;
            outBands[M_P50 * totalZones + zoneIdx] = p50;
            outBands[M_P60 * totalZones + zoneIdx] = p60;
            outBands[M_P70 * totalZones + zoneIdx] = p70;
            outBands[M_P75 * totalZones + zoneIdx] = p75;
            outBands[M_P80 * totalZones + zoneIdx] = p80;
            outBands[M_P90 * totalZones + zoneIdx] = p90;
            outBands[M_P95 * totalZones + zoneIdx] = p95;
            outBands[M_P99 * totalZones + zoneIdx] = p99;
            outBands[M_IQR * totalZones + zoneIdx] = iqr;
            outBands[M_MODE * totalZones + zoneIdx] = mode;
            outBands[M_AAD * totalZones + zoneIdx] = aad;
            outBands[M_MAD_MEDIAN * totalZones + zoneIdx] = madMedian;
            outBands[M_MAD_MODE * totalZones + zoneIdx] = madMode;
            outBands[M_SKEWNESS * totalZones + zoneIdx] = skewness;
            outBands[M_KURTOSIS * totalZones + zoneIdx] = kurtosis;
            outBands[M_QUADRATIC_MEAN * totalZones + zoneIdx] = quadMean;
            outBands[M_CUBIC_MEAN * totalZones + zoneIdx] = cubicMean;
            outBands[M_CANOPY_COVER * totalZones + zoneIdx] = cover;
            outBands[M_POINT_DENSITY * totalZones + zoneIdx] = pointDensity;
        }
    } else {
        // nMin == 0, but nValid > 0
        if (tid == 0) {
            int totalZones = coarseCols * coarseRows;
            for (int b = 0; b < kNumBands; ++b) {
                outBands[b * totalZones + zoneIdx] = nodataValue;
            }
            outBands[M_CANOPY_COVER * totalZones + zoneIdx] = 0.0f;
            outBands[M_POINT_DENSITY * totalZones + zoneIdx] = pointDensity;
        }
    }
}

} // namespace

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
) {
    int deviceCount = 0;
    if (cudaGetDeviceCount(&deviceCount) != cudaSuccess || deviceCount <= 0) {
        return false;
    }

    size_t fineTotal = static_cast<size_t>(fineCols) * fineRows;
    size_t coarseTotal = static_cast<size_t>(coarseCols) * coarseRows;
    if (fineTotal == 0 || coarseTotal == 0) return false;

    float* d_dsm = nullptr;
    float* d_dtm = nullptr;
    float* d_outBands = nullptr;

    size_t fineBytes = fineTotal * sizeof(float);
    size_t outBytes = kNumBands * coarseTotal * sizeof(float);

    bool success = true;
    if (cudaMalloc(&d_dsm, fineBytes) != cudaSuccess ||
        cudaMalloc(&d_outBands, outBytes) != cudaSuccess) {
        success = false;
    }

    if (success && h_dtmGrid != nullptr) {
        if (cudaMalloc(&d_dtm, fineBytes) != cudaSuccess ||
            cudaMemcpy(d_dtm, h_dtmGrid, fineBytes, cudaMemcpyHostToDevice) != cudaSuccess) {
            success = false;
        }
    }

    if (success) {
        cudaMemcpy(d_dsm, h_dsmGrid, fineBytes, cudaMemcpyHostToDevice);

        dim3 blocks(coarseCols, coarseRows);
        int threadsPerBlock = 256;

        FusedZonalMetricsKernel<<<blocks, threadsPerBlock>>>(
            d_dsm, d_dtm,
            fineCols, fineRows,
            fineCellSize, coarseCellSize,
            coarseCols, coarseRows,
            minHt, heightCut, nodataValue,
            d_outBands
        );

        cudaMemcpy(h_outBands, d_outBands, outBytes, cudaMemcpyDeviceToHost);
        cudaDeviceSynchronize();
    }

    if (d_dsm) cudaFree(d_dsm);
    if (d_dtm) cudaFree(d_dtm);
    if (d_outBands) cudaFree(d_outBands);

    return success;
}
