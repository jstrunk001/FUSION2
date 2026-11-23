<#
Builds all FUSION2 tools, runs the unit tests, bundles the resulting .exe
files into a versioned zip, and (optionally) publishes that zip as a GitHub
Release asset.

This version is written so that an automated agent (or any non-interactive
session) can run it reliably:
  - it never prompts unless it is attached to a keyboard (use -Yes to
    pre-approve the one publish confirmation)
  - compiler output goes to a log file; the console shows short progress
    lines, and the last lines of the log if the compile fails
  - unit tests run with a private temp folder and a time limit, so a bad
    TEMP setting in the calling shell or a hung test cannot break the run
  - every failure ends with one "BUILD FAILED: <reason>" line and a nonzero
    exit code; success ends with "BUILD SUCCEEDED" and exit code 0
  - -TestsOnly builds and tests without touching tracked files in the repo

Usage (from any shell):
  powershell -NoProfile -ExecutionPolicy Bypass -File .\build_v2.ps1
  .\build_v2.ps1                  # configure + build + test + bundle (local)
  .\build_v2.ps1 -TestsOnly       # configure + build + test, nothing else:
                                    no repo files rewritten, no bin/ copy
  .\build_v2.ps1 -NoDocs          # skip the Quarto manual render
  .\build_v2.ps1 -Publish -Yes    # also publish to GitHub Releases and
                                    delete the previous release; -Yes
                                    answers the "same version" prompt
  .\build_v2.ps1 -VcpkgRoot "C:\vcpkg"   # pass a vcpkg toolchain file
  .\build_v2.ps1 -LocalRoot "G:\FUSION2_build"   # use a different local drive

Layout this script maintains, rooted under $LocalRoot rather than the repo
itself (see -LocalRoot's default below and its comment for why):
  build/     -- CMake configure + compile output
  bin/       -- flat copy of the built .exe files
  dist/      -- the current versioned zip bundle only
  archive/   -- previous zip bundles, moved here when a new one is made
  logs/      -- compile.log, configure.log, unit_tests.log, quarto.log
  test_tmp/  -- scratch folder used as TEMP while the unit tests run
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
  , [string]$BuildDir = (Join-Path $LocalRoot "build")
  , [switch]$Reconfigure
  , [switch]$FullGDAL
  , [string]$GDALDir = ""
  # build and test only: no docs/generated rewrite, no repo bin/ mirror, no
  # manual render, no zip bundle
  , [switch]$TestsOnly
  # skip the Quarto manual render (the slowest optional step)
  , [switch]$NoDocs
  # answer "yes" to the publish-under-an-unchanged-version confirmation
  , [switch]$Yes
  # print every compiler line to the console as well as the log file
  , [switch]$ShowCompileOutput
  # a unit-test run longer than this is killed and counted as a failure
  , [int]$TestTimeoutMinutes = 20
  # how long to wait for another build.ps1/build_v2.ps1 using the same
  # $LocalRoot before giving up
  , [int]$LockTimeoutMinutes = 30
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
$log_dir = Join-Path $LocalRoot "logs"
$test_tmp_dir = Join-Path $LocalRoot "test_tmp"
foreach ($needed_dir in @($LocalRoot, $log_dir, $test_tmp_dir)) {
    if (-not (Test-Path $needed_dir)) {
        New-Item -ItemType Directory -Path $needed_dir | Out-Null
    }
}
Write-Host "Build output root (outside Box Sync): $LocalRoot"
Write-Host "Logs: $log_dir"

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
$lock_deadline = (Get-Date).AddMinutes($LockTimeoutMinutes)
while ($null -eq $build_lock) {
    try {
        $build_lock = [System.IO.File]::Open($lock_path, 'OpenOrCreate', 'ReadWrite', 'None')
    } catch [System.IO.IOException] {
        if ((Get-Date) -gt $lock_deadline) {
            throw "Another build script has held $lock_path for $LockTimeoutMinutes minutes -- giving up. Check for a stuck build process."
        }
        Write-Host "Another build script is running in $LocalRoot -- waiting for it to finish..."
        Start-Sleep -Seconds 10
    }
}

# Any terminating error ends here: release the lock, print one summary line
# that names the reason, and exit nonzero so a calling agent or script can
# tell the build failed without reading the whole transcript.
trap {
    if ($build_lock) { $build_lock.Dispose() }
    Write-Host ""
    Write-Host "BUILD FAILED: $($_.Exception.Message)"
    exit 1
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

# Run a native command, send everything it prints (stdout and stderr) to a
# log file, and return its exit code. Under $ErrorActionPreference = "Stop",
# Windows PowerShell 5.1 turns any stderr line of a native command into a
# terminating error when stderr is redirected, so the preference is relaxed
# for the duration of the call and the exit code is checked by the caller.
function Invoke-NativeLogged {
    param(
        [string]$Exe
      , [string[]]$Arguments
      , [string]$LogPath
      , [switch]$Echo
      )
    $saved_preference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        & $Exe @Arguments 2>&1 |
            ForEach-Object { "$_" } |
            Tee-Object -FilePath $LogPath |
            ForEach-Object { if ($Echo) { Write-Host $_ } }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved_preference
    }
}

function Write-LogTail {
    param(
        [string]$LogPath
      , [int]$Lines = 40
      )
    if (Test-Path $LogPath) {
        Write-Host "---- last $Lines lines of $LogPath ----"
        Get-Content $LogPath -Tail $Lines | ForEach-Object { Write-Host $_ }
        Write-Host "---- end of log excerpt ----"
    }
}

# the newest Rtools folder under C:\ (a direct, non-recursive listing)
function Get-RtoolsRoot {
    Get-ChildItem "C:\" -Directory -Filter "rtools*" -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
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
        if ($Yes) {
            Write-Host "-Yes given: publishing under the unchanged version v$project_version."
        } elseif ([Console]::IsInputRedirected) {
            throw "Version v$project_version is already published and no keyboard is attached to confirm. Bump the version in CMakeLists.txt (and add a CHANGELOG.md entry), or re-run with -Yes to publish under the unchanged version."
        } else {
            $confirm = Read-Host "Publish anyway under the unchanged version v$project_version? [y/N]"
            if ($confirm -notmatch '^[Yy]$') {
                throw "Publish cancelled -- bump the version in CMakeLists.txt (and add a CHANGELOG.md entry) and re-run."
            }
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
#   plain terminal. Fall back to Rtools if the tools aren't already on PATH.
$rtools_root = Get-RtoolsRoot
$mingw_bin = $null
if ($rtools_root) {
    $mingw_bin = Join-Path $rtools_root.FullName "x86_64-w64-mingw32.static.posix\bin"
}
$have_cmake = Get-Command cmake -ErrorAction SilentlyContinue
$have_make = Get-Command make -ErrorAction SilentlyContinue
$gdal_dir_override = $null
if (-not $have_cmake -or -not $have_make) {
    if (-not $rtools_root) {
        throw "cmake/make not found on PATH, and no Rtools install found under C:\ to fall back to."
    }
    $usr_bin = Join-Path $rtools_root.FullName "usr\bin"
    $env:Path = "$mingw_bin;$usr_bin;$env:Path"
    Write-Host "cmake/make not on PATH -- using Rtools toolchain at $($rtools_root.FullName)"
}

# Determine GDAL directory
if ($GDALDir) {
    $gdal_dir_override = $GDALDir
    Write-Host "Using user-specified GDAL directory: $gdal_dir_override"
} elseif ($FullGDAL) {
    if (-not $rtools_root) {
        throw "-FullGDAL needs an Rtools install under C:\, and none was found."
    }
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
        & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo_root "build_gdal_minimal.ps1")
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
    $configure_log = Join-Path $log_dir "configure.log"
    $configure_exit = Invoke-NativeLogged -Exe "cmake" -Arguments $configure_args -LogPath $configure_log -Echo:$ShowCompileOutput
    if ($configure_exit -ne 0) {
        Write-LogTail -LogPath $configure_log
        throw "CMake configure failed (exit code $configure_exit) -- full output in $configure_log"
    }
}

#4. compile all tool executables and the unit tests in Release mode
Write-Host "Compiling (Release)... full output goes to $(Join-Path $log_dir 'compile.log')"
$compile_log = Join-Path $log_dir "compile.log"
$compile_exit = Invoke-NativeLogged `
    -Exe "cmake" `
    -Arguments @("--build", $BuildDir, "--config", "Release", "--parallel") `
    -LogPath $compile_log `
    -Echo:$ShowCompileOutput
if ($compile_exit -ne 0) {
    Write-LogTail -LogPath $compile_log
    throw "Compilation failed (exit code $compile_exit) -- full output in $compile_log"
}
Write-Host "Compile finished."

#4b. run the unit tests, stop the build if any fail, and save their output
#    for the user manual's unit-test appendix (docs/appendices/unit_tests.qmd)
#  - the saved file has no timestamp, so it only changes when the tests or
#    their results change
#  - several tests write scratch files under the system temp folder
#    (std::filesystem::temp_directory_path()), which resolves to
#    C:\Windows -- a folder that cannot be written -- when TMP/TEMP are
#    unset or unusable in the calling shell. The tests therefore run with
#    TMP and TEMP pointed at a folder this script owns, restored afterwards.
#  - the tests run from the repo root, as they did before, because some
#    refer to paths relative to it
$tests_exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "fusion_tests.exe" -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $tests_exe) {
    throw "Build did not produce fusion_tests.exe -- check $compile_log."
}
Write-Host "Running unit tests (time limit $TestTimeoutMinutes minutes, temp folder $test_tmp_dir)..."
$test_stdout_log = Join-Path $log_dir "unit_tests.log"
$test_stderr_log = Join-Path $log_dir "unit_tests_stderr.log"
$saved_tmp = $env:TMP
$saved_temp = $env:TEMP
$env:TMP = $test_tmp_dir
$env:TEMP = $test_tmp_dir
$test_timed_out = $false
try {
    $test_process = Start-Process `
        -FilePath $tests_exe.FullName `
        -WorkingDirectory $repo_root `
        -RedirectStandardOutput $test_stdout_log `
        -RedirectStandardError $test_stderr_log `
        -NoNewWindow `
        -PassThru
    # reading Handle makes PowerShell keep the process handle open, which is
    # required for ExitCode to be readable after the process ends
    $null = $test_process.Handle
    $finished_in_time = $test_process.WaitForExit($TestTimeoutMinutes * 60 * 1000)
    if (-not $finished_in_time) {
        $test_timed_out = $true
        $test_process.Kill()
        $test_process.WaitForExit()
    }
    $test_exit = $test_process.ExitCode
} finally {
    $env:TMP = $saved_tmp
    $env:TEMP = $saved_temp
}

$test_output = @()
if (Test-Path $test_stdout_log) { $test_output += Get-Content $test_stdout_log }
if (Test-Path $test_stderr_log) { $test_output += Get-Content $test_stderr_log }
$test_output = @($test_output | ForEach-Object { "$_" })
$test_fail_lines = @($test_output | Where-Object { $_ -match '^\s*FAIL' })

if ($test_timed_out) {
    throw "Unit tests did not finish within $TestTimeoutMinutes minutes and were stopped -- output so far is in $test_stdout_log"
}
if ($test_exit -ne 0 -or $test_fail_lines.Count -gt 0) {
    Write-Host "---- unit test output (first 80 FAIL lines, then the last 15 lines) ----"
    $test_fail_lines | Select-Object -First 80 | ForEach-Object { Write-Host "  $_" }
    $test_output | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
    Write-Host "---- full output in $test_stdout_log ----"
    throw "Unit tests failed (exit code $test_exit, $($test_fail_lines.Count) FAIL lines)."
}
$test_output | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
Write-Host "Unit tests passed (exit code 0, no FAIL lines). Full output in $test_stdout_log"

if ($TestsOnly) {
    $build_lock.Dispose()
    Write-Host ""
    Write-Host "BUILD SUCCEEDED (-TestsOnly: compiled and tested; bin/, docs/generated, and the bundle were not touched)."
    exit 0
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
        throw "Build did not produce $tool_name.exe -- check $compile_log."
    }
    $destination_path = Join-Path $bin_dir "$tool_name.exe"
    Invoke-WithRetry { Copy-Item $found_exe.FullName $destination_path -Force }
}
Write-Host "Copied $($tool_names.Count) executables into $bin_dir."

#5a-i. copy proj.db alongside the tools in bin/ -- see CMakeLists.txt's
#      matching configure-time copy and GDALRaster.cpp's ConfigureProjData()
#      for why every tool looks for proj_data/proj.db next to its own exe
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
#    (strip comes from PATH, or from the known Rtools mingw folder -- never
#    from a recursive search of C:\)
$strip_cmd = Get-Command strip -ErrorAction SilentlyContinue
if (-not $strip_cmd -and $mingw_bin) {
    $rtools_strip = Join-Path $mingw_bin "strip.exe"
    if (Test-Path $rtools_strip) { $strip_cmd = $rtools_strip }
}
if ($strip_cmd) {
    Write-Host "Stripping symbols from executables in $bin_dir..."
    foreach ($tool_name in $tool_names) {
        $exe_path = Join-Path $bin_dir "$tool_name.exe"
        if (Test-Path $exe_path) {
            Invoke-WithRetry { & $strip_cmd $exe_path }
        }
    }
} else {
    Write-Warning "strip not found -- executables in bin/ keep their debug symbols."
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

#5ab. save each tool's built-in help (/?) for the user manual's help
#     chapter (docs/reference/tool_help.qmd), so it always matches the tools
$help_dir = Join-Path $repo_root "docs\generated\help"
if (-not (Test-Path $help_dir)) {
    New-Item -ItemType Directory -Path $help_dir | Out-Null
}
foreach ($tool_name in $tool_names) {
    $saved_preference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $help_text = & (Join-Path $bin_dir "$tool_name.exe") "/?" 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = $saved_preference
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
if ($NoDocs) {
    Write-Host "-NoDocs given -- skipping the user manual."
} elseif (Get-Command quarto -ErrorAction SilentlyContinue) {
    Write-Host "Rendering the user manual (HTML and PDF) via Quarto... full output goes to $(Join-Path $log_dir 'quarto.log')"
    $docs_dir = Join-Path $repo_root "docs"
    $quarto_log = Join-Path $log_dir "quarto.log"
    $quarto_exit = Invoke-NativeLogged -Exe "quarto" -Arguments @("render", $docs_dir) -LogPath $quarto_log -Echo:$ShowCompileOutput
    if ($quarto_exit -ne 0) {
        Write-LogTail -LogPath $quarto_log
        throw "Rendering the user manual failed (exit code $quarto_exit) -- full output in $quarto_log"
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
        if ($LASTEXITCODE -ne 0) {
            throw "Could not delete previous release $existing_tag."
        }
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
    if ($LASTEXITCODE -ne 0) {
        throw "gh release create failed (exit code $LASTEXITCODE)."
    }
    Write-Host "Published release: $release_tag"
}

$build_lock.Dispose()
Write-Host ""
Write-Host "BUILD SUCCEEDED: v$bundle_version"
Write-Host "  executables: $bin_dir (mirrored to $repo_bin_dir)"
Write-Host "  bundle:      $bundle_path"
Write-Host "  logs:        $log_dir"
exit 0
