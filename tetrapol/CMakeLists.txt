# FoxSDR "tetrapol" GPL helper: the control-channel signalling decoder of tetrapol-kit (airphel/tetrapol-kit-2023,
# GPL-2.0) behind the FoxSDR helper pipe protocol (version 1). Signalling only: no voice, no data content, no keys.
# Licence: GPL-2.0-only (see LICENSE, UPSTREAM.md).
#
#   cmake -S tetrapol -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4 --target tetrapol-helper
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(tetrapol_helper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(UP ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/compat)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly tetrapol.helper.json) -----------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/tetrapol.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\n" "\\n\" \\\n\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from tetrapol.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

# --- the library part: upstream files, as taken (see UPSTREAM.md for the patches) --------------------------------
# Left out: tch.c, lsdu_vch.c, lsdu_cd.c (traffic), tsdu_print.c, frame_json.c, tsdu_json.c (raw dumps), tetrapol_dump.
set(TP_SOURCES
  ${UP}/addr.c ${UP}/bch.c ${UP}/bit_utils.c ${UP}/cch.c ${UP}/data_frame.c ${UP}/frame.c ${UP}/hdlc_frame.c
  ${UP}/link.c ${UP}/log.c ${UP}/misc.c ${UP}/msg_coding.c ${UP}/pch.c ${UP}/phys_ch.c ${UP}/rch.c ${UP}/sdch.c
  ${UP}/terminal.c ${UP}/tetrapol.c ${UP}/tp_timer.c ${UP}/tpdu.c ${UP}/tsdu.c)
add_library(tetrapol_lib STATIC ${TP_SOURCES} ${SRC}/helper_out.c)
target_include_directories(tetrapol_lib PUBLIC ${UP} ${SRC} ${COMPAT})
if(MSVC)
  target_compile_options(tetrapol_lib PRIVATE /W3 /utf-8 /wd4200 /wd4244 /wd4267 /wd4996 /wd4018)
  target_compile_definitions(tetrapol_lib PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
else()
  target_compile_options(tetrapol_lib PRIVATE -Wall)
endif()

add_executable(tetrapol-helper ${SRC}/helper_main.c ${SRC}/gmsk.c)
target_link_libraries(tetrapol-helper PRIVATE tetrapol_lib)
target_include_directories(tetrapol-helper PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen)
if(MSVC)
  target_compile_options(tetrapol-helper PRIVATE /W3 /utf-8)
  target_compile_definitions(tetrapol-helper PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
else()
  target_link_libraries(tetrapol-helper PRIVATE m)
endif()

# --- tests ----------------------------------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
