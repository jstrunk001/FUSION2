# Evaluation: GPU Acceleration for FUSION2

## 1. Executive Summary
The GPU acceleration architecture and algorithmic innovations developed in `C:\Users\Jacob\Box\sync\R\analyses\2026_RasterMetrics\julia` (specifically in `GpuMetricsPipeline.jl`) demonstrate proven speedups from **50.0 s/tile down to 0.42 s/tile (~119x speedup)** on 106-million-pixel tiles. 

Applying these identical architectural patterns to FUSION2 offers substantial, high-impact performance gains for several core tools.

---

## 2. Core GPU Mechanics in the Julia Project
1. **Shared-Memory CDF Histogram (Quantile Estimation without Sorting)**:
   - Traditional percentile computation (e.g. FUSION's `ComputePointStatBundle`) requires collecting floats in an array and executing a CPU sort (`std::sort` or QuickSelect), which is $O(N \log N)$ and memory-bandwidth heavy.
   - The Julia CUDA kernel allocates a 2,048-bin shared-memory histogram ($8.2\text{ KB}$ per threadblock).
   - 256 CUDA threads cooperatively populate the histogram using atomic additions, compute mean/variance/min/max in registers, and evaluate all 15 canopy percentiles in a single cumulative distribution pass ($<1\text{ ms}$ per grid cell).

2. **Fused DTM Bilinear Interpolation & Differencing**:
   - Rather than pre-interpolating a coarse DTM across the fine resolution canvas on CPU, the CUDA kernel computes sub-millimeter fractional coordinate offsets and samples the 4 coarse DTM corner cells directly in GPU registers, subtracting ground elevation on the fly.
   - Eliminates intermediate raster memory allocations and cache misses.

3. **Multi-Threaded RAM Pipelining**:
   - Hides storage I/O latency behind GPU computation using an asynchronous host-to-device streaming queue.

---

## 3. High-Impact Opportunities in FUSION2

### A. New Tool: Native Raster `gridmetrics` / `rastermetrics`
- **Context**: Currently, FUSION2's `gridmetrics.exe` processes point clouds (`.las`/`.laz`). When users have high-resolution photogrammetric/CHM rasters (e.g., NAIP3D, DAP, stereo-imagery DSMs), calculating canopy metrics requires external pipelines (like R `terra` or Julia `ArchGDAL`).
- **Opportunity**: Porting `GpuMetricsPipeline.jl`'s fused CUDA kernel directly into a native C++/CUDA tool in FUSION2 (e.g., `rastermetrics.exe` or a raster mode in `gridmetrics.exe`) would provide instantaneous canopy metric rasters directly within the FUSION2 CLI suite.

### B. Accelerated CHM Analytics: `canopymaxima` & `topometrics`
- **`canopymaxima.exe`**: Currently performs a nested 2D loop window search on CPU for Variable Window Local Maxima (VLM). On fine grids, window lookups across millions of cells are compute-intensive. On GPU, this is an embarrassingly parallel 2D stencil operation that can execute in tens of milliseconds per tile.
- **`topometrics.exe`**: Horn's slope and aspect algorithms are local $3 \times 3$ finite-difference stencils, mapping directly to simple, highly efficient CUDA kernels.

### C. Point Cloud Metric Aggregation in `gridmetrics.exe`
- **Current Bottleneck**: For dense lidar tiles (20–100M points), `gridmetrics` bins points into `CellAccumulator` vectors and calls `ComputePointStatBundle`, sorting point elevations per cell on CPU.
- **GPU Path**: 
  - Offload cell reductions to GPU via CUDA or Thrust: points are binned into cell IDs, followed by the same 2,048-bin shared-memory histogram reduction.
  - Avoids sorting thousands of points in every cell, accelerating `gridmetrics`'s statistical reduction phase by 10–30x.

---

## 4. Implementation Recommendations & Next Steps
1. **CUDA Optional Dependency in CMake**:
   - Keep FUSION2's core CPU build lightweight and self-contained (no external GPU requirements by default).
   - Add a CMake toggle (`-DENABLE_CUDA=ON`) that links `nvcc` and compiles accelerated kernel modules when an NVIDIA GPU is available.
2. **Benchmark Priority**:
   - Prototype `rastermetrics` in C++/CUDA targeting FUSION2's output standards (GeoTIFF multi-band and SQLite `/output-table`).
