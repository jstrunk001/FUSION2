#ifndef FUSION_GEOM_SPATIALMASK_H
#define FUSION_GEOM_SPATIALMASK_H

#include <filesystem>
#include <vector>

namespace fusion::geom {

// A subregion filter usable in either of two modes: a rectangular extent
// (the existing /extent:<LLX,LLY,URX,URY> behavior every tool already had
// inlined separately) or an arbitrary polygon loaded from a shapefile (the
// new /mask:<path.shp> option). Contains(x,y) is the one call sites need;
// which mode is active is an internal detail.
class SpatialMask {
public:
    void SetExtent(double minX, double minY, double maxX, double maxY);

    // Loads every polygon feature in the shapefile's first layer as the
    // mask (their union -- a point inside any one feature is inside the
    // mask). Returns false if the file can't be opened or has no polygon
    // features.
    bool LoadShapefile(const std::filesystem::path& shpPath);

    bool Contains(double x, double y) const;
    void GetBounds(double& minX, double& minY, double& maxX, double& maxY) const;
    bool IsActive() const { return m_active; }

private:
    struct Ring {
        std::vector<double> x;
        std::vector<double> y;
    };
    struct Polygon {
        std::vector<Ring> rings; // ring 0 = exterior, others = holes
    };

    bool m_active{false};
    bool m_isPolygonMode{false};

    double m_minX{0.0};
    double m_minY{0.0};
    double m_maxX{0.0};
    double m_maxY{0.0};

    std::vector<Polygon> m_polygons;
};

} // namespace fusion::geom

#endif // FUSION_GEOM_SPATIALMASK_H
