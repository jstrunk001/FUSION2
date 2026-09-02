#include "fusion/raster/GDALRaster.h"

#include <gdal_priv.h>
#include <cpl_conv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace fusion::raster {

class GDALRaster::Impl {
public:
    GDALDataset* dataset{nullptr};
    std::vector<double> geotransform{0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    std::vector<double> invGeotransform{0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    bool isReadWrite{false};

    ~Impl() {
        if (dataset) {
            GDALClose(dataset);
            dataset = nullptr;
        }
    }
};

GDALRaster::GDALRaster() : m_impl(std::make_unique<Impl>()) {
    GDALAllRegister();
}

GDALRaster::~GDALRaster() = default;

GDALRaster::GDALRaster(GDALRaster&&) noexcept = default;
GDALRaster& GDALRaster::operator=(GDALRaster&&) noexcept = default;

bool GDALRaster::Open(const std::filesystem::path& filePath, bool readWrite) {
    Close();

    GDALAccess access = readWrite ? GA_Update : GA_ReadOnly;
    m_impl->dataset = static_cast<GDALDataset*>(GDALOpen(filePath.string().c_str(), access));
    if (!m_impl->dataset) {
        return false;
    }

    m_impl->isReadWrite = readWrite;
    m_info.width = m_impl->dataset->GetRasterXSize();
    m_info.height = m_impl->dataset->GetRasterYSize();
    m_info.numBands = m_impl->dataset->GetRasterCount();

    double gt[6] = {0, 1, 0, 0, 0, -1};
    if (m_impl->dataset->GetGeoTransform(gt) == CE_None) {
        m_impl->geotransform.assign(gt, gt + 6);
        GDALInvGeoTransform(gt, m_impl->invGeotransform.data());
    }

    m_info.originX = m_impl->geotransform[0];
    m_info.originY = m_impl->geotransform[3];
    m_info.pixelWidth = m_impl->geotransform[1];
    m_info.pixelHeight = std::abs(m_impl->geotransform[5]);

    m_info.minX = m_info.originX;
    m_info.maxX = m_info.originX + (m_info.width * m_impl->geotransform[1]);
    m_info.maxY = m_info.originY;
    m_info.minY = m_info.originY + (m_info.height * m_impl->geotransform[5]);
    if (m_info.minY > m_info.maxY) std::swap(m_info.minY, m_info.maxY);

    if (m_info.numBands > 0) {
        GDALRasterBand* band = m_impl->dataset->GetRasterBand(1);
        int hasNoData = 0;
        double nd = band->GetNoDataValue(&hasNoData);
        m_info.hasNoData = (hasNoData != 0);
        m_info.noDataValue = m_info.hasNoData ? nd : -9999.0;
    }

    const char* proj = m_impl->dataset->GetProjectionRef();
    if (proj) {
        m_info.projectionWKT = proj;
    }

    return true;
}

bool GDALRaster::Create(const std::filesystem::path& filePath,
                         int width, int height, int numBands,
                         const std::string& dataType,
                         const std::string& driverName,
                         const std::string& projectionWKT,
                         const double* geotransform,
                         double noDataValue) {
    Close();

    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName(driverName.c_str());
    if (!driver) {
        return false;
    }

    GDALDataType dt = GDT_Float32;
    if (dataType == "Int16") dt = GDT_Int16;
    else if (dataType == "UInt16") dt = GDT_UInt16;
    else if (dataType == "Int32") dt = GDT_Int32;
    else if (dataType == "Float64") dt = GDT_Float64;

    char** options = nullptr;
    if (driverName == "GTiff") {
        options = CSLSetNameValue(options, "COMPRESS", "LZW");
        options = CSLSetNameValue(options, "TILED", "YES");
    } else if (driverName == "COG") {
        options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    }

    m_impl->dataset = driver->Create(filePath.string().c_str(), width, height, numBands, dt, options);
    CSLDestroy(options);

    if (!m_impl->dataset) {
        return false;
    }

    m_info.width = width;
    m_info.height = height;
    m_info.numBands = numBands;
    m_info.noDataValue = noDataValue;
    m_info.hasNoData = true;

    if (geotransform) {
        m_impl->dataset->SetGeoTransform(const_cast<double*>(geotransform));
        m_impl->geotransform.assign(geotransform, geotransform + 6);
        GDALInvGeoTransform(const_cast<double*>(geotransform), m_impl->invGeotransform.data());

        m_info.originX = geotransform[0];
        m_info.originY = geotransform[3];
        m_info.pixelWidth = geotransform[1];
        m_info.pixelHeight = std::abs(geotransform[5]);
        m_info.minX = m_info.originX;
        m_info.maxX = m_info.originX + (width * geotransform[1]);
        m_info.maxY = m_info.originY;
        m_info.minY = m_info.originY + (height * geotransform[5]);
        if (m_info.minY > m_info.maxY) std::swap(m_info.minY, m_info.maxY);
    }

    if (!projectionWKT.empty()) {
        m_impl->dataset->SetProjection(projectionWKT.c_str());
        m_info.projectionWKT = projectionWKT;
    }

    for (int b = 1; b <= numBands; ++b) {
        GDALRasterBand* band = m_impl->dataset->GetRasterBand(b);
        band->SetNoDataValue(noDataValue);
    }

    return true;
}

void GDALRaster::Close() {
    if (m_impl && m_impl->dataset) {
        GDALClose(m_impl->dataset);
        m_impl->dataset = nullptr;
    }
    m_info = {};
}

bool GDALRaster::IsOpen() const {
    return (m_impl && m_impl->dataset != nullptr);
}

std::optional<double> GDALRaster::GetElevation(double x, double y, SampleMethod method, int bandIdx) const {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands) {
        return std::nullopt;
    }

    double colDouble = m_impl->invGeotransform[0] + (x * m_impl->invGeotransform[1]) + (y * m_impl->invGeotransform[2]);
    double rowDouble = m_impl->invGeotransform[3] + (x * m_impl->invGeotransform[4]) + (y * m_impl->invGeotransform[5]);

    if (method == SampleMethod::Nearest) {
        int col = static_cast<int>(std::floor(colDouble));
        int row = static_cast<int>(std::floor(rowDouble));
        return GetCellValue(col, row, bandIdx);
    }

    int col0 = static_cast<int>(std::floor(colDouble - 0.5));
    int row0 = static_cast<int>(std::floor(rowDouble - 0.5));
    int col1 = col0 + 1;
    int row1 = row0 + 1;

    auto v00 = GetCellValue(col0, row0, bandIdx);
    auto v10 = GetCellValue(col1, row0, bandIdx);
    auto v01 = GetCellValue(col0, row1, bandIdx);
    auto v11 = GetCellValue(col1, row1, bandIdx);

    if (!v00 || !v10 || !v01 || !v11) {
        return GetCellValue(static_cast<int>(std::round(colDouble)), static_cast<int>(std::round(rowDouble)), bandIdx);
    }

    double tx = colDouble - (col0 + 0.5);
    double ty = rowDouble - (row0 + 0.5);

    double top = (1.0 - tx) * (*v00) + tx * (*v10);
    double bottom = (1.0 - tx) * (*v01) + tx * (*v11);
    return (1.0 - ty) * top + ty * bottom;
}

std::optional<double> GDALRaster::GetCellValue(int col, int row, int bandIdx) const {
    if (!IsOpen() || col < 0 || col >= m_info.width || row < 0 || row >= m_info.height) {
        return std::nullopt;
    }

    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    float val = 0.0f;
    if (band->RasterIO(GF_Read, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0) != CE_None) {
        return std::nullopt;
    }

    if (m_info.hasNoData && std::abs(val - m_info.noDataValue) < 1e-5) {
        return std::nullopt;
    }

    return static_cast<double>(val);
}

bool GDALRaster::SetCellValue(int col, int row, double value, int bandIdx) {
    if (!IsOpen() || col < 0 || col >= m_info.width || row < 0 || row >= m_info.height) {
        return false;
    }

    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    float val = static_cast<float>(value);
    return (band->RasterIO(GF_Write, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0) == CE_None);
}

bool GDALRaster::SetBandDescription(int bandIdx, const std::string& description) {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands) {
        return false;
    }

    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    band->SetDescription(description.c_str());
    return true;
}

std::string GDALRaster::GetBandDescription(int bandIdx) const {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands) {
        return "";
    }

    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    const char* desc = band->GetDescription();
    return desc ? std::string(desc) : "";
}

bool GDALRaster::ReadBandData(int bandIdx, std::vector<float>& buffer) const {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands) {
        return false;
    }

    buffer.resize(static_cast<size_t>(m_info.width) * m_info.height);
    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    return (band->RasterIO(GF_Read, 0, 0, m_info.width, m_info.height, buffer.data(), m_info.width, m_info.height, GDT_Float32, 0, 0) == CE_None);
}

bool GDALRaster::WriteBandData(int bandIdx, const std::vector<float>& buffer) {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands || buffer.size() != static_cast<size_t>(m_info.width) * m_info.height) {
        return false;
    }

    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    return (band->RasterIO(GF_Write, 0, 0, m_info.width, m_info.height, const_cast<float*>(buffer.data()), m_info.width, m_info.height, GDT_Float32, 0, 0) == CE_None);
}

bool GDALRaster::BuildVRT(const std::filesystem::path& outputVRTPath,
                           const std::vector<std::filesystem::path>& inputRasterPaths,
                           bool relativePaths) {
    if (inputRasterPaths.empty()) return false;

    std::vector<char*> inputFiles;
    std::vector<std::string> pathStrings;
    pathStrings.reserve(inputRasterPaths.size());

    for (const auto& path : inputRasterPaths) {
        pathStrings.push_back(path.string());
        inputFiles.push_back(const_cast<char*>(pathStrings.back().c_str()));
    }

    GDALBuildVRTOptions* options = GDALBuildVRTOptionsNew(nullptr, nullptr);
    int error = 0;
    GDALDatasetH vrtDataset = GDALBuildVRT(outputVRTPath.string().c_str(),
                                           static_cast<int>(inputFiles.size()),
                                           nullptr,
                                           inputFiles.data(),
                                           options,
                                           &error);
    GDALBuildVRTOptionsFree(options);

    if (vrtDataset) {
        GDALClose(vrtDataset);
        return true;
    }
    return false;
}

bool GDALRaster::MergeVRTToGeoTIFF(const std::filesystem::path& vrtPath,
                                    const std::filesystem::path& outputGeoTIFFPath,
                                    const std::string& compressOption) {
    GDALDataset* vrtDS = static_cast<GDALDataset*>(GDALOpen(vrtPath.string().c_str(), GA_ReadOnly));
    if (!vrtDS) return false;

    char* argCompress = const_cast<char*>("-co");
    std::string compStr = "COMPRESS=" + compressOption;
    char* argCompVal = const_cast<char*>(compStr.c_str());
    char* argv[] = { argCompress, argCompVal, nullptr };

    GDALTranslateOptions* options = GDALTranslateOptionsNew(argv, nullptr);
    int error = 0;
    GDALDatasetH outDS = GDALTranslate(outputGeoTIFFPath.string().c_str(),
                                       vrtDS,
                                       options,
                                       &error);
    GDALTranslateOptionsFree(options);
    GDALClose(vrtDS);

    if (outDS) {
        GDALClose(outDS);
        return true;
    }
    return false;
}

bool GDALRaster::ConvertToCOG(const std::filesystem::path& inputRasterPath,
                              const std::filesystem::path& outputCOGPath,
                              const std::string& compressOption) {
    GDALDataset* inDS = static_cast<GDALDataset*>(GDALOpen(inputRasterPath.string().c_str(), GA_ReadOnly));
    if (!inDS) return false;

    std::string compStr = "COMPRESS=" + (compressOption.empty() ? std::string("DEFLATE") : compressOption);
    char* argv[] = {
        const_cast<char*>("-of"), const_cast<char*>("COG"),
        const_cast<char*>("-co"), const_cast<char*>(compStr.c_str()),
        nullptr
    };

    GDALTranslateOptions* options = GDALTranslateOptionsNew(argv, nullptr);
    int error = 0;
    GDALDatasetH outDS = GDALTranslate(outputCOGPath.string().c_str(),
                                       inDS,
                                       options,
                                       &error);
    GDALTranslateOptionsFree(options);
    GDALClose(inDS);

    if (outDS) {
        GDALClose(outDS);
        return true;
    }
    return false;
}

bool GDALRaster::CropToExtent(const std::filesystem::path& inputRasterPath,
                               const std::filesystem::path& outputRasterPath,
                               double minX, double minY, double maxX, double maxY) {
    GDALDataset* inDS = static_cast<GDALDataset*>(GDALOpen(inputRasterPath.string().c_str(), GA_ReadOnly));
    if (!inDS) return false;

    // -projwin takes the upper-left and lower-right corners: (minX, maxY) and (maxX, minY).
    std::string ulx = std::to_string(minX);
    std::string uly = std::to_string(maxY);
    std::string lrx = std::to_string(maxX);
    std::string lry = std::to_string(minY);

    char* argv[] = {
        const_cast<char*>("-projwin"),
        const_cast<char*>(ulx.c_str()), const_cast<char*>(uly.c_str()),
        const_cast<char*>(lrx.c_str()), const_cast<char*>(lry.c_str()),
        nullptr
    };

    GDALTranslateOptions* options = GDALTranslateOptionsNew(argv, nullptr);
    int error = 0;
    GDALDatasetH outDS = GDALTranslate(outputRasterPath.string().c_str(),
                                       inDS,
                                       options,
                                       &error);
    GDALTranslateOptionsFree(options);
    GDALClose(inDS);

    if (outDS) {
        GDALClose(outDS);
        return true;
    }
    return false;
}

} // namespace fusion::raster
