#include "fusion/lidar/LASPointCloud.h"
#include "fusion/lidar/COPCIndex.h"

#include <iostream>
#include <cstring>
#include <cmath>
#include <cctype>
#include <algorithm>

#include <laszip/laszip_api.h>

namespace fusion::lidar {

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
    bool isCOPC{false};
    COPCIndex copcIndex;

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

    // Look for the OGC WKT coordinate system VLR ("LASF_Projection",
    // record ID 2112) so callers that write derived rasters (groundfilter,
    // returndensity) can carry the input point cloud's CRS through instead
    // of writing an unprojected GeoTIFF.
    // GeoTIFF-key VLRs (34735/34736/34737) are kept as raw bytes alongside
    // it, and the global encoding bits are kept too, so LASWriter can
    // reproduce the input's coordinate system and GPS time convention.
    m_header.globalEncoding = h.global_encoding;
    m_header.projectionWKT.clear();
    m_header.geoKeyVLRs.clear();
    for (laszip_U32 v = 0; v < h.number_of_variable_length_records; ++v) {
        const laszip_vlr_struct& vlr = h.vlrs[v];
        if (std::strncmp(vlr.user_id, "LASF_Projection", 16) != 0 || !vlr.data) continue;

        if (vlr.record_id == 2112 && m_header.projectionWKT.empty()) {
            const char* wkt = reinterpret_cast<const char*>(vlr.data);
            m_header.projectionWKT.assign(wkt, strnlen(wkt, vlr.record_length_after_header));
        } else if (vlr.record_id == 34735 || vlr.record_id == 34736 || vlr.record_id == 34737) {
            LASHeaderInfo::RawVLR raw;
            raw.recordID = vlr.record_id;
            raw.description.assign(vlr.description, strnlen(vlr.description, 32));
            raw.data.assign(vlr.data, vlr.data + vlr.record_length_after_header);
            m_header.geoKeyVLRs.push_back(std::move(raw));
        }
    }

    m_impl->currentPointIndex = 0;

    m_impl->isCOPC = COPCIndex::IsCOPCFile(filePath);
    if (m_impl->isCOPC) {
        m_impl->isCOPC = m_impl->copcIndex.ReadIndex(filePath);
    }

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

// Maps the point laszip_read_point() just decoded into m_impl->point onto
// a PointRecord. Shared by ReadNextPoint's plain sequential walk and
// ReadPointsInExtent's chunk-seeking COPC path so the field mapping can't
// drift between the two.
static void DecodePointRecord(const laszip_point_struct& p, const LASHeaderInfo& header, PointRecord& pt) {
    pt.x = (p.X * header.xScaleFactor) + header.xOffset;
    pt.y = (p.Y * header.yScaleFactor) + header.yOffset;
    pt.z = (p.Z * header.zScaleFactor) + header.zOffset;
    pt.intensity = p.intensity;
    pt.pointSourceID = p.point_source_ID;
    pt.gpsTime = p.gps_time;
    pt.red = p.rgb[0];
    pt.green = p.rgb[1];
    pt.blue = p.rgb[2];
    pt.nir = p.rgb[3];

    if (header.pointFormat >= 6) { // LAS 1.4 Point Formats 6 to 10 -- extended fields
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
}

bool LASReader::ReadNextPoint(PointRecord& pt) {
    if (!IsOpen() || m_impl->currentPointIndex >= m_header.pointCount) {
        return false;
    }

    if (laszip_read_point(m_impl->handle) != 0) {
        return false;
    }

    DecodePointRecord(*m_impl->point, m_header, pt);

    m_impl->currentPointIndex++;
    return true;
}

bool LASReader::IsCOPC() const {
    return m_impl && m_impl->isCOPC;
}

void LASReader::ReadPointsInExtent(double minX, double minY, double maxX, double maxY,
                                    const std::function<void(const PointRecord&)>& callback) {
    if (!IsOpen()) {
        return;
    }

    if (m_impl->isCOPC) {
        // Query only the chunks whose own bounds overlap the extent, and
        // seek straight to each one's starting point index -- laszip_seek_
        // point() already knows how to jump to any point index inside a
        // LAZ file's chunk table, so no manual byte-offset handling is
        // needed here. A chunk's own bounds are a bounding box, so points
        // inside a partially-overlapping chunk still need the exact x/y
        // filter below.
        auto chunks = m_impl->copcIndex.QueryChunks(minX, minY, maxX, maxY);
        PointRecord pt;
        for (const auto& chunk : chunks) {
            if (laszip_seek_point(m_impl->handle, static_cast<laszip_I64>(chunk.startingPointIndex)) != 0) {
                continue;
            }
            for (int32_t i = 0; i < chunk.pointCount; ++i) {
                if (laszip_read_point(m_impl->handle) != 0) {
                    break;
                }
                DecodePointRecord(*m_impl->point, m_header, pt);
                if (pt.x >= minX && pt.x <= maxX && pt.y >= minY && pt.y <= maxY) {
                    callback(pt);
                }
            }
        }
        // Leave the reader positioned at a defined, reusable state for any
        // caller that follows with a plain ReadNextPoint() loop.
        Rewind();
        return;
    }

    // Non-COPC fallback: today's plain sequential read, filtered inline.
    Rewind();
    PointRecord pt;
    while (ReadNextPoint(pt)) {
        if (pt.x >= minX && pt.x <= maxX && pt.y >= minY && pt.y <= maxY) {
            callback(pt);
        }
    }
}

void LASReader::Rewind() {
    if (IsOpen()) {
        laszip_seek_point(m_impl->handle, 0);
        m_impl->currentPointIndex = 0;
    }
}


// Writes via the same vendored LASzip DLL API the reader uses. An earlier
// version of this class hand-rolled raw LAS point structs and wrote them
// through a plain std::ofstream; that path stamped the header's declared
// pointDataFormat (e.g. 1, 2, 3) while always writing the 20-byte Format-0
// record layout underneath, and never compressed .laz output. Routing every
// write through laszip_write_point makes the on-disk record match whatever
// point_data_format is set on the header (the per-format record length
// below must still be set explicitly -- laszip does not infer it), and
// compression follows naturally from the `compress` flag passed to
// laszip_open_writer.
class LASWriter::Impl {
public:
    laszip_POINTER handle{nullptr};
    laszip_point_struct* point{nullptr};
    uint64_t pointCount{0};
    bool isFormat6Plus{false};
    double xScaleFactor{0.001}, yScaleFactor{0.001}, zScaleFactor{0.001};
    double xOffset{0.0}, yOffset{0.0}, zOffset{0.0};

    ~Impl() {
        CloseHandle();
    }

    void CloseHandle() {
        if (handle) {
            laszip_close_writer(handle);
            laszip_destroy(handle);
            handle = nullptr;
        }
        point = nullptr;
    }
};

LASWriter::LASWriter() : m_impl(std::make_unique<Impl>()) {}
LASWriter::~LASWriter() = default;

bool LASWriter::Open(const std::filesystem::path& filePath, const LASHeaderInfo& headerInfo) {
    Close();

    if (laszip_create(&m_impl->handle) != 0) {
        m_impl->handle = nullptr;
        return false;
    }

    laszip_header_struct hdr;
    std::memset(&hdr, 0, sizeof(hdr));

    m_impl->isFormat6Plus = (headerInfo.versionMinor >= 4 || headerInfo.pointFormat >= 6);

    if (m_impl->isFormat6Plus) {
        hdr.version_major = 1;
        hdr.version_minor = 4;
        hdr.header_size = 375;
        hdr.offset_to_point_data = 375;
        uint8_t requestedFormat = (headerInfo.pointFormat >= 6) ? headerInfo.pointFormat : 6;
        // Formats 9 and 10 carry mandatory waveform-packet fields that
        // PointRecord does not hold -- write the nearest format without
        // waveform data instead of claiming a format whose required fields
        // would always be empty.
        if (requestedFormat == 9) requestedFormat = 6;
        if (requestedFormat == 10) requestedFormat = 8;
        hdr.point_data_format = requestedFormat;
        switch (requestedFormat) {
            case 8:  hdr.point_data_record_length = 38; break;
            case 7:  hdr.point_data_record_length = 36; break;
            default: hdr.point_data_record_length = 30; break; // format 6
        }
    } else {
        hdr.version_major = 1;
        hdr.version_minor = 2;
        hdr.header_size = 227;
        hdr.offset_to_point_data = 227;
        uint8_t requestedFormat = headerInfo.pointFormat;
        if (requestedFormat == 4) requestedFormat = 1;
        if (requestedFormat == 5) requestedFormat = 3;
        hdr.point_data_format = requestedFormat;
        switch (requestedFormat) {
            case 3:  hdr.point_data_record_length = 34; break;
            case 2:  hdr.point_data_record_length = 26; break;
            case 1:  hdr.point_data_record_length = 28; break;
            default: hdr.point_data_record_length = 20; break; // format 0
        }
    }

    hdr.x_scale_factor = (headerInfo.xScaleFactor != 0.0) ? headerInfo.xScaleFactor : 0.001;
    hdr.y_scale_factor = (headerInfo.yScaleFactor != 0.0) ? headerInfo.yScaleFactor : 0.001;
    hdr.z_scale_factor = (headerInfo.zScaleFactor != 0.0) ? headerInfo.zScaleFactor : 0.001;
    hdr.x_offset = headerInfo.xOffset;
    hdr.y_offset = headerInfo.yOffset;
    hdr.z_offset = headerInfo.zOffset;
    hdr.max_x = headerInfo.maxX;
    hdr.min_x = headerInfo.minX;
    hdr.max_y = headerInfo.maxY;
    hdr.min_y = headerInfo.minY;
    hdr.max_z = headerInfo.maxZ;
    hdr.min_z = headerInfo.minZ;

    if (!headerInfo.systemID.empty()) {
        std::strncpy(hdr.system_identifier, headerInfo.systemID.c_str(), sizeof(hdr.system_identifier) - 1);
    }
    std::strncpy(hdr.generating_software, "FUSION2", sizeof(hdr.generating_software) - 1);

    // Global encoding: carry bit 0 (GPS time type) through from the input so
    // point timestamps keep their meaning. Set bit 4 (WKT) whenever a WKT
    // VLR is written and either the input set it or the output is LAS 1.4
    // (where the spec requires it) -- without bit 4, readers such as
    // lidR/rlas ignore the WKT VLR and treat the file as unprojected.
    // Waveform bits (1-2) are never carried, since no waveform data is written.
    const bool writeWKT = !headerInfo.projectionWKT.empty();
    hdr.global_encoding = headerInfo.globalEncoding & 0x0001;
    if (writeWKT && (m_impl->isFormat6Plus || (headerInfo.globalEncoding & 0x0010))) {
        hdr.global_encoding |= 0x0010;
    }

    if (laszip_set_header(m_impl->handle, &hdr) != 0) {
        m_impl->CloseHandle();
        return false;
    }

    // Write OGC WKT coordinate system VLR ("LASF_Projection", record ID 2112)
    // when present, so derived point clouds (e.g. tile clips in the batch
    // pipeline) retain the CRS instead of becoming unprojected.
    if (writeWKT) {
        laszip_add_vlr(m_impl->handle,
                       "LASF_Projection",
                       2112,
                       static_cast<laszip_U16>(headerInfo.projectionWKT.size() + 1),
                       "OGC Coordinate System WKT",
                       reinterpret_cast<const laszip_U8*>(headerInfo.projectionWKT.c_str()));
    }

    // Write GeoTIFF-key coordinate system VLRs unchanged. A LAS 1.4 output
    // with a WKT VLR skips them, since the spec disallows GeoTIFF keys
    // alongside WKT there; otherwise they are the only coordinate system
    // record the input had, and dropping them would leave it unprojected.
    if (!(m_impl->isFormat6Plus && writeWKT)) {
        for (const auto& raw : headerInfo.geoKeyVLRs) {
            laszip_add_vlr(m_impl->handle,
                           "LASF_Projection",
                           raw.recordID,
                           static_cast<laszip_U16>(raw.data.size()),
                           raw.description.c_str(),
                           raw.data.data());
        }
    }

    m_impl->xScaleFactor = hdr.x_scale_factor;
    m_impl->yScaleFactor = hdr.y_scale_factor;
    m_impl->zScaleFactor = hdr.z_scale_factor;
    m_impl->xOffset = hdr.x_offset;
    m_impl->yOffset = hdr.y_offset;
    m_impl->zOffset = hdr.z_offset;

    std::string ext = filePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    laszip_BOOL compress = (ext == ".laz") ? 1 : 0;

    if (laszip_open_writer(m_impl->handle, filePath.string().c_str(), compress) != 0) {
        laszip_CHAR* err = nullptr;
        laszip_get_error(m_impl->handle, &err);
        std::cerr << "LASWriter::Open failed for " << filePath << ": " << (err ? err : "unknown") << "\n";
        m_impl->CloseHandle();
        return false;
    }

    if (laszip_get_point_pointer(m_impl->handle, &m_impl->point) != 0) {
        m_impl->CloseHandle();
        return false;
    }

    m_impl->pointCount = 0;
    return true;
}

bool LASWriter::WritePoint(const PointRecord& pt) {
    if (!m_impl->handle || !m_impl->point) return false;

    laszip_point_struct& p = *m_impl->point;
    std::memset(&p, 0, sizeof(p));

    p.X = static_cast<laszip_I32>(std::llround((pt.x - m_impl->xOffset) / m_impl->xScaleFactor));
    p.Y = static_cast<laszip_I32>(std::llround((pt.y - m_impl->yOffset) / m_impl->yScaleFactor));
    p.Z = static_cast<laszip_I32>(std::llround((pt.z - m_impl->zOffset) / m_impl->zScaleFactor));
    p.intensity = pt.intensity;
    p.point_source_ID = pt.pointSourceID;
    p.gps_time = pt.gpsTime;
    p.rgb[0] = pt.red;
    p.rgb[1] = pt.green;
    p.rgb[2] = pt.blue;
    p.rgb[3] = pt.nir;

    if (m_impl->isFormat6Plus) { // LAS 1.4 Point Formats 6 to 10 -- extended fields
        p.extended_return_number = pt.returnNumber & 0x0F;
        p.extended_number_of_returns = pt.numberOfReturns & 0x0F;
        p.extended_classification = pt.classification;
        p.extended_scan_angle = pt.scanAngle;
        p.extended_scanner_channel = pt.scannerChannel & 0x03;
        p.extended_classification_flags = (pt.synthetic ? 0x01 : 0) |
                                           (pt.keypoint ? 0x02 : 0) |
                                           (pt.withheld ? 0x04 : 0) |
                                           (pt.overlap ? 0x08 : 0);
    } else { // Legacy LAS Formats 0 to 5
        p.return_number = pt.returnNumber & 0x07;
        p.number_of_returns = pt.numberOfReturns & 0x07;
        p.classification = pt.classification & 0x1F;
        p.synthetic_flag = pt.synthetic ? 1 : 0;
        p.keypoint_flag = pt.keypoint ? 1 : 0;
        p.withheld_flag = pt.withheld ? 1 : 0;
        p.scan_angle_rank = static_cast<laszip_I8>(std::clamp<int16_t>(pt.scanAngle, -128, 127));
    }

    if (laszip_write_point(m_impl->handle) != 0) {
        return false;
    }
    // laszip_update_inventory accumulates one point's worth of running
    // count/bounding-box state per call -- it must be invoked once per
    // point written (calling it only once at Close() undercounted, since it
    // is not itself a summary/finalize step).
    laszip_update_inventory(m_impl->handle);

    m_impl->pointCount++;
    return true;
}

void LASWriter::Close() {
    if (m_impl && m_impl->handle) {
        m_impl->CloseHandle();
    }
}

} // namespace fusion::lidar

