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

// Per the ASPRS LAS spec, Variable Length Records can appear in any order
// after the header -- the COPC info VLR is not guaranteed to be the first
// one. This walks the real VLR chain (userId == "copc", recordId == 1)
// instead of assuming a fixed 375-byte LAS 1.4 header followed immediately
// by the COPC VLR; a file where an authoring tool wrote a projection/GeoTIFF
// VLR first would otherwise cause a false "not COPC" fallback to an
// unindexed sequential scan. Header Size (offset 94) and Number of VLRs
// (offset 100) sit at the same fixed byte offsets across LAS 1.2-1.4, so
// both can be read directly without a version-specific header struct. See
// the COPC spec (https://copc.io/) for the VLR header layout (54 bytes) and
// the 160-byte info VLR payload (center/halfsize, hierarchy root
// offset/size) that follows it.
bool COPCIndex::ReadInfo(std::ifstream& f) {
    char sig[4];
    f.seekg(0, std::ios::beg);
    f.read(sig, sizeof(sig));
    if (f.gcount() != static_cast<std::streamsize>(sizeof(sig)) || std::strncmp(sig, "LASF", 4) != 0) {
        return false;
    }

    uint16_t headerSize = 0;
    uint32_t numberOfVLRs = 0;
    f.seekg(94, std::ios::beg);
    f.read(reinterpret_cast<char*>(&headerSize), sizeof(headerSize));
    f.seekg(100, std::ios::beg);
    f.read(reinterpret_cast<char*>(&numberOfVLRs), sizeof(numberOfVLRs));
    if (!f) {
        return false;
    }

    uint64_t vlrPos = headerSize;
    for (uint32_t i = 0; i < numberOfVLRs; ++i) {
        f.seekg(static_cast<std::streamoff>(vlrPos), std::ios::beg);

        char reserved[2];
        char userId[16];
        uint16_t recordId = 0;
        uint16_t recordLength = 0;
        char description[32];
        f.read(reserved, sizeof(reserved));
        f.read(userId, sizeof(userId));
        f.read(reinterpret_cast<char*>(&recordId), sizeof(recordId));
        f.read(reinterpret_cast<char*>(&recordLength), sizeof(recordLength));
        f.read(description, sizeof(description));
        if (!f) {
            return false;
        }

        uint64_t payloadPos = vlrPos + 54;
        if (std::strncmp(userId, "copc", 4) == 0 && recordId == 1) {
            char payload[160];
            f.seekg(static_cast<std::streamoff>(payloadPos), std::ios::beg);
            f.read(payload, sizeof(payload));
            if (f.gcount() != static_cast<std::streamsize>(sizeof(payload))) {
                return false;
            }

            const char* c = payload;
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

        vlrPos = payloadPos + recordLength;
    }

    return false;
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

    // Voxel keys along an axis range over [0, 2^level - 1], not [0, level] --
    // comparing keyX/keyY directly to level (the prior condition here) only
    // happened to be correct at level <= 1, and inflated every interior
    // cell's bounds out to the root's max at higher levels.
    int64_t cellsPerAxis = (int64_t(1) << e.level);
    outMinX = rootMinX + (cellWidth * e.keyX);
    outMaxX = (e.keyX == cellsPerAxis - 1) ? rootMaxX : rootMinX + (cellWidth * (e.keyX + 1));
    outMinY = rootMinY + (cellWidth * e.keyY);
    outMaxY = (e.keyY == cellsPerAxis - 1) ? rootMaxY : rootMinY + (cellWidth * (e.keyY + 1));
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
