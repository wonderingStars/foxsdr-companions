# FoxSDR "mdc" GPL helper: MDC-1200 and Fleetsync I decoders (Matthew Kaufman's libraries) behind the FoxSDR helper
# pipe protocol (version 1). Licence: GPL-2.0-only (see LICENSE, UPSTREAM.md).
#
#   cmake -S mdc -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(mdc_helper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(UP ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly mdc.helper.json) --------------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/mdc.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\n" "\\n\" \\\n\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from mdc.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

# --- the helper ------------------------------------------------------------------------------------------------
add_executable(mdc-helper ${SRC}/main.c ${UP}/mdc_decode.c ${UP}/fsync_decode.c)
target_include_directories(mdc-helper PRIVATE ${UP} ${SRC} ${CMAKE_CURRENT_BINARY_DIR}/gen)
if(WIN32)
  target_compile_definitions(mdc-helper PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
  target_compile_options(mdc-helper PRIVATE /W3 /utf-8 /wd4305)  # 4305: upstream float tables written as doubles
else()
  target_compile_options(mdc-helper PRIVATE -Wall)
  target_link_libraries(mdc-helper PRIVATE m)
endif()

# --- test tools (not shipped): signal generator and Kaufman's own round-trip self test ----------------------------
add_executable(mdc-gen test/tools/mdc_gen.c ${UP}/mdc_encode.c)
target_include_directories(mdc-gen PRIVATE ${UP})
add_executable(mdc-upstream-selftest ${UP}/mdc_test.c ${UP}/mdc_decode.c ${UP}/mdc_encode.c)
target_include_directories(mdc-upstream-selftest PRIVATE ${UP})
if(WIN32)
  target_compile_definitions(mdc-gen PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(mdc-upstream-selftest PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
  target_compile_options(mdc-gen PRIVATE /W3 /utf-8)
  target_compile_options(mdc-upstream-selftest PRIVATE /W3 /utf-8)
else()
  target_link_libraries(mdc-gen PRIVATE m)
  target_link_libraries(mdc-upstream-selftest PRIVATE m)
endif()

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
