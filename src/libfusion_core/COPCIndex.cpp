#include "fusion/lidar/COPCIndex.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fusion::lidar {

bool COPCIndex::IsCOPCFile(const std::filesystem::path& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    COPCIndex probe;
    return probe.ReadInfo(f);
}

bool COPCIndex::ReadIndex(const std::filesystem::path& filePath) {
    m_valid = false;
    m_chunks.clear();

    std::ifstream f(filePath, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }

    if (!ReadInfo(f)) {
        return false;
    }

    ReadPage(f, m_rootHierOffset, static_cast<int32_t>(m_rootHierSize / 32));
    SortAndAccumulate();
    m_valid = true;
    return true;
}

// The first 589 bytes of a COPC file are: the 375-byte LAS 1.4 header, one
// 54-byte VLR header, then the 160-byte COPC info VLR payload -- see the
// COPC spec (https://copc.io/). "copc" as the VLR user ID (bytes 377-380 of
// the file) identifies it; the info VLR's own fields (center/halfsize,
// hierarchy root offset/size) start at byte 429.
bool COPCIndex::ReadInfo(std::ifstream& f) {
    char buf[589];
    f.seekg(0, std::ios::beg);
    f.read(buf, sizeof(buf));
    if (f.gcount() != static_cast<std::streamsize>(sizeof(buf))) {
        return false;
    }

    if (std::strncmp(buf, "LASF", 4) != 0) {
        return false;
    }
    if (std::strncmp(&buf[377], "copc", 4) != 0) {
        return false;
    }
    // COPC info VLR record ID must be 1, version (bytes 393-394) must be 1.0
    if (buf[393] != 1 || buf[394] != 0) {
        return false;
    }

    const char* c = &buf[429];
    auto readDouble = [&c]() { double v; std::memcpy(&v, c, sizeof(v)); c += sizeof(v); return v; };
    auto readU64 = [&c]() { uint64_t v; std::memcpy(&v, c, sizeof(v)); c += sizeof(v); return v; };

    m_centerX = readDouble();
    m_centerY = readDouble();
    /* center_z */ readDouble();
    m_halfsize = readDouble();
    /* spacing */ readDouble();
    m_rootHierOffset = readU64();
    m_rootHierSize = static_cast<uint32_t>(readU64());

    return true;
}

void COPCIndex::ReadPage(std::ifstream& f, uint64_t offset, int32_t entryCount) {
    for (int32_t i = 0; i < entryCount; ++i) {
        f.seekg(static_cast<std::streamoff>(offset + static_cast<uint64_t>(i) * 32), std::ios::beg);

        RawEntry e;
        f.read(reinterpret_cast<char*>(&e.level), sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&e.keyX), sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&e.keyY), sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&e.keyZ), sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&e.offset), sizeof(int64_t));
        f.read(reinterpret_cast<char*>(&e.byteSize), sizeof(int32_t));
        f.read(reinterpret_cast<char*>(&e.pointCount), sizeof(int32_t));
        if (!f) {
            return;
        }

        if (e.pointCount > 0) {
            COPCChunk chunk;
            chunk.level = e.level;
            chunk.offset = e.offset;
            chunk.byteSize = e.byteSize;
            chunk.pointCount = e.pointCount;
            ComputeEntryBounds(e, chunk.minX, chunk.minY, chunk.maxX, chunk.maxY);
            m_chunks.push_back(chunk);
        } else if (e.pointCount == -1) {
            // Child hierarchy page -- offset/byteSize point at it instead of a data chunk.
            ReadPage(f, static_cast<uint64_t>(e.offset), e.byteSize / 32);
        }
        // pointCount == 0: no data for this key (and this port doesn't need child recursion for it -- COPC's
        // hierarchy pages already enumerate every populated node directly reachable from the root page).
    }
}

// Octree cell bounds for one entry's voxel key, relative to the root
// bounds (center +/- halfsize) -- ported from PDAL's Key::bounds()
// (io/private/copc/Key.hpp), the same derivation FUSIONMetrics-master's
// COPCIndex.cpp credits.
void COPCIndex::ComputeEntryBounds(const RawEntry& e, double& outMinX, double& outMinY, double& outMaxX, double& outMaxY) const {
    double rootMinX = m_centerX - m_halfsize;
    double rootMaxX = m_centerX + m_halfsize;
    double rootMinY = m_centerY - m_halfsize;
    double rootMaxY = m_centerY + m_halfsize;

    double cellWidth = (rootMaxX - rootMinX) / std::pow(2.0, e.level);

    outMinX = (e.keyX == 0) ? rootMinX : rootMinX + (cellWidth * e.keyX);
    outMaxX = (e.keyX == e.level) ? rootMaxX : rootMinX + (cellWidth * (e.keyX + 1));
    outMinY = (e.keyY == 0) ? rootMinY : rootMinY + (cellWidth * e.keyY);
    outMaxY = (e.keyY == e.level) ? rootMaxY : rootMinY + (cellWidth * (e.keyY + 1));
}

void COPCIndex::SortAndAccumulate() {
    std::sort(m_chunks.begin(), m_chunks.end(),
              [](const COPCChunk& a, const COPCChunk& b) { return a.offset < b.offset; });

    uint64_t running = 0;
    for (auto& chunk : m_chunks) {
        chunk.startingPointIndex = running;
        running += static_cast<uint64_t>(chunk.pointCount);
    }
}

std::vector<COPCChunk> COPCIndex::QueryChunks(double minX, double minY, double maxX, double maxY) const {
    std::vector<COPCChunk> result;
    for (const auto& chunk : m_chunks) {
        bool overlaps = !(chunk.maxX < minX || chunk.minX > maxX || chunk.maxY < minY || chunk.minY > maxY);
        if (overlaps) {
            result.push_back(chunk);
        }
    }
    return result;
}

} // namespace fusion::lidar
