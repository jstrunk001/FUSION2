#include "fusion/raster/GDALRaster.h"

#include <gdal_priv.h>
#include <cpl_conv.h>
#include <cpl_vsi.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <atomic>
#include <filesystem>
#include <mutex>

namespace fusion::raster {

double PixelCentreX(const RasterInfo& info, int col) {
    return info.originX + (col + 0.5) * info.pixelWidth;
}

double PixelCentreY(const RasterInfo& info, int row) {
    return info.originY - (row + 0.5) * info.pixelHeight;
}

// Points PROJ at the proj.db vendored beside this executable (see
// CMakeLists.txt's configure-time copy and build_gdal_minimal.ps1's
// Install-ProjDb) instead of letting PROJ search its own default locations
// for one. This project's statically-linked PROJ ships no proj.db of its
// own, so every unconfigured run was searching for one, failing to find it,
// and printing a "Cannot find proj.db" warning on every invocation.
// Measured contribution to wall-clock time was within noise (see
// speed_benchmark.qmd's commit history) -- the fix is worth keeping for
// eliminating the failed search and the warning, not as a performance claim
// on its own. A missing proj_data/ folder is not an error: PROJ just falls
// back to its own (slower) default search, same as before this existed.
static void ConfigureProjData() {
    char execPathBuf[2048];
    if (!CPLGetExecPath(execPathBuf, sizeof(execPathBuf))) return;

    std::filesystem::path projDataDir = std::filesystem::path(execPathBuf).parent_path() / "proj_data";
    if (!std::filesystem::exists(projDataDir / "proj.db")) return;

    std::string projDataDirStr = projDataDir.string();
    CPLSetConfigOption("PROJ_DATA", projDataDirStr.c_str()); // PROJ >= 9.1
    CPLSetConfigOption("PROJ_LIB", projDataDirStr.c_str());  // PROJ < 9.1, harmless if unused
}

// GDALAllRegister() must run before any GDAL entry point is used, but
// several GDALRaster methods (BuildVRT, MergeVRTToGeoTIFF, ConvertToCOG,
// CropToExtent) are static and callable without ever constructing a
// GDALRaster instance -- e.g. pipeline.cpp's orchestrator process, which
// only ever calls these static helpers directly. Route every entry point
// through this so registration happens exactly once regardless of whether
// an instance was ever constructed in the calling process.
static void EnsureRegistered() {
    static std::once_flag registeredOnce;
    std::call_once(registeredOnce, []() {
        ConfigureProjData();
        GDALAllRegister();
    });
}

// Recognized single-tile raster file extensions when a directory is passed
// to Open() in place of one file -- e.g. a folder of DTM tiles covering a
// project area rather than one already-mosaicked ground surface raster.
static const std::vector<std::string>& RasterTileExtensions() {
    static const std::vector<std::string> extensions = {".tif", ".tiff", ".img", ".asc", ".bil", ".dtm"};
    return extensions;
}

// Every raster file directly inside dirPath (non-recursive) whose extension
// matches RasterTileExtensions(), sorted for a deterministic mosaic.
static std::vector<std::filesystem::path> FindRasterTiles(const std::filesystem::path& dirPath) {
    std::vector<std::filesystem::path> tiles;
    for (const auto& entry : std::filesystem::directory_iterator(dirPath)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        const auto& known = RasterTileExtensions();
        if (std::find(known.begin(), known.end(), ext) != known.end()) {
            // Absolute, since the mosaic VRT is written to /vsimem/ rather
            // than alongside the tiles -- a relative source path would fail
            // to resolve against that virtual location.
            tiles.push_back(std::filesystem::absolute(entry.path()));
        }
    }
    std::sort(tiles.begin(), tiles.end());
    return tiles;
}

class GDALRaster::Impl {
public:
    GDALDataset* dataset{nullptr};
    std::vector<double> geotransform{0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    std::vector<double> invGeotransform{0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    bool isReadWrite{false};

    // In-memory per-band pixel cache, indexed 0-based (bandCache[b-1] holds
    // band b). Sized to numBands whenever a dataset is opened/created, but
    // each entry starts empty and is only populated (via one bulk RasterIO
    // read) the first time that band is actually sampled -- see
    // GDALRaster::EnsureBandCached(). This avoids one RasterIO call per
    // point per corner during height normalization, which otherwise turns
    // into tens of millions of single-cell GDAL calls on a large tile.
    std::vector<std::vector<float>> bandCache;

    ~Impl() {
        if (dataset) {
            GDALClose(dataset);
            dataset = nullptr;
        }
    }
};

GDALRaster::GDALRaster() : m_impl(std::make_unique<Impl>()) {
    EnsureRegistered();
}

GDALRaster::~GDALRaster() = default;

GDALRaster::GDALRaster(GDALRaster&&) noexcept = default;
GDALRaster& GDALRaster::operator=(GDALRaster&&) noexcept = default;

bool GDALRaster::Open(const std::filesystem::path& filePath, bool readWrite) {
    Close();

    // A directory of DTM/raster tiles (e.g. a project's per-tile ground
    // surface models) stands in for one already-mosaicked raster file --
    // mosaic them into a throwaway in-memory VRT and open that instead.
    // Read-only, since a mosaic of several source files has no single file
    // to write updates back into.
    std::string openPath = filePath.string();
    std::string vrtScratchPath;
    if (!readWrite && std::filesystem::is_directory(filePath)) {
        std::vector<std::filesystem::path> tiles = FindRasterTiles(filePath);
        if (tiles.empty()) {
            return false;
        }
        static std::atomic<uint64_t> counter{0};
        vrtScratchPath = "/vsimem/fusion_dtm_mosaic_" + std::to_string(counter.fetch_add(1)) + ".vrt";
        if (!BuildVRT(vrtScratchPath, tiles, /*relativePaths=*/false)) {
            return false;
        }
        openPath = vrtScratchPath;
    }

    GDALAccess access = readWrite ? GA_Update : GA_ReadOnly;
    m_impl->dataset = static_cast<GDALDataset*>(GDALOpen(openPath.c_str(), access));
    if (!vrtScratchPath.empty()) {
        VSIUnlink(vrtScratchPath.c_str());
    }
    if (!m_impl->dataset) {
        return false;
    }

    m_impl->isReadWrite = readWrite;
    m_info.width = m_impl->dataset->GetRasterXSize();
    m_info.height = m_impl->dataset->GetRasterYSize();
    m_info.numBands = m_impl->dataset->GetRasterCount();
    m_impl->bandCache.assign(static_cast<size_t>(std::max(m_info.numBands, 0)), std::vector<float>());

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
    m_impl->bandCache.assign(static_cast<size_t>(std::max(numBands, 0)), std::vector<float>());

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
    if (m_impl) {
        if (m_impl->dataset) {
            GDALClose(m_impl->dataset);
            m_impl->dataset = nullptr;
        }
        m_impl->bandCache.clear();
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

    // Check if within bounds or within 1.0 cell of bounds for edge clamping
    // (the raster spans 0..width columns and 0..height rows, so one cell
    // beyond is -1 on the left/top and width+1 / height+1 on the right/bottom)
    if (colDouble < -1.0 || colDouble > static_cast<double>(m_info.width) + 1.0 ||
        rowDouble < -1.0 || rowDouble > static_cast<double>(m_info.height) + 1.0) {
        return std::nullopt;
    }

    if (method == SampleMethod::Nearest) {
        int col = std::clamp(static_cast<int>(std::floor(colDouble)), 0, m_info.width - 1);
        int row = std::clamp(static_cast<int>(std::floor(rowDouble)), 0, m_info.height - 1);
        return GetCellValue(col, row, bandIdx);
    }

    int col0 = static_cast<int>(std::floor(colDouble - 0.5));
    int row0 = static_cast<int>(std::floor(rowDouble - 0.5));
    int col1 = col0 + 1;
    int row1 = row0 + 1;

    double tx = std::clamp(colDouble - (col0 + 0.5), 0.0, 1.0);
    double ty = std::clamp(rowDouble - (row0 + 0.5), 0.0, 1.0);

    int c0 = std::clamp(col0, 0, m_info.width - 1);
    int c1 = std::clamp(col1, 0, m_info.width - 1);
    int r0 = std::clamp(row0, 0, m_info.height - 1);
    int r1 = std::clamp(row1, 0, m_info.height - 1);

    auto v00 = GetCellValue(c0, r0, bandIdx);
    auto v10 = GetCellValue(c1, r0, bandIdx);
    auto v01 = GetCellValue(c0, r1, bandIdx);
    auto v11 = GetCellValue(c1, r1, bandIdx);

    if (!v00 || !v10 || !v01 || !v11) {
        int col = std::clamp(static_cast<int>(std::round(colDouble)), 0, m_info.width - 1);
        int row = std::clamp(static_cast<int>(std::round(rowDouble)), 0, m_info.height - 1);
        auto nearest = GetCellValue(col, row, bandIdx);
        if (nearest) return nearest;
        if (v00) return v00;
        if (v10) return v10;
        if (v01) return v01;
        if (v11) return v11;
        int cFloor = std::clamp(static_cast<int>(std::floor(colDouble)), 0, m_info.width - 1);
        int rFloor = std::clamp(static_cast<int>(std::floor(rowDouble)), 0, m_info.height - 1);
        return GetCellValue(cFloor, rFloor, bandIdx);
    }

    double top = (1.0 - tx) * (*v00) + tx * (*v10);
    double bottom = (1.0 - tx) * (*v01) + tx * (*v11);
    return (1.0 - ty) * top + ty * bottom;
}

bool GDALRaster::EnsureBandCached(int bandIdx) const {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands) {
        return false;
    }

    std::vector<float>& cache = m_impl->bandCache[static_cast<size_t>(bandIdx - 1)];
    if (!cache.empty()) {
        return true;
    }

    size_t cellCount = static_cast<size_t>(m_info.width) * static_cast<size_t>(m_info.height);
    if (cellCount == 0) {
        return false;
    }

    // Single bulk read for the whole band, instead of one RasterIO call per
    // cell -- populated lazily on first access to this band so a band that's
    // never sampled (e.g. an unused auxiliary band) never pays this cost.
    cache.resize(cellCount);
    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    if (band->RasterIO(GF_Read, 0, 0, m_info.width, m_info.height, cache.data(),
                        m_info.width, m_info.height, GDT_Float32, 0, 0) != CE_None) {
        cache.clear();
        return false;
    }

    return true;
}

std::optional<double> GDALRaster::GetCellValue(int col, int row, int bandIdx) const {
    if (!IsOpen() || col < 0 || col >= m_info.width || row < 0 || row >= m_info.height) {
        return std::nullopt;
    }

    if (!EnsureBandCached(bandIdx)) {
        return std::nullopt;
    }

    size_t idx = static_cast<size_t>(row) * static_cast<size_t>(m_info.width) + static_cast<size_t>(col);
    float val = m_impl->bandCache[static_cast<size_t>(bandIdx - 1)][idx];

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
    if (band->RasterIO(GF_Write, col, row, 1, 1, &val, 1, 1, GDT_Float32, 0, 0) != CE_None) {
        return false;
    }

    // Keep an already-populated cache for this band consistent with the
    // write, so a subsequent GetCellValue()/GetElevation() on this same
    // instance doesn't read back a stale cached value.
    if (bandIdx >= 1 && bandIdx <= m_info.numBands) {
        std::vector<float>& cache = m_impl->bandCache[static_cast<size_t>(bandIdx - 1)];
        if (!cache.empty()) {
            size_t idx = static_cast<size_t>(row) * static_cast<size_t>(m_info.width) + static_cast<size_t>(col);
            cache[idx] = val;
        }
    }

    return true;
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

bool GDALRaster::ReadBandWindow(int bandIdx, int xOffset, int yOffset, int xSize, int ySize, std::vector<float>& buffer) const {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands) {
        return false;
    }
    if (xOffset < 0 || yOffset < 0 || xSize <= 0 || ySize <= 0 ||
        xOffset + xSize > m_info.width || yOffset + ySize > m_info.height) {
        return false;
    }

    buffer.resize(static_cast<size_t>(xSize) * ySize);
    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    return (band->RasterIO(GF_Read, xOffset, yOffset, xSize, ySize, buffer.data(), xSize, ySize, GDT_Float32, 0, 0) == CE_None);
}

bool GDALRaster::WriteBandData(int bandIdx, const std::vector<float>& buffer) {
    if (!IsOpen() || bandIdx < 1 || bandIdx > m_info.numBands || buffer.size() != static_cast<size_t>(m_info.width) * m_info.height) {
        return false;
    }

    GDALRasterBand* band = m_impl->dataset->GetRasterBand(bandIdx);
    bool ok = (band->RasterIO(GF_Write, 0, 0, m_info.width, m_info.height, const_cast<float*>(buffer.data()), m_info.width, m_info.height, GDT_Float32, 0, 0) == CE_None);

    // Invalidate any existing cache for this band rather than trying to keep
    // it in sync -- it will be lazily repopulated from disk on next read.
    if (ok) {
        m_impl->bandCache[static_cast<size_t>(bandIdx - 1)].clear();
    }

    return ok;
}

bool GDALRaster::BuildVRT(const std::filesystem::path& outputVRTPath,
                           const std::vector<std::filesystem::path>& inputRasterPaths,
                           bool relativePaths) {
    EnsureRegistered();
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
    if (!vrtDataset) return false;

    // GDALBuildVRT does not carry band descriptions (names) into the VRT,
    // so readers would see generic band names -- copy them from the first
    // input raster, which every tile shares. They are saved when the VRT
    // is closed below.
    GDALDatasetH firstInput = GDALOpen(pathStrings.front().c_str(), GA_ReadOnly);
    if (firstInput) {
        int numBands = (std::min)(GDALGetRasterCount(firstInput), GDALGetRasterCount(vrtDataset));
        for (int b = 1; b <= numBands; ++b) {
            const char* name = GDALGetDescription(GDALGetRasterBand(firstInput, b));
            if (name && name[0] != '\0') {
                GDALSetDescription(GDALGetRasterBand(vrtDataset, b), name);
            }
        }
        GDALClose(firstInput);
    }

    GDALClose(vrtDataset);
    return true;
}

bool GDALRaster::MergeVRTToGeoTIFF(const std::filesystem::path& vrtPath,
                                    const std::filesystem::path& outputGeoTIFFPath,
                                    const std::string& compressOption) {
    EnsureRegistered();
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
    EnsureRegistered();
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
    EnsureRegistered();
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
