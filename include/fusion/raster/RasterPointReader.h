#ifndef FUSION_RASTER_RASTERPOINTREADER_H
#define FUSION_RASTER_RASTERPOINTREADER_H

#include <filesystem>
#include <vector>

#include "fusion/lidar/LASPointCloud.h"
#include "fusion/raster/GDALRaster.h"

namespace fusion::raster {

// Reads a surface raster (a canopy height model or a surface model) as a
// stream of point records, so tools written for point clouds can take a
// raster. Each pixel is one first return at the pixel center, with the pixel
// value as its elevation. Rows are read in blocks of 512 so a large raster
// is never held in memory whole.
//
// A missing-data pixel (NaN or the raster's NoData value) is skipped. With
// nodataAsZero it is instead returned as a zero-height point flagged with
// PointRecord::synthetic, so a caller that subtracts a ground raster can
// leave that point alone.
class RasterPointReader {
public:
    // Opens the raster. Returns false when it cannot be opened.
    bool Open(const std::filesystem::path& rasterPath, bool nodataAsZero = false);

    // Limits reading to the pixels whose centers lie inside the rectangle.
    // Call after Open and before the first ReadNextPoint.
    void SetWindow(double minX, double minY, double maxX, double maxY);

    // Fills pixel with the next point. Returns false at the end of the
    // window, or when a block of the raster cannot be read; see HadReadError.
    bool ReadNextPoint(fusion::lidar::PointRecord& pixel);

    bool HadReadError() const { return m_hadReadError; }
    const RasterInfo& GetInfo() const { return m_info; }
    void Close();

private:
    bool LoadNextBlock();

    GDALRaster m_raster;
    RasterInfo m_info;
    bool m_nodataAsZero{false};
    bool m_hadReadError{false};

    // pixel range to read: columns colFirst..colLast, rows rowFirst..rowLast
    int m_colFirst{0};
    int m_colLast{-1};
    int m_rowFirst{0};
    int m_rowLast{-1};

    // the block of rows now being handed out
    std::vector<float> m_block;
    int m_blockStartRow{0};
    int m_blockRows{0};
    int m_nextRowInBlock{0};
    int m_nextColInWindow{0};
    int m_nextBlockStartRow{0};
};

} // namespace fusion::raster

#endif
