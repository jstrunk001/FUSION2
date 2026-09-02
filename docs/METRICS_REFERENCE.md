# Lidar & Terrain Metrics Reference

This reference manual documents the statistical elevation, canopy structure, return intensity, pulse density, and experimental spatial metrics computed by `gridmetrics`, `cloudmetrics`, and `ExperimentalMetrics`.

---

## 1. Elevation Statistics & Percentiles

Calculated across normalized height values ($Z_i = Z_{point} - Z_{ground}$) for all points or canopy points ($Z_i \ge h_{min}$).

| Metric | Name | Formula / Description |
| :--- | :--- | :--- |
| `elev_min` | Minimum Height | $\min(Z_i)$ |
| `elev_max` | Maximum Height | $\max(Z_i)$ |
| `elev_mean` | Mean Height | $\bar{Z} = \frac{1}{N} \sum_{i=1}^N Z_i$ |
| `elev_mode` | Mode Height | Peak location of height distribution histogram. |
| `elev_stddev` | Standard Deviation | $S = \sqrt{\frac{1}{N-1} \sum (Z_i - \bar{Z})^2}$ |
| `elev_variance` | Variance | $S^2$ |
| `elev_cv` | Coefficient of Variation | $CV = \frac{S}{\bar{Z}}$ |
| `elev_skewness` | Skewness | $\frac{1}{N S^3} \sum (Z_i - \bar{Z})^3$ |
| `elev_kurtosis` | Kurtosis | $\frac{1}{N S^4} \sum (Z_i - \bar{Z})^4 - 3$ |
| `elev_iqr` | Interquartile Range | $IQR = P_{75} - P_{25}$ |
| `elev_p01`–`elev_p99` | Height Percentiles | 1st, 5th, 10th, 20th, 25th, 30th, 40th, 50th, 60th, 70th, 75th, 80th, 90th, 95th, 99th height percentiles. |

---

## 2. Canopy Cover & Structure Metrics

| Metric | Name | Formula / Description |
| :--- | :--- | :--- |
| `canopy_cover` / `cover_2m` | Canopy Cover Fraction | Fraction of returns above the canopy height cutoff ($h_{min}$, default 2.0 m):<br>$$\text{Cover} = \frac{N_{Z \ge h_{min}}}{N_{total}}$$ |
| `canopy_relief_ratio` | Canopy Relief Ratio ($CRR$) | Relative position of mean height within total height range:<br>$$CRR = \frac{\bar{Z} - Z_{min}}{Z_{max} - Z_{min}}$$ |
| `strata_fraction_i` | Height Strata Fraction | Proportion of returns falling into specified height interval $[h_a, h_b]$. |

---

## 3. Pulse Density & Return Type Ratios

Calculated by `returndensity.exe` and `gridmetrics.exe`:

| Metric | Name | Description |
| :--- | :--- | :--- |
| `pulse_density` | Pulse Density | Total first-returns or unique pulses per square meter (pts/m²). |
| `point_density` | Total Point Density | Total returns of all return types per square meter (pts/m²). |
| `first_return_ratio` | First Return Percentage | $\frac{N_{first}}{N_{total}} \times 100\%$ |
| `ground_return_ratio` | Ground Return Percentage | Percentage of classified ground returns ($Class = 2$). |
| `int_mean_first` | First Return Mean Intensity | Mean return intensity of first-returns. |
| `int_mean_all` | All Return Mean Intensity | Mean return intensity across all returns. |

---

## 4. Novel 2D & 3D Experimental Spatial Metrics

Computed by `fusion::metrics::ComputeExperimentalMetrics` in `ExperimentalMetrics.h` using 2D grid resolution (`cellSize`) and 3D voxel resolution (`voxelSize`).

### 4.1 Relative Height Ratio Metrics
Ratio of canopy height parameters ($Z \ge h_{min}$) relative to overall plot height statistics:

- `zMinRat`: Relative minimum canopy height ratio ($|\frac{Z_{min,canopy} - Z_{min,all}}{Z_{min,all}}| \times 100$).
- `zMaxRat`: Canopy maximum relative ratio ($100 \times \frac{Z_{max,canopy}}{Z_{max,all}}$).
- `zSdRat`: Canopy height standard deviation ratio ($100 \times \frac{S_{canopy}}{S_{all}}$).
- `zMeanRat`: Canopy mean height ratio ($100 \times \frac{\bar{Z}_{canopy}}{\bar{Z}_{all}}$).
- `zCvRat`: Canopy height CV ratio ($100 \times \frac{CV_{canopy}}{CV_{all}}$).

---

### 4.2 2D & 3D Spatial Radial Distances & Correlations

- **2D Radial Distance**: $R_{xy} = \sqrt{X^2 + Y^2}$
  - `xyMinRt`, `xyMaxRt`, `xySdRt`, `xyMnRt`, `xyCvRt`: Min, Max, StdDev, Mean, and CV of 2D radial distances.
  - `xyMinRtRat`, `xyMaxRtRat`, `xySdRtRat`, `xyMnRtRat`, `xyCvRtRat`: Ratio of canopy subset to overall 2D radial metrics ($100 \times \frac{M_{canopy}}{M_{all}}$).
  - `xyCor`, `xyCorRat`: Pearson correlation coefficient between $X$ and $Y$ coordinates ($\rho_{X,Y}$).

- **3D Radial Distance**: $R_{xyz} = \sqrt{X^2 + Y^2 + Z^2}$
  - `xyzMinRt`, `xyzMaxRt`, `xyzSdRt`, `xyzMnRt`, `xyzCvRt`: Min, Max, StdDev, Mean, and CV of 3D radial distances.
  - `xyzCor`, `xyzCorRat`: Pearson correlation coefficient between 2D radial distance $R_{xy}$ and height $Z$ ($\rho_{R_{xy}, Z}$).

---

### 4.3 2D Area & Columnar Grid Volume

Grid cells of size `cellSize` $\times$ `cellSize` ($A_{cell} = \text{cellSize}^2$).

- `gridAreaAll`: Total footprint area of occupied 2D grid cells ($N_{cells,all} \times A_{cell}$).
- `gridArea`: Canopy footprint area of grid cells containing canopy returns ($N_{cells,canopy} \times A_{cell}$).
- `areaCover`: Canopy area coverage ratio ($100 \times \frac{\text{gridArea}}{\text{gridAreaAll}}$).
- `gridVolAll`: Total column grid volume ($\sum_{\text{occupied cells}} A_{cell} \times (Z_{max,cell} - Z_{min,cell})$).
- `gridVol`: Canopy column grid volume ($\sum_{\text{canopy cells}} A_{cell} \times (Z_{max,cell} - Z_{min,cell})$).
- `gridVolRat`: Canopy column volume percentage ($100 \times \frac{\text{gridVol}}{\text{gridVolAll}}$).

---

### 4.4 3D Voxel Volume

Voxels of size `voxelSize` $\times$ `voxelSize` $\times$ `voxelSize` ($V_{voxel} = \text{voxelSize}^3$).

- `voxVolAll`: Total occupied 3D voxel volume ($N_{voxels,all} \times V_{voxel}$).
- `voxVol`: Occupied canopy voxel volume ($N_{voxels,canopy} \times V_{voxel}$).
- `voxVolRat`: Voxel volume canopy ratio ($100 \times \frac{\text{voxVol}}{\text{voxVolAll}}$).
- `voxVolScl`: Scaled voxel occupancy metric ($\frac{\text{voxVol}}{\text{cellSize} \times \text{voxelSize}}$).
