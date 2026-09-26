cmake_minimum_required(VERSION 3.16)
project(openautoroute VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The core is a plain static library so both the OpenCPN plugin and the Flutter FFI wrapper can link it.
add_library(openautoroute_core STATIC
  src/cost_grid.cpp
  src/pathfinder.cpp
  src/gpx.cpp
  src/s57.cpp
)
target_include_directories(openautoroute_core PUBLIC include)
target_compile_options(openautoroute_core PRIVATE -Wall -Wextra -Wpedantic)

# S-57 parsing goes through GDAL/OGR when available; the rest of the core builds and tests without it.
option(OAR_WITH_GDAL "Parse S-57 ENCs with GDAL/OGR" OFF)
if(OAR_WITH_GDAL)
  find_package(GDAL REQUIRED)
  target_link_libraries(openautoroute_core PUBLIC GDAL::GDAL)
  target_compile_definitions(openautoroute_core PUBLIC OAR_WITH_GDAL=1)
endif()

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE openautoroute_core)
target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME core COMMAND test_core)
