#ifndef FUSION_GEOM_POLYGONFEATURESET_H
#define FUSION_GEOM_POLYGONFEATURESET_H

#include <filesystem>
#include <string>
#include <vector>

namespace fusion::geom {

// Loads every polygon feature in a shapefile as a separate, independently
// queryable feature -- unlike SpatialMask, which unions every feature into
// one mask. Used wherever a point needs to be routed to *which* polygon it
// falls in (e.g. clipdata's /multifile, cloudmetrics' /shape per-plot mode),
// not just whether it falls in any of them.
class PolygonFeatureSet {
public:
    // Loads every polygon/multipolygon feature in the shapefile's first
    // layer. labelField, if given, names an attribute field read via
    // OGRFeature::GetFieldAsString() to become that feature's Label(); a
    // feature missing the field (or when labelField is empty) falls back to
    // its zero-padded feature index. Returns false if the file can't be
    // opened or has no polygon features.
    bool LoadShapefile(const std::filesystem::path& shpPath, const std::string& labelField = "");

    size_t FeatureCount() const { return m_features.size(); }
    const std::string& Label(size_t featureIndex) const;

    // First feature (by shapefile order) whose polygon contains (x,y), or
    // npos if none does. A bbox pre-check per feature runs before the
    // ray-casting test.
    size_t FindContaining(double x, double y) const;

    static constexpr size_t npos = static_cast<size_t>(-1);

private:
    struct Ring {
        std::vector<double> x;
        std::vector<double> y;
    };
    struct Polygon {
        std::vector<Ring> rings; // ring 0 = exterior, others = holes
    };
    struct Feature {
        std::vector<Polygon> polygons; // a MultiPolygon feature has >1
        std::string label;
        double minX{0.0}, minY{0.0}, maxX{0.0}, maxY{0.0};
    };

    std::vector<Feature> m_features;
};

} // namespace fusion::geom

#endif // FUSION_GEOM_POLYGONFEATURESET_H
