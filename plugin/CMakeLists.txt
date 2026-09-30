# OpenCPN plugin front end for the open-autoroute core. Off by default; configure with
#   cmake -S . -B build -DOAR_BUILD_PLUGIN=ON -DOCPN_INCLUDE_DIR=/path/to/OpenCPN/include
# where OCPN_INCLUDE_DIR holds ocpn_plugin.h from the OpenCPN release you run (it needs wxWidgets 3.2 GTK3 development files).
if(NOT OCPN_INCLUDE_DIR)
  message(FATAL_ERROR "Set -DOCPN_INCLUDE_DIR to the folder holding OpenCPN's ocpn_plugin.h")
endif()

find_package(wxWidgets 3.2 REQUIRED COMPONENTS core base gl)
include(${wxWidgets_USE_FILE})

add_library(openautoroute_pi MODULE openautoroute_pi.cpp)
target_include_directories(openautoroute_pi PRIVATE ${OCPN_INCLUDE_DIR})
target_compile_definitions(openautoroute_pi PRIVATE ${wxWidgets_DEFINITIONS} OAR_VERSION="${OAR_VERSION}")
target_link_libraries(openautoroute_pi PRIVATE openautoroute_core ${wxWidgets_LIBRARIES})
set_target_properties(openautoroute_pi PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(openautoroute_pi PRIVATE /W4)
  # Unlike Linux's ELF .so (and macOS's .dylib with -undefined dynamic_lookup), a Windows DLL has no
  # "resolve this from whatever process loads me" mechanism: every opencpn_plugin_120 base-class method,
  # InsertPlugInTool, PlugIn_Waypoint, GetHostApi and friends are implemented inside OpenCPN.exe itself,
  # and MSVC's linker needs an actual import library for them, not just the header declaring them.
  # OpenCPN publishes exactly this as a prebuilt artifact: https://github.com/OpenCPN/opencpn-libs.
  # All its api-NN/msvc-wx32/opencpn.lib files are built from the same OpenCPN 5.11 commit but export
  # progressively larger symbol sets as NN increases; api-20 (matching this plugin's own
  # opencpn_plugin_120 base class) was missing GetHostApi/HostApi121, so api-22 is used instead for its
  # larger export set. Found the same way as the other platform fixes: the Windows build actually
  # failing in CI with a wall of unresolved externals once it got past compiling.
  if(NOT OCPN_API_LIB)
    message(FATAL_ERROR "Set -DOCPN_API_LIB to opencpn-libs/api-22/msvc-wx32/opencpn.lib")
  endif()
  target_link_libraries(openautoroute_pi PRIVATE "${OCPN_API_LIB}")
else()
  target_compile_options(openautoroute_pi PRIVATE -Wall -Wextra -fvisibility=hidden)
  # Windows DLLs are never "lib"-prefixed by convention (and CMake's own MODULE default already
  # omits it there); only Linux/BSD and macOS .dylibs use the prefix.
  set_target_properties(openautoroute_pi PROPERTIES PREFIX "lib")
endif()
if(APPLE)
  # opencpn_plugin_120, PlugIn_Waypoint_ExV2 and friends are base classes OpenCPN's own executable
  # implements, not this header-only SDK -- a plugin is only ever dlopen()'d by a running OpenCPN,
  # which resolves them then. Linux's ELF .so permits that unresolved at link time by default;
  # macOS's ld does not unless told to expect it. Found by the macOS build actually failing in CI.
  target_link_options(openautoroute_pi PRIVATE -undefined dynamic_lookup)
  # CMake's MODULE library type defaults to a .so suffix even on Apple (only SHARED targets default
  # to .dylib there) -- but OpenCPN's own macOS plugin layout expects a real .dylib (see package_macos.sh
  # and https://github.com/leamas/opencpn/wiki/Tarballs). Found the same way: the build succeeded but
  # produced libopenautoroute_pi.so, which the packaging step then couldn't find as a .dylib.
  set_target_properties(openautoroute_pi PROPERTIES SUFFIX ".dylib")
endif()
# Linux/BSD only: the plugin's own symbols (create_pi, destroy_pi) are exported by DECL_EXP; everything else stays private.
install(TARGETS openautoroute_pi LIBRARY DESTINATION lib/opencpn)
