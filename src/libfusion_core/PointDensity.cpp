#include "fusion/lidar/PointDensity.h"

#include "fusion/lidar/MergedPointCloudReader.h"
#include "fusion/raster/GDALRaster.h"

#include <cmath>
#include <cstddef>

namespace fusion::lidar {

bool WritePointCountRaster(const std::vector<std::filesystem::path>& files,
                           double cellSize,
                           const std::filesystem::path& outputPath) {
    if (cellSize <= 0.0) return false;

    MergedPointCloudReader reader;
    if (!reader.Open(files)) return false;

    // Copy the header rather than holding a reference to it: Close() below
    // resets the reader's header, and the raster's position and coordinate
    // system are taken from this copy after the reader is closed.
    const LASHeaderInfo header = reader.GetHeader();

    int cols = static_cast<int>(std::ceil((header.maxX - header.minX) / cellSize));
    int rows = static_cast<int>(std::ceil((header.maxY - header.minY) / cellSize));
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    std::vector<float> counts(static_cast<size_t>(cols) * static_cast<size_t>(rows), 0.0f);
    PointRecord pt;
    while (reader.ReadNextPoint(pt)) {
        int col = static_cast<int>((pt.x - header.minX) / cellSize);
        int row = static_cast<int>((header.maxY - pt.y) / cellSize);
        // A point exactly on the maximum-X or minimum-Y edge lands one past
        // the last column or row when the extent is a whole multiple of the
        // cell size; it belongs to the last cell, not outside the grid.
        if (col == cols && pt.x <= header.maxX) col = cols - 1;
        if (row == rows && pt.y >= header.minY) row = rows - 1;
        if (col >= 0 && col < cols && row >= 0 && row < rows) {
            counts[static_cast<size_t>(row) * static_cast<size_t>(cols) + static_cast<size_t>(col)] += 1.0f;
        }
    }
    reader.Close();

    double geotransform[6] = { header.minX, cellSize, 0.0, header.maxY, 0.0, -cellSize };
    fusion::raster::GDALRaster raster;
    if (!raster.Create(outputPath, cols, rows, 1, "Float32", "GTiff", header.projectionWKT, geotransform, -9999.0)) {
        return false;
    }
    raster.SetBandDescription(1, "point_density");
    bool wrote = raster.WriteBandData(1, counts);
    raster.Close();
    return wrote;
}

} // namespace fusion::lidar
