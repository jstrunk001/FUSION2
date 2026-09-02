#ifndef FUSION_METRICS_EXPERIMENTALMETRICS_H
#define FUSION_METRICS_EXPERIMENTALMETRICS_H

#include <vector>
#include <string>
#include <map>

namespace fusion::metrics {

struct Point3D {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

struct ExperimentalMetricsOptions {
    double minHt{2.0};        // Minimum height cutoff for canopy points
    double cellSize{10.0};     // 2D grid cell resolution (resxy)
    double voxelSize{20.0};    // 3D voxel resolution (resxyz)
};

struct ExperimentalMetricsResults {
    // Relative Z Ratio Metrics
    double zMinRat{-9999.0};
    double zMaxRat{-9999.0};
    double zSdRat{-9999.0};
    double zMeanRat{-9999.0};
    double zCvRat{-9999.0};
    double zIQRat{-9999.0};

    // X & Y Spatial Range & Variance Metrics
    double xMin{-9999.0};
    double xMax{-9999.0};
    double xSd{-9999.0};
    double xRatHt{-9999.0};

    double yMin{-9999.0};
    double yMax{-9999.0};
    double ySd{-9999.0};
    double yRatHt{-9999.0};

    // 2D Radial Distance & XY Correlation
    double xyMinRt{-9999.0};
    double xyMinRtRat{-9999.0};
    double xyMaxRt{-9999.0};
    double xyMaxRtRat{-9999.0};
    double xySdRt{-9999.0};
    double xySdRtRat{-9999.0};
    double xyMnRt{-9999.0};
    double xyMnRtRat{-9999.0};
    double xyCvRt{-9999.0};
    double xyCvRtRat{-9999.0};
    double xyCor{-9999.0};
    double xyCorRat{-9999.0};

    // 3D Radial Distance & XYZ Correlation
    double xyzMinRt{-9999.0};
    double xyzMinRtRat{-9999.0};
    double xyzMaxRt{-9999.0};
    double xyzMaxRtRat{-9999.0};
    double xyzSdRt{-9999.0};
    double xyzSdRtRat{-9999.0};
    double xyzMnRt{-9999.0};
    double xyzMnRtRat{-9999.0};
    double xyzCvRt{-9999.0};
    double xyzCvRtRat{-9999.0};
    double xyzCor{-9999.0};
    double xyzCorRat{-9999.0};

    // 2D Area & Columnar Volume
    double gridAreaAll{-9999.0};
    double gridArea{-9999.0};
    double areaCover{-9999.0};
    double areaScl{-9999.0};

    double gridVolAll{-9999.0};
    double gridVol{-9999.0};
    double gridVolRat{-9999.0};
    double gridVolScl{-9999.0};

    // 3D Voxel Volume
    double voxVolAll{-9999.0};
    double voxVol{-9999.0};
    double voxVolRat{-9999.0};
    double voxVolScl{-9999.0};
};

ExperimentalMetricsResults ComputeExperimentalMetrics(
    const std::vector<Point3D>& points,
    const ExperimentalMetricsOptions& options
);

std::map<std::string, double> GetExperimentalMetricsAsMap(const ExperimentalMetricsResults& res);
std::vector<std::string> GetExperimentalMetricsNames();

} // namespace fusion::metrics

#endif // FUSION_METRICS_EXPERIMENTALMETRICS_H
