#ifndef FUSION_LIDAR_POINTDENSITY_H
#define FUSION_LIDAR_POINTDENSITY_H

#include <filesystem>
#include <vector>

namespace fusion::lidar {

// Counts the points in each grid cell across one or more LAS/LAZ files and
// writes the counts as a single-band Float32 GeoTIFF. The grid starts at
// the files' combined minimum X and maximum Y, uses square cells of
// cellSize (in the files' own linear units), and carries the files'
// coordinate system. Points lying exactly on the maximum-X or minimum-Y
// edge are counted in the last column or row rather than dropped.
//
// Returns false if the files cannot be opened or the raster cannot be
// written.
bool WritePointCountRaster(const std::vector<std::filesystem::path>& files,
                           double cellSize,
                           const std::filesystem::path& outputPath);

} // namespace fusion::lidar

#endif // FUSION_LIDAR_POINTDENSITY_H
