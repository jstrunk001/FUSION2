#ifndef FUSION_LIDAR_LASPOINTCLOUD_H
#define FUSION_LIDAR_LASPOINTCLOUD_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <cstdint>

namespace fusion::lidar {

struct PointRecord {
    double x{0.0};
    double y{0.0};
    double z{0.0};
    uint16_t intensity{0};
    uint8_t returnNumber{1};
    uint8_t numberOfReturns{1};
    uint8_t classification{0};
    int16_t scanAngle{0};
    uint16_t pointSourceID{0};
    double gpsTime{0.0};
    uint16_t red{0};
    uint16_t green{0};
    uint16_t blue{0};
    uint16_t nir{0};
    bool withheld{false};
    bool keypoint{false};
    bool synthetic{false};
    bool overlap{false};
    uint8_t scannerChannel{0};
};

struct LASHeaderInfo {
    uint64_t pointCount{0};
    double minX{0.0};
    double maxX{0.0};
    double minY{0.0};
    double maxY{0.0};
    double minZ{0.0};
    double maxZ{0.0};
    double xScaleFactor{0.001};
    double yScaleFactor{0.001};
    double zScaleFactor{0.001};
    double xOffset{0.0};
    double yOffset{0.0};
    double zOffset{0.0};
    uint8_t pointFormat{0};
    uint8_t versionMajor{1};
    uint8_t versionMinor{2};
    bool isCompressed{false};
    std::string systemID;
    std::string generatingSoftware;
};

class LASReader {
public:
    LASReader();
    ~LASReader();

    LASReader(const LASReader&) = delete;
    LASReader& operator=(const LASReader&) = delete;
    LASReader(LASReader&&) noexcept;
    LASReader& operator=(LASReader&&) noexcept;

    bool Open(const std::filesystem::path& filePath);
    void Close();
    bool IsOpen() const;

    const LASHeaderInfo& GetHeader() const { return m_header; }
    uint64_t GetPointCount() const { return m_header.pointCount; }

    bool ReadNextPoint(PointRecord& pt);
    void Rewind();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    LASHeaderInfo m_header;
};

class LASWriter {
public:
    LASWriter();
    ~LASWriter();

    LASWriter(const LASWriter&) = delete;
    LASWriter& operator=(const LASWriter&) = delete;

    bool Open(const std::filesystem::path& filePath, const LASHeaderInfo& header);
    bool WritePoint(const PointRecord& pt);
    void Close();

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace fusion::lidar

#endif // FUSION_LIDAR_LASPOINTCLOUD_H
