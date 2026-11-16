# FoxSDR "multimon" GPL helper: multimon-ng 1.6.2 decoders behind the FoxSDR helper pipe protocol (version 1).
# Licence: GPL-2.0-or-later (see LICENSE, UPSTREAM.md).
#
#   cmake -S multimon -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4 --target multimon-helper
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(multimon_helper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(UP ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly multimon.helper.json) ----------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/multimon.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\n" "\\n\" \\\n\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from multimon.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

# --- the helper -----------------------------------------------------------------------------------------------
set(DEMOD_SOURCES
  ${UP}/pocsag.c ${UP}/selcall.c ${UP}/hdlc.c ${UP}/uart.c ${UP}/clip.c ${UP}/fms.c
  ${UP}/demod_zvei1.c ${UP}/demod_zvei2.c ${UP}/demod_zvei3.c ${UP}/demod_pzvei.c ${UP}/demod_dzvei.c
  ${UP}/demod_ccir.c ${UP}/demod_eia.c ${UP}/demod_eea.c ${UP}/demod_ufsk12.c
  ${UP}/demod_poc24.c ${UP}/demod_poc12.c ${UP}/demod_poc5.c ${UP}/demod_hapn48.c ${UP}/demod_fsk96.c
  ${UP}/demod_dtmf.c ${UP}/demod_dualtone.c ${UP}/demod_clipfsk.c ${UP}/demod_fmsfsk.c
  ${UP}/demod_afsk24.c ${UP}/demod_afsk24_3.c ${UP}/demod_afsk24_2.c ${UP}/demod_afsk12.c
  ${UP}/demod_flex.c ${UP}/demod_flex_next.c ${UP}/demod_gsc.c ${UP}/demod_eas.c ${UP}/demod_morse.c
  ${UP}/demod_x10.c ${UP}/costabi.c ${UP}/costabf.c ${UP}/bch.c ${UP}/cJSON.c)

add_executable(multimon-helper ${SRC}/helper_main.c ${SRC}/helper_events.c ${DEMOD_SOURCES})
target_include_directories(multimon-helper PRIVATE ${UP} ${SRC} ${CMAKE_CURRENT_BINARY_DIR}/gen)
# Input is standard input only: no audio device, no X11, no SDL (the same switches multimon-ng's own CMake uses).
target_compile_definitions(multimon-helper PRIVATE
  ONLY_RAW DUMMY_AUDIO NO_X11 NO_SDL3 CHARSET_UTF8 MAX_VERBOSE_LEVEL=3 _USE_MATH_DEFINES)
if(WIN32)
  target_compile_definitions(multimon-helper PRIVATE WINDOWS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()
if(MSVC)
  target_compile_options(multimon-helper PRIVATE /W3 /utf-8)
  # FLEX and FLEX_NEXT stamp lines with time(NULL): give them the helper's sample-count clock instead.
  set_source_files_properties(${UP}/demod_flex.c ${UP}/demod_flex_next.c
    PROPERTIES COMPILE_OPTIONS "/FIhelper_compat.h")
  # demod_flex_next.c uses typeof(), which MSVC only accepts in its C23 mode.
  set_property(SOURCE ${UP}/demod_flex_next.c APPEND PROPERTY COMPILE_OPTIONS "/std:clatest")
else()
  target_compile_options(multimon-helper PRIVATE -Wall)
  set_source_files_properties(${UP}/demod_flex.c ${UP}/demod_flex_next.c
    PROPERTIES COMPILE_OPTIONS "-include;helper_compat.h")
  target_link_libraries(multimon-helper PRIVATE m)
endif()

# --- gen-ng: multimon-ng's own test signal generator, used only to make test input (not shipped) -----------------
add_executable(gen-ng
  ${UP}/gen.c ${UP}/gen_dtmf.c ${UP}/gen_sin.c ${UP}/gen_zvei.c ${UP}/gen_hdlc.c ${UP}/gen_uart.c
  ${UP}/gen_clipfsk.c ${UP}/gen_flex.c ${UP}/gen_pocsag.c ${UP}/gen_scope.c ${UP}/costabi.c ${UP}/bch.c)
target_include_directories(gen-ng PRIVATE ${UP} ${CMAKE_CURRENT_SOURCE_DIR}/test/tools/compat)
target_compile_definitions(gen-ng PRIVATE MAX_VERBOSE_LEVEL=1 DUMMY_AUDIO ONLY_RAW CHARSET_UTF8 _USE_MATH_DEFINES)
if(WIN32)
  target_compile_definitions(gen-ng PRIVATE WINDOWS _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
  target_sources(gen-ng PRIVATE ${UP}/win32_getopt.c)
endif()
if(MSVC)
  target_compile_options(gen-ng PRIVATE /W3 /utf-8)
else()
  target_link_libraries(gen-ng PRIVATE m)
endif()

# --- tests ----------------------------------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(test)
endif()
