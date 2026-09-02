cmake_minimum_required(VERSION 3.20)
project(FUSIONUpdate VERSION 5.0.0 LANGUAGES C CXX)

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

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Core static library
add_library(fusion_core STATIC
    src/libfusion_core/GDALRaster.cpp
    src/libfusion_core/LASPointCloud.cpp
    src/libfusion_core/BatchPipeline.cpp
    src/libfusion_core/StatusMessenger.cpp
    src/libfusion_core/ExperimentalMetrics.cpp
    src/libfusion_core/ProcessRunner.cpp
    src/libfusion_core/StageRegistry.cpp
    src/libfusion_core/PipelineState.cpp
)

target_link_libraries(fusion_core PUBLIC GDAL::GDAL)

if(WIN32)
    # When linking static GDAL/PROJ from MinGW/Rtools, resolve circular/transitive static references with linker group
    target_link_libraries(fusion_core PUBLIC
        -Wl,--start-group
        iconv
        z
        bz2
        zstd
        lzma
        lz4
        deflate
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
        nghttp2
        ssh2
        gcrypt
        gpg-error
        idn2
        unistring
        psl
        brotlidec
        brotlicommon
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
