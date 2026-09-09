#include "fusion/geom/PolygonFeatureSet.h"
#include "fusion/geom/PointInPolygon.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <iomanip>
#include <sstream>

namespace fusion::geom {

bool PolygonFeatureSet::LoadShapefile(const std::filesystem::path& shpPath, const std::string& labelField) {
    GDALAllRegister();

    GDALDataset* ds = static_cast<GDALDataset*>(GDALOpenEx(
        shpPath.string().c_str(), GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    if (!ds) {
        return false;
    }

    OGRLayer* layer = ds->GetLayer(0);
    if (!layer) {
        GDALClose(ds);
        return false;
    }

    m_features.clear();

    layer->ResetReading();
    int nextIndex = 0;
    for (auto& ogrFeature : *layer) {
        OGRGeometry* geom = ogrFeature->GetGeometryRef();
        if (!geom) {
            continue;
        }

        std::vector<OGRPolygon*> polys;
        const OGRwkbGeometryType flatType = wkbFlatten(geom->getGeometryType());
        if (flatType == wkbPolygon) {
            polys.push_back(geom->toPolygon());
        } else if (flatType == wkbMultiPolygon) {
            OGRMultiPolygon* multi = geom->toMultiPolygon();
            for (int i = 0; i < multi->getNumGeometries(); ++i) {
                polys.push_back(multi->getGeometryRef(i));
            }
        } else {
            continue; // not a polygon feature -- ignore (points/lines/etc.)
        }

        Feature feature;
        for (OGRPolygon* poly : polys) {
            Polygon p;
            auto addRing = [&p](OGRLinearRing* ring) {
                if (!ring) {
                    return;
                }
                Ring r;
                const int n = ring->getNumPoints();
                r.x.reserve(n);
                r.y.reserve(n);
                for (int i = 0; i < n; ++i) {
                    r.x.push_back(ring->getX(i));
                    r.y.push_back(ring->getY(i));
                }
                p.rings.push_back(std::move(r));
            };
            addRing(poly->getExteriorRing());
            for (int i = 0; i < poly->getNumInteriorRings(); ++i) {
                addRing(poly->getInteriorRing(i));
            }
            if (!p.rings.empty()) {
                feature.polygons.push_back(std::move(p));
            }
        }
        if (feature.polygons.empty()) {
            continue;
        }

        OGREnvelope env;
        geom->getEnvelope(&env);
        feature.minX = env.MinX;
        feature.minY = env.MinY;
        feature.maxX = env.MaxX;
        feature.maxY = env.MaxY;

        bool haveLabel = false;
        if (!labelField.empty()) {
            int fieldIdx = ogrFeature->GetFieldIndex(labelField.c_str());
            if (fieldIdx >= 0 && ogrFeature->IsFieldSetAndNotNull(fieldIdx)) {
                feature.label = ogrFeature->GetFieldAsString(fieldIdx);
                haveLabel = true;
            }
        }
        if (!haveLabel) {
            std::ostringstream oss;
            oss << std::setw(4) << std::setfill('0') << nextIndex;
            feature.label = oss.str();
        }

        m_features.push_back(std::move(feature));
        nextIndex++;
    }

    GDALClose(ds);

    return !m_features.empty();
}

const std::string& PolygonFeatureSet::Label(size_t featureIndex) const {
    return m_features.at(featureIndex).label;
}

size_t PolygonFeatureSet::FindContaining(double x, double y) const {
    for (size_t f = 0; f < m_features.size(); ++f) {
        const Feature& feature = m_features[f];
        if (x < feature.minX || x > feature.maxX || y < feature.minY || y > feature.maxY) {
            continue;
        }
        for (const auto& poly : feature.polygons) {
            int totalCrossings = 0;
            for (const auto& ring : poly.rings) {
                totalCrossings += CountRingCrossings(ring.x, ring.y, x, y);
            }
            if ((totalCrossings % 2) == 1) {
                return f;
            }
        }
    }
    return npos;
}

} // namespace fusion::geom
