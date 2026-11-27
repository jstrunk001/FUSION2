#include "fusion/raster/RasterPointReader.h"

#include <algorithm>
#include <cmath>

namespace fusion::raster {

static const int kBlockRows = 512;

bool RasterPointReader::Open(const std::filesystem::path& rasterPath, bool nodataAsZero) {
    if (!m_raster.Open(rasterPath)) return false;
    m_info = m_raster.GetInfo();
    m_nodataAsZero = nodataAsZero;
    m_hadReadError = false;

    // the whole raster is the default window
    m_colFirst = 0;
    m_colLast = m_info.width - 1;
    m_rowFirst = 0;
    m_rowLast = m_info.height - 1;
    m_blockRows = 0;
    m_nextRowInBlock = 0;
    m_nextColInWindow = 0;
    m_nextBlockStartRow = m_rowFirst;
    return true;
}

void RasterPointReader::SetWindow(double minX, double minY, double maxX, double maxY) {
    // 1. pixel range whose centers fall inside the rectangle (columns right, rows down)
    int colFirst = static_cast<int>(std::ceil((minX - m_info.originX) / m_info.pixelWidth - 0.5));
    int colLast = static_cast<int>(std::floor((maxX - m_info.originX) / m_info.pixelWidth - 0.5));
    int rowFirst = static_cast<int>(std::ceil((m_info.originY - maxY) / m_info.pixelHeight - 0.5));
    int rowLast = static_cast<int>(std::floor((m_info.originY - minY) / m_info.pixelHeight - 0.5));

    // 2. keep the range inside the raster
    m_colFirst = (std::max)(colFirst, 0);
    m_rowFirst = (std::max)(rowFirst, 0);
    m_colLast = (std::min)(colLast, m_info.width - 1);
    m_rowLast = (std::min)(rowLast, m_info.height - 1);

    // 3. start reading from the top of the range
    m_blockRows = 0;
    m_nextRowInBlock = 0;
    m_nextColInWindow = 0;
    m_nextBlockStartRow = m_rowFirst;
}

bool RasterPointReader::LoadNextBlock() {
    if (m_nextBlockStartRow > m_rowLast) return false;
    int windowCols = m_colLast - m_colFirst + 1;
    int rowsInBlock = (std::min)(kBlockRows, m_rowLast - m_nextBlockStartRow + 1);
    if (!m_raster.ReadBandWindow(1, m_colFirst, m_nextBlockStartRow, windowCols, rowsInBlock, m_block)) {
        m_hadReadError = true;
        return false;
    }
    m_blockStartRow = m_nextBlockStartRow;
    m_blockRows = rowsInBlock;
    m_nextRowInBlock = 0;
    m_nextColInWindow = 0;
    m_nextBlockStartRow += rowsInBlock;
    return true;
}

bool RasterPointReader::ReadNextPoint(fusion::lidar::PointRecord& pixel) {
    if (m_colFirst > m_colLast || m_rowFirst > m_rowLast) return false;
    int windowCols = m_colLast - m_colFirst + 1;
    const float noDataF = static_cast<float>(m_info.noDataValue);

    while (true) {
        // 1. move to the next block when the current one is used up
        if (m_nextRowInBlock >= m_blockRows) {
            if (!LoadNextBlock()) return false;
        }

        // 2. take the next pixel of the block
        int rowInBlock = m_nextRowInBlock;
        int colInWindow = m_nextColInWindow;
        m_nextColInWindow++;
        if (m_nextColInWindow >= windowCols) {
            m_nextColInWindow = 0;
            m_nextRowInBlock++;
        }

        // 3. skip a missing-data pixel, or return it as a zero-height point
        float value = m_block[static_cast<size_t>(rowInBlock) * windowCols + colInWindow];
        bool isMissing = std::isnan(value) || (m_info.hasNoData && value == noDataF);
        if (isMissing && !m_nodataAsZero) continue;

        pixel = fusion::lidar::PointRecord();
        pixel.x = PixelCentreX(m_info, m_colFirst + colInWindow);
        pixel.y = PixelCentreY(m_info, m_blockStartRow + rowInBlock);
        if (isMissing) {
            pixel.z = 0.0;
            pixel.synthetic = true;
        } else {
            pixel.z = value;
        }
        return true;
    }
}

void RasterPointReader::Close() {
    m_raster.Close();
    m_block.clear();
    m_blockRows = 0;
}

} // namespace fusion::raster
