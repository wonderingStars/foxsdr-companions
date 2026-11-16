# FoxSDR "weakmodes2" GPL helper: FST4, FST4W and Q65 receiver (pipe protocol version 1).
# Licence: GPL-3.0-or-later (see LICENSE, UPSTREAM.md).
#
#   cmake -S weakmodes2 -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4 --target weakmodes2-helper wm2gen
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(weakmodes2_helper C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(UP ${CMAKE_CURRENT_SOURCE_DIR}/upstream)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly weakmodes2.helper.json) ---------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/weakmodes2.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/wm2_manifest.h
     "/* generated from weakmodes2.helper.json by CMakeLists.txt */
#define WM2_MANIFEST_JSON R\"WM2JSON(${MANIFEST_TEXT})WM2JSON\"
")

# --- the decoder library: OnAir's FST4 / Q65 code (upstream/) and the small compatibility headers (compat/) ----------
add_library(wm2core STATIC
  ${UP}/hfdig/hfdig_fst4.cpp ${UP}/hfdig/hfdig_q65.cpp ${UP}/hfdig/hfdig_q65_tables.cpp
  ${UP}/hfdig/hfdig_wsjt.cpp ${UP}/hfdig/hfdig_wsjt_tables.cpp
  ${UP}/hfdig/hfdig_ftx_msg.cpp ${UP}/hfdig/hfdig_ftx_tables.cpp ${UP}/hfdig/hfdig_wspr.cpp
  src/wm2_q65ff.cpp
  ${UP}/wsjtx_q65/q65.c ${UP}/wsjtx_q65/qracodes.c ${UP}/wsjtx_q65/npfwht.c ${UP}/wsjtx_q65/pdmath.c ${UP}/wsjtx_q65/qra15_65_64_irr_e23.c)
target_include_directories(wm2core PUBLIC ${UP}/hfdig ${UP}/include ${CMAKE_CURRENT_SOURCE_DIR}/compat ${CMAKE_CURRENT_SOURCE_DIR}/src ${UP}/wsjtx_q65)
target_compile_definitions(wm2core PUBLIC _USE_MATH_DEFINES)
if(MSVC)
  target_compile_options(wm2core PRIVATE /W3 /utf-8 /O2)
  target_compile_definitions(wm2core PUBLIC _CRT_SECURE_NO_WARNINGS NOMINMAX)
  # GCC builtins used by the upstream C++ files (force-included for those only: the C files must not see C++ headers)
  set_source_files_properties(${UP}/hfdig/hfdig_wsjt.cpp ${UP}/hfdig/hfdig_ftx_tables.cpp PROPERTIES COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/compat/wm2_msvc.h")
else()
  target_compile_options(wm2core PRIVATE -O2 -Wall)
endif()

add_executable(weakmodes2-helper src/wm2_main.cpp)
target_link_libraries(weakmodes2-helper PRIVATE wm2core)
target_include_directories(weakmodes2-helper PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
if(NOT MSVC)
  find_package(Threads REQUIRED)
  target_link_libraries(weakmodes2-helper PRIVATE Threads::Threads)
endif()

# --- test tools (not part of the helper) ----------------------------------------------------------------------------
add_executable(wm2gen test/tools/wm2gen.cpp)
target_link_libraries(wm2gen PRIVATE wm2core)

enable_testing()
add_subdirectory(test)
