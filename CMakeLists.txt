cmake_minimum_required(VERSION 3.16)
project(openautoroute LANGUAGES CXX)   # no version until the first release; a release build passes -DOAR_VERSION

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -Wall/-Wextra/-Wpedantic are GCC/Clang flags; MSVC's cl.exe hard-errors on them (D8021, not just a
# warning) rather than ignoring them. Found the same way as plugin/CMakeLists.txt's MSVC guard: the
# Windows build failing in CI, on this core library the plugin links against.
if(MSVC)
  set(OAR_WARNING_FLAGS /W4)
else()
  set(OAR_WARNING_FLAGS -Wall -Wextra -Wpedantic)
endif()

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
target_compile_options(openautoroute_core PRIVATE ${OAR_WARNING_FLAGS})
set_target_properties(openautoroute_core PROPERTIES POSITION_INDEPENDENT_CODE ON)  # linked into the plugin's shared library

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE openautoroute_core)
target_compile_options(test_core PRIVATE ${OAR_WARNING_FLAGS})
add_test(NAME core COMMAND test_core)

# Bulk validation tool: parses every cell in a directory tree (see tools/s57_scan.cpp).
add_executable(s57_scan tools/s57_scan.cpp)
target_link_libraries(s57_scan PRIVATE openautoroute_core)

# Shared release version: the CLI tool reports it via --version, and the OpenCPN plugin (below) reports it to OpenCPN's
# Plugin Manager. A release build sets it from the tag; local builds default to "dev".
set(OAR_VERSION "dev" CACHE STRING "Version reported by openautoroute --version and by the OpenCPN plugin")

# Command-line router: ENC cells in, GPX route out (import into OpenCPN to check it).
add_executable(openautoroute tools/openautoroute.cpp)
target_compile_definitions(openautoroute PRIVATE OAR_VERSION="${OAR_VERSION}")
target_link_libraries(openautoroute PRIVATE openautoroute_core)

# Optional OpenCPN plugin front end (needs OpenCPN's ocpn_plugin.h and wxWidgets 3.2; see plugin/CMakeLists.txt).
option(OAR_BUILD_PLUGIN "Build the OpenCPN plugin" OFF)
if(OAR_BUILD_PLUGIN)
  add_subdirectory(plugin)
endif()
