cmake_minimum_required(VERSION 3.20)
project(FUSIONUpdate VERSION 5.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find GDAL
find_package(GDAL REQUIRED)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${GDAL_INCLUDE_DIRS}
)

# Core static library
add_library(fusion_core STATIC
    src/libfusion_core/GDALRaster.cpp
    src/libfusion_core/LASPointCloud.cpp
    src/libfusion_core/BatchPipeline.cpp
    src/libfusion_core/StatusMessenger.cpp
)

target_link_libraries(fusion_core PUBLIC ${GDAL_LIBRARIES})

# Executable: ltktools
add_executable(ltktools src/tools/ltktools/ltktools.cpp)
target_link_libraries(ltktools PRIVATE fusion_core)

# Executable: gridmetrics
add_executable(gridmetrics src/tools/gridmetrics/gridmetrics.cpp)
target_link_libraries(gridmetrics PRIVATE fusion_core)

# Executable: clipdata
add_executable(clipdata src/tools/clipdata/clipdata.cpp)
target_link_libraries(clipdata PRIVATE fusion_core)

# Executable: groundfilter
add_executable(groundfilter src/tools/groundfilter/groundfilter.cpp)
target_link_libraries(groundfilter PRIVATE fusion_core)

# Executable: canopymodel
add_executable(canopymodel src/tools/canopymodel/canopymodel.cpp)
target_link_libraries(canopymodel PRIVATE fusion_core)

# Executable: catalog
add_executable(catalog src/tools/catalog/catalog.cpp)
target_link_libraries(catalog PRIVATE fusion_core)
