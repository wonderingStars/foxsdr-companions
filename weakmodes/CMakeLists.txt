# FoxSDR "weakmodes" GPL helper: FT2L, MSK144 and JT9 receive-only decoders (ports of WSJT-X / wsjtx-ft2libre code)
# behind the FoxSDR helper pipe protocol version 1. Licence: GPL-3.0-or-later (see LICENSE, UPSTREAM.md).
#
#   cmake -S weakmodes -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(weakmodes_helper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly weakmodes.helper.json) ---------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/weakmodes.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\n" "\\n\" \\\n\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from weakmodes.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

file(GLOB WM_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(weakmodes-helper ${WM_SOURCES})
target_include_directories(weakmodes-helper PRIVATE src ${CMAKE_CURRENT_BINARY_DIR}/gen)
if(WIN32)
  target_compile_definitions(weakmodes-helper PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
endif()
if(MSVC)
  target_compile_options(weakmodes-helper PRIVATE /W3 /utf-8 /EHsc)
else()
  target_compile_options(weakmodes-helper PRIVATE -Wall -O2)
endif()

# --- test tools: generators ported from the same upstream (they only write WAV files), and wmtool ----------------
# A generator may use code in src/ (the encoders) or in tools/; each tool lists what it needs here.
function(wm_tool NAME)
  add_executable(${NAME} ${ARGN})
  target_include_directories(${NAME} PRIVATE src tools)
  if(WIN32)
    target_compile_definitions(${NAME} PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
  endif()
  if(MSVC)
    target_compile_options(${NAME} PRIVATE /W3 /utf-8 /EHsc)
  endif()
endfunction()
wm_tool(wmtool tools/wmtool.cpp)
wm_tool(fft_test tools/fft_test.cpp src/wm_fft.cpp)
wm_tool(ldpc_test tools/ldpc_test.cpp src/ldpc174_91.cpp src/wm_fft.cpp)
wm_tool(unpack77_test tools/unpack77_test.cpp src/unpack77.cpp)
wm_tool(ft2l_gen tools/ft2l_gen.cpp src/wm_fft.cpp src/ft2_wave.cpp src/ldpc174_91.cpp)
wm_tool(msk144_gen tools/msk144_gen.cpp src/wm_fft.cpp)
wm_tool(jt9_gen tools/jt9_gen.cpp src/wm_fft.cpp)

enable_testing()
add_test(NAME pack77_roundtrip COMMAND unpack77_test)
add_test(NAME fft_vs_dft COMMAND fft_test)
add_test(NAME ldpc174_91 COMMAND ldpc_test)
add_subdirectory(test)
