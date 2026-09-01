#include "fusion/lidar/LASPointCloud.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>

namespace fusion::lidar {

#pragma pack(push, 1)
struct RawLASHeader {
    char fileSignature[4];       // "LASF"
    uint16_t fileSourceID;
    uint16_t globalEncoding;
    uint32_t guid1;
    uint16_t guid2;
    uint16_t guid3;
    uint8_t guid4[8];
    uint8_t versionMajor;
    uint8_t versionMinor;
    char systemIdentifier[32];
    char generatingSoftware[32];
    uint16_t fileCreationDay;
    uint16_t fileCreationYear;
    uint16_t headerSize;
    uint32_t offsetToPointData;
    uint32_t numberOfVLRs;
    uint8_t pointDataFormat;
    uint16_t pointDataRecordLength;
    uint32_t numberOfPointRecords;
    uint32_t numberOfPointsByReturn[5];
    double xScaleFactor;
    double yScaleFactor;
    double zScaleFactor;
    double xOffset;
    double yOffset;
    double zOffset;
    double maxX;
    double minX;
    double maxY;
    double minY;
    double maxZ;
    double minZ;
};

struct RawPointFormat0 {
    int32_t x;
    int32_t y;
    int32_t z;
    uint16_t intensity;
    uint8_t returnBits; // return_num(3), num_returns(3), scan_dir(1), edge_flight(1)
    uint8_t classification;
    int8_t scanAngleRank;
    uint8_t userData;
    uint16_t pointSourceID;
};
#pragma pack(pop)

class LASReader::Impl {
public:
    std::ifstream file;
    RawLASHeader rawHeader;
    uint64_t currentPointIndex{0};

    ~Impl() {
        if (file.is_open()) {
            file.close();
        }
    }
};

LASReader::LASReader() : m_impl(std::make_unique<Impl>()) {}
LASReader::~LASReader() = default;

LASReader::LASReader(LASReader&&) noexcept = default;
LASReader& LASReader::operator=(LASReader&&) noexcept = default;

bool LASReader::Open(const std::filesystem::path& filePath) {
    Close();

    m_impl->file.open(filePath.string(), std::ios::binary);
    if (!m_impl->file.is_open()) {
        return false;
    }

    m_impl->file.read(reinterpret_cast<char*>(&m_impl->rawHeader), sizeof(RawLASHeader));
    if (std::memcmp(m_impl->rawHeader.fileSignature, "LASF", 4) != 0) {
        m_impl->file.close();
        return false;
    }

    m_header.versionMajor = m_impl->rawHeader.versionMajor;
    m_header.versionMinor = m_impl->rawHeader.versionMinor;
    m_header.pointFormat = m_impl->rawHeader.pointDataFormat;
    m_header.pointCount = m_impl->rawHeader.numberOfPointRecords;
    m_header.minX = m_impl->rawHeader.minX;
    m_header.maxX = m_impl->rawHeader.maxX;
    m_header.minY = m_impl->rawHeader.minY;
    m_header.maxY = m_impl->rawHeader.maxY;
    m_header.minZ = m_impl->rawHeader.minZ;
    m_header.maxZ = m_impl->rawHeader.maxZ;
    m_header.isCompressed = (filePath.extension() == ".laz");

    // Seek to first point record
    m_impl->file.seekg(m_impl->rawHeader.offsetToPointData, std::ios::beg);
    m_impl->currentPointIndex = 0;

    return true;
}

void LASReader::Close() {
    if (m_impl && m_impl->file.is_open()) {
        m_impl->file.close();
    }
    m_header = {};
}

bool LASReader::IsOpen() const {
    return (m_impl && m_impl->file.is_open());
}

bool LASReader::ReadNextPoint(PointRecord& pt) {
    if (!IsOpen() || m_impl->currentPointIndex >= m_header.pointCount) {
        return false;
    }

    RawPointFormat0 rawPt;
    m_impl->file.read(reinterpret_cast<char*>(&rawPt), sizeof(RawPointFormat0));
    if (!m_impl->file) return false;

    // Convert raw int32 coordinates using scale factors & offsets
    pt.x = (rawPt.x * m_impl->rawHeader.xScaleFactor) + m_impl->rawHeader.xOffset;
    pt.y = (rawPt.y * m_impl->rawHeader.yScaleFactor) + m_impl->rawHeader.yOffset;
    pt.z = (rawPt.z * m_impl->rawHeader.zScaleFactor) + m_impl->rawHeader.zOffset;
    pt.intensity = rawPt.intensity;
    pt.returnNumber = rawPt.returnBits & 0x07;
    pt.numberOfReturns = (rawPt.returnBits >> 3) & 0x07;
    pt.classification = rawPt.classification & 0x1F;
    pt.scanAngle = rawPt.scanAngleRank;
    pt.pointSourceID = rawPt.pointSourceID;

    // Read additional fields based on point format (GPS time, RGB)
    if (m_impl->rawHeader.pointDataFormat == 1 || m_impl->rawHeader.pointDataFormat == 3) {
        double gpsTime = 0.0;
        m_impl->file.read(reinterpret_cast<char*>(&gpsTime), sizeof(double));
        pt.gpsTime = gpsTime;
    }
    if (m_impl->rawHeader.pointDataFormat == 2 || m_impl->rawHeader.pointDataFormat == 3) {
        uint16_t rgb[3] = {0, 0, 0};
        m_impl->file.read(reinterpret_cast<char*>(rgb), sizeof(rgb));
        pt.red = rgb[0];
        pt.green = rgb[1];
        pt.blue = rgb[2];
    }

    // Skip any extra bytes per record if record length > format size
    int recordLengthRead = sizeof(RawPointFormat0) +
                           ((m_impl->rawHeader.pointDataFormat == 1 || m_impl->rawHeader.pointDataFormat == 3) ? 8 : 0) +
                           ((m_impl->rawHeader.pointDataFormat == 2 || m_impl->rawHeader.pointDataFormat == 3) ? 6 : 0);
    if (m_impl->rawHeader.pointDataRecordLength > recordLengthRead) {
        m_impl->file.seekg(m_impl->rawHeader.pointDataRecordLength - recordLengthRead, std::ios::cur);
    }

    m_impl->currentPointIndex++;
    return true;
}

void LASReader::Rewind() {
    if (IsOpen()) {
        m_impl->file.seekg(m_impl->rawHeader.offsetToPointData, std::ios::beg);
        m_impl->currentPointIndex = 0;
    }
}


class LASWriter::Impl {
public:
    std::ofstream file;
    RawLASHeader rawHeader;
    uint64_t pointCount{0};

    ~Impl() {
        if (file.is_open()) {
            file.close();
        }
    }
};

LASWriter::LASWriter() : m_impl(std::make_unique<Impl>()) {}
LASWriter::~LASWriter() = default;

bool LASWriter::Open(const std::filesystem::path& filePath, const LASHeaderInfo& headerInfo) {
    Close();

    m_impl->file.open(filePath.string(), std::ios::binary);
    if (!m_impl->file.is_open()) {
        return false;
    }

    std::memset(&m_impl->rawHeader, 0, sizeof(RawLASHeader));
    std::memcpy(m_impl->rawHeader.fileSignature, "LASF", 4);
    m_impl->rawHeader.versionMajor = 1;
    m_impl->rawHeader.versionMinor = 2;
    m_impl->rawHeader.headerSize = sizeof(RawLASHeader);
    m_impl->rawHeader.offsetToPointData = sizeof(RawLASHeader);
    m_impl->rawHeader.pointDataFormat = 0;
    m_impl->rawHeader.pointDataRecordLength = sizeof(RawPointFormat0);
    m_impl->rawHeader.xScaleFactor = 0.001;
    m_impl->rawHeader.yScaleFactor = 0.001;
    m_impl->rawHeader.zScaleFactor = 0.001;
    m_impl->rawHeader.xOffset = headerInfo.minX;
    m_impl->rawHeader.yOffset = headerInfo.minY;
    m_impl->rawHeader.zOffset = headerInfo.minZ;

    m_impl->file.write(reinterpret_cast<char*>(&m_impl->rawHeader), sizeof(RawLASHeader));
    m_impl->pointCount = 0;
    return true;
}

bool LASWriter::WritePoint(const PointRecord& pt) {
    if (!m_impl->file.is_open()) return false;

    RawPointFormat0 rawPt;
    rawPt.x = static_cast<int32_t>((pt.x - m_impl->rawHeader.xOffset) / m_impl->rawHeader.xScaleFactor);
    rawPt.y = static_cast<int32_t>((pt.y - m_impl->rawHeader.yOffset) / m_impl->rawHeader.yScaleFactor);
    rawPt.z = static_cast<int32_t>((pt.z - m_impl->rawHeader.zOffset) / m_impl->rawHeader.zScaleFactor);
    rawPt.intensity = pt.intensity;
    rawPt.returnBits = (pt.returnNumber & 0x07) | ((pt.numberOfReturns & 0x07) << 3);
    rawPt.classification = pt.classification;
    rawPt.scanAngleRank = pt.scanAngle;
    rawPt.userData = 0;
    rawPt.pointSourceID = pt.pointSourceID;

    m_impl->file.write(reinterpret_cast<char*>(&rawPt), sizeof(RawPointFormat0));
    m_impl->pointCount++;
    return true;
}

void LASWriter::Close() {
    if (m_impl && m_impl->file.is_open()) {
        m_impl->rawHeader.numberOfPointRecords = static_cast<uint32_t>(m_impl->pointCount);
        m_impl->file.seekp(0, std::ios::beg);
        m_impl->file.write(reinterpret_cast<char*>(&m_impl->rawHeader), sizeof(RawLASHeader));
        m_impl->file.close();
    }
}

} // namespace fusion::lidar
