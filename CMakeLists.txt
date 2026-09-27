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
  src/chart_grid.cpp
  src/planner.cpp
)
target_include_directories(openautoroute_core PUBLIC include)
target_compile_options(openautoroute_core PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(openautoroute_core PROPERTIES POSITION_INDEPENDENT_CODE ON)  # linked into the plugin's shared library

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE openautoroute_core)
target_compile_options(test_core PRIVATE -Wall -Wextra -Wpedantic)
add_test(NAME core COMMAND test_core)

# Bulk validation tool: parses every cell in a directory tree (see tools/s57_scan.cpp).
add_executable(s57_scan tools/s57_scan.cpp)
target_link_libraries(s57_scan PRIVATE openautoroute_core)

# Command-line router: ENC cells in, GPX route out (import into OpenCPN to check it).
add_executable(openautoroute tools/openautoroute.cpp)
target_link_libraries(openautoroute PRIVATE openautoroute_core)

# Optional OpenCPN plugin front end (needs OpenCPN's ocpn_plugin.h and wxWidgets 3.2; see plugin/CMakeLists.txt).
option(OAR_BUILD_PLUGIN "Build the OpenCPN plugin" OFF)
if(OAR_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
