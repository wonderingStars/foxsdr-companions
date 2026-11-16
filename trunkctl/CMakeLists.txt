# FoxSDR "trunkctl" GPL helper: trunking control channels (Motorola SmartNet/SmartZone Type II, EDACS) behind the
# FoxSDR helper pipe protocol (version 1). Licence: GPL-3.0-only (see LICENSE, UPSTREAM.md).
#
#   cmake -S trunkctl -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4 --target trunkctl
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(trunkctl_helper CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly trunkctl.helper.json) -------------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/trunkctl.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(ASCII 13 CR)
string(REPLACE "${CR}" "" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from trunkctl.helper.json by CMakeLists.txt */\nstatic const char HELPER_MANIFEST_JSON[] = R\"TCM(${MANIFEST_TEXT})TCM\";\n")

add_executable(trunkctl
  ${SRC}/main.cpp ${SRC}/smartnet.cpp ${SRC}/edacs.cpp ${SRC}/bch4028.cpp ${SRC}/slicer.cpp ${SRC}/emitter.cpp)
target_include_directories(trunkctl PRIVATE ${SRC} ${CMAKE_CURRENT_BINARY_DIR}/gen)
if(MSVC)
  target_compile_options(trunkctl PRIVATE /W3 /utf-8)
  target_compile_definitions(trunkctl PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(trunkctl PRIVATE -Wall -Wextra)
endif()

# --- test signal generator (not shipped) and tests ---------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_executable(trunkgen test/tools/trunkgen.cpp ${SRC}/bch4028.cpp)
  add_executable(trunkctl_sigtool test/tools/sigtool.cpp)
  set_target_properties(trunkctl_sigtool PROPERTIES OUTPUT_NAME sigtool) # unique target name in the joined build; file name unchanged
  if(MSVC)
    target_compile_options(trunkgen PRIVATE /W3 /utf-8)
    target_compile_definitions(trunkgen PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(trunkctl_sigtool PRIVATE _CRT_SECURE_NO_WARNINGS)
  endif()
  add_subdirectory(test)
endif()
