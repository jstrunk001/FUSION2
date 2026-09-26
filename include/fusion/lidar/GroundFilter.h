#ifndef FUSION_LIDAR_GROUNDFILTER_H
#define FUSION_LIDAR_GROUNDFILTER_H

#include <cstdint>
#include <optional>
#include <vector>

namespace fusion::lidar {

// Parameters of the Kraus & Pfeifer (1998) iterative ground filter, using
// legacy FUSION GroundFilter's switch names and defaults. g, w, and
// tolerance are in the point cloud's vertical units.
struct KrausPfeiferParams {
    double cellSize{10.0};  // intermediate surface cell size (horizontal units)
    double g{-2.0};         // residual at or below which a point gets full weight
    double w{2.5};          // width above g over which the weight falls to 0
    double a{1.0};          // weight function steepness
    double b{4.0};          // weight function exponent
    int iterations{5};      // number of surface/weight passes
    // When set, the final ground class is every point within
    // [-tolerance, +tolerance] of the last surface. When not set, a point
    // is ground if its weight function is non-zero (residual <= g + w).
    std::optional<double> tolerance;
    // Coarse-to-fine stage (Pfeifer's hierarchical extension of Kraus &
    // Pfeifer): the filter first runs on a coarseCellSize grid, and any
    // point more than coarseCut above that coarse surface is excluded
    // from the fine passes. This removes canopy returns over gaps in the
    // ground returns wider than the fine neighbourhood (3 x cellSize),
    // which the fine surface alone cannot reach down past, while leaving
    // small terrain relief to the fine passes. 0 disables either value's
    // stage; unset coarseCut defaults to 4 x w.
    double coarseCellSize{0.0};
    std::optional<double> coarseCut;
};

// The Kraus & Pfeifer weight for a residual v (point elevation minus the
// current surface): 1 for v <= g, 0 for v > g + w, and
// 1 / (1 + (a * (v - g))^b) in between.
double KrausPfeiferWeight(double v, const KrausPfeiferParams& params);

// Classifies ground points by iterative robust surface fitting. Each pass
// fits, for every params.cellSize grid cell, a weighted least-squares
// plane through the points in that cell and its 8 neighbours, so ground
// returns in nearby cells pull down cells that hold only canopy returns.
// A plane rather than a weighted mean keeps sloped ground from drifting
// as weights change. Cells with no weighted points take their neighbours'
// level. Each point is compared with its own cell's plane, and its weight
// for the next pass comes from KrausPfeiferWeight. Points above the
// surface lose weight, so the surface sinks from the canopy toward the
// ground over the passes.
//
// x, y, z must be the same length; the grid spans [minX, maxX] x
// [minY, maxY]. Returns one flag per point: 1 = ground, 0 = not ground.
std::vector<uint8_t> ClassifyGroundKrausPfeifer(
        const std::vector<double>& x,
        const std::vector<double>& y,
        const std::vector<double>& z,
        double minX, double minY, double maxX, double maxY,
        const KrausPfeiferParams& params);

// Fills NaN cells of a cols x rows grid (row-major) by repeatedly setting
// each empty cell that borders a filled cell to the mean of its filled
// 8-neighbours, working outward until no empty cell remains. A grid with
// no filled cells at all is left unchanged.
void FillEmptyCells(std::vector<double>& grid, int cols, int rows);

} // namespace fusion::lidar

#endif // FUSION_LIDAR_GROUNDFILTER_H
