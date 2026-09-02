<#
Builds a minimal static build of LASzip (LAZ compression/decompression)
tailored for FUSION Update tools.

Produces a static liblaszip3.a / liblaszip_api3.a plus the laszip_api.h C API
header in deps/laszip_minimal/, with zero external DLL dependencies. Mirrors
build_gdal_minimal.ps1's download/build/install pattern.

Builds in a local temp directory ($env:TEMP\fusion_laszip) to avoid
sync/locking issues caused by cloud sync drives (e.g. Box Sync / OneDrive),
then installs the compact output into deps/laszip_minimal/.

Usage:
  .\build_laszip_minimal.ps1             # download + build + install minimal LASzip
  .\build_laszip_minimal.ps1 -Clean      # wipe existing build/install and re-build
#>

param(
    [string]$LaszipVersion = "3.5.0"
  , [string]$InstallDir = ""
  , [switch]$Clean
)

$ErrorActionPreference = "Stop"
$script_root = $PSScriptRoot
Set-Location $script_root

if (-not $InstallDir) {
    $InstallDir = Join-Path $script_root "deps\laszip_minimal"
}
$deps_root = Join-Path $script_root "deps"
$source_tar = Join-Path $deps_root "laszip-$LaszipVersion.tar.gz"

# Build in local temp directory to avoid cloud drive (Box Sync) locking and speed up compilation
$local_temp = Join-Path $env:TEMP "fusion_laszip"
$source_dir = Join-Path $local_temp "LASzip-$LaszipVersion"
$build_dir  = Join-Path $local_temp "build"

# Check if already installed
$installed_header = Join-Path $InstallDir "include\laszip\laszip_api.h"
if (-not $Clean -and (Test-Path $installed_header)) {
    Write-Host "Minimal static LASzip is already installed at: $InstallDir"
    Write-Host "Pass -Clean to force a rebuild."
    exit 0
}

if ($Clean) {
    Write-Host "Cleaning existing LASzip build/install..."
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

# 2. Download LASzip source archive if needed
if (-not (Test-Path $source_tar)) {
    $download_url = "https://github.com/LASzip/LASzip/archive/refs/tags/$LaszipVersion.tar.gz"
    Write-Host "Downloading LASzip v$LaszipVersion source from $download_url..."
    & curl.exe -L -f -o $source_tar $download_url
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to download LASzip source archive."
    }
    Write-Host "Downloaded LASzip source ($([math]::Round((Get-Item $source_tar).Length / 1MB, 2)) MB)."
}

# 3. Extract source archive into local temp directory
if (-not (Test-Path $source_dir)) {
    Write-Host "Extracting LASzip v$LaszipVersion source into local temp directory..."
    $tar_bin = Join-Path $env:SystemRoot "System32\tar.exe"
    if (-not (Test-Path $tar_bin)) { $tar_bin = "tar.exe" }
    & $tar_bin -xzf $source_tar -C $local_temp
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $source_dir)) {
        throw "Failed to extract LASzip archive."
    }
}

# 4. Configure minimal static LASzip (static libs, no tests/examples, zero external deps)
Write-Host "Configuring minimal static LASzip..."
$configure_args = @(
    "-S", $source_dir
  , "-B", $build_dir
  , "-G", "Unix Makefiles"
  , "-DCMAKE_BUILD_TYPE=Release"
  , "-DCMAKE_INSTALL_PREFIX=$InstallDir"
  , "-DLASZIP_BUILD_STATIC=ON"
    # LASzip 3.5.0's laszip_dll.cpp uses uint32_t without including <cstdint>,
    # which newer GCC (13+) no longer pulls in transitively -- force-include
    # it for every translation unit rather than patching the vendored source.
  , "-DCMAKE_CXX_FLAGS=-include cstdint"
)

& cmake @configure_args
if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration for minimal LASzip failed."
}

# 5. Compile
Write-Host "Compiling minimal static LASzip in parallel..."
& cmake --build $build_dir --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Compilation of minimal LASzip failed."
}

# 6. Install to prefix
Write-Host "Installing minimal LASzip to $InstallDir..."
& cmake --install $build_dir
if ($LASTEXITCODE -ne 0) {
    throw "Installation of minimal LASzip failed."
}

# 7. Clean up local temp directory to save disk space
Write-Host "Cleaning up local temp build directory..."
Remove-Item -Recurse -Force $local_temp -ErrorAction SilentlyContinue

Write-Host "Successfully built and installed minimal static LASzip at $InstallDir"
