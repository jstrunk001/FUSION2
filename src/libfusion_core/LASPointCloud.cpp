#include "fusion/lidar/LASPointCloud.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

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
    uint32_t numberOfPointRecords;          // Legacy point count (offset 107)
    uint32_t numberOfPointsByReturn[5];    // Legacy return count
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
    // LAS 1.3+ waveform offset (offset 227)
    uint64_t startOfWaveformDataPacketRecord;
    // LAS 1.4+ extended header fields (offset 235)
    uint64_t startOfExtendedVLR;              // offset 235
    uint32_t numberOfExtendedVLRs;            // offset 243
    uint64_t extendedNumberOfPointRecords;    // offset 247 (64-bit point count)
    uint64_t extendedNumberOfPointsByReturn[15]; // offset 255
};

struct RawPointFormat0 { // 20 bytes (Legacy Formats 0 - 5)
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

struct RawPointFormat6 { // 30 bytes (LAS 1.4 Formats 6 - 10)
    int32_t x;
    int32_t y;
    int32_t z;
    uint16_t intensity;
    uint8_t returnBits; // return_num(4), num_returns(4)
    uint8_t flags;      // synthetic(1), keypoint(1), withheld(1), overlap(1), scanner_channel(2), scan_dir(1), edge_flight(1)
    uint8_t classification;
    uint8_t userData;
    int16_t scanAngle;  // 16-bit scan angle (* 0.006 deg)
    uint16_t pointSourceID;
    double gpsTime;     // mandatory in format 6-10
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

    std::memset(&m_impl->rawHeader, 0, sizeof(RawLASHeader));

    // Read initial standard 227-byte header portion
    m_impl->file.read(reinterpret_cast<char*>(&m_impl->rawHeader), 227);
    if (m_impl->file.gcount() < 227 || std::memcmp(m_impl->rawHeader.fileSignature, "LASF", 4) != 0) {
        m_impl->file.close();
        return false;
    }

    // If header is LAS 1.4 or headerSize >= 375, read extended 148 bytes up to 375 bytes
    if ((m_impl->rawHeader.versionMajor == 1 && m_impl->rawHeader.versionMinor >= 4) || m_impl->rawHeader.headerSize >= 375) {
        uint16_t bytesToRead = (std::min)(static_cast<uint16_t>(m_impl->rawHeader.headerSize), static_cast<uint16_t>(sizeof(RawLASHeader))) - 227;
        if (bytesToRead > 0) {
            m_impl->file.read(reinterpret_cast<char*>(&m_impl->rawHeader) + 227, bytesToRead);
        }
    }

    m_header.versionMajor = m_impl->rawHeader.versionMajor;
    m_header.versionMinor = m_impl->rawHeader.versionMinor;
    m_header.pointFormat = m_impl->rawHeader.pointDataFormat;

    // Determine point count (LAS 1.4 64-bit field vs legacy 32-bit field)
    if (m_impl->rawHeader.versionMinor >= 4 || m_impl->rawHeader.extendedNumberOfPointRecords > 0) {
        m_header.pointCount = m_impl->rawHeader.extendedNumberOfPointRecords;
        if (m_header.pointCount == 0 && m_impl->rawHeader.numberOfPointRecords > 0) {
            m_header.pointCount = m_impl->rawHeader.numberOfPointRecords;
        }
    } else {
        m_header.pointCount = m_impl->rawHeader.numberOfPointRecords;
    }

    m_header.minX = m_impl->rawHeader.minX;
    m_header.maxX = m_impl->rawHeader.maxX;
    m_header.minY = m_impl->rawHeader.minY;
    m_header.maxY = m_impl->rawHeader.maxY;
    m_header.minZ = m_impl->rawHeader.minZ;
    m_header.maxZ = m_impl->rawHeader.maxZ;
    m_header.xScaleFactor = m_impl->rawHeader.xScaleFactor;
    m_header.yScaleFactor = m_impl->rawHeader.yScaleFactor;
    m_header.zScaleFactor = m_impl->rawHeader.zScaleFactor;
    m_header.xOffset = m_impl->rawHeader.xOffset;
    m_header.yOffset = m_impl->rawHeader.yOffset;
    m_header.zOffset = m_impl->rawHeader.zOffset;
    m_header.isCompressed = (filePath.extension() == ".laz" || (m_impl->rawHeader.pointDataFormat & 128) != 0);
    m_header.systemID = std::string(m_impl->rawHeader.systemIdentifier, strnlen(m_impl->rawHeader.systemIdentifier, 32));
    m_header.generatingSoftware = std::string(m_impl->rawHeader.generatingSoftware, strnlen(m_impl->rawHeader.generatingSoftware, 32));

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

    uint8_t fmt = m_impl->rawHeader.pointDataFormat & 0x3F; // mask out LAZ compression bit if present

    if (fmt >= 6) { // LAS 1.4 Point Formats 6 to 10
        RawPointFormat6 rawPt6;
        m_impl->file.read(reinterpret_cast<char*>(&rawPt6), sizeof(RawPointFormat6));
        if (!m_impl->file) return false;

        pt.x = (rawPt6.x * m_impl->rawHeader.xScaleFactor) + m_impl->rawHeader.xOffset;
        pt.y = (rawPt6.y * m_impl->rawHeader.yScaleFactor) + m_impl->rawHeader.yOffset;
        pt.z = (rawPt6.z * m_impl->rawHeader.zScaleFactor) + m_impl->rawHeader.zOffset;
        pt.intensity = rawPt6.intensity;
        pt.returnNumber = rawPt6.returnBits & 0x0F;
        pt.numberOfReturns = (rawPt6.returnBits >> 4) & 0x0F;
        pt.synthetic = (rawPt6.flags & 0x01) != 0;
        pt.keypoint = (rawPt6.flags & 0x02) != 0;
        pt.withheld = (rawPt6.flags & 0x04) != 0;
        pt.overlap = (rawPt6.flags & 0x08) != 0;
        pt.scannerChannel = (rawPt6.flags >> 4) & 0x03;
        pt.classification = rawPt6.classification;
        pt.scanAngle = rawPt6.scanAngle;
        pt.pointSourceID = rawPt6.pointSourceID;
        pt.gpsTime = rawPt6.gpsTime;

        int recordLengthRead = sizeof(RawPointFormat6); // 30 bytes

        // RGB for formats 7, 8, 10
        if (fmt == 7 || fmt == 8 || fmt == 10) {
            uint16_t rgb[3] = {0, 0, 0};
            m_impl->file.read(reinterpret_cast<char*>(rgb), sizeof(rgb));
            pt.red = rgb[0];
            pt.green = rgb[1];
            pt.blue = rgb[2];
            recordLengthRead += 6;
        }
        // NIR for formats 8, 10
        if (fmt == 8 || fmt == 10) {
            uint16_t nir = 0;
            m_impl->file.read(reinterpret_cast<char*>(&nir), sizeof(nir));
            pt.nir = nir;
            recordLengthRead += 2;
        }
        // Waveform for formats 9, 10 (29 bytes)
        if (fmt == 9 || fmt == 10) {
            m_impl->file.seekg(29, std::ios::cur);
            recordLengthRead += 29;
        }

        if (m_impl->rawHeader.pointDataRecordLength > recordLengthRead) {
            m_impl->file.seekg(m_impl->rawHeader.pointDataRecordLength - recordLengthRead, std::ios::cur);
        }
    } else { // Legacy LAS Formats 0 to 5
        RawPointFormat0 rawPt;
        m_impl->file.read(reinterpret_cast<char*>(&rawPt), sizeof(RawPointFormat0));
        if (!m_impl->file) return false;

        pt.x = (rawPt.x * m_impl->rawHeader.xScaleFactor) + m_impl->rawHeader.xOffset;
        pt.y = (rawPt.y * m_impl->rawHeader.yScaleFactor) + m_impl->rawHeader.yOffset;
        pt.z = (rawPt.z * m_impl->rawHeader.zScaleFactor) + m_impl->rawHeader.zOffset;
        pt.intensity = rawPt.intensity;
        pt.returnNumber = rawPt.returnBits & 0x07;
        pt.numberOfReturns = (rawPt.returnBits >> 3) & 0x07;
        pt.classification = rawPt.classification & 0x1F;
        pt.synthetic = (rawPt.classification & 0x20) != 0;
        pt.keypoint = (rawPt.classification & 0x40) != 0;
        pt.withheld = (rawPt.classification & 0x80) != 0;
        pt.overlap = false;
        pt.scannerChannel = 0;
        pt.scanAngle = rawPt.scanAngleRank;
        pt.pointSourceID = rawPt.pointSourceID;

        int recordLengthRead = sizeof(RawPointFormat0); // 20 bytes

        if (fmt == 1 || fmt == 3 || fmt == 4 || fmt == 5) {
            double gpsTime = 0.0;
            m_impl->file.read(reinterpret_cast<char*>(&gpsTime), sizeof(double));
            pt.gpsTime = gpsTime;
            recordLengthRead += 8;
        }
        if (fmt == 2 || fmt == 3 || fmt == 5) {
            uint16_t rgb[3] = {0, 0, 0};
            m_impl->file.read(reinterpret_cast<char*>(rgb), sizeof(rgb));
            pt.red = rgb[0];
            pt.green = rgb[1];
            pt.blue = rgb[2];
            recordLengthRead += 6;
        }
        if (fmt == 4 || fmt == 5) {
            m_impl->file.seekg(29, std::ios::cur);
            recordLengthRead += 29;
        }

        if (m_impl->rawHeader.pointDataRecordLength > recordLengthRead) {
            m_impl->file.seekg(m_impl->rawHeader.pointDataRecordLength - recordLengthRead, std::ios::cur);
        }
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
    bool isFormat6Plus{false};

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

    m_impl->isFormat6Plus = (headerInfo.versionMinor >= 4 || headerInfo.pointFormat >= 6);

    if (m_impl->isFormat6Plus) {
        m_impl->rawHeader.versionMajor = 1;
        m_impl->rawHeader.versionMinor = 4;
        m_impl->rawHeader.headerSize = sizeof(RawLASHeader); // 375 bytes
        m_impl->rawHeader.offsetToPointData = sizeof(RawLASHeader);
        m_impl->rawHeader.pointDataFormat = (headerInfo.pointFormat >= 6) ? headerInfo.pointFormat : 6;
        m_impl->rawHeader.pointDataRecordLength = sizeof(RawPointFormat6); // 30 bytes
    } else {
        m_impl->rawHeader.versionMajor = 1;
        m_impl->rawHeader.versionMinor = 2;
        m_impl->rawHeader.headerSize = 227; // LAS 1.2 standard header size
        m_impl->rawHeader.offsetToPointData = 227;
        m_impl->rawHeader.pointDataFormat = headerInfo.pointFormat;
        m_impl->rawHeader.pointDataRecordLength = sizeof(RawPointFormat0); // 20 bytes
    }

    m_impl->rawHeader.xScaleFactor = (headerInfo.xScaleFactor != 0.0) ? headerInfo.xScaleFactor : 0.001;
    m_impl->rawHeader.yScaleFactor = (headerInfo.yScaleFactor != 0.0) ? headerInfo.yScaleFactor : 0.001;
    m_impl->rawHeader.zScaleFactor = (headerInfo.zScaleFactor != 0.0) ? headerInfo.zScaleFactor : 0.001;
    m_impl->rawHeader.xOffset = headerInfo.xOffset;
    m_impl->rawHeader.yOffset = headerInfo.yOffset;
    m_impl->rawHeader.zOffset = headerInfo.zOffset;
    m_impl->rawHeader.minX = headerInfo.minX;
    m_impl->rawHeader.maxX = headerInfo.maxX;
    m_impl->rawHeader.minY = headerInfo.minY;
    m_impl->rawHeader.maxY = headerInfo.maxY;
    m_impl->rawHeader.minZ = headerInfo.minZ;
    m_impl->rawHeader.maxZ = headerInfo.maxZ;

    size_t headerWriteSize = m_impl->isFormat6Plus ? sizeof(RawLASHeader) : 227;
    m_impl->file.write(reinterpret_cast<char*>(&m_impl->rawHeader), headerWriteSize);
    m_impl->pointCount = 0;
    return true;
}

bool LASWriter::WritePoint(const PointRecord& pt) {
    if (!m_impl->file.is_open()) return false;

    if (m_impl->isFormat6Plus) {
        RawPointFormat6 rawPt6;
        rawPt6.x = static_cast<int32_t>((pt.x - m_impl->rawHeader.xOffset) / m_impl->rawHeader.xScaleFactor);
        rawPt6.y = static_cast<int32_t>((pt.y - m_impl->rawHeader.yOffset) / m_impl->rawHeader.yScaleFactor);
        rawPt6.z = static_cast<int32_t>((pt.z - m_impl->rawHeader.zOffset) / m_impl->rawHeader.zScaleFactor);
        rawPt6.intensity = pt.intensity;
        rawPt6.returnBits = (pt.returnNumber & 0x0F) | ((pt.numberOfReturns & 0x0F) << 4);
        rawPt6.flags = (pt.synthetic ? 0x01 : 0) |
                       (pt.keypoint ? 0x02 : 0) |
                       (pt.withheld ? 0x04 : 0) |
                       (pt.overlap ? 0x08 : 0) |
                       ((pt.scannerChannel & 0x03) << 4);
        rawPt6.classification = pt.classification;
        rawPt6.userData = 0;
        rawPt6.scanAngle = pt.scanAngle;
        rawPt6.pointSourceID = pt.pointSourceID;
        rawPt6.gpsTime = pt.gpsTime;

        m_impl->file.write(reinterpret_cast<char*>(&rawPt6), sizeof(RawPointFormat6));
    } else {
        RawPointFormat0 rawPt;
        rawPt.x = static_cast<int32_t>((pt.x - m_impl->rawHeader.xOffset) / m_impl->rawHeader.xScaleFactor);
        rawPt.y = static_cast<int32_t>((pt.y - m_impl->rawHeader.yOffset) / m_impl->rawHeader.yScaleFactor);
        rawPt.z = static_cast<int32_t>((pt.z - m_impl->rawHeader.zOffset) / m_impl->rawHeader.zScaleFactor);
        rawPt.intensity = pt.intensity;
        rawPt.returnBits = (pt.returnNumber & 0x07) | ((pt.numberOfReturns & 0x07) << 3);
        rawPt.classification = (pt.classification & 0x1F) |
                               (pt.synthetic ? 0x20 : 0) |
                               (pt.keypoint ? 0x40 : 0) |
                               (pt.withheld ? 0x80 : 0);
        rawPt.scanAngleRank = static_cast<int8_t>(std::clamp<int16_t>(pt.scanAngle, -128, 127));
        rawPt.userData = 0;
        rawPt.pointSourceID = pt.pointSourceID;

        m_impl->file.write(reinterpret_cast<char*>(&rawPt), sizeof(RawPointFormat0));
    }

    m_impl->pointCount++;
    return true;
}

void LASWriter::Close() {
    if (m_impl && m_impl->file.is_open()) {
        if (m_impl->isFormat6Plus) {
            m_impl->rawHeader.extendedNumberOfPointRecords = m_impl->pointCount;
            m_impl->rawHeader.numberOfPointRecords = (m_impl->pointCount <= 0xFFFFFFFF) ? static_cast<uint32_t>(m_impl->pointCount) : 0;
            size_t headerWriteSize = sizeof(RawLASHeader);
            m_impl->file.seekp(0, std::ios::beg);
            m_impl->file.write(reinterpret_cast<char*>(&m_impl->rawHeader), headerWriteSize);
        } else {
            m_impl->rawHeader.numberOfPointRecords = static_cast<uint32_t>(m_impl->pointCount);
            m_impl->file.seekp(0, std::ios::beg);
            m_impl->file.write(reinterpret_cast<char*>(&m_impl->rawHeader), 227);
        }
        m_impl->file.close();
    }
}

} // namespace fusion::lidar

