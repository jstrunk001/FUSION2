#include "fusion/lidar/LASPointCloud.h"

#include "lasreader.hpp"
#include "laswriter.hpp"
#include <fstream>
#include <iostream>

namespace fusion::lidar {

class LASReader::Impl {
public:
    LASreadOpener lasreadopener;
    LASreader* lasreader{nullptr};

    ~Impl() {
        if (lasreader) {
            lasreader->close();
            delete lasreader;
            lasreader = nullptr;
        }
    }
};

LASReader::LASReader() : m_impl(std::make_unique<Impl>()) {}
LASReader::~LASReader() = default;

LASReader::LASReader(LASReader&&) noexcept = default;
LASReader& operator=(LASReader&&) noexcept = default;

bool LASReader::Open(const std::filesystem::path& filePath) {
    Close();

    std::string pathStr = filePath.string();
    m_impl->lasreadopener.set_file_name(pathStr.c_str());
    m_impl->lasreader = m_impl->lasreadopener.open();

    if (!m_impl->lasreader) {
        return false;
    }

    const auto& header = m_impl->lasreader->header;
    m_header.pointCount = header.number_of_point_records ? header.number_of_point_records : header.extended_number_of_point_records;
    m_header.minX = header.min_x;
    m_header.maxX = header.max_x;
    m_header.minY = header.min_y;
    m_header.maxY = header.max_y;
    m_header.minZ = header.min_z;
    m_header.maxZ = header.max_z;
    m_header.pointFormat = header.point_data_format;
    m_header.versionMajor = header.version_major;
    m_header.versionMinor = header.version_minor;
    m_header.isCompressed = (filePath.extension() == ".laz");

    return true;
}

void LASReader::Close() {
    if (m_impl && m_impl->lasreader) {
        m_impl->lasreader->close();
        delete m_impl->lasreader;
        m_impl->lasreader = nullptr;
    }
    m_header = {};
}

bool LASReader::IsOpen() const {
    return (m_impl && m_impl->lasreader != nullptr);
}

bool LASReader::ReadNextPoint(PointRecord& pt) {
    if (!IsOpen()) return false;

    if (!m_impl->lasreader->read_point()) {
        return false;
    }

    const auto& p = m_impl->lasreader->point;
    pt.x = p.get_x();
    pt.y = p.get_y();
    pt.z = p.get_z();
    pt.intensity = p.intensity;
    pt.returnNumber = p.return_number;
    pt.numberOfReturns = p.number_of_returns;
    pt.classification = p.classification;
    pt.scanAngle = static_cast<int8_t>(p.scan_angle_rank);
    pt.pointSourceID = p.point_source_ID;
    pt.gpsTime = p.gps_time;
    pt.red = p.rgb[0];
    pt.green = p.rgb[1];
    pt.blue = p.rgb[2];
    pt.withheld = p.withheld_flag;
    pt.keypoint = p.keypoint_flag;
    pt.synthetic = p.synthetic_flag;

    return true;
}

void LASReader::Rewind() {
    if (IsOpen()) {
        m_impl->lasreader->seek(0);
    }
}


class LASWriter::Impl {
public:
    LASwriteOpener laswriteopener;
    LASwriter* laswriter{nullptr};
    LASheader header;
    LASpoint point;

    ~Impl() {
        if (laswriter) {
            laswriter->close();
            delete laswriter;
            laswriter = nullptr;
        }
    }
};

LASWriter::LASWriter() : m_impl(std::make_unique<Impl>()) {}
LASWriter::~LASWriter() = default;

bool LASWriter::Open(const std::filesystem::path& filePath, const LASHeaderInfo& headerInfo) {
    Close();

    std::string pathStr = filePath.string();
    m_impl->laswriteopener.set_file_name(pathStr.c_str());

    m_impl->header.point_data_format = headerInfo.pointFormat;
    m_impl->header.point_data_record_length = (headerInfo.pointFormat == 2 || headerInfo.pointFormat == 3) ? 34 : 28;
    m_impl->header.version_major = headerInfo.versionMajor;
    m_impl->header.version_minor = headerInfo.versionMinor;
    m_impl->header.x_scale_factor = 0.001;
    m_impl->header.y_scale_factor = 0.001;
    m_impl->header.z_scale_factor = 0.001;

    m_impl->laswriter = m_impl->laswriteopener.open(&m_impl->header);
    if (!m_impl->laswriter) {
        return false;
    }

    m_impl->point.init(&m_impl->header, m_impl->header.point_data_format, m_impl->header.point_data_record_length, 0);
    return true;
}

bool LASWriter::WritePoint(const PointRecord& pt) {
    if (!m_impl->laswriter) return false;

    m_impl->point.set_x(pt.x);
    m_impl->point.set_y(pt.y);
    m_impl->point.set_z(pt.z);
    m_impl->point.intensity = pt.intensity;
    m_impl->point.return_number = pt.returnNumber;
    m_impl->point.number_of_returns = pt.numberOfReturns;
    m_impl->point.classification = pt.classification;
    m_impl->point.scan_angle_rank = pt.scanAngle;
    m_impl->point.point_source_ID = pt.pointSourceID;
    m_impl->point.gps_time = pt.gpsTime;
    m_impl->point.rgb[0] = pt.red;
    m_impl->point.rgb[1] = pt.green;
    m_impl->point.rgb[2] = pt.blue;
    m_impl->point.withheld_flag = pt.withheld;
    m_impl->point.keypoint_flag = pt.keypoint;
    m_impl->point.synthetic_flag = pt.synthetic;

    m_impl->laswriter->write_point(&m_impl->point);
    m_impl->laswriter->update_inventory(&m_impl->point);
    return true;
}

void LASWriter::Close() {
    if (m_impl && m_impl->laswriter) {
        m_impl->laswriter->update_header(&m_impl->header, true);
        m_impl->laswriter->close();
        delete m_impl->laswriter;
        m_impl->laswriter = nullptr;
    }
}

} // namespace fusion::lidar
