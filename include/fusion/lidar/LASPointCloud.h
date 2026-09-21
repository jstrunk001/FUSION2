#ifndef FUSION_LIDAR_LASPOINTCLOUD_H
#define FUSION_LIDAR_LASPOINTCLOUD_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <cstdint>
#include <functional>

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
    // OGC WKT coordinate system string, read from the "LASF_Projection"
    // VLR (record ID 2112) when present -- empty if the file carries no WKT
    // VLR (e.g. an older file using GeoTIFF keys instead).
    std::string projectionWKT;
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

    // True if the opened file carries a valid COPC (Cloud Optimized Point
    // Cloud) info VLR -- detected at Open() time.
    bool IsCOPC() const;

    // Invokes callback once per point whose x/y falls in [minX,maxX] x
    // [minY,maxY]. When IsCOPC() is true, uses the file's own COPC chunk
    // index to seek (via laszip_seek_point) directly to the chunks that
    // overlap the extent, skipping chunks that don't -- for a non-COPC
    // file, falls back to a plain sequential read-and-filter over every
    // point (today's existing pattern, just wrapped behind the same call).
    // Rewinds the reader first, so it always scans from the start.
    void ReadPointsInExtent(double minX, double minY, double maxX, double maxY,
                             const std::function<void(const PointRecord&)>& callback);

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
