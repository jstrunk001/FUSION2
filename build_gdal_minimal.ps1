<#
Builds a lightweight, minimal static build of GDAL tailored specifically for
FUSION Update tools (GeoTIFF, Cloud Optimized GeoTIFF / COG, and VRT only).

Produces a static libgdal.a and associated CMake configuration in deps/gdal_minimal/
with zero external DLL dependencies.

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
$source_dir = Join-Path $deps_root "gdal-$GdalVersion"
$build_dir = Join-Path $deps_root "gdal_build"

# Check if already installed
$installed_cmake = Join-Path $InstallDir "lib\cmake\gdal\GDALConfig.cmake"
$installed_cmake64 = Join-Path $InstallDir "lib64\cmake\gdal\GDALConfig.cmake"
if (-not $Clean -and ((Test-Path $installed_cmake) -or (Test-Path $installed_cmake64))) {
    Write-Host "Minimal static GDAL is already installed at: $InstallDir"
    Write-Host "Pass -Clean to force a rebuild."
    exit 0
}

if ($Clean) {
    Write-Host "Cleaning existing GDAL build/install..."
    if (Test-Path $build_dir) { Remove-Item -Recurse -Force $build_dir }
    if (Test-Path $InstallDir) { Remove-Item -Recurse -Force $InstallDir }
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

# 3. Extract source archive
if (-not (Test-Path $source_dir)) {
    Write-Host "Extracting GDAL v$GdalVersion source..."
    $tar_bin = Join-Path $env:SystemRoot "System32\tar.exe"
    if (-not (Test-Path $tar_bin)) { $tar_bin = "tar.exe" }
    & $tar_bin -xzf $source_tar -C $deps_root
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
  , "-DGDAL_USE_CURL=OFF"
  , "-DGDAL_USE_EXPAT=OFF"
  , "-DGDAL_USE_GEOS=OFF"
  , "-DGDAL_USE_ICONV=OFF"
  , "-DGDAL_USE_OPENSSL=OFF"
  , "-DGDAL_USE_HDF4=OFF"
  , "-DGDAL_USE_HDF5=OFF"
  , "-DGDAL_USE_NETCDF=OFF"
  , "-DGDAL_USE_POPPLER=OFF"
  , "-DGDAL_USE_POSTGRESQL=OFF"
  , "-DGDAL_USE_MYSQL=OFF"
  , "-DGDAL_USE_ODBC=OFF"
  , "-DGDAL_USE_ARROW=OFF"
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

Write-Host "Successfully built and installed minimal static GDAL at $InstallDir"
