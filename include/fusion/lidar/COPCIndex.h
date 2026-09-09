#ifndef FUSION_LIDAR_COPCINDEX_H
#define FUSION_LIDAR_COPCINDEX_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace fusion::lidar {

// Reads the index information from a COPC (Cloud Optimized Point Cloud)
// formatted LAZ file's info VLR and hierarchy pages, and rearranges the
// chunk table into offset order so cumulative per-chunk point counts (and
// therefore each chunk's starting point index) can be computed once, up
// front. That starting index is exactly what LASzip's laszip_seek_point()
// needs -- LASzip already knows how to jump to a given point index inside
// a LAZ file's own chunk table, so the reader (see LASReader::
// ReadPointsInExtent) never needs to manage byte offsets itself, only which
// chunks (by starting-point-index and point-count) overlap a query extent.
//
// Ported from FUSIONMetrics-master's COPCIndex.h/.cpp (Common/FUSION_util),
// adapted to this project's modern-C++ conventions (std::ifstream instead
// of FILE*, no MFC/CSpatialExtent dependency, no debug-print members).
struct COPCChunk {
    int32_t level{-1};
    int64_t offset{0};
    int32_t byteSize{0};
    int32_t pointCount{0}; // -1 means "child hierarchy page", not a data chunk
    double minX{0.0}, minY{0.0}, maxX{0.0}, maxY{0.0};
    uint64_t startingPointIndex{0}; // populated after sorting by offset
};

class COPCIndex {
public:
    // True if filePath's header carries a valid "copc" info VLR (LAS 1.4,
    // COPC version 1.0). Does not require ReadIndex() to have run first.
    static bool IsCOPCFile(const std::filesystem::path& filePath);

    bool ReadIndex(const std::filesystem::path& filePath);
    bool IsValid() const { return m_valid; }

    // Every data chunk (pointCount > 0) whose bounds overlap [minX,maxX] x
    // [minY,maxY], appended to result. Z is not considered -- every tool
    // this serves queries by a 2D tile/cell extent.
    std::vector<COPCChunk> QueryChunks(double minX, double minY, double maxX, double maxY) const;

private:
    struct RawEntry {
        int32_t level{0}, keyX{0}, keyY{0}, keyZ{0};
        int64_t offset{0};
        int32_t byteSize{0};
        int32_t pointCount{0};
    };

    bool ReadInfo(std::ifstream& f);
    void ReadPage(std::ifstream& f, uint64_t offset, int32_t entryCount);
    void ComputeEntryBounds(const RawEntry& e, double& outMinX, double& outMinY, double& outMaxX, double& outMaxY) const;
    void SortAndAccumulate();

    bool m_valid{false};
    double m_centerX{0.0}, m_centerY{0.0}, m_halfsize{0.0};
    uint64_t m_rootHierOffset{0};
    uint32_t m_rootHierSize{0};
    std::vector<COPCChunk> m_chunks;
};

} // namespace fusion::lidar

#endif // FUSION_LIDAR_COPCINDEX_H
