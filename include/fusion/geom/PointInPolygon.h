#ifndef FUSION_GEOM_POINTINPOLYGON_H
#define FUSION_GEOM_POINTINPOLYGON_H

#include <cstddef>
#include <vector>

namespace fusion::geom {

// Standard even-odd ray-casting point-in-ring test (PNPOLY). Implemented by
// hand rather than via OGRGeometry::Contains() because the vendored static
// GDAL build has GEOS disabled (see build_gdal_minimal.ps1's
// -DGDAL_USE_GEOS=OFF), and OGR's geometry predicates are unreliable without
// it -- reading ring vertices directly and testing membership ourselves
// sidesteps that dependency entirely. Shared by SpatialMask (one unioned
// mask) and PolygonFeatureSet (per-feature membership).
inline int CountRingCrossings(const std::vector<double>& rx, const std::vector<double>& ry, double x, double y) {
    int crossings = 0;
    const size_t n = rx.size();
    if (n < 3) {
        return 0;
    }
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const bool straddles = (ry[i] > y) != (ry[j] > y);
        if (straddles) {
            const double xIntersect = (rx[j] - rx[i]) * (y - ry[i]) / (ry[j] - ry[i]) + rx[i];
            if (x < xIntersect) {
                crossings++;
            }
        }
    }
    return crossings;
}

} // namespace fusion::geom

#endif // FUSION_GEOM_POINTINPOLYGON_H
