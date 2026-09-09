#ifndef FUSION_METRICS_POINTCLOUDSTATS_H
#define FUSION_METRICS_POINTCLOUDSTATS_H

#include <string>
#include <vector>

namespace fusion::metrics {

struct PointStatBundle {
    float min{0.0f}, max{0.0f}, mean{0.0f}, stddev{0.0f}, variance{0.0f}, cv{0.0f}, skewness{0.0f}, kurtosis{0.0f}, crr{0.0f};
    float mode{0.0f}, median{0.0f}, iqr{0.0f};
    float p01{0.0f}, p05{0.0f}, p10{0.0f}, p20{0.0f}, p25{0.0f}, p30{0.0f}, p40{0.0f}, p50{0.0f}, p60{0.0f}, p70{0.0f}, p75{0.0f}, p80{0.0f}, p90{0.0f}, p95{0.0f}, p99{0.0f};
    float aad{0.0f};                       // average absolute deviation about the mean
    float madMedian{0.0f}, madMode{0.0f};  // average absolute deviation about the median / mode
    float l1{0.0f}, l2{0.0f}, l3{0.0f}, l4{0.0f}, lCV{0.0f}, lSkewness{0.0f}, lKurtosis{0.0f};
    float quadraticMean{0.0f}, cubicMean{0.0f};
};

// values.empty() is a precondition failure for callers -- they resolve the
// nodata/noheight sentinel themselves before calling; this function always
// assumes at least one value. Sorts its own copy, so callers don't need to
// pre-sort.
PointStatBundle ComputePointStatBundle(std::vector<float> values);

// "elev_" or "int_" (or a spectral-channel prefix like "red_") -> full
// column-name list, in the same order PointStatBundleAsVector emits values,
// so the two can't drift out of sync with each other.
std::vector<std::string> PointStatBundleColumnNames(const std::string& prefix);
std::vector<float> PointStatBundleAsVector(const PointStatBundle& b);

// Profile area: the one legacy elevation-stat column not covered by
// PointStatBundle, since it needs the full percentile curve (p01..p99)
// rather than a single-vector reduction. Trapezoidal-rule area under the
// percentile-vs-percentile-rank curve.
float ComputeProfileArea(const PointStatBundle& bundle);

// Shared bucket-assignment logic gridmetrics' /strata and /intstrata (and
// densitymetrics, and cloudmetrics' new /strata support) all need
// identical semantics for: the first threshold index the elevation is
// still >= to, or thresholds.size() if elevation clears every threshold.
size_t AssignStratumIndex(double elevation, const std::vector<double>& thresholds);

} // namespace fusion::metrics

#endif // FUSION_METRICS_POINTCLOUDSTATS_H
