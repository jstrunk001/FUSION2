#include "fusion/metrics/ExperimentalMetrics.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <set>
#include <unordered_map>
#include <tuple>
#include <utility>
#include <cstdint>



namespace fusion::metrics {

namespace {

struct PairHash {
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<T1>{}(p.first);
        auto h2 = std::hash<T2>{}(p.second);
        return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
    }
};

struct Tuple3Hash {
    template <class T1, class T2, class T3>
    std::size_t operator()(const std::tuple<T1, T2, T3>& t) const {
        auto h1 = std::hash<T1>{}(std::get<0>(t));
        auto h2 = std::hash<T2>{}(std::get<1>(t));
        auto h3 = std::hash<T3>{}(std::get<2>(t));
        std::size_t seed = h1;
        seed ^= h2 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        seed ^= h3 + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        return seed;
    }
};

double ComputeMean(const std::vector<double>& vec) {
    if (vec.empty()) return 0.0;
    double sum = std::accumulate(vec.begin(), vec.end(), 0.0);
    return sum / vec.size();
}

double ComputeStdDev(const std::vector<double>& vec, double mean) {
    if (vec.size() <= 1) return 0.0;
    double sqSum = 0.0;
    for (double v : vec) {
        double diff = v - mean;
        sqSum += diff * diff;
    }
    return std::sqrt(sqSum / (vec.size() - 1));
}

double ComputePearsonCor(const std::vector<double>& x, const std::vector<double>& y) {
    if (x.size() <= 1 || x.size() != y.size()) return 0.0;
    double meanX = ComputeMean(x);
    double meanY = ComputeMean(y);

    double num = 0.0;
    double denX = 0.0;
    double denY = 0.0;

    for (size_t i = 0; i < x.size(); ++i) {
        double dx = x[i] - meanX;
        double dy = y[i] - meanY;
        num += dx * dy;
        denX += dx * dx;
        denY += dy * dy;
    }

    if (denX <= 0.0 || denY <= 0.0) return 0.0;
    return num / std::sqrt(denX * denY);
}

} // namespace

ExperimentalMetricsResults ComputeExperimentalMetrics(
    const std::vector<Point3D>& points,
    const ExperimentalMetricsOptions& options
) {
    ExperimentalMetricsResults res;

    if (points.empty()) return res;

    std::vector<double> zAll, xAll, yAll, rtxyAll, rtxyzAll;
    zAll.reserve(points.size());
    xAll.reserve(points.size());
    yAll.reserve(points.size());
    rtxyAll.reserve(points.size());
    rtxyzAll.reserve(points.size());

    std::vector<double> zCanopy, xCanopy, yCanopy, rtxyCanopy, rtxyzCanopy;

    double minZAll = 1e9, maxZAll = -1e9;
    double minXAll = 1e9, maxXAll = -1e9;
    double minYAll = 1e9, maxYAll = -1e9;
    double minRtxyAll = 1e9, maxRtxyAll = -1e9;
    double minRtxyzAll = 1e9, maxRtxyzAll = -1e9;

    std::unordered_map<std::pair<int64_t, int64_t>, std::pair<double, double>, PairHash> grid2DAll; // cell -> {minZ, maxZ}
    std::unordered_map<std::pair<int64_t, int64_t>, std::pair<double, double>, PairHash> grid2DCanopy;

    std::set<std::tuple<int64_t, int64_t, int64_t>> voxels3DAll;
    std::set<std::tuple<int64_t, int64_t, int64_t>> voxels3DCanopy;

    double cs = (options.cellSize > 0.0) ? options.cellSize : 10.0;
    double vs = (options.voxelSize > 0.0) ? options.voxelSize : 20.0;

    for (const auto& pt : points) {
        zAll.push_back(pt.z);
        xAll.push_back(pt.x);
        yAll.push_back(pt.y);

        double rxy = std::sqrt(pt.x * pt.x + pt.y * pt.y);
        double rxyz = std::sqrt(pt.x * pt.x + pt.y * pt.y + pt.z * pt.z);

        rtxyAll.push_back(rxy);
        rtxyzAll.push_back(rxyz);

        minZAll = std::min(minZAll, pt.z);
        maxZAll = std::max(maxZAll, pt.z);
        minXAll = std::min(minXAll, pt.x);
        maxXAll = std::max(maxXAll, pt.x);
        minYAll = std::min(minYAll, pt.y);
        maxYAll = std::max(maxYAll, pt.y);

        minRtxyAll = std::min(minRtxyAll, rxy);
        maxRtxyAll = std::max(maxRtxyAll, rxy);
        minRtxyzAll = std::min(minRtxyzAll, rxyz);
        maxRtxyzAll = std::max(maxRtxyzAll, rxyz);

        int64_t cX = static_cast<int64_t>(std::floor(pt.x / cs));
        int64_t cY = static_cast<int64_t>(std::floor(pt.y / cs));
        auto cKey = std::make_pair(cX, cY);

        if (grid2DAll.find(cKey) == grid2DAll.end()) {
            grid2DAll[cKey] = {pt.z, pt.z};
        } else {
            grid2DAll[cKey].first = std::min(grid2DAll[cKey].first, pt.z);
            grid2DAll[cKey].second = std::max(grid2DAll[cKey].second, pt.z);
        }

        int64_t vX = static_cast<int64_t>(std::floor(pt.x / vs));
        int64_t vY = static_cast<int64_t>(std::floor(pt.y / vs));
        int64_t vZ = static_cast<int64_t>(std::floor(pt.z / vs));
        voxels3DAll.insert(std::make_tuple(vX, vY, vZ));

        if (pt.z >= options.minHt) {
            zCanopy.push_back(pt.z);
            xCanopy.push_back(pt.x);
            yCanopy.push_back(pt.y);
            rtxyCanopy.push_back(rxy);
            rtxyzCanopy.push_back(rxyz);

            if (grid2DCanopy.find(cKey) == grid2DCanopy.end()) {
                grid2DCanopy[cKey] = {pt.z, pt.z};
            } else {
                grid2DCanopy[cKey].first = std::min(grid2DCanopy[cKey].first, pt.z);
                grid2DCanopy[cKey].second = std::max(grid2DCanopy[cKey].second, pt.z);
            }

            voxels3DCanopy.insert(std::make_tuple(vX, vY, vZ));
        }
    }

    double meanZAll = ComputeMean(zAll);
    double sdZAll = ComputeStdDev(zAll, meanZAll);
    double cvZAll = (meanZAll != 0.0) ? (sdZAll / meanZAll) : 0.0;
    double rangeZAll = maxZAll - minZAll;

    double meanRtxyAll = ComputeMean(rtxyAll);
    double sdRtxyAll = ComputeStdDev(rtxyAll, meanRtxyAll);
    double cvRtxyAll = (meanRtxyAll != 0.0) ? (sdRtxyAll / meanRtxyAll) : 0.0;
    double corXYAll = ComputePearsonCor(xAll, yAll);

    double meanRtxyzAll = ComputeMean(rtxyzAll);
    double sdRtxyzAll = ComputeStdDev(rtxyzAll, meanRtxyzAll);
    double cvRtxyzAll = (meanRtxyzAll != 0.0) ? (sdRtxyzAll / meanRtxyzAll) : 0.0;
    double corXYZAll = ComputePearsonCor(rtxyAll, zAll);

    if (!zCanopy.empty()) {
        double minZCanopy = 1e9, maxZCanopy = -1e9;
        for (double z : zCanopy) {
            minZCanopy = std::min(minZCanopy, z);
            maxZCanopy = std::max(maxZCanopy, z);
        }
        double meanZCanopy = ComputeMean(zCanopy);
        double sdZCanopy = ComputeStdDev(zCanopy, meanZCanopy);
        double cvZCanopy = (meanZCanopy != 0.0) ? (sdZCanopy / meanZCanopy) : 0.0;
        double rangeZCanopy = maxZCanopy - minZCanopy;

        // Relative Z Metrics
        if (std::abs(minZAll) > 1e-6) {
            res.zMinRat = std::abs(100.0 * (minZCanopy - minZAll) / minZAll);
        } else {
            res.zMinRat = 0.0;
        }
        if (std::abs(maxZAll) > 1e-6) {
            res.zMaxRat = 100.0 * maxZCanopy / maxZAll;
        }
        if (sdZAll > 0.0) {
            res.zSdRat = 100.0 * sdZCanopy / sdZAll;
        }
        if (std::abs(meanZAll) > 1e-6) {
            res.zMeanRat = 100.0 * meanZCanopy / meanZAll;
        }
        if (std::abs(cvZAll) > 1e-6) {
            res.zCvRat = 100.0 * cvZCanopy / cvZAll;
        }
        if (rangeZAll > 0.0) {
            res.zIQRat = 100.0 * rangeZCanopy / rangeZAll;
        }

        // X Metrics
        double minXCanopy = 1e9, maxXCanopy = -1e9;
        for (double x : xCanopy) {
            minXCanopy = std::min(minXCanopy, x);
            maxXCanopy = std::max(maxXCanopy, x);
        }
        res.xMin = minXCanopy;
        res.xMax = maxXCanopy;
        res.xSd = ComputeStdDev(xCanopy, ComputeMean(xCanopy));
        if ((maxXAll - minXAll) > 0.0) {
            res.xRatHt = 100.0 * (maxXCanopy - minXCanopy) / (maxXAll - minXAll);
        }

        // Y Metrics
        double minYCanopy = 1e9, maxYCanopy = -1e9;
        for (double y : yCanopy) {
            minYCanopy = std::min(minYCanopy, y);
            maxYCanopy = std::max(maxYCanopy, y);
        }
        res.yMin = minYCanopy;
        res.yMax = maxYCanopy;
        res.ySd = ComputeStdDev(yCanopy, ComputeMean(yCanopy));
        if ((maxYAll - minYAll) > 0.0) {
            res.yRatHt = 100.0 * (maxYCanopy - minYCanopy) / (maxYAll - minYAll);
        }

        // 2D Radial Distance Metrics
        double minRtxyCanopy = 1e9, maxRtxyCanopy = -1e9;
        for (double r : rtxyCanopy) {
            minRtxyCanopy = std::min(minRtxyCanopy, r);
            maxRtxyCanopy = std::max(maxRtxyCanopy, r);
        }
        res.xyMinRt = minRtxyCanopy;
        if (minRtxyAll > 0.0) res.xyMinRtRat = 100.0 * minRtxyCanopy / minRtxyAll;
        res.xyMaxRt = maxRtxyCanopy;
        if (maxRtxyAll > 0.0) res.xyMaxRtRat = 100.0 * maxRtxyCanopy / maxRtxyAll;

        double meanRtxyCanopy = ComputeMean(rtxyCanopy);
        double sdRtxyCanopy = ComputeStdDev(rtxyCanopy, meanRtxyCanopy);
        double cvRtxyCanopy = (meanRtxyCanopy != 0.0) ? (sdRtxyCanopy / meanRtxyCanopy) : 0.0;

        res.xySdRt = sdRtxyCanopy;
        if (sdRtxyAll > 0.0) res.xySdRtRat = 100.0 * sdRtxyCanopy / sdRtxyAll;
        res.xyMnRt = meanRtxyCanopy;
        if (meanRtxyAll > 0.0) res.xyMnRtRat = 100.0 * meanRtxyCanopy / meanRtxyAll;
        res.xyCvRt = cvRtxyCanopy;
        if (cvRtxyAll > 0.0) res.xyCvRtRat = 100.0 * cvRtxyCanopy / cvRtxyAll;

        res.xyCor = ComputePearsonCor(xCanopy, yCanopy);
        if (std::abs(corXYAll) > 1e-6) res.xyCorRat = 100.0 * res.xyCor / corXYAll;

        // 3D Radial Distance Metrics
        double minRtxyzCanopy = 1e9, maxRtxyzCanopy = -1e9;
        for (double r : rtxyzCanopy) {
            minRtxyzCanopy = std::min(minRtxyzCanopy, r);
            maxRtxyzCanopy = std::max(maxRtxyzCanopy, r);
        }
        res.xyzMinRt = minRtxyzCanopy;
        if (minRtxyzAll > 0.0) res.xyzMinRtRat = 100.0 * minRtxyzCanopy / minRtxyzAll;
        res.xyzMaxRt = maxRtxyzCanopy;
        if (maxRtxyzAll > 0.0) res.xyzMaxRtRat = 100.0 * maxRtxyzCanopy / maxRtxyzAll;

        double meanRtxyzCanopy = ComputeMean(rtxyzCanopy);
        double sdRtxyzCanopy = ComputeStdDev(rtxyzCanopy, meanRtxyzCanopy);
        double cvRtxyzCanopy = (meanRtxyzCanopy != 0.0) ? (sdRtxyzCanopy / meanRtxyzCanopy) : 0.0;

        res.xyzSdRt = sdRtxyzCanopy;
        if (sdRtxyzAll > 0.0) res.xyzSdRtRat = 100.0 * sdRtxyzCanopy / sdRtxyzAll;
        res.xyzMnRt = meanRtxyzCanopy;
        if (meanRtxyzAll > 0.0) res.xyzMnRtRat = 100.0 * meanRtxyzCanopy / meanRtxyzAll;
        res.xyzCvRt = cvRtxyzCanopy;
        if (cvRtxyzAll > 0.0) res.xyzCvRtRat = 100.0 * cvRtxyzCanopy / cvRtxyzAll;

        res.xyzCor = ComputePearsonCor(rtxyCanopy, zCanopy);
        if (std::abs(corXYZAll) > 1e-6) res.xyzCorRat = 100.0 * res.xyzCor / corXYZAll;
    }

    // 2D Area & Columnar Volume
    double cellArea = cs * cs;
    res.gridAreaAll = grid2DAll.size() * cellArea;
    res.gridArea = grid2DCanopy.size() * cellArea;

    if (res.gridAreaAll > 0.0) {
        res.areaCover = 100.0 * res.gridArea / res.gridAreaAll;
        res.areaScl = res.areaCover;
    }

    double volAllSum = 0.0;
    for (const auto& kv : grid2DAll) {
        volAllSum += (kv.second.second - kv.second.first) * cellArea;
    }
    res.gridVolAll = volAllSum;

    double volCanopySum = 0.0;
    for (const auto& kv : grid2DCanopy) {
        volCanopySum += (kv.second.second - kv.second.first) * cellArea;
    }
    res.gridVol = volCanopySum;

    if (res.gridVolAll > 0.0) {
        res.gridVolRat = 100.0 * res.gridVol / res.gridVolAll;
    }
    if (res.gridAreaAll > 0.0) {
        res.gridVolScl = 100.0 * res.gridVol / res.gridAreaAll;
    }

    // 3D Voxel Volume
    double voxelVolUnit = vs * vs * vs;
    res.voxVolAll = voxels3DAll.size() * voxelVolUnit;
    res.voxVol = voxels3DCanopy.size() * voxelVolUnit;

    if (res.voxVolAll > 0.0) {
        res.voxVolRat = 100.0 * res.voxVol / res.voxVolAll;
    }
    if (res.gridAreaAll > 0.0) {
        res.voxVolScl = 100.0 * res.voxVol / res.gridAreaAll;
    }

    return res;
}

std::map<std::string, double> GetExperimentalMetricsAsMap(const ExperimentalMetricsResults& res) {
    return {
        {"zMinRat", res.zMinRat}, {"zMaxRat", res.zMaxRat}, {"zSdRat", res.zSdRat},
        {"zMeanRat", res.zMeanRat}, {"zCvRat", res.zCvRat}, {"zIQRat", res.zIQRat},
        {"xMin", res.xMin}, {"xMax", res.xMax}, {"xSd", res.xSd}, {"xRatHt", res.xRatHt},
        {"yMin", res.yMin}, {"yMax", res.yMax}, {"ySd", res.ySd}, {"yRatHt", res.yRatHt},
        {"xyMinRt", res.xyMinRt}, {"xyMinRtRat", res.xyMinRtRat},
        {"xyMaxRt", res.xyMaxRt}, {"xyMaxRtRat", res.xyMaxRtRat},
        {"xySdRt", res.xySdRt}, {"xySdRtRat", res.xySdRtRat},
        {"xyMnRt", res.xyMnRt}, {"xyMnRtRat", res.xyMnRtRat},
        {"xyCvRt", res.xyCvRt}, {"xyCvRtRat", res.xyCvRtRat},
        {"xyCor", res.xyCor}, {"xyCorRat", res.xyCorRat},
        {"xyzMinRt", res.xyzMinRt}, {"xyzMinRtRat", res.xyzMinRtRat},
        {"xyzMaxRt", res.xyzMaxRt}, {"xyzMaxRtRat", res.xyzMaxRtRat},
        {"xyzSdRt", res.xyzSdRt}, {"xyzSdRtRat", res.xyzSdRtRat},
        {"xyzMnRt", res.xyzMnRt}, {"xyzMnRtRat", res.xyzMnRtRat},
        {"xyzCvRt", res.xyzCvRt}, {"xyzCvRtRat", res.xyzCvRtRat},
        {"xyzCor", res.xyzCor}, {"xyzCorRat", res.xyzCorRat},
        {"gridAreaAll", res.gridAreaAll}, {"gridArea", res.gridArea},
        {"areaCover", res.areaCover}, {"areaScl", res.areaScl},
        {"gridVolAll", res.gridVolAll}, {"gridVol", res.gridVol},
        {"gridVolRat", res.gridVolRat}, {"gridVolScl", res.gridVolScl},
        {"voxVolAll", res.voxVolAll}, {"voxVol", res.voxVol},
        {"voxVolRat", res.voxVolRat}, {"voxVolScl", res.voxVolScl}
    };
}

std::vector<std::string> GetExperimentalMetricsNames() {
    return {
        "zMinRat", "zMaxRat", "zSdRat", "zMeanRat", "zCvRat", "zIQRat",
        "xMin", "xMax", "xSd", "xRatHt",
        "yMin", "yMax", "ySd", "yRatHt",
        "xyMinRt", "xyMinRtRat", "xyMaxRt", "xyMaxRtRat",
        "xySdRt", "xySdRtRat", "xyMnRt", "xyMnRtRat",
        "xyCvRt", "xyCvRtRat", "xyCor", "xyCorRat",
        "xyzMinRt", "xyzMinRtRat", "xyzMaxRt", "xyzMaxRtRat",
        "xyzSdRt", "xyzSdRtRat", "xyzMnRt", "xyzMnRtRat",
        "xyzCvRt", "xyzCvRtRat", "xyzCor", "xyzCorRat",
        "gridAreaAll", "gridArea", "areaCover", "areaScl",
        "gridVolAll", "gridVol", "gridVolRat", "gridVolScl",
        "voxVolAll", "voxVol", "voxVolRat", "voxVolScl"
    };
}

} // namespace fusion::metrics
