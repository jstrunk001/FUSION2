#include "fusion/geom/SpatialMask.h"
#include "fusion/geom/PointInPolygon.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>

namespace fusion::geom {

void SpatialMask::SetExtent(double minX, double minY, double maxX, double maxY) {
    m_minX = minX;
    m_minY = minY;
    m_maxX = maxX;
    m_maxY = maxY;
    m_isPolygonMode = false;
    m_active = true;
}

bool SpatialMask::LoadShapefile(const std::filesystem::path& shpPath) {
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

    m_polygons.clear();
    bool haveBounds = false;

    layer->ResetReading();
    for (auto& feature : *layer) {
        OGRGeometry* geom = feature->GetGeometryRef();
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
                m_polygons.push_back(std::move(p));
            }
        }

        OGREnvelope env;
        geom->getEnvelope(&env);
        if (!haveBounds) {
            m_minX = env.MinX;
            m_minY = env.MinY;
            m_maxX = env.MaxX;
            m_maxY = env.MaxY;
            haveBounds = true;
        } else {
            m_minX = (std::min)(m_minX, env.MinX);
            m_minY = (std::min)(m_minY, env.MinY);
            m_maxX = (std::max)(m_maxX, env.MaxX);
            m_maxY = (std::max)(m_maxY, env.MaxY);
        }
    }

    GDALClose(ds);

    if (m_polygons.empty()) {
        return false;
    }

    m_isPolygonMode = true;
    m_active = true;
    return true;
}

bool SpatialMask::Contains(double x, double y) const {
    if (!m_active) {
        return true; // no mask configured -- nothing filtered out
    }

    if (!m_isPolygonMode) {
        return (x >= m_minX && x <= m_maxX && y >= m_minY && y <= m_maxY);
    }

    for (const auto& poly : m_polygons) {
        int totalCrossings = 0;
        for (const auto& ring : poly.rings) {
            totalCrossings += CountRingCrossings(ring.x, ring.y, x, y);
        }
        if ((totalCrossings % 2) == 1) {
            return true;
        }
    }
    return false;
}

void SpatialMask::GetBounds(double& minX, double& minY, double& maxX, double& maxY) const {
    minX = m_minX;
    minY = m_minY;
    maxX = m_maxX;
    maxY = m_maxY;
}

} // namespace fusion::geom
