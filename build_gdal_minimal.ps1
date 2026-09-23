<#
Builds a lightweight, minimal static build of GDAL tailored specifically for
FUSION Update tools (GeoTIFF, Cloud Optimized GeoTIFF / COG, and VRT only).

Produces a static libgdal.a and associated CMake configuration in deps/gdal_minimal/
with zero external DLL dependencies.

Builds in a local temp directory ($env:TEMP\fusion_gdal) to avoid sync/locking
issues caused by cloud sync drives (e.g. Box Sync / OneDrive), then installs
the compact output into deps/gdal_minimal/.

Usage:
  .\build_gdal_minimal.ps1             # download + build + install minimal GDAL
  .\build_gdal_minimal.ps1 -Clean      # wipe existing build/install and re-build
#>

param(
    [string]$GdalVersion = "3.12.1"
  , [string]$InstallDir = ""
  , [switch]$Clean
)

$ErrorActionPreference = "Stop"
$script_root = $PSScriptRoot
Set-Location $script_root

if (-not $InstallDir) {
    $InstallDir = Join-Path $script_root "deps\gdal_minimal"
}
$deps_root = Join-Path $script_root "deps"
$source_tar = Join-Path $deps_root "gdal-$GdalVersion.tar.gz"

# Vendors PROJ's own proj.db (its EPSG/CRS lookup database) alongside the
# minimal GDAL install. GDAL links PROJ from this machine's Rtools install
# (see PROJ_DIR in build/CMakeCache.txt after configuring), but Rtools'
# static PROJ build ships no proj.db of its own -- every tool run was
# searching for one, failing to find it, and printing a "Cannot find
# proj.db" warning on every invocation (see GDALRaster.cpp's
# EnsureRegistered(), which points PROJ_DATA at this file at runtime).
# Measured wall-clock impact of the fix was within noise -- this closes a
# real failed-search/warning-spam gap, not a performance one. Copied from
# the same Rtools install GDAL's PROJ comes from, so the database schema
# always matches the linked PROJ version -- called both when GDAL is
# freshly built below and when this script exits early because GDAL is
# already installed, so an existing install can still pick up a missing
# proj.db without a full -Clean rebuild.
function Install-ProjDb {
    param([string]$GdalInstallDir)
    $proj_data_dir = Join-Path $GdalInstallDir "share\proj"
    if (Test-Path (Join-Path $proj_data_dir "proj.db")) {
        return
    }
    $rtools_proj_db = Get-ChildItem "C:\rtools*" -Recurse -Filter "proj.db" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($rtools_proj_db) {
        if (-not (Test-Path $proj_data_dir)) {
            New-Item -ItemType Directory -Path $proj_data_dir | Out-Null
        }
        Copy-Item $rtools_proj_db.FullName (Join-Path $proj_data_dir "proj.db") -Force
        Write-Host "Vendored proj.db from $($rtools_proj_db.FullName) into $proj_data_dir"
    } else {
        Write-Warning "No proj.db found under C:\rtools* -- built tools will fall back to PROJ's default (slower, warning-emitting) search at runtime."
    }
}

# Build in local temp directory to avoid cloud drive (Box Sync) locking and speed up compilation
$local_temp = Join-Path $env:TEMP "fusion_gdal"
$source_dir = Join-Path $local_temp "gdal-$GdalVersion"
$build_dir  = Join-Path $local_temp "build"

# Check if already installed
$installed_cmake = Join-Path $InstallDir "lib\cmake\gdal\GDALConfig.cmake"
$installed_cmake64 = Join-Path $InstallDir "lib64\cmake\gdal\GDALConfig.cmake"
if (-not $Clean -and ((Test-Path $installed_cmake) -or (Test-Path $installed_cmake64))) {
    Write-Host "Minimal static GDAL is already installed at: $InstallDir"
    Write-Host "Pass -Clean to force a rebuild."
    Install-ProjDb -GdalInstallDir $InstallDir
    exit 0
}

if ($Clean) {
    Write-Host "Cleaning existing GDAL build/install..."
    if (Test-Path $local_temp) { Remove-Item -Recurse -Force $local_temp -ErrorAction SilentlyContinue }
    if (Test-Path $InstallDir) { Remove-Item -Recurse -Force $InstallDir -ErrorAction SilentlyContinue }
}

# 1. Ensure Rtools toolchain (gcc, cmake, make) is on PATH
$have_cmake = Get-Command cmake -ErrorAction SilentlyContinue
$have_make = Get-Command make -ErrorAction SilentlyContinue
if (-not $have_cmake -or -not $have_make) {
    $rtools_root = Get-ChildItem "C:\" -Directory -Filter "rtools*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $rtools_root) {
        throw "cmake/make not found on PATH, and no Rtools install found under C:\."
    }
    $mingw_bin = Join-Path $rtools_root.FullName "x86_64-w64-mingw32.static.posix\bin"
    $usr_bin = Join-Path $rtools_root.FullName "usr\bin"
    $env:Path = "$mingw_bin;$usr_bin;$env:Path"
    Write-Host "Using Rtools toolchain at $($rtools_root.FullName)"
}

if (-not (Test-Path $deps_root)) {
    New-Item -ItemType Directory -Path $deps_root | Out-Null
}
if (-not (Test-Path $local_temp)) {
    New-Item -ItemType Directory -Path $local_temp | Out-Null
}

# 2. Download GDAL source archive if needed
if (-not (Test-Path $source_tar)) {
    $download_url = "https://github.com/OSGeo/gdal/releases/download/v$GdalVersion/gdal-$GdalVersion.tar.gz"
    Write-Host "Downloading GDAL v$GdalVersion source from $download_url..."
    & curl.exe -L -f -o $source_tar $download_url
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download GDAL source archive."
    }
    Write-Host "Downloaded GDAL source ($([math]::Round((Get-Item $source_tar).Length / 1MB, 2)) MB)."
}

# 3. Extract source archive into local temp directory
if (-not (Test-Path $source_dir)) {
    Write-Host "Extracting GDAL v$GdalVersion source into local temp directory..."
    $tar_bin = Join-Path $env:SystemRoot "System32\tar.exe"
    if (-not (Test-Path $tar_bin)) { $tar_bin = "tar.exe" }
    & $tar_bin -xzf $source_tar -C $local_temp
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $source_dir)) {
        throw "Failed to extract GDAL archive."
    }
}

# 4. Configure Minimal Static GDAL
Write-Host "Configuring minimal static GDAL..."
$configure_args = @(
    "-S", $source_dir
  , "-B", $build_dir
  , "-G", "Unix Makefiles"
  , "-DCMAKE_BUILD_TYPE=Release"
  , "-DCMAKE_INSTALL_PREFIX=$InstallDir"
  , "-DBUILD_SHARED_LIBS=OFF"
  , "-DBUILD_APPS=OFF"
  , "-DBUILD_TESTING=OFF"
  , "-DGDAL_BUILD_OPTIONAL_DRIVERS=OFF"
  , "-DOGR_BUILD_OPTIONAL_DRIVERS=OFF"
  , "-DGDAL_ENABLE_DRIVER_GTIFF=ON"
  , "-DGDAL_ENABLE_DRIVER_COG=ON"
  , "-DGDAL_ENABLE_DRIVER_VRT=ON"
  , "-DGDAL_USE_TIFF_INTERNAL=ON"
  , "-DGDAL_USE_GEOTIFF_INTERNAL=ON"
  , "-DGDAL_USE_ZLIB_INTERNAL=ON"
  , "-DGDAL_USE_LERC_INTERNAL=ON"
  , "-DGDAL_USE_SQLITE3=ON"
  , "-DGDAL_USE_ARMADILLO=OFF"
  , "-DGDAL_USE_ARROW=OFF"
  , "-DGDAL_USE_AVIF=OFF"
  , "-DGDAL_USE_BLOSC=OFF"
  , "-DGDAL_USE_BRUNSLI=OFF"
  , "-DGDAL_USE_CFITSIO=OFF"
  , "-DGDAL_USE_CRNLIB=OFF"
  , "-DGDAL_USE_CRYPTOPP=OFF"
  , "-DGDAL_USE_CURL=OFF"
  , "-DGDAL_USE_DEFLATE=OFF"
  , "-DGDAL_USE_ECW=OFF"
  , "-DGDAL_USE_EXPAT=OFF"
  , "-DGDAL_USE_EXPRTK=OFF"
  , "-DGDAL_USE_FREEXL=OFF"
  , "-DGDAL_USE_FYBA=OFF"
  , "-DGDAL_USE_GEOS=OFF"
  , "-DGDAL_USE_GIF=OFF"
  , "-DGDAL_USE_GTA=OFF"
  , "-DGDAL_USE_HDF4=OFF"
  , "-DGDAL_USE_HDF5=OFF"
  # This Rtools 4.3 install's own hdf5-targets.cmake references a
  # mirror_server.exe that isn't actually present (a corrupt/incomplete
  # HDF5 package in the toolchain, unrelated to GDAL) -- GDAL_USE_HDF5=OFF
  # above doesn't stop CheckDependentLibraries.cmake from still probing for
  # it, so explicitly disable the find_package(HDF5) call itself.
  , "-DCMAKE_DISABLE_FIND_PACKAGE_HDF5=ON"
  , "-DGDAL_USE_HEIF=OFF"
  , "-DGDAL_USE_HDFS=OFF"
  , "-DGDAL_USE_ICONV=OFF"
  , "-DGDAL_USE_IDB=OFF"
  , "-DGDAL_USE_ILMBASE=OFF"
  , "-DGDAL_USE_JBIG=OFF"
  , "-DGDAL_USE_JXL=OFF"
  , "-DGDAL_USE_KDU=OFF"
  , "-DGDAL_USE_KEA=OFF"
  , "-DGDAL_USE_LIBKML=OFF"
  , "-DGDAL_USE_LIBLZMA=OFF"
  , "-DGDAL_USE_LIBXML2=OFF"
  , "-DGDAL_USE_LURATECH=OFF"
  , "-DGDAL_USE_LZ4=OFF"
  , "-DGDAL_USE_MONGOCXX=OFF"
  , "-DGDAL_USE_MRSID=OFF"
  , "-DGDAL_USE_MSSQL_NCLI=OFF"
  , "-DGDAL_USE_MSSQL_ODBC=OFF"
  , "-DGDAL_USE_MUPARSER=OFF"
  , "-DGDAL_USE_MYSQL=OFF"
  , "-DGDAL_USE_NETCDF=OFF"
  , "-DGDAL_USE_ODBC=OFF"
  , "-DGDAL_USE_OGDI=OFF"
  , "-DGDAL_USE_OPENCV=OFF"
  , "-DGDAL_USE_OPENEXR=OFF"
  , "-DGDAL_USE_OPENJPEG=OFF"
  , "-DGDAL_USE_OPENSSL=OFF"
  , "-DGDAL_USE_PARQUET=OFF"
  , "-DGDAL_USE_PCRE2=OFF"
  , "-DGDAL_USE_PDFIUM=OFF"
  , "-DGDAL_USE_PNG=OFF"
  , "-DGDAL_USE_POPPLER=OFF"
  , "-DGDAL_USE_POSTGRESQL=OFF"
  , "-DGDAL_USE_QHULL=OFF"
  , "-DGDAL_USE_RASTERLITE2=OFF"
  , "-DGDAL_USE_RDB=OFF"
  , "-DGDAL_USE_SPATIALITE=OFF"
  , "-DGDAL_USE_TEIGHA=OFF"
  , "-DGDAL_USE_TILEDB=OFF"
  , "-DGDAL_USE_WEBP=OFF"
  , "-DGDAL_USE_XERCESC=OFF"
  , "-DGDAL_USE_ZSTD=OFF"
  , "-DGDAL_BUILD_PYTHON_BINDINGS=OFF"
)

& cmake @configure_args
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration for minimal GDAL failed."
}

# 5. Compile
Write-Host "Compiling minimal static GDAL in parallel..."
& cmake --build $build_dir --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Compilation of minimal GDAL failed."
}

# 6. Install to prefix
Write-Host "Installing minimal GDAL to $InstallDir..."
& cmake --install $build_dir
if ($LASTEXITCODE -ne 0) {
    throw "Installation of minimal GDAL failed."
}

# 7. Vendor proj.db alongside the freshly built GDAL install (see
# Install-ProjDb's definition above for why).
Install-ProjDb -GdalInstallDir $InstallDir

# 8. Clean up local temp directory to save disk space
Write-Host "Cleaning up local temp build directory..."
Remove-Item -Recurse -Force $local_temp -ErrorAction SilentlyContinue

Write-Host "Successfully built and installed minimal static GDAL at $InstallDir"
