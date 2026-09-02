<#
Builds all FUSION Update tools, bundles the resulting .exe files into a
versioned zip, and (optionally) publishes that zip as a GitHub Release
asset.

Usage:
  .\build.ps1                 # configure + build + bundle only (local)
  .\build.ps1 -Publish         # also publish the bundle to GitHub Releases
                                 and delete the previous release
  .\build.ps1 -VcpkgRoot "C:\vcpkg"   # pass a vcpkg toolchain file

Layout this script maintains (see .gitignore -- none of these are tracked):
  build/    -- CMake configure + compile output
  bin/      -- flat copy of the 13 built .exe files
  dist/     -- the current versioned zip bundle only
  archive/  -- previous zip bundles, moved here when a new one is made
#>

param(
    [switch]$Publish
  , [string]$VcpkgRoot = $env:VCPKG_ROOT
  , [string]$BuildDir = "build"
  , [switch]$Reconfigure
  )

$ErrorActionPreference = "Stop"
$repo_root = $PSScriptRoot
Set-Location $repo_root

$tool_names = @(
    "ltktools", "gridmetrics", "clipdata", "groundfilter", "canopymodel"
  , "catalog", "canopymaxima", "treeseg", "cloudmetrics", "topometrics"
  , "filterdata", "thindata", "returndensity"
  )

#1. read the project version out of CMakeLists.txt (single source of truth)
$cmake_text = Get-Content (Join-Path $repo_root "CMakeLists.txt") -Raw
$version_match = [regex]::Match($cmake_text, 'project\([^\)]*VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $version_match.Success) {
    throw "Could not find a VERSION in CMakeLists.txt's project() call."
}
$project_version = $version_match.Groups[1].Value
$build_stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$bundle_version = "$project_version-$build_stamp"
Write-Host "Building FUSION Update tools v$bundle_version"

#2. configure the CMake build directory (only if missing, or -Reconfigure passed)
$cache_path = Join-Path $BuildDir "CMakeCache.txt"
if ($Reconfigure -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir -Confirm:$false
}
if (-not (Test-Path $cache_path)) {
    $configure_args = @("-S", ".", "-B", $BuildDir)
    if ($VcpkgRoot) {
        $toolchain_file = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
        $configure_args += "-DCMAKE_TOOLCHAIN_FILE=$toolchain_file"
    }
    Write-Host "Configuring CMake in '$BuildDir'..."
    & cmake @configure_args
}

#3. compile all 13 tool executables in Release mode
Write-Host "Compiling (Release)..."
& cmake --build $BuildDir --config Release --parallel

#4. collect the built exes into a flat bin/ folder, regardless of whether
#   the generator is single-config (exe lands in build/) or multi-config
#   (exe lands in build/Release/)
if (-not (Test-Path "bin")) {
    New-Item -ItemType Directory -Path "bin" | Out-Null
}
foreach ($tool_name in $tool_names) {
    $found_exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "$tool_name.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found_exe) {
        throw "Build did not produce $tool_name.exe -- check the compile output above."
    }
    Copy-Item $found_exe.FullName (Join-Path "bin" "$tool_name.exe") -Force
}
Write-Host "Copied $($tool_names.Count) executables into bin/."

#5. archive the previous bundle (if any) before making a new one
if (-not (Test-Path "dist")) {
    New-Item -ItemType Directory -Path "dist" | Out-Null
}
if (-not (Test-Path "archive")) {
    New-Item -ItemType Directory -Path "archive" | Out-Null
}
$previous_bundles = Get-ChildItem -Path "dist" -Filter "*.zip" -ErrorAction SilentlyContinue
foreach ($previous_bundle in $previous_bundles) {
    Move-Item $previous_bundle.FullName (Join-Path "archive" $previous_bundle.Name) -Force
    Write-Host "Archived previous bundle: archive/$($previous_bundle.Name)"
}

#6. zip the freshly built exes into the new versioned bundle
$bundle_name = "fusion_update_tools_v$bundle_version.zip"
$bundle_path = Join-Path "dist" $bundle_name
$exe_paths = $tool_names | ForEach-Object { Join-Path "bin" "$_.exe" }
Compress-Archive -Path $exe_paths -DestinationPath $bundle_path -Force
Write-Host "Created bundle: dist/$bundle_name"

#7. publish to GitHub Releases, replacing the previous release, if -Publish was passed
if ($Publish) {
    Write-Host "Publishing dist/$bundle_name to GitHub Releases..."
    $release_tag = "tools-v$bundle_version"
    $existing_releases = & gh release list --limit 100 2>$null |
        ForEach-Object { ($_ -split "`t")[0] } |
        Where-Object { $_ -like "tools-v*" }
    foreach ($existing_tag in $existing_releases) {
        Write-Host "Deleting previous release: $existing_tag"
        & gh release delete $existing_tag --yes --cleanup-tag
    }
    & gh release create $release_tag $bundle_path `
        --title "FUSION Update Tools v$bundle_version" `
        --notes "Automated build bundle of all 13 CLI executables (gridmetrics, clipdata, groundfilter, canopymodel, catalog, canopymaxima, treeseg, cloudmetrics, topometrics, filterdata, thindata, returndensity, ltktools)."
    Write-Host "Published release: $release_tag"
}

Write-Host "Done."
