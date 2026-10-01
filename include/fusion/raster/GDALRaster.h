#ifndef FUSION_RASTER_GDALRASTER_H
#define FUSION_RASTER_GDALRASTER_H

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <cstdint>

class GDALDataset;
class GDALRasterBand;

namespace fusion::raster {

enum class SampleMethod {
    Nearest,
    Bilinear
};

enum class OutputMode {
    SingleBand,
    MultiBand
};

struct RasterInfo {
    int width{0};
    int height{0};
    int numBands{0};
    double originX{0.0};
    double originY{0.0};
    double pixelWidth{1.0};
    double pixelHeight{1.0};
    double minX{0.0};
    double maxX{0.0};
    double minY{0.0};
    double maxY{0.0};
    double noDataValue{-9999.0};
    bool hasNoData{false};
    std::string projectionWKT;
};

class GDALRaster {
public:
    GDALRaster();
    ~GDALRaster();

    GDALRaster(const GDALRaster&) = delete;
    GDALRaster& operator=(const GDALRaster&) = delete;
    GDALRaster(GDALRaster&&) noexcept;
    GDALRaster& operator=(GDALRaster&&) noexcept;

    bool Open(const std::filesystem::path& filePath, bool readWrite = false);

    bool Create(const std::filesystem::path& filePath,
                int width, int height, int numBands = 1,
                const std::string& dataType = "Float32",
                const std::string& driverName = "GTiff",
                const std::string& projectionWKT = "",
                const double* geotransform = nullptr,
                double noDataValue = -9999.0);

    void Close();
    bool IsOpen() const;

    const RasterInfo& GetInfo() const { return m_info; }
    int GetWidth() const { return m_info.width; }
    int GetHeight() const { return m_info.height; }
    int GetNumBands() const { return m_info.numBands; }
    double GetNoDataValue() const { return m_info.noDataValue; }

    std::optional<double> GetElevation(double x, double y, SampleMethod method = SampleMethod::Bilinear, int band = 1) const;
    std::optional<double> GetCellValue(int col, int row, int band = 1) const;
    bool SetCellValue(int col, int row, double value, int band = 1);

    bool SetBandDescription(int band, const std::string& description);
    std::string GetBandDescription(int band) const;

    bool ReadBandData(int band, std::vector<float>& buffer) const;
    bool ReadBandWindow(int band, int xOffset, int yOffset, int xSize, int ySize, std::vector<float>& buffer) const;
    bool WriteBandData(int band, const std::vector<float>& buffer);

    static bool BuildVRT(const std::filesystem::path& outputVRTPath,
                        const std::vector<std::filesystem::path>& inputRasterPaths,
                        bool relativePaths = true);

    static bool MergeVRTToGeoTIFF(const std::filesystem::path& vrtPath,
                                 const std::filesystem::path& outputGeoTIFFPath,
                                 const std::string& compressOption = "LZW");

    static bool ConvertToCOG(const std::filesystem::path& inputRasterPath,
                            const std::filesystem::path& outputCOGPath,
                            const std::string& compressOption = "DEFLATE");

    // Crops a raster down to [minX,maxX] x [minY,maxY] in the raster's own
    // georeferenced coordinates. Used to trim a buffered tile raster back to
    // its core (unbuffered) extent before it's used downstream or mosaicked.
    static bool CropToExtent(const std::filesystem::path& inputRasterPath,
                            const std::filesystem::path& outputRasterPath,
                            double minX, double minY, double maxX, double maxY);

private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
    RasterInfo m_info;

    // Populates the in-memory cache for the given (1-based) band, via a
    // single bulk RasterIO read, if it isn't already cached. GetCellValue
    // uses this instead of issuing one RasterIO call per cell -- see
    // GDALRaster.cpp for why. Returns false if the band can't be cached
    // (raster not open, band out of range, or the read itself fails).
    bool EnsureBandCached(int bandIdx) const;
};

} // namespace fusion::raster

#endif // FUSION_RASTER_GDALRASTER_H
