#include "fusion/lidar/MergedPointCloudReader.h"

#include <algorithm>
#include <limits>

namespace fusion::lidar {

MergedPointCloudReader::MergedPointCloudReader() = default;
MergedPointCloudReader::~MergedPointCloudReader() = default;

MergedPointCloudReader::MergedPointCloudReader(MergedPointCloudReader&&) noexcept = default;
MergedPointCloudReader& MergedPointCloudReader::operator=(MergedPointCloudReader&&) noexcept = default;

bool MergedPointCloudReader::Open(const std::vector<std::filesystem::path>& files) {
    Close();

    if (files.empty()) {
        return false;
    }

    // Peek each file's header (open, read header, close) to synthesize a
    // combined header spanning every file -- callers that size an output
    // grid from GetHeader() before iterating (canopymodel, groundfilter,
    // returndensity) need the full input set's extent, not just the first
    // file's.
    LASHeaderInfo merged;
    merged.minX = (std::numeric_limits<double>::max)();
    merged.minY = (std::numeric_limits<double>::max)();
    merged.minZ = (std::numeric_limits<double>::max)();
    merged.maxX = std::numeric_limits<double>::lowest();
    merged.maxY = std::numeric_limits<double>::lowest();
    merged.maxZ = std::numeric_limits<double>::lowest();

    bool anyCompressed = false;
    bool first = true;

    for (const auto& file : files) {
        LASReader peek;
        if (!peek.Open(file)) {
            return false;
        }
        const LASHeaderInfo& h = peek.GetHeader();

        if (first) {
            merged.xScaleFactor = h.xScaleFactor;
            merged.yScaleFactor = h.yScaleFactor;
            merged.zScaleFactor = h.zScaleFactor;
            merged.xOffset = h.xOffset;
            merged.yOffset = h.yOffset;
            merged.zOffset = h.zOffset;
            merged.pointFormat = h.pointFormat;
            merged.versionMajor = h.versionMajor;
            merged.versionMinor = h.versionMinor;
            merged.systemID = h.systemID;
            merged.generatingSoftware = h.generatingSoftware;
            merged.projectionWKT = h.projectionWKT;
            first = false;
        }

        merged.pointCount += h.pointCount;
        merged.minX = (std::min)(merged.minX, h.minX);
        merged.minY = (std::min)(merged.minY, h.minY);
        merged.minZ = (std::min)(merged.minZ, h.minZ);
        merged.maxX = (std::max)(merged.maxX, h.maxX);
        merged.maxY = (std::max)(merged.maxY, h.maxY);
        merged.maxZ = (std::max)(merged.maxZ, h.maxZ);
        anyCompressed = anyCompressed || h.isCompressed;
    }

    merged.isCompressed = anyCompressed;
    m_header = merged;
    m_files = files;
    m_currentFileIndex = 0;

    if (!OpenFileAtIndex(0)) {
        Close();
        return false;
    }

    m_isOpen = true;
    return true;
}

bool MergedPointCloudReader::OpenFileAtIndex(size_t index) {
    m_currentReader.Close();
    if (index >= m_files.size()) {
        return false;
    }
    return m_currentReader.Open(m_files[index]);
}

void MergedPointCloudReader::Close() {
    m_currentReader.Close();
    m_files.clear();
    m_currentFileIndex = 0;
    m_header = {};
    m_isOpen = false;
}

bool MergedPointCloudReader::IsOpen() const {
    return m_isOpen;
}

bool MergedPointCloudReader::ReadNextPoint(PointRecord& pt) {
    if (!m_isOpen) {
        return false;
    }

    while (!m_currentReader.ReadNextPoint(pt)) {
        m_currentFileIndex++;
        if (!OpenFileAtIndex(m_currentFileIndex)) {
            return false;
        }
    }

    return true;
}

void MergedPointCloudReader::Rewind() {
    if (!m_isOpen) {
        return;
    }
    m_currentFileIndex = 0;
    OpenFileAtIndex(0);
}

} // namespace fusion::lidar
