# FoxSDR "js8" GPL helper: the JS8Call receive side (JS8 Normal, Fast, Turbo, Slow) behind the FoxSDR helper pipe
# protocol (version 1). Licence: GPL-3.0-only (see LICENSE, UPSTREAM.md).
#
#   cmake -S js8 -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(js8_helper CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly js8.helper.json) ---------------------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/js8.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\n" "\\n\" \\\n\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from js8.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

# --- the decoder: JS8Call's JS8.cpp, varicode and the JSC dictionary, without Qt / Boost / Eigen / FFTW ---------------
add_library(js8core STATIC
  ${SRC}/js8_decoder.cpp
  ${SRC}/js8_varicode.cpp
  ${SRC}/fftw_shim.cpp
  ${SRC}/jsc_map.cpp)
target_include_directories(js8core PUBLIC ${SRC})
if(MSVC)
  target_compile_options(js8core PRIVATE /W3 /utf-8 /EHsc /O2 /bigobj)
else()
  target_compile_options(js8core PRIVATE -Wall -O2)
endif()

# --- the helper ------------------------------------------------------------------------------------------------------
add_executable(js8-helper ${SRC}/helper_main.cpp)
target_link_libraries(js8-helper PRIVATE js8core)
target_include_directories(js8-helper PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
if(MSVC)
  target_compile_options(js8-helper PRIVATE /W3 /utf-8 /EHsc)
  target_compile_definitions(js8-helper PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- the test tool: writes JS8 audio files; not part of the helper -------------------------------------------------
add_executable(js8gen tools/js8gen.cpp)
target_link_libraries(js8gen PRIVATE js8core)
if(MSVC)
  target_compile_options(js8gen PRIVATE /W3 /utf-8 /EHsc)
  target_compile_definitions(js8gen PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

enable_testing()
add_subdirectory(test)
