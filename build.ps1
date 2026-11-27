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
  .\build.ps1 -Toolchain mingw   # force the MinGW/Rtools build (no GPU code)
  .\build.ps1 -Toolchain msvc    # require the MSVC build; stop if its tools are missing

Toolchain (-Toolchain auto, the default):
  The standard build uses Microsoft's C++ compiler (MSVC) with static GDAL,
  PROJ, and LASzip from vcpkg (triplet x64-windows-static), so every exe is
  self-contained. nvcc, the CUDA compiler, works only with MSVC on Windows,
  so this is the only build that can include GPU code. Fallbacks:
    - MSVC, CMake, and vcpkg present, plus the CUDA Toolkit 12.8 or newer
      and its Visual Studio plug-in: MSVC build with GPU code (/gpu works).
    - MSVC, CMake, and vcpkg present, no usable CUDA Toolkit: MSVC build
      without GPU code (/gpu prints a notice and runs on the CPU).
    - MSVC or vcpkg missing: the MinGW/Rtools build with the minimal GDAL
      in deps/, without GPU code.
  The MSVC build uses build_msvc/ and the MinGW build uses build/, so the
  two never share a CMake cache.

Layout this script maintains, rooted under $LocalRoot rather than the repo
itself (see -LocalRoot's default below and its comment for why):
  build/    -- CMake configure + compile output
  bin/      -- flat copy of the 13 built .exe files
  dist/     -- the current versioned zip bundle only
  archive/  -- previous zip bundles, moved here when a new one is made
#>

param(
    [switch]$Publish
  , [switch]$NoVcpkg
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
  # empty means: build_msvc/ for the MSVC build, build/ for MinGW
  , [string]$BuildDir = ""
  , [ValidateSet("auto", "msvc", "mingw")]
    [string]$Toolchain = "auto"
  , [switch]$Reconfigure
  , [switch]$FullGDAL
  , [string]$GDALDir = ""
  )

if ($NoVcpkg) {
    $VcpkgRoot = ""
}

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

# finds a Visual Studio install that has the 64-bit C++ compiler, and the
# CMake generator name for it; returns $null when there is none
function Find-MsvcInstall {
    # A. ask vswhere, Visual Studio's install locator, for installs with C++ tools
    $vswhere_path = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    $install_paths = @()
    if (Test-Path $vswhere_path) {
        $install_paths = @(& $vswhere_path -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2>$null |
            Where-Object { $_ })
    }

    # B. vswhere does not always list every install, so also check the
    #    standard install folders (<year or major version>\<edition>)
    $install_paths += @(Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*" -Directory -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName })

    # C. keep the first install that has cl.exe and a known CMake generator,
    #    trying Visual Studio 2022 first because CUDA supports it longest
    $generator_by_folder = @{
        "2022" = "Visual Studio 17 2022"
        "18" = "Visual Studio 18 2026"
        "2019" = "Visual Studio 16 2019"
    }
    $install_paths_sorted = $install_paths | Select-Object -Unique |
        Sort-Object { if ($_ -match '\\2022\\') { 0 } else { 1 } }
    foreach ($install_path in $install_paths_sorted) {
        $cl_exe = Get-ChildItem (Join-Path $install_path "VC\Tools\MSVC\*\bin\Hostx64\x64\cl.exe") -ErrorAction SilentlyContinue |
            Select-Object -First 1
        $version_folder = Split-Path (Split-Path $install_path -Parent) -Leaf
        $generator = $generator_by_folder[$version_folder]
        if ($cl_exe -and $generator) {
            return @{
                InstallPath = $install_path
                Generator = $generator
                ClExe = $cl_exe.FullName
            }
        }
    }
    return $null
}

# finds the newest CUDA Toolkit that has nvcc.exe; returns $null when none
function Find-CudaToolkit {
    # A. the system-wide CUDA_PATH the installer sets, which a terminal opened
    #    before the install does not yet see, then this session's own value
    $candidate_dirs = @(@(
        [Environment]::GetEnvironmentVariable("CUDA_PATH", "Machine")
        $env:CUDA_PATH
    ) | Where-Object { $_ })

    # B. every toolkit in the standard install folder, newest version first
    $toolkit_root = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA"
    $candidate_dirs += @(Get-ChildItem $toolkit_root -Directory -Filter "v*" -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^v\d+\.\d+$' } |
        Sort-Object { [version]$_.Name.Substring(1) } -Descending |
        ForEach-Object { $_.FullName })

    # C. return the first one with nvcc.exe, with its version from the folder name
    foreach ($candidate_dir in $candidate_dirs) {
        $nvcc_exe = Join-Path $candidate_dir "bin\nvcc.exe"
        $folder_name = Split-Path $candidate_dir -Leaf
        if ((Test-Path $nvcc_exe) -and $folder_name -match '^v(\d+\.\d+)$') {
            return @{
                Path = $candidate_dir.TrimEnd('\')
                Version = $Matches[1]
                NvccExe = $nvcc_exe
            }
        }
    }
    return $null
}

#2. choose the compiler toolchain: MSVC + vcpkg (standard, GPU-capable) or
#   the MinGW/Rtools fallback -- see "Toolchain" in the header for the rules

    # A. find vcpkg: -VcpkgRoot / VCPKG_ROOT first, then the usual C:\vcpkg
    if (-not $NoVcpkg -and -not $VcpkgRoot -and (Test-Path "C:\vcpkg\vcpkg.exe")) {
        $VcpkgRoot = "C:\vcpkg"
    }
    $vcpkg_exe = if ($VcpkgRoot) { Join-Path $VcpkgRoot "vcpkg.exe" } else { $null }
    $has_vcpkg = $vcpkg_exe -and (Test-Path $vcpkg_exe)

    # B. find MSVC, and CMake on PATH or the copy Visual Studio ships with
    $msvc_install = Find-MsvcInstall
    $has_cmake = [bool](Get-Command cmake -ErrorAction SilentlyContinue)
    if (-not $has_cmake -and $msvc_install) {
        $vs_cmake_bin = Join-Path $msvc_install.InstallPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
        if (Test-Path (Join-Path $vs_cmake_bin "cmake.exe")) {
            $env:Path = "$vs_cmake_bin;$env:Path"
            $has_cmake = $true
        }
    }

    # C. decide; -Toolchain msvc stops here when a required tool is missing
    $missing_msvc_tools = @()
    if (-not $msvc_install) { $missing_msvc_tools += "Visual Studio C++ compiler (cl.exe)" }
    if (-not $has_cmake) { $missing_msvc_tools += "CMake" }
    if (-not $has_vcpkg) { $missing_msvc_tools += "vcpkg (pass -VcpkgRoot or set VCPKG_ROOT)" }
    $use_msvc = ($Toolchain -ne "mingw") -and ($missing_msvc_tools.Count -eq 0)
    if ($Toolchain -eq "msvc" -and -not $use_msvc) {
        throw "-Toolchain msvc was requested, but these are missing: $($missing_msvc_tools -join '; ')"
    }
    if ($Toolchain -eq "auto" -and -not $use_msvc) {
        Write-Warning "MSVC build not possible (missing: $($missing_msvc_tools -join '; ')). Falling back to the MinGW/Rtools build, which has no GPU code."
    }

    # D. for MSVC, decide whether the GPU code can be built: the toolkit must
    #    be 12.8 or newer (the first with every architecture CMakeLists.txt
    #    lists) and its plug-in must be installed in this Visual Studio
    $use_cuda = $false
    $cuda_toolkit = $null
    if ($use_msvc) {
        $cuda_toolkit = Find-CudaToolkit
        if (-not $cuda_toolkit) {
            Write-Warning "CUDA Toolkit not found -- building MSVC tools without GPU code. Install the CUDA Toolkit (winget install Nvidia.CUDA) for /gpu support."
        } elseif ([version]$cuda_toolkit.Version -lt [version]"12.8") {
            Write-Warning "CUDA Toolkit $($cuda_toolkit.Version) is older than 12.8 -- building without GPU code."
        } else {
            $cuda_plugin_pattern = Join-Path $msvc_install.InstallPath "MSBuild\Microsoft\VC\*\BuildCustomizations\CUDA $($cuda_toolkit.Version).props"
            if (-not (Get-ChildItem $cuda_plugin_pattern -ErrorAction SilentlyContinue)) {
                Write-Warning "CUDA $($cuda_toolkit.Version) is installed, but its Visual Studio plug-in is not in $($msvc_install.InstallPath) -- building without GPU code. Re-run the CUDA installer after Visual Studio to add it."
            } else {
                $use_cuda = $true
            }
        }
    }

    # E. pick the build folder for the chosen toolchain, unless -BuildDir was given
    if (-not $BuildDir) {
        $build_folder_name = if ($use_msvc) { "build_msvc" } else { "build" }
        $BuildDir = Join-Path $LocalRoot $build_folder_name
    }
    $build_mode = if ($use_cuda) { "msvc-cuda" } elseif ($use_msvc) { "msvc-cpu" } else { "mingw" }
    Write-Host "Toolchain: $build_mode   build folder: $BuildDir"
    if ($use_msvc) {
        Write-Host "  MSVC: $($msvc_install.ClExe)"
        Write-Host "  vcpkg: $VcpkgRoot"
    }
    if ($use_cuda) {
        Write-Host "  CUDA $($cuda_toolkit.Version): $($cuda_toolkit.NvccExe)"
    }

#2a. MSVC: install vcpkg's static GDAL and LASzip if they are not there yet
#    (about 15 minutes the first time; later builds reuse them)
$vcpkg_triplet = "x64-windows-static"
$vcpkg_triplet_dir = $null
if ($use_msvc) {
    $vcpkg_triplet_dir = Join-Path $VcpkgRoot "installed\$vcpkg_triplet"
    $has_vcpkg_gdal = Test-Path (Join-Path $vcpkg_triplet_dir "share\gdal\GDALConfig.cmake")
    $has_vcpkg_laszip = Test-Path (Join-Path $vcpkg_triplet_dir "lib\laszip3.lib")
    if (-not $has_vcpkg_gdal -or -not $has_vcpkg_laszip) {
        # run from vcpkg's own folder in classic mode, so vcpkg does not pick
        # up this repo's vcpkg.json and build into the Box-synced repo
        Write-Host "Installing static GDAL and LASzip with vcpkg ($vcpkg_triplet)..."
        Push-Location $VcpkgRoot
        & $vcpkg_exe install "gdal[core]" laszip --triplet $vcpkg_triplet --classic
        $vcpkg_exit = $LASTEXITCODE
        Pop-Location
        if ($vcpkg_exit -ne 0) {
            throw "vcpkg install of gdal[core] and laszip failed (exit code $vcpkg_exit) -- see the output above."
        }
    }
}

#2b. MinGW: make sure cmake/gcc/make are reachable -- on this machine GDAL
#   is built into the Rtools mingw toolchain rather than a system-wide
#   install, and Rtools is normally only put on PATH by an active R session,
#   not by a plain terminal. Fall back to searching for it if the tools
#   aren't already on PATH (e.g. someone installs a standalone toolchain later).
$have_cmake = Get-Command cmake -ErrorAction SilentlyContinue
$have_make = Get-Command make -ErrorAction SilentlyContinue
$gdal_dir_override = $null
if (-not $use_msvc -and (-not $have_cmake -or -not $have_make)) {
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

# Determine GDAL directory (MinGW only; the MSVC build takes GDAL from vcpkg)
if ($use_msvc) {
    if ($GDALDir -or $FullGDAL) {
        Write-Warning "-GDALDir and -FullGDAL apply only to the MinGW build and are ignored for MSVC."
    }
} elseif ($GDALDir) {
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
#   - the build mode (msvc-cuda, msvc-cpu, mingw) is saved beside the cache;
#     when it changes (for example the CUDA Toolkit was installed or
#     removed), the cache is rebuilt with --fresh, because the Visual Studio
#     CUDA setting (-T cuda=...) cannot change in an existing cache
$build_mode_path = Join-Path $BuildDir "fusion_build_mode.txt"
$is_mode_changed = (Test-Path $cache_path) -and (Test-Path $build_mode_path) -and
    ((Get-Content $build_mode_path -Raw).Trim() -ne $build_mode)
if ($use_cuda) {
    # the Visual Studio CUDA plug-in finds the toolkit through these
    # variables; a terminal opened before the CUDA install does not have them
    $env:CUDA_PATH = $cuda_toolkit.Path
    $cuda_version_variable = "CUDA_PATH_V" + $cuda_toolkit.Version.Replace(".", "_")
    Set-Item -Path "env:$cuda_version_variable" -Value $cuda_toolkit.Path
    $env:Path = "$(Join-Path $cuda_toolkit.Path 'bin');$env:Path"
}
if (-not (Test-Path $cache_path) -or $is_mode_changed) {
    if ($use_msvc) {
        # CMAKE_BUILD_TYPE is set here although Visual Studio builds are
        # multi-config: the vcpkg toolchain reads it at project() and,
        # without it, finds vcpkg's Debug libraries before the Release ones
        $configure_args = @(
            "-S", $repo_root
          , "-B", $BuildDir
          , "-G", $msvc_install.Generator
          , "-A", "x64"
          , "-DCMAKE_BUILD_TYPE=Release"
          , "-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake')"
          , "-DVCPKG_TARGET_TRIPLET=$vcpkg_triplet"
          , "-DVCPKG_MANIFEST_MODE=OFF"
          )
        if ($use_cuda) {
            $configure_args += @("-T", "cuda=$($cuda_toolkit.Path)", "-DFUSION_ENABLE_CUDA=ON")
        } else {
            $configure_args += "-DFUSION_ENABLE_CUDA=OFF"
        }
    } else {
        $configure_args = @("-S", ".", "-B", $BuildDir, "-G", "Unix Makefiles", "-DCMAKE_BUILD_TYPE=Release")
        if ($gdal_dir_override) {
            $configure_args += "-DGDAL_DIR=$gdal_dir_override"
        }
        if ($VcpkgRoot) {
            $toolchain_file = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
            $configure_args += "-DCMAKE_TOOLCHAIN_FILE=$toolchain_file"
        }
    }
    if ($is_mode_changed) {
        $configure_args = @("--fresh") + $configure_args
        Write-Host "Build mode changed to $build_mode -- reconfiguring from scratch."
    }
    Write-Host "Configuring CMake in '$BuildDir'..."
    & cmake @configure_args
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed -- see the output above."
    }
    [System.IO.File]::WriteAllText($build_mode_path, $build_mode)
}

#3a. confirm the configured build matches the chosen mode: CMakeLists.txt
#    falls back to CPU code without saying so loudly when nvcc is not found
$cache_text = Get-Content $cache_path -Raw
$is_cuda_configured = $cache_text -match '(?m)^CMAKE_CUDA_COMPILER:[A-Z]+=.*nvcc'
if ($use_cuda -and -not $is_cuda_configured) {
    throw "CUDA was found, but the CMake cache in $BuildDir has no CUDA compiler -- re-run with -Reconfigure."
}

#4. compile all 15 tool executables and the unit tests in Release mode
Write-Host "Compiling (Release)..."
& cmake --build $BuildDir --config Release --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Compilation failed -- see the compiler output above."
}

#4b. run the unit tests, stop the build if any fail, and save their output
#    for the user manual's unit-test appendix (docs/appendices/unit_tests.qmd)
#  - the saved file has no timestamp, so it only changes when the tests or
#    their results change
$tests_exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "fusion_tests.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $tests_exe) {
    throw "Build did not produce fusion_tests.exe -- check the compile output above."
}
Write-Host "Running unit tests..."
$test_output = & $tests_exe.FullName 2>&1 | ForEach-Object { "$_" }
$test_exit = $LASTEXITCODE
$test_output | ForEach-Object { Write-Host "  $_" }
if ($test_exit -ne 0) {
    throw "Unit tests failed (exit code $test_exit) -- see the FAIL lines above."
}
$generated_dir = Join-Path $repo_root "docs\generated"
if (-not (Test-Path $generated_dir)) {
    New-Item -ItemType Directory -Path $generated_dir | Out-Null
}
$test_md = @("``````text") + $test_output + @("``````")
[System.IO.File]::WriteAllLines((Join-Path $generated_dir "unit_test_results.md"), [string[]]$test_md)

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
#  - the MSVC build links vcpkg's PROJ, so it ships that PROJ's own proj.db
$proj_db_source = Join-Path $repo_root "deps\gdal_minimal\share\proj\proj.db"
if ($use_msvc) {
    $proj_db_source = Join-Path $vcpkg_triplet_dir "share\proj\proj.db"
}
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
#  - MinGW only: MSVC Release exes keep their debug information in a
#    separate .pdb file, so there is nothing to strip, and GNU strip is not
#    meant for MSVC-linked exes
$strip_cmd = $null
if (-not $use_msvc) {
    $strip_cmd = Get-Command strip -ErrorAction SilentlyContinue
}
if (-not $use_msvc -and -not $strip_cmd) {
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

#5ab0. record how this bundle was built, so a user can tell whether their
#      copy has GPU code; the file goes into the zip beside the exes
$cuda_architectures_match = [regex]::Match($cmake_text, 'set\(CMAKE_CUDA_ARCHITECTURES\s+"([^"]+)"\)')
$compiler_line = if ($use_msvc) {
    "Compiler: MSVC ($($msvc_install.Generator)), static GDAL/PROJ/LASzip from vcpkg $vcpkg_triplet"
} else {
    "Compiler: MinGW (Rtools), static GDAL from $gdal_dir_override"
}
$gpu_line = if ($use_cuda) {
    "GPU code: yes -- CUDA $($cuda_toolkit.Version), architectures $($cuda_architectures_match.Groups[1].Value). /gpu needs an NVIDIA card and driver; without one the tools run on the CPU."
} else {
    "GPU code: no -- /gpu prints a notice and the tools run on the CPU."
}
$build_info_lines = @(
    "FUSION2 tools v$bundle_version"
    "Build mode: $build_mode"
    $compiler_line
    $gpu_line
    "Built: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
)
$build_info_path = Join-Path $bin_dir "build_info.txt"
[System.IO.File]::WriteAllLines($build_info_path, [string[]]$build_info_lines)
Write-Host $gpu_line

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

#5ab. save each tool's built-in help (/?) for the user manual's help
#     chapter (docs/reference/tool_help.qmd), so it always matches the tools
$help_dir = Join-Path $repo_root "docs\generated\help"
if (-not (Test-Path $help_dir)) {
    New-Item -ItemType Directory -Path $help_dir | Out-Null
}
foreach ($tool_name in $tool_names) {
    $help_text = & (Join-Path $bin_dir "$tool_name.exe") "/?" 2>&1 | ForEach-Object { "$_" }
    $help_md = @("``````text") + $help_text + @("``````")
    [System.IO.File]::WriteAllLines((Join-Path $help_dir "$tool_name.md"), [string[]]$help_md)
}
Write-Host "Saved help text for $($tool_names.Count) tools into $help_dir."


#5ac. copy CHANGELOG.md into the manual's changelog appendix, minus its
#     own "# Changelog" title, which would otherwise start a second chapter
$changelog_lines = Get-Content (Join-Path $repo_root "CHANGELOG.md") -Encoding utf8
$lines_to_skip = 0
for ($i = 0; $i -lt $changelog_lines.Count; $i++) {
    if ($changelog_lines[$i] -match '^# ') {
        $lines_to_skip = $i + 1
        break
    }
}
$changelog_body = $changelog_lines | Select-Object -Skip $lines_to_skip
[System.IO.File]::WriteAllLines((Join-Path $repo_root "docs\generated\changelog.md"), [string[]]$changelog_body)

#5b. render the user manual (a Quarto book in docs/) to an HTML site and a
#    PDF, then copy the PDF to docs/pdf/ for the release bundle
#  - one "quarto render" builds both formats listed in docs/_quarto.yml;
#    the HTML site lands in docs/output/ (not tracked in git)
$pdf_doc_path = Join-Path "docs" "pdf\FUSION2_Manual.pdf"
if (Get-Command quarto -ErrorAction SilentlyContinue) {
    Write-Host "Rendering the user manual (HTML and PDF) via Quarto..."
    $docs_dir = Join-Path $repo_root "docs"
    & quarto render $docs_dir
    if ($LASTEXITCODE -ne 0) {
        throw "Rendering the user manual failed -- see the Quarto output above."
    }
    $pdf_dir = Join-Path $docs_dir "pdf"
    if (-not (Test-Path $pdf_dir)) {
        New-Item -ItemType Directory -Path $pdf_dir | Out-Null
    }
    $rendered_pdf = Join-Path $docs_dir "output\FUSION2_Manual.pdf"
    if (-not (Test-Path $rendered_pdf)) {
        throw "Quarto finished but $rendered_pdf was not written."
    }
    Copy-Item $rendered_pdf $pdf_doc_path -Force
    Write-Host "Generated PDF manual at $pdf_doc_path"
} else {
    Write-Host "Quarto not found -- skipping the user manual."
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
$zip_paths += $build_info_path
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
    $release_notes += "`n`n### Build`n`n$compiler_line`n`n$gpu_line"

    & gh release create $release_tag @release_assets `
        --title "FUSION2 v$bundle_version" `
        --notes $release_notes
    Write-Host "Published release: $release_tag"
}

$build_lock.Dispose()
Write-Host "Done."
