#include "fusion/metrics/PointCloudStats.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace fusion::metrics {

namespace {

// Moved unchanged from gridmetrics.cpp -- both tools already included and
// called the equivalent logic before this shared file existed.
float GetPercentile(const std::vector<float>& sortedData, double p) {
    if (sortedData.empty()) return -9999.0f;
    if (sortedData.size() == 1) return sortedData[0];
    double idx = p * (sortedData.size() - 1);
    size_t i0 = static_cast<size_t>(std::floor(idx));
    size_t i1 = std::min(i0 + 1, sortedData.size() - 1);
    double frac = idx - i0;
    return static_cast<float>((1.0 - frac) * sortedData[i0] + frac * sortedData[i1]);
}

// sortedData must already be sorted ascending -- the only caller,
// ComputePointStatBundle(), sorts its working copy before calling this.
// Binning a value into floor(v / binSize) is a non-decreasing function of v
// for binSize > 0, so on sorted input every bin's members land in one
// contiguous run; that lets this walk the data once, tracking only the
// current run's bin/count and the best run seen so far, instead of
// building a std::map<int,int> (one heap-allocated tree node per distinct
// bin) to group values by bin before picking the largest group. Tie-
// breaking (more than one bin sharing the maximum count) matches the
// original map-based version: the first (lowest-elevation) bin to reach
// the maximum count wins, since bins are visited in ascending order and a
// later bin only overtakes the current best on a strictly greater count.
float GetMode(const std::vector<float>& sortedData, float binSize = 0.5f) {
    if (sortedData.empty()) return -9999.0f;

    int bestBin = static_cast<int>(std::floor(sortedData[0] / binSize));
    int bestCount = 0;
    int currentBin = bestBin;
    int currentCount = 0;

    for (float v : sortedData) {
        int b = static_cast<int>(std::floor(v / binSize));
        if (b == currentBin) {
            currentCount++;
        } else {
            currentBin = b;
            currentCount = 1;
        }
        if (currentCount > bestCount) {
            bestCount = currentCount;
            bestBin = currentBin;
        }
    }

    return (bestBin + 0.5f) * binSize;
}

// Sample L-moments (Hosking 1990) from already-sorted data, via the
// standard probability-weighted-moment estimators b0..b3. Guards against
// the sample sizes each higher moment needs (b1 needs n>=2, b2 needs n>=3,
// b3 needs n>=4) by leaving the corresponding L-moment (and anything
// derived from it) at 0 for smaller samples, rather than dividing by zero.
struct LMoments {
    double l1{0.0}, l2{0.0}, l3{0.0}, l4{0.0};
};

LMoments ComputeLMoments(const std::vector<float>& sortedData) {
    LMoments result;
    const size_t n = sortedData.size();
    if (n == 0) return result;

    double b0 = 0.0, b1 = 0.0, b2 = 0.0, b3 = 0.0;
    for (size_t idx = 0; idx < n; ++idx) {
        double i = static_cast<double>(idx + 1); // 1-indexed order statistic rank
        double x = sortedData[idx];
        b0 += x;
        if (n >= 2) b1 += x * (i - 1.0) / (static_cast<double>(n) - 1.0);
        if (n >= 3) b2 += x * (i - 1.0) * (i - 2.0) / ((static_cast<double>(n) - 1.0) * (static_cast<double>(n) - 2.0));
        if (n >= 4) b3 += x * (i - 1.0) * (i - 2.0) * (i - 3.0) / ((static_cast<double>(n) - 1.0) * (static_cast<double>(n) - 2.0) * (static_cast<double>(n) - 3.0));
    }
    b0 /= n;
    b1 /= n;
    b2 /= n;
    b3 /= n;

    result.l1 = b0;
    if (n >= 2) result.l2 = 2.0 * b1 - b0;
    if (n >= 3) result.l3 = 6.0 * b2 - 6.0 * b1 + b0;
    if (n >= 4) result.l4 = 20.0 * b3 - 30.0 * b2 + 12.0 * b1 - b0;
    return result;
}

} // namespace

PointStatBundle ComputePointStatBundle(std::vector<float> values) {
    PointStatBundle b;
    std::sort(values.begin(), values.end());

    const size_t n = values.size();
    double minV = values.front();
    double maxV = values.back();
    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / n;

    double sqSum = 0.0, cubeSum = 0.0, quadSum = 0.0, absSum = 0.0, sqValSum = 0.0, cubeValSum = 0.0;
    for (float v : values) {
        double diff = v - mean;
        sqSum += diff * diff;
        cubeSum += diff * diff * diff;
        quadSum += diff * diff * diff * diff;
        absSum += std::abs(diff);
        sqValSum += static_cast<double>(v) * v;
        cubeValSum += static_cast<double>(v) * v * v;
    }

    double var = (n > 1) ? (sqSum / (n - 1)) : 0.0;
    double stdDev = std::sqrt(sqSum / n);
    double cv = (mean != 0.0) ? (stdDev / mean) : 0.0;
    double skew = (stdDev > 0.0) ? ((cubeSum / n) / std::pow(stdDev, 3.0)) : 0.0;
    double kurt = (stdDev > 0.0) ? ((quadSum / n) / std::pow(stdDev, 4.0)) : 0.0;
    double crr = (maxV > minV) ? ((mean - minV) / (maxV - minV)) : 0.0;

    b.min = static_cast<float>(minV);
    b.max = static_cast<float>(maxV);
    b.mean = static_cast<float>(mean);
    b.stddev = static_cast<float>(stdDev);
    b.variance = static_cast<float>(var);
    b.cv = static_cast<float>(cv);
    b.skewness = static_cast<float>(skew);
    b.kurtosis = static_cast<float>(kurt);
    b.crr = static_cast<float>(crr);

    b.mode = GetMode(values);
    b.median = GetPercentile(values, 0.50);
    b.iqr = GetPercentile(values, 0.75) - GetPercentile(values, 0.25);

    b.p01 = GetPercentile(values, 0.01);
    b.p05 = GetPercentile(values, 0.05);
    b.p10 = GetPercentile(values, 0.10);
    b.p20 = GetPercentile(values, 0.20);
    b.p25 = GetPercentile(values, 0.25);
    b.p30 = GetPercentile(values, 0.30);
    b.p40 = GetPercentile(values, 0.40);
    b.p50 = GetPercentile(values, 0.50);
    b.p60 = GetPercentile(values, 0.60);
    b.p70 = GetPercentile(values, 0.70);
    b.p75 = GetPercentile(values, 0.75);
    b.p80 = GetPercentile(values, 0.80);
    b.p90 = GetPercentile(values, 0.90);
    b.p95 = GetPercentile(values, 0.95);
    b.p99 = GetPercentile(values, 0.99);

    b.aad = static_cast<float>(absSum / n);

    double madMedianSum = 0.0, madModeSum = 0.0;
    for (float v : values) {
        madMedianSum += std::abs(v - b.median);
        madModeSum += std::abs(v - b.mode);
    }
    b.madMedian = static_cast<float>(madMedianSum / n);
    b.madMode = static_cast<float>(madModeSum / n);

    LMoments lm = ComputeLMoments(values);
    b.l1 = static_cast<float>(lm.l1);
    b.l2 = static_cast<float>(lm.l2);
    b.l3 = static_cast<float>(lm.l3);
    b.l4 = static_cast<float>(lm.l4);
    b.lCV = (lm.l1 != 0.0) ? static_cast<float>(lm.l2 / lm.l1) : 0.0f;
    b.lSkewness = (lm.l2 != 0.0) ? static_cast<float>(lm.l3 / lm.l2) : 0.0f;
    b.lKurtosis = (lm.l2 != 0.0) ? static_cast<float>(lm.l4 / lm.l2) : 0.0f;

    b.quadraticMean = static_cast<float>(std::sqrt(sqValSum / n));
    b.cubicMean = static_cast<float>(std::cbrt(cubeValSum / n));

    return b;
}

std::vector<std::string> PointStatBundleColumnNames(const std::string& prefix) {
    return {
        prefix + "min", prefix + "max", prefix + "mean", prefix + "stddev", prefix + "variance",
        prefix + "cv", prefix + "skewness", prefix + "kurtosis", prefix + "crr",
        prefix + "mode", prefix + "median", prefix + "iqr",
        prefix + "p01", prefix + "p05", prefix + "p10", prefix + "p20", prefix + "p25",
        prefix + "p30", prefix + "p40", prefix + "p50", prefix + "p60", prefix + "p70",
        prefix + "p75", prefix + "p80", prefix + "p90", prefix + "p95", prefix + "p99",
        prefix + "aad", prefix + "mad_median", prefix + "mad_mode",
        prefix + "l1", prefix + "l2", prefix + "l3", prefix + "l4",
        prefix + "l_cv", prefix + "l_skewness", prefix + "l_kurtosis",
        prefix + "quadratic_mean", prefix + "cubic_mean"
    };
}

std::vector<float> PointStatBundleAsVector(const PointStatBundle& b) {
    return {
        b.min, b.max, b.mean, b.stddev, b.variance, b.cv, b.skewness, b.kurtosis, b.crr,
        b.mode, b.median, b.iqr,
        b.p01, b.p05, b.p10, b.p20, b.p25, b.p30, b.p40, b.p50, b.p60, b.p70,
        b.p75, b.p80, b.p90, b.p95, b.p99,
        b.aad, b.madMedian, b.madMode,
        b.l1, b.l2, b.l3, b.l4, b.lCV, b.lSkewness, b.lKurtosis,
        b.quadraticMean, b.cubicMean
    };
}

float ComputeProfileArea(const PointStatBundle& bundle) {
    // Trapezoidal-rule area under the percentile-vs-percentile-rank curve,
    // using the same 15 percentile points the bundle already computes
    // (p01..p99), each weighted by the percentile-rank gap to the next
    // point. A flat (all points at the same height) curve has zero area.
    static const double kRanks[] = {0.01, 0.05, 0.10, 0.20, 0.25, 0.30, 0.40, 0.50, 0.60, 0.70, 0.75, 0.80, 0.90, 0.95, 0.99};
    const float kValues[] = {
        bundle.p01, bundle.p05, bundle.p10, bundle.p20, bundle.p25, bundle.p30, bundle.p40,
        bundle.p50, bundle.p60, bundle.p70, bundle.p75, bundle.p80, bundle.p90, bundle.p95, bundle.p99
    };
    const size_t n = sizeof(kRanks) / sizeof(kRanks[0]);

    double area = 0.0;
    for (size_t i = 0; i + 1 < n; ++i) {
        double dRank = kRanks[i + 1] - kRanks[i];
        double avgHeight = (static_cast<double>(kValues[i]) + kValues[i + 1]) / 2.0 - bundle.min;
        area += dRank * avgHeight;
    }
    return static_cast<float>(area);
}

size_t AssignStratumIndex(double elevation, const std::vector<double>& thresholds) {
    size_t sIdx = 0;
    while (sIdx < thresholds.size() && elevation >= thresholds[sIdx]) {
        sIdx++;
    }
    return sIdx;
}

StrataStatBundle ComputeStrataStatBundle(const std::vector<double>& values, size_t totalCloudPoints) {
    StrataStatBundle b;
    b.count = static_cast<int>(values.size());
    b.proportion = (totalCloudPoints > 0) ? (static_cast<double>(b.count) / totalCloudPoints) : 0.0;
    if (values.empty()) return b;

    double sum = std::accumulate(values.begin(), values.end(), 0.0);
    double mean = sum / values.size();
    double sqSum = 0.0;
    double minV = values.front();
    double maxV = values.front();
    for (double v : values) {
        double diff = v - mean;
        sqSum += diff * diff;
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }

    b.mean = static_cast<float>(mean);
    b.stddev = static_cast<float>(std::sqrt(sqSum / values.size()));
    b.min = static_cast<float>(minV);
    b.max = static_cast<float>(maxV);
    return b;
}

std::vector<std::string> StrataStatBundleColumnNames(const std::string& prefix) {
    return {prefix + "count", prefix + "proportion", prefix + "mean", prefix + "stddev", prefix + "min", prefix + "max"};
}

std::vector<double> StrataStatBundleAsVector(const StrataStatBundle& b) {
    return {static_cast<double>(b.count), b.proportion, b.mean, b.stddev, b.min, b.max};
}

} // namespace fusion::metrics
