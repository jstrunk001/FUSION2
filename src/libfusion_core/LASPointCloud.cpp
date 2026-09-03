#include "fusion/lidar/LASPointCloud.h"

#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <algorithm>

#include <laszip/laszip_api.h>

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

// Reads via the vendored LASzip DLL API (deps/laszip_minimal), which handles
// both plain LAS and LASzip-compressed LAZ through the same laszip_open_reader
// call -- this replaces an earlier hand-rolled raw-struct parser that could
// only read uncompressed LAS and silently produced garbage on real LAZ input.
class LASReader::Impl {
public:
    laszip_POINTER handle{nullptr};
    laszip_header_struct* header{nullptr};
    laszip_point_struct* point{nullptr};
    uint64_t currentPointIndex{0};

    ~Impl() {
        CloseHandle();
    }

    void CloseHandle() {
        if (handle) {
            laszip_close_reader(handle);
            laszip_destroy(handle);
            handle = nullptr;
        }
        header = nullptr;
        point = nullptr;
    }
};

LASReader::LASReader() : m_impl(std::make_unique<Impl>()) {}
LASReader::~LASReader() = default;

LASReader::LASReader(LASReader&&) noexcept = default;
LASReader& LASReader::operator=(LASReader&&) noexcept = default;

bool LASReader::Open(const std::filesystem::path& filePath) {
    Close();

    if (laszip_create(&m_impl->handle) != 0) {
        m_impl->handle = nullptr;
        return false;
    }

    laszip_BOOL isCompressed = 0;
    if (laszip_open_reader(m_impl->handle, filePath.string().c_str(), &isCompressed) != 0) {
        laszip_CHAR* err = nullptr;
        laszip_get_error(m_impl->handle, &err);
        std::cerr << "LASReader::Open failed for " << filePath << ": " << (err ? err : "unknown") << "\n";
        m_impl->CloseHandle();
        return false;
    }

    if (laszip_get_header_pointer(m_impl->handle, &m_impl->header) != 0 ||
        laszip_get_point_pointer(m_impl->handle, &m_impl->point) != 0) {
        laszip_CHAR* err = nullptr;
        laszip_get_error(m_impl->handle, &err);
        std::cerr << "LASReader::Open get pointer failed: " << (err ? err : "unknown") << "\n";
        m_impl->CloseHandle();
        return false;
    }

    const laszip_header_struct& h = *m_impl->header;

    m_header.versionMajor = h.version_major;
    m_header.versionMinor = h.version_minor;
    m_header.pointFormat = h.point_data_format;

    // Determine point count (LAS 1.4 64-bit field vs legacy 32-bit field)
    if (h.version_minor >= 4 || h.extended_number_of_point_records > 0) {
        m_header.pointCount = h.extended_number_of_point_records;
        if (m_header.pointCount == 0 && h.number_of_point_records > 0) {
            m_header.pointCount = h.number_of_point_records;
        }
    } else {
        m_header.pointCount = h.number_of_point_records;
    }

    m_header.minX = h.min_x;
    m_header.maxX = h.max_x;
    m_header.minY = h.min_y;
    m_header.maxY = h.max_y;
    m_header.minZ = h.min_z;
    m_header.maxZ = h.max_z;
    m_header.xScaleFactor = h.x_scale_factor;
    m_header.yScaleFactor = h.y_scale_factor;
    m_header.zScaleFactor = h.z_scale_factor;
    m_header.xOffset = h.x_offset;
    m_header.yOffset = h.y_offset;
    m_header.zOffset = h.z_offset;
    m_header.isCompressed = (isCompressed != 0);
    m_header.systemID = std::string(h.system_identifier, strnlen(h.system_identifier, 32));
    m_header.generatingSoftware = std::string(h.generating_software, strnlen(h.generating_software, 32));

    m_impl->currentPointIndex = 0;

    return true;
}

void LASReader::Close() {
    if (m_impl) {
        m_impl->CloseHandle();
    }
    m_header = {};
}

bool LASReader::IsOpen() const {
    return (m_impl && m_impl->handle != nullptr);
}

bool LASReader::ReadNextPoint(PointRecord& pt) {
    if (!IsOpen() || m_impl->currentPointIndex >= m_header.pointCount) {
        return false;
    }

    if (laszip_read_point(m_impl->handle) != 0) {
        return false;
    }

    const laszip_point_struct& p = *m_impl->point;

    pt.x = (p.X * m_header.xScaleFactor) + m_header.xOffset;
    pt.y = (p.Y * m_header.yScaleFactor) + m_header.yOffset;
    pt.z = (p.Z * m_header.zScaleFactor) + m_header.zOffset;
    pt.intensity = p.intensity;
    pt.pointSourceID = p.point_source_ID;
    pt.gpsTime = p.gps_time;
    pt.red = p.rgb[0];
    pt.green = p.rgb[1];
    pt.blue = p.rgb[2];
    pt.nir = p.rgb[3];

    if (m_header.pointFormat >= 6) { // LAS 1.4 Point Formats 6 to 10 -- extended fields
        pt.returnNumber = p.extended_return_number;
        pt.numberOfReturns = p.extended_number_of_returns;
        pt.classification = p.extended_classification;
        pt.scanAngle = p.extended_scan_angle;
        pt.scannerChannel = p.extended_scanner_channel;
        pt.synthetic = (p.extended_classification_flags & 0x01) != 0;
        pt.keypoint = (p.extended_classification_flags & 0x02) != 0;
        pt.withheld = (p.extended_classification_flags & 0x04) != 0;
        pt.overlap = (p.extended_classification_flags & 0x08) != 0;
    } else { // Legacy LAS Formats 0 to 5
        pt.returnNumber = p.return_number;
        pt.numberOfReturns = p.number_of_returns;
        pt.classification = p.classification;
        pt.scanAngle = p.scan_angle_rank;
        pt.scannerChannel = 0;
        pt.synthetic = (p.synthetic_flag != 0);
        pt.keypoint = (p.keypoint_flag != 0);
        pt.withheld = (p.withheld_flag != 0);
        pt.overlap = false;
    }

    m_impl->currentPointIndex++;
    return true;
}

void LASReader::Rewind() {
    if (IsOpen()) {
        laszip_seek_point(m_impl->handle, 0);
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

