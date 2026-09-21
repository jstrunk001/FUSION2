#include "fusion/geom/PolygonFeatureSet.h"
#include "fusion/geom/PointInPolygon.h"

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cmath>
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

    if (m_features.empty()) {
        return false;
    }

    BuildSpatialIndex();

    return true;
}

const std::string& PolygonFeatureSet::Label(size_t featureIndex) const {
    return m_features.at(featureIndex).label;
}

void PolygonFeatureSet::BuildSpatialIndex() {
    // Union extent of every feature's bounding box -- a query point outside
    // this extent can't fall inside any feature's (smaller) bbox, so
    // FindContaining() can reject it immediately without touching the grid.
    m_gridMinX = m_features.front().minX;
    m_gridMinY = m_features.front().minY;
    m_gridMaxX = m_features.front().maxX;
    m_gridMaxY = m_features.front().maxY;
    for (const Feature& feature : m_features) {
        m_gridMinX = std::min(m_gridMinX, feature.minX);
        m_gridMinY = std::min(m_gridMinY, feature.minY);
        m_gridMaxX = std::max(m_gridMaxX, feature.maxX);
        m_gridMaxY = std::max(m_gridMaxY, feature.maxY);
    }

    // Size the grid to the feature count so each cell holds a handful of
    // candidates on average, rather than a fixed bin count that's too coarse
    // for a large feature set or too fine (all overhead, no benefit) for a
    // small one. Clamped to [8, 100] per side.
    int dim = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(m_features.size()))));
    dim = std::clamp(dim, 8, 100);
    m_gridCols = dim;
    m_gridRows = dim;

    double extentX = m_gridMaxX - m_gridMinX;
    double extentY = m_gridMaxY - m_gridMinY;
    m_cellWidth = (extentX > 0.0) ? (extentX / m_gridCols) : 1.0;
    m_cellHeight = (extentY > 0.0) ? (extentY / m_gridRows) : 1.0;

    m_grid.assign(static_cast<size_t>(m_gridCols) * static_cast<size_t>(m_gridRows), {});

    for (size_t f = 0; f < m_features.size(); ++f) {
        const Feature& feature = m_features[f];

        int colMin = static_cast<int>(std::floor((feature.minX - m_gridMinX) / m_cellWidth));
        int colMax = static_cast<int>(std::floor((feature.maxX - m_gridMinX) / m_cellWidth));
        int rowMin = static_cast<int>(std::floor((feature.minY - m_gridMinY) / m_cellHeight));
        int rowMax = static_cast<int>(std::floor((feature.maxY - m_gridMinY) / m_cellHeight));

        colMin = std::clamp(colMin, 0, m_gridCols - 1);
        colMax = std::clamp(colMax, 0, m_gridCols - 1);
        rowMin = std::clamp(rowMin, 0, m_gridRows - 1);
        rowMax = std::clamp(rowMax, 0, m_gridRows - 1);

        for (int row = rowMin; row <= rowMax; ++row) {
            for (int col = colMin; col <= colMax; ++col) {
                m_grid[static_cast<size_t>(row) * static_cast<size_t>(m_gridCols) + static_cast<size_t>(col)].push_back(f);
            }
        }
    }
}

size_t PolygonFeatureSet::FindContaining(double x, double y) const {
    if (m_gridCols <= 0 || m_gridRows <= 0) {
        return npos;
    }
    if (x < m_gridMinX || x > m_gridMaxX || y < m_gridMinY || y > m_gridMaxY) {
        return npos;
    }

    int col = static_cast<int>(std::floor((x - m_gridMinX) / m_cellWidth));
    int row = static_cast<int>(std::floor((y - m_gridMinY) / m_cellHeight));
    col = std::clamp(col, 0, m_gridCols - 1);
    row = std::clamp(row, 0, m_gridRows - 1);

    const std::vector<size_t>& candidates = m_grid[static_cast<size_t>(row) * static_cast<size_t>(m_gridCols) + static_cast<size_t>(col)];

    for (size_t f : candidates) {
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
