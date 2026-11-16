# FoxSDR "sondes" GPL helper: radiosonde decoders from rs1729/RS (GPL-3.0) behind the FoxSDR helper pipe protocol.
# Licence: GNU GPL v3 (see LICENSE, UPSTREAM.md).
#
#   cmake -S sondes -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4 --target sondes sondes-gen
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(sondes_helper CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(UP  ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly sondes.helper.json) -----------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/sondes.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REGEX REPLACE "[ \t\r\n]+" " " MANIFEST_TEXT "${MANIFEST_TEXT}")
string(STRIP "${MANIFEST_TEXT}" MANIFEST_TEXT)
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from sondes.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

# --- the upstream decoders: C compiled as C++ with the glue header force-included -------------------------------
# Every decoder file has a main(); each is renamed so that all can live in one program.
function(sondes_decoder file entry)
  set(extra ${ARGN})
  set_source_files_properties(${UP}/${file} PROPERTIES LANGUAGE CXX
      COMPILE_OPTIONS "/FI${SRC}/sondes_glue.h"
      COMPILE_DEFINITIONS "main=${entry};${extra}")
endfunction()

# the same renames for the self-contained Mk IIa file, which carries its own copy of the demodulator
set(MK2A_RENAMES
  read_wav_header=mk2a_read_wav_header f32buf_sample=mk2a_f32buf_sample read_slbit=mk2a_read_slbit
  read_softbit=mk2a_read_softbit read_softbit2p=mk2a_read_softbit2p init_buffers=mk2a_init_buffers
  free_buffers=mk2a_free_buffers find_header=mk2a_find_header f32soft_read=mk2a_f32soft_read
  find_binhead=mk2a_find_binhead find_softbinhead=mk2a_find_softbinhead
  dsp_t=mk2a_dsp_t pcm_t=mk2a_pcm_t hsbit_t=mk2a_hsbit_t dft_t=mk2a_dft_t hdb_t=mk2a_hdb_t iq_dc_t=mk2a_iq_dc_t)

sondes_decoder(lms6Xmod.c lms6_main)
sondes_decoder(mp3h1mod.c mrz_main)
sondes_decoder(mts01mod.c mts01_main)
sondes_decoder(cf06ht03mod.c cf06_main)
sondes_decoder(rs92mod.c rs92_main)
sondes_decoder(mk2a1680mod.c mk2a_main ${MK2A_RENAMES})

set(DECODER_SOURCES
  ${UP}/lms6Xmod.c ${UP}/mp3h1mod.c ${UP}/mts01mod.c ${UP}/cf06ht03mod.c ${UP}/rs92mod.c ${UP}/mk2a1680mod.c
  ${UP}/demod_mod.c ${UP}/bch_ecc_mod.c)
set_source_files_properties(${UP}/demod_mod.c ${UP}/bch_ecc_mod.c PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "/FI${SRC}/sondes_glue.h")

add_executable(sondes ${SRC}/helper_main.cpp ${SRC}/sondes_glue.cpp ${DECODER_SOURCES})
target_include_directories(sondes PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gen ${UP} ${SRC})
if(MSVC)
  target_compile_options(sondes PRIVATE /O2 /W1 /wd4996 /wd4244 /wd4267 /wd4305 /wd4018 /wd4101 /wd4100 /EHsc /Zc:strictStrings-)
  target_compile_definitions(sondes PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
endif()
set_source_files_properties(${SRC}/helper_main.cpp ${SRC}/sondes_glue.cpp PROPERTIES COMPILE_OPTIONS "")

# --- test signal generator and checker (own code; uses upstream's Reed-Solomon encoder) -------------------------
add_executable(sondes-gen test/gen.cpp ${UP}/bch_ecc_mod.c)
target_include_directories(sondes-gen PRIVATE ${UP} ${SRC})
if(MSVC)
  target_compile_options(sondes-gen PRIVATE /O2 /W1 /wd4996 /wd4244 /wd4267 /wd4305 /wd4018 /EHsc /Zc:strictStrings-)
  target_compile_definitions(sondes-gen PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# --- tests (CTest): signals from sondes-gen, pinned expectations in test/expected/*.jsonl -----------------------
enable_testing()
include(${CMAKE_CURRENT_SOURCE_DIR}/test/cases.cmake)
