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
target_compile_definitions(openautoroute_pi PRIVATE ${wxWidgets_DEFINITIONS})
target_compile_options(openautoroute_pi PRIVATE -Wall -Wextra -fvisibility=hidden)
target_link_libraries(openautoroute_pi PRIVATE openautoroute_core ${wxWidgets_LIBRARIES})
set_target_properties(openautoroute_pi PROPERTIES PREFIX "lib" POSITION_INDEPENDENT_CODE ON)
# Linux/BSD only: the plugin's own symbols (create_pi, destroy_pi) are exported by DECL_EXP; everything else stays private.
install(TARGETS openautoroute_pi LIBRARY DESTINATION lib/opencpn)
