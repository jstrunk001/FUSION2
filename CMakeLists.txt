cmake_minimum_required(VERSION 3.20)
project(FUSIONUpdate VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:Release>:-ffunction-sections>
        $<$<CONFIG:Release>:-fdata-sections>
    )
    add_link_options(
        $<$<CONFIG:Release>:-Wl,--gc-sections>
        $<$<CONFIG:Release>:-s>
    )
endif()

# Find GDAL using CMake Config mode to automatically pull in static dependencies
find_package(GDAL CONFIG REQUIRED)

# LASzip has no exported CMake config (see build_laszip_minimal.ps1, which
# vendors it the same way build_gdal_minimal.ps1 vendors GDAL) -- locate its
# static libs and headers directly instead of find_package().
set(LASZIP_MINIMAL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/laszip_minimal")
find_library(LASZIP_BASE_LIBRARY NAMES laszip3 PATHS "${LASZIP_MINIMAL_DIR}/lib" NO_DEFAULT_PATH)
if(NOT LASZIP_BASE_LIBRARY)
    message(FATAL_ERROR "Minimal static LASzip not found under ${LASZIP_MINIMAL_DIR} -- run build_laszip_minimal.ps1 first.")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${LASZIP_MINIMAL_DIR}/include
    ${LASZIP_MINIMAL_DIR}/include/laszip
    ${CMAKE_CURRENT_SOURCE_DIR}/deps/sqlite3_amalgamation
)

# Core static library
add_library(fusion_core STATIC
    src/libfusion_core/GDALRaster.cpp
    src/libfusion_core/LASPointCloud.cpp
    src/libfusion_core/InputResolver.cpp
    src/libfusion_core/MergedPointCloudReader.cpp
    src/libfusion_core/SpatialMask.cpp
    src/libfusion_core/PolygonFeatureSet.cpp
    src/libfusion_core/SurfaceStats.cpp
    src/libfusion_core/PointCloudStats.cpp
    src/libfusion_core/SpectralChannels.cpp
    src/libfusion_core/COPCIndex.cpp
    src/libfusion_core/BatchPipeline.cpp
    src/libfusion_core/StatusMessenger.cpp
    src/libfusion_core/ExperimentalMetrics.cpp
    src/libfusion_core/ProcessRunner.cpp
    src/libfusion_core/StageRegistry.cpp
    src/libfusion_core/PipelineState.cpp
    src/libfusion_core/PointFilter.cpp
    src/libfusion_core/TableWriter.cpp
    src/libfusion_core/ChmSmoothing.cpp
    deps/sqlite3_amalgamation/sqlite3.c
)

target_link_libraries(fusion_core PUBLIC GDAL::GDAL)

if(WIN32)
    # When linking static GDAL/PROJ from MinGW/Rtools, resolve circular/transitive static references with linker group.
    # LASzip's laszip_api3 (references symbols defined in laszip3's
    # laszip_dll.cpp) is folded into this same group rather than given its
    # own -- a separate start-group/end-group pair here previously broke
    # resolution of unrelated symbols (libcrypto's zlib/winsock refs) by
    # splitting what GNU ld needs to treat as one atomic resolution pass.
    # deflate, nghttp2, psl, brotlidec, and brotlicommon are not present as
    # static libraries in the current Rtools 4.3 toolchain (they were
    # present under a previous Rtools 4.5 install this project was last
    # built against) -- dropped from this group since GDAL is vendored with
    # GDAL_USE_CURL=OFF/GDAL_USE_DEFLATE=OFF (see build_gdal_minimal.ps1),
    # so libgdal.a itself never calls into them; PROJ's own curl usage
    # (found transitively via find_dependency(PROJ CONFIG)) is limited to
    # optional network grid downloads this project's local-file-only tools
    # never trigger.
    target_link_libraries(fusion_core PUBLIC
        -Wl,--start-group
        ${LASZIP_BASE_LIBRARY}
        iconv
        z
        bz2
        zstd
        lzma
        lz4
        Lerc
        aec
        webp
        sharpyuv
        bcrypt
        ws2_32
        crypt32
        wldap32
        secur32
        ssl
        crypto
        ssh2
        gcrypt
        gpg-error
        idn2
        unistring
        -Wl,--end-group
    )
endif()

# Core Executables
add_executable(gridmetrics src/tools/gridmetrics/gridmetrics.cpp)
target_link_libraries(gridmetrics PRIVATE fusion_core)

add_executable(clipdata src/tools/clipdata/clipdata.cpp)
target_link_libraries(clipdata PRIVATE fusion_core)

add_executable(groundfilter src/tools/groundfilter/groundfilter.cpp)
target_link_libraries(groundfilter PRIVATE fusion_core)

add_executable(canopymodel src/tools/canopymodel/canopymodel.cpp)
target_link_libraries(canopymodel PRIVATE fusion_core)

add_executable(catalog src/tools/catalog/catalog.cpp)
target_link_libraries(catalog PRIVATE fusion_core)

# Analytical & Utility Suite Executables
add_executable(canopymaxima src/tools/canopymaxima/canopymaxima.cpp)
target_link_libraries(canopymaxima PRIVATE fusion_core)

add_executable(treeseg src/tools/treeseg/treeseg.cpp)
target_link_libraries(treeseg PRIVATE fusion_core)

add_executable(cloudmetrics src/tools/cloudmetrics/cloudmetrics.cpp)
target_link_libraries(cloudmetrics PRIVATE fusion_core)

add_executable(topometrics src/tools/topometrics/topometrics.cpp)
target_link_libraries(topometrics PRIVATE fusion_core)

add_executable(gridsurfacestats src/tools/gridsurfacestats/gridsurfacestats.cpp)
target_link_libraries(gridsurfacestats PRIVATE fusion_core)

add_executable(densitymetrics src/tools/densitymetrics/densitymetrics.cpp)
target_link_libraries(densitymetrics PRIVATE fusion_core)

add_executable(filterdata src/tools/filterdata/filterdata.cpp)
target_link_libraries(filterdata PRIVATE fusion_core)

add_executable(thindata src/tools/thindata/thindata.cpp)
target_link_libraries(thindata PRIVATE fusion_core)

add_executable(returndensity src/tools/returndensity/returndensity.cpp)
target_link_libraries(returndensity PRIVATE fusion_core)

# Multi-tool batch pipeline orchestrator (tiles/buffers the input once and
# chains any of the above tools per tile as child processes -- see
# src/tools/pipeline/pipeline.cpp)
add_executable(pipeline src/tools/pipeline/pipeline.cpp)
target_link_libraries(pipeline PRIVATE fusion_core)

# Lightweight test harness: plain assert-style checks (no GoogleTest/Catch2
# dependency), registered with CTest so `ctest --test-dir build` runs them
# the normal way. See tests/cpp/test_assert.h for the CHECK() macro.
enable_testing()
add_executable(fusion_tests
    tests/cpp/test_main.cpp
    tests/cpp/test_point_filter.cpp
    tests/cpp/test_point_cloud_stats.cpp
    tests/cpp/test_las_writer.cpp
    tests/cpp/test_chm_smoothing.cpp
    tests/cpp/test_table_writer.cpp
)
target_link_libraries(fusion_tests PRIVATE fusion_core)
add_test(NAME fusion_tests COMMAND fusion_tests)
