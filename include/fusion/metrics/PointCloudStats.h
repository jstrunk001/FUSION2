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

// The simplified 6-metric summary reported per height-stratum bucket
// (/strata, /intstrata, /rgbstrata) in place of the full 38-field
// PointStatBundle -- a stratum bucket is one of many repeated per row, so
// the full bundle's percentile/L-moment columns multiply out to an
// impractical column count once several buckets are involved.
struct StrataStatBundle {
    int count{0};
    double proportion{0.0};
    float mean{0.0f};
    float stddev{0.0f};
    float min{0.0f};
    float max{0.0f};
};

// totalCloudPoints is the denominator for `proportion` (the whole
// cloud/cell/feature's point count, not just this bucket's) -- pass 0 to
// force proportion to 0 rather than dividing by zero. count/proportion are
// always well-defined, even for an empty bucket (both are simply 0); the
// caller decides whether mean/stddev/min/max keep this bundle's default of
// 0 for an empty bucket or resolve to a /noheight sentinel value instead --
// this function performs no sentinel substitution itself, matching
// ComputePointStatBundle's separation of concerns above.
StrataStatBundle ComputeStrataStatBundle(const std::vector<double>& values, size_t totalCloudPoints);

std::vector<std::string> StrataStatBundleColumnNames(const std::string& prefix);
std::vector<double> StrataStatBundleAsVector(const StrataStatBundle& b);

} // namespace fusion::metrics

#endif // FUSION_METRICS_POINTCLOUDSTATS_H
