# FoxSDR "rtl433" GPL helper: rtl_433 decoders behind the FoxSDR helper pipe protocol (version 1).
# Licence: GPL-2.0-or-later (see LICENSE, UPSTREAM.md).
#
#   cmake -S rtl433 -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4 --target rtl433-helper
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(rtl433_helper C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly rtl433.helper.json) ------------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/rtl433.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REPLACE "\r" "" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\n" "\\n\" \\\n\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from rtl433.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

# --- the helper -----------------------------------------------------------------------------------------------
file(GLOB CORE_SOURCES ${SRC}/*.c)
file(GLOB DEVICE_SOURCES ${SRC}/devices/*.c)

add_executable(rtl433-helper ${CORE_SOURCES} ${DEVICE_SOURCES})
target_include_directories(rtl433-helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}/gen)
# No threads, no TLS, no radio drivers, no network: none of rtl_433's optional libraries are used.
target_compile_definitions(rtl433-helper PRIVATE _USE_MATH_DEFINES)
if(WIN32)
  target_compile_definitions(rtl433-helper PRIVATE _CRT_NONSTDC_NO_DEPRECATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
  target_link_libraries(rtl433-helper PRIVATE bcrypt)
endif()
if(MSVC)
  # as upstream's own MSVC build: no warnings for type truncation
  target_compile_options(rtl433-helper PRIVATE /W3 /utf-8 /wd4244 /wd4267 /wd4305)
else()
  target_compile_options(rtl433-helper PRIVATE -Wall)
  target_link_libraries(rtl433-helper PRIVATE m)
endif()

# --- test tool: shift / decimate a wideband recording down to the rate the decoders are tuned for (not shipped) --
add_executable(rtl433-iqshift ${CMAKE_CURRENT_SOURCE_DIR}/tools/iqshift.c)
if(MSVC)
  target_compile_definitions(rtl433-iqshift PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(rtl433-iqshift PRIVATE /W3 /O2 /fp:fast)
else()
  target_compile_options(rtl433-iqshift PRIVATE -Wall -O2)
  target_link_libraries(rtl433-iqshift PRIVATE m)
endif()

# --- tests ----------------------------------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
