#ifndef FUSION_LIDAR_MERGEDPOINTCLOUDREADER_H
#define FUSION_LIDAR_MERGEDPOINTCLOUDREADER_H

#include "fusion/lidar/LASPointCloud.h"

#include <filesystem>
#include <vector>

namespace fusion::lidar {

// Streams points from several LAS/LAZ files in sequence as though they were
// one file -- the same public surface as LASReader (Open/Close/IsOpen/
// GetHeader/GetPointCount/ReadNextPoint/Rewind), so any tool's existing
// `while (reader.ReadNextPoint(pt))` loop works unchanged against either
// type. Only one underlying file is open at a time (opened lazily as each
// prior file is exhausted), matching LASReader's own streaming design --
// point count/extent do not depend on holding every file open at once.
class MergedPointCloudReader {
public:
    MergedPointCloudReader();
    ~MergedPointCloudReader();

    MergedPointCloudReader(const MergedPointCloudReader&) = delete;
    MergedPointCloudReader& operator=(const MergedPointCloudReader&) = delete;
    MergedPointCloudReader(MergedPointCloudReader&&) noexcept;
    MergedPointCloudReader& operator=(MergedPointCloudReader&&) noexcept;

    bool Open(const std::vector<std::filesystem::path>& files);
    void Close();
    bool IsOpen() const;

    const LASHeaderInfo& GetHeader() const { return m_header; }
    uint64_t GetPointCount() const { return m_header.pointCount; }

    bool ReadNextPoint(PointRecord& pt);
    void Rewind();

private:
    bool OpenFileAtIndex(size_t index);

    std::vector<std::filesystem::path> m_files;
    size_t m_currentFileIndex{0};
    LASReader m_currentReader;
    LASHeaderInfo m_header;
    bool m_isOpen{false};
};

} // namespace fusion::lidar

#endif // FUSION_LIDAR_MERGEDPOINTCLOUDREADER_H
