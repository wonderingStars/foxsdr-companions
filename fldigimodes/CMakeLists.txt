# FoxSDR "fldigimodes" GPL helper: fldigi's Olivia, Contestia, MFSK, MT63 and THOR receivers and RSID detector behind
# the FoxSDR helper pipe protocol (version 1). Receive only.
# Licence: GPL-3.0-only (see LICENSE, UPSTREAM.md).
#
#   cmake -S fldigimodes -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4 --target fldigimodes
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(fldigimodes_helper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(UP ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(SHIM ${CMAKE_CURRENT_SOURCE_DIR}/shim)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly fldigimodes.helper.json) ---------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/fldigimodes.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from fldigimodes.helper.json by CMakeLists.txt */
#define HELPER_MANIFEST_JSON R\"FMJSON(${MANIFEST_TEXT})FMJSON\"
")

# fldigi's own sources, receive side
set(FLDIGI_CORE
  ${UP}/filters/filters.cxx ${UP}/filters/viterbi.cxx ${UP}/filters/fftfilt.cxx ${UP}/misc/misc.cxx
  ${UP}/mfsk/interleave.cxx ${UP}/mfsk/mfskvaricode.cxx ${UP}/mfsk/mfsk.cxx
  ${UP}/mt63/dsp.cxx ${UP}/mt63/mt63base.cxx
  ${UP}/thor/thor.cxx ${UP}/thor/thorvaricode.cxx ${UP}/thor/dominovar.cxx)

set(HELPER_SOURCES
  ${SRC}/helper_main.cpp ${SRC}/modes.cpp ${SRC}/factory.cpp ${SRC}/fm_runtime.cpp ${SRC}/olivia_decoder.cpp ${SRC}/mfsk_decoder.cpp ${SRC}/thor_decoder.cpp ${SRC}/mt63_decoder.cpp ${SRC}/rsid_detector.cpp
  ${FLDIGI_CORE})

function(fm_target_setup T)
  # the shim directory comes first so that fldigi's "modem.h", "globals.h", "config.h" ... are ours
  target_include_directories(${T} PRIVATE ${SHIM} ${UP}/include ${UP}/include/jalocha ${UP}/rsid ${SRC} ${CMAKE_CURRENT_BINARY_DIR}/gen)
  target_compile_definitions(${T} PRIVATE _USE_MATH_DEFINES PACKAGE_VERSION="foxsdr-helper" __restrict__=__restrict)
  if(WIN32)
    target_compile_definitions(${T} PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS NOMINMAX)
  endif()
  if(MSVC)
    target_compile_options(${T} PRIVATE /W3 /utf-8 /wd4244 /wd4267 /wd4305 /wd4996 /wd4018)
  else()
    target_compile_options(${T} PRIVATE -Wall -Wno-unused -Wno-sign-compare)
  endif()
endfunction()

add_executable(fldigimodes ${HELPER_SOURCES})
fm_target_setup(fldigimodes)
target_compile_definitions(fldigimodes PRIVATE HELPER_UPSTREAM="fldigi 4.1.23 61b97f4133c4")

# --- test tool: the ONLY place the transmitter classes are compiled in (FOXSDR_TEST_TX) ----------------------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
