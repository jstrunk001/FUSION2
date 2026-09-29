<#
Builds all FUSION2 tools, bundles the resulting .exe files into a
versioned zip, and (optionally) publishes that zip as a GitHub Release
asset.

Usage:
  .\build.ps1                 # configure + build + bundle only (local)
  .\build.ps1 -Publish         # also publish the bundle to GitHub Releases
                                 and delete the previous release
  .\build.ps1 -VcpkgRoot "C:\vcpkg"   # pass a vcpkg toolchain file
  .\build.ps1 -LocalRoot "G:\FUSION2_build"   # use a different local drive

Layout this script maintains, rooted under $LocalRoot rather than the repo
itself (see -LocalRoot's default below and its comment for why):
  build/    -- CMake configure + compile output
  bin/      -- flat copy of the 13 built .exe files
  dist/     -- the current versioned zip bundle only
  archive/  -- previous zip bundles, moved here when a new one is made
#>

param(
    [switch]$Publish
  , [string]$VcpkgRoot = $env:VCPKG_ROOT
  # Build output was previously rooted directly under the repo, which lives
  # under Box Sync -- real timed comparisons (see tests/R/speed_benchmark.qmd)
  # showed Box Sync's filter driver adds erratic multi-second overhead to
  # launching these ~26MB statically-linked GDAL binaries specifically (a
  # local, non-synced copy of the identical binary was fast and stable; the
  # same file under Box Sync spiked unpredictably), enough to make a
  # small/fast tool run look slower than legacy FUSION even though it is
  # actually faster once that hosting-location overhead is removed.
  # $env:LOCALAPPDATA is per-user, never cloud-synced, and exists on any
  # Windows machine without hardcoding a specific drive letter.
  , [string]$LocalRoot = (Join-Path $env:LOCALAPPDATA "FUSION2_build")
  , [string]$BuildDir = (Join-Path $LocalRoot "build")
  , [switch]$Reconfigure
  , [switch]$FullGDAL
  , [string]$GDALDir = ""
  )

$ErrorActionPreference = "Stop"
$repo_root = $PSScriptRoot
Set-Location $repo_root

$bin_dir = Join-Path $LocalRoot "bin"
$dist_dir = Join-Path $LocalRoot "dist"
$archive_dir = Join-Path $LocalRoot "archive"
if (-not (Test-Path $LocalRoot)) {
    New-Item -ItemType Directory -Path $LocalRoot | Out-Null
}
Write-Host "Build output root (outside Box Sync): $LocalRoot"

# Only one build may use a build root at a time. Two overlapping runs
# share build/, bin/, dist/, and the repo's bin/ mirror: a -Reconfigure run
# deletes build/ while the other compiles in it, both copy executables into
# bin/ (leaving a mix of two builds), and step 6 moves the other run's
# half-written zip into archive/ (an IOException, and an incomplete zip).
# The lock is an exclusive open handle on build.lock, not a marker file:
# Windows closes the handle when this process exits, even on a crash, so
# a lock can never be left behind. A second run waits for the first.
$lock_path = Join-Path $LocalRoot "build.lock"
$build_lock = $null
$lock_deadline = (Get-Date).AddMinutes(30)
while ($null -eq $build_lock) {
    try {
        $build_lock = [System.IO.File]::Open($lock_path, 'OpenOrCreate', 'ReadWrite', 'None')
    } catch [System.IO.IOException] {
        if ((Get-Date) -gt $lock_deadline) {
            throw "Another build.ps1 has held $lock_path for 30 minutes -- giving up. Check for a stuck build process."
        }
        Write-Host "Another build.ps1 is running in $LocalRoot -- waiting for it to finish..."
        Start-Sleep -Seconds 10
    }
}
# release the lock if a later step throws; the normal path releases it at
# the end of the script
trap {
    if ($build_lock) { $build_lock.Dispose() }
    break
}

# a freshly written .exe is sometimes briefly locked by antivirus real-time
# scanning right after the file handle closes, which makes an immediate
# Copy-Item or Compress-Archive read fail with "used by another process" --
# this retries a few times with a short pause instead of failing the build
function Invoke-WithRetry {
    param(
        [scriptblock]$Action
      , [int]$MaxAttempts = 5
      , [int]$DelaySeconds = 2
      )
    for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
        try {
            & $Action
            return
        } catch {
            if ($attempt -eq $MaxAttempts) {
                throw
            }
            Write-Host "  (attempt $attempt failed: $($_.Exception.Message) -- retrying)"
            Start-Sleep -Seconds $DelaySeconds
        }
    }
}

function Get-VersionChangelogNotes {
    param(
        [string]$ChangelogPath
      , [string]$Version
      )
    if (-not (Test-Path $ChangelogPath)) { return $null }
    $lines = Get-Content $ChangelogPath
    $in_version = $false
    $notes = [System.Collections.Generic.List[string]]::new()
    
    foreach ($line in $lines) {
        if ($line -match "^##\s+\[$([regex]::Escape($Version))\]") {
            $in_version = $true
            continue
        } elseif ($in_version -and $line -match "^##\s+\[") {
            break
        }
        if ($in_version) {
            $notes.Add($line)
        }
    }
    
    $result = ($notes -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($result)) {
        return $null
    }
    return $result
}

$tool_names = @(
    "gridmetrics", "clipdata", "groundfilter", "canopymodel"
  , "catalog", "canopymaxima", "treeseg", "cloudmetrics", "topometrics"
  , "filterdata", "thindata", "returndensity", "pipeline"
  , "gridsurfacestats", "densitymetrics"
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
Write-Host "Building FUSION2 tools v$bundle_version"

#1a. when publishing, request a version bump if this version was already
#    released, and remind about the changelog -- checked now, before the
#    (potentially long) compile step, not after
if ($Publish) {
    # (ask gh for tag names directly: the first column of gh's plain-text
    # listing is the release title, not its tag)
    $prior_release_tags = & gh release list --limit 100 --json tagName --jq ".[].tagName" 2>$null |
        Where-Object { $_ -like "tools-v*" }
    $prior_versions = $prior_release_tags | ForEach-Object {
        [regex]::Match($_, '^tools-v([0-9]+\.[0-9]+\.[0-9]+)').Groups[1].Value
    } | Where-Object { $_ -ne "" }

    if ($prior_versions -contains $project_version) {
        Write-Warning "CMakeLists.txt still reports v$project_version -- a release under that same version is already published ($($prior_release_tags -join ', ')). Bump VERSION in CMakeLists.txt's project() call before publishing another release, unless this re-publish is intentional."
        $confirm = Read-Host "Publish anyway under the unchanged version v$project_version? [y/N]"
        if ($confirm -notmatch '^[Yy]$') {
            throw "Publish cancelled -- bump the version in CMakeLists.txt (and add a CHANGELOG.md entry) and re-run."
        }
    }

    $changelog_path = Join-Path $repo_root "CHANGELOG.md"
    $has_changelog_entry = (Test-Path $changelog_path) -and
        (Select-String -Path $changelog_path -Pattern "\[$([regex]::Escape($project_version))\]" -Quiet)
    if (-not $has_changelog_entry) {
        Write-Warning "CHANGELOG.md has no entry for v$project_version yet -- add one describing what changed in this release before publishing."
    }
}

#2. make sure cmake/gcc/make are reachable -- on this machine GDAL is built
#   into the Rtools mingw toolchain rather than a system-wide install, and
#   Rtools is normally only put on PATH by an active R session, not by a
#   plain terminal. Fall back to searching for it if the tools aren't
#   already on PATH (e.g. someone installs a standalone toolchain later).
$have_cmake = Get-Command cmake -ErrorAction SilentlyContinue
$have_make = Get-Command make -ErrorAction SilentlyContinue
$gdal_dir_override = $null
if (-not $have_cmake -or -not $have_make) {
    $rtools_root = Get-ChildItem "C:\" -Directory -Filter "rtools*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if (-not $rtools_root) {
        throw "cmake/make not found on PATH, and no Rtools install found under C:\ to fall back to."
    }
    $mingw_bin = Join-Path $rtools_root.FullName "x86_64-w64-mingw32.static.posix\bin"
    $usr_bin = Join-Path $rtools_root.FullName "usr\bin"
    $env:Path = "$mingw_bin;$usr_bin;$env:Path"
    Write-Host "cmake/make not on PATH -- using Rtools toolchain at $($rtools_root.FullName)"
}

# Determine GDAL directory
if ($GDALDir) {
    $gdal_dir_override = $GDALDir
    Write-Host "Using user-specified GDAL directory: $gdal_dir_override"
} elseif ($FullGDAL) {
    $rtools_root = Get-ChildItem "C:\" -Directory -Filter "rtools*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    $gdal_dir_override = Join-Path $rtools_root.FullName "x86_64-w64-mingw32.static.posix\lib\cmake\gdal"
    Write-Host "Using Full Rtools static GDAL (multi-format profile): $gdal_dir_override"
} else {
    # Default: Minimal Static GDAL (lightweight, GeoTIFF / COG only)
    $minimal_gdal_dir = Join-Path $repo_root "deps\gdal_minimal"
    $minimal_cmake_dir = Join-Path $minimal_gdal_dir "lib\cmake\gdal"
    if (-not (Test-Path $minimal_cmake_dir)) {
        $minimal_cmake_dir = Join-Path $minimal_gdal_dir "lib64\cmake\gdal"
    }
    if (-not (Test-Path $minimal_cmake_dir)) {
        Write-Host "Minimal static GDAL not found. Building it now via build_gdal_minimal.ps1..."
        & powershell -ExecutionPolicy Bypass -File (Join-Path $repo_root "build_gdal_minimal.ps1")
        if ($LASTEXITCODE -ne 0) {
            throw "Failed to build minimal static GDAL."
        }
        if (Test-Path (Join-Path $minimal_gdal_dir "lib\cmake\gdal")) {
            $minimal_cmake_dir = Join-Path $minimal_gdal_dir "lib\cmake\gdal"
        } elseif (Test-Path (Join-Path $minimal_gdal_dir "lib64\cmake\gdal")) {
            $minimal_cmake_dir = Join-Path $minimal_gdal_dir "lib64\cmake\gdal"
        }
    }
    $gdal_dir_override = $minimal_cmake_dir
    Write-Host "Using Minimal Static GDAL (lightweight GeoTIFF/COG profile): $gdal_dir_override"
}

#3. configure the CMake build directory (only if missing, or -Reconfigure passed)
$cache_path = Join-Path $BuildDir "CMakeCache.txt"
if ($Reconfigure -and (Test-Path $BuildDir)) {
    Remove-Item -Recurse -Force $BuildDir -Confirm:$false
}
if (-not (Test-Path $cache_path)) {
    $configure_args = @("-S", ".", "-B", $BuildDir, "-G", "Unix Makefiles", "-DCMAKE_BUILD_TYPE=Release")
    if ($gdal_dir_override) {
        $configure_args += "-DGDAL_DIR=$gdal_dir_override"
    }
    if ($VcpkgRoot) {
        $toolchain_file = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
        $configure_args += "-DCMAKE_TOOLCHAIN_FILE=$toolchain_file"
    }
    Write-Host "Configuring CMake in '$BuildDir'..."
    & cmake @configure_args
}

#4. compile all 13 tool executables in Release mode
Write-Host "Compiling (Release)..."
& cmake --build $BuildDir --config Release --parallel

#5. collect the built exes into a flat bin/ folder, regardless of whether
#   the generator is single-config (exe lands in build/) or multi-config
#   (exe lands in build/Release/)
if (-not (Test-Path $bin_dir)) {
    New-Item -ItemType Directory -Path $bin_dir | Out-Null
}
foreach ($tool_name in $tool_names) {
    $found_exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "$tool_name.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $found_exe) {
        throw "Build did not produce $tool_name.exe -- check the compile output above."
    }
    $destination_path = Join-Path $bin_dir "$tool_name.exe"
    Invoke-WithRetry { Copy-Item $found_exe.FullName $destination_path -Force }
}
Write-Host "Copied $($tool_names.Count) executables into $bin_dir."

#4a. copy proj.db alongside the tools in bin/ -- see CMakeLists.txt's
#    matching configure-time copy and GDALRaster.cpp's ConfigureProjData()
#    for why every tool looks for proj_data/proj.db next to its own exe
$proj_db_source = Join-Path $repo_root "deps\gdal_minimal\share\proj\proj.db"
if (Test-Path $proj_db_source) {
    $bin_proj_data_dir = Join-Path $bin_dir "proj_data"
    if (-not (Test-Path $bin_proj_data_dir)) {
        New-Item -ItemType Directory -Path $bin_proj_data_dir | Out-Null
    }
    Invoke-WithRetry { Copy-Item $proj_db_source (Join-Path $bin_proj_data_dir "proj.db") -Force }
    Write-Host "Copied proj.db into $bin_proj_data_dir."
} else {
    Write-Warning "proj.db not found at $proj_db_source -- run build_gdal_minimal.ps1 to vendor it. Tools in bin/ will fall back to PROJ's default (slower, warning-emitting) search at runtime."
}

#5a. strip debug/relocation symbols from all executables in bin/
$strip_cmd = Get-Command strip -ErrorAction SilentlyContinue
if (-not $strip_cmd) {
    $rtools_strip = Get-ChildItem "C:\rtools*" -Recurse -Filter "strip.exe" -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($rtools_strip) { $strip_cmd = $rtools_strip.FullName }
}
if ($strip_cmd) {
    Write-Host "Stripping symbols from executables in $bin_dir..."
    foreach ($tool_name in $tool_names) {
        $exe_path = Join-Path $bin_dir "$tool_name.exe"
        if (Test-Path $exe_path) {
            Invoke-WithRetry { & $strip_cmd $exe_path }
        }
    }
}

#5aa. mirror executables and proj_data to the repo's own bin/ folder as well
$repo_bin_dir = Join-Path $repo_root "bin"
if (-not (Test-Path $repo_bin_dir)) {
    New-Item -ItemType Directory -Path $repo_bin_dir | Out-Null
}
foreach ($tool_name in $tool_names) {
    $src = Join-Path $bin_dir "$tool_name.exe"
    $dest = Join-Path $repo_bin_dir "$tool_name.exe"
    if (Test-Path $src) {
        Invoke-WithRetry { Copy-Item $src $dest -Force }
    }
}
if (Test-Path (Join-Path $bin_dir "proj_data")) {
    $repo_proj_data_dir = Join-Path $repo_bin_dir "proj_data"
    if (-not (Test-Path $repo_proj_data_dir)) {
        New-Item -ItemType Directory -Path $repo_proj_data_dir | Out-Null
    }
    Invoke-WithRetry { Copy-Item (Join-Path $bin_dir "proj_data\proj.db") (Join-Path $repo_proj_data_dir "proj.db") -Force }
}
Write-Host "Mirrored $($tool_names.Count) executables and proj.db into $repo_bin_dir."


#5b. render documentation (HTML website & PDF manual) via Quarto if available
$pdf_doc_path = Join-Path "docs" "pdf\FUSION_Documentation.pdf"
if (Get-Command quarto -ErrorAction SilentlyContinue) {
    Write-Host "Rendering documentation (HTML & PDF) via Quarto..."
    $docs_dir = Join-Path $repo_root "docs"
    & quarto render $docs_dir --to html
    
    # Ensure docs/pdf directory exists
    $pdf_dir = Join-Path $docs_dir "pdf"
    if (-not (Test-Path $pdf_dir)) {
        New-Item -ItemType Directory -Path $pdf_dir | Out-Null
    }
    
    # Render PDF manual using Quarto's built-in Typst engine
    & quarto render (Join-Path $docs_dir "index.md") --to typst --output "FUSION_Documentation.pdf"
    $rendered_pdf = Join-Path $docs_dir "output\FUSION_Documentation.pdf"
    if (Test-Path $rendered_pdf) {
        Copy-Item $rendered_pdf $pdf_doc_path -Force
        Remove-Item $rendered_pdf -Force -ErrorAction SilentlyContinue
        Write-Host "Generated PDF manual at docs/pdf/FUSION_Documentation.pdf"
    }
} else {
    Write-Host "Quarto not found -- skipping HTML/PDF documentation build."
}

#6. archive the previous bundle (if any) before making a new one
if (-not (Test-Path $dist_dir)) {
    New-Item -ItemType Directory -Path $dist_dir | Out-Null
}
if (-not (Test-Path $archive_dir)) {
    New-Item -ItemType Directory -Path $archive_dir | Out-Null
}
$previous_bundles = Get-ChildItem -Path $dist_dir -Filter "*.zip" -ErrorAction SilentlyContinue
foreach ($previous_bundle in $previous_bundles) {
    Move-Item $previous_bundle.FullName (Join-Path $archive_dir $previous_bundle.Name) -Force
    Write-Host "Archived previous bundle: $(Join-Path $archive_dir $previous_bundle.Name)"
}

#7. zip the freshly built exes, CHANGELOG.md (and PDF manual if present) into the new versioned bundle
$bundle_name = "FUSION2_tools_v$bundle_version.zip"
$bundle_path = Join-Path $dist_dir $bundle_name
$zip_paths = $tool_names | ForEach-Object { Join-Path $bin_dir "$_.exe" }
$changelog_src = Join-Path $repo_root "CHANGELOG.md"
if (Test-Path $changelog_src) {
    $zip_paths += $changelog_src
}
if (Test-Path $pdf_doc_path) {
    $zip_paths += $pdf_doc_path
}
if (Test-Path (Join-Path $bin_dir "proj_data")) {
    $zip_paths += Join-Path $bin_dir "proj_data"
}

Invoke-WithRetry {
    if (Test-Path $bundle_path) {
        Remove-Item $bundle_path -Force
    }
    Compress-Archive -Path $zip_paths -DestinationPath $bundle_path -ErrorAction Stop
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip_reader = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path $bundle_path))
$zipped_names = $zip_reader.Entries | ForEach-Object { $_.Name }
$zip_reader.Dispose()
$expected_names = $tool_names | ForEach-Object { "$_.exe" }
$missing_names = $expected_names | Where-Object { $zipped_names -notcontains $_ }
if ($missing_names.Count -gt 0) {
    throw "Bundle is missing executables: $($missing_names -join ', ') -- not publishing an incomplete zip."
}
Write-Host "Created bundle: $bundle_path (verified all $($tool_names.Count) executables present)"

#8. publish to GitHub Releases, replacing the previous release, if -Publish was passed
if ($Publish) {
    Write-Host "Publishing $bundle_path to GitHub Releases..."
    $release_tag = "tools-v$bundle_version"
    $existing_releases = & gh release list --limit 100 --json tagName --jq ".[].tagName" 2>$null |
        Where-Object { $_ -like "tools-v*" }
    foreach ($existing_tag in $existing_releases) {
        Write-Host "Deleting previous release: $existing_tag"
        & gh release delete $existing_tag --yes --cleanup-tag
    }
    $release_assets = @($bundle_path)
    if (Test-Path $pdf_doc_path) {
        $release_assets += $pdf_doc_path
    }
    $version_notes = Get-VersionChangelogNotes -ChangelogPath $changelog_src -Version $project_version
    $release_notes = if ($version_notes) {
        "FUSION2 v$bundle_version release.`n`n### Changes in v$project_version`n`n$version_notes"
    } else {
        "Automated build bundle of all $($tool_names.Count) CLI executables ($($tool_names -join ', ')) and documentation manual."
    }

    & gh release create $release_tag @release_assets `
        --title "FUSION2 v$bundle_version" `
        --notes $release_notes
    Write-Host "Published release: $release_tag"
}

$build_lock.Dispose()
Write-Host "Done."
