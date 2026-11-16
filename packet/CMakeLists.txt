# FoxSDR "packet" GPL helper: Dire Wolf 1.8.2 receive path (AX.25, FX.25, IL2P) behind the FoxSDR helper pipe protocol.
# Licence: GPL-2.0-or-later (see LICENSE, UPSTREAM.md).
#
#   cmake -S packet -B <build dir> -G "Visual Studio 17 2022" -A x64
#   cmake --build <build dir> --config Release --parallel 4
#   ctest --test-dir <build dir> -C Release --output-on-failure
cmake_minimum_required(VERSION 3.15)
project(packet_helper C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# static C runtime: the helper runs on a PC without the Visual C++ redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(UP ${CMAKE_CURRENT_SOURCE_DIR}/upstream)
set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(COMPAT ${CMAKE_CURRENT_SOURCE_DIR}/compat)

# --- manifest embedded in the program (--foxsdr-helper-info prints exactly packet.helper.json) -------------------
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/packet.helper.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${MANIFEST})
file(READ ${MANIFEST} MANIFEST_TEXT)
string(REPLACE "\\" "\\\\" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\"" "\\\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
string(REPLACE "\n" "\\n\" \\\n\"" MANIFEST_TEXT "${MANIFEST_TEXT}")
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gen/helper_manifest.h
     "/* generated from packet.helper.json by CMakeLists.txt */\n#define HELPER_MANIFEST_JSON \"${MANIFEST_TEXT}\"\n")

# Dire Wolf's receive chain: the sources atest.c links, minus the APRS decoder, GPS and serial code it never calls.
set(RX_SOURCES
  ${UP}/ais.c ${UP}/demod.c ${UP}/demod_afsk.c ${UP}/demod_psk.c ${UP}/demod_9600.c ${UP}/dsp.c
  ${UP}/fx25_extract.c ${UP}/fx25_encode.c ${UP}/fx25_init.c ${UP}/fx25_rec.c
  ${UP}/hdlc_rec.c ${UP}/hdlc_rec2.c
  ${UP}/il2p_codec.c ${UP}/il2p_scramble.c ${UP}/il2p_rec.c ${UP}/il2p_payload.c ${UP}/il2p_init.c ${UP}/il2p_header.c
  ${UP}/multi_modem.c ${UP}/rrbb.c ${UP}/fcs_calc.c ${UP}/ax25_pad.c ${UP}/ax25_pad2.c ${UP}/dtime_now.c
  ${UP}/misc/strsep.c ${UP}/misc/strtok_r.c ${UP}/misc/strcasestr.c ${UP}/misc/strlcpy.c ${UP}/misc/strlcat.c)

function(packet_target_setup tgt)
  target_include_directories(${tgt} PRIVATE ${UP} ${COMPAT} ${CMAKE_CURRENT_BINARY_DIR}/gen)
  target_compile_definitions(${tgt} PRIVATE _USE_MATH_DEFINES PACKET_HELPER=1 MAJOR_VERSION=1 MINOR_VERSION=8 PATCH_VERSION=2)
  if(WIN32)
    target_compile_definitions(${tgt} PRIVATE __WIN32__=1 _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_DEPRECATE)
  endif()
  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W3 /utf-8 /FI${COMPAT}/dw_compat.h)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -include ${COMPAT}/dw_compat.h)
    target_link_libraries(${tgt} PRIVATE m)
  endif()
endfunction()

# --- the helper -----------------------------------------------------------------------------------------------
add_executable(packet-helper ${SRC}/packet_main.c ${SRC}/dw_console.c ${RX_SOURCES})
packet_target_setup(packet-helper)

# --- gen_packets: Dire Wolf's own signal generator, built as a TEST TOOL (it writes a WAV file; it never transmits) --
option(PACKET_TEST_TOOLS "Build gen_packets, the upstream test signal generator" ON)
if(PACKET_TEST_TOOLS)
  add_executable(gen_packets
    ${UP}/gen_packets.c ${UP}/gen_tone.c ${UP}/hdlc_send.c ${UP}/fx25_send.c ${UP}/il2p_send.c ${UP}/morse.c ${UP}/dtmf.c
    ${UP}/dsp.c ${UP}/fcs_calc.c ${UP}/ax25_pad.c ${UP}/ax25_pad2.c
    ${UP}/fx25_encode.c ${UP}/fx25_extract.c ${UP}/fx25_init.c
    ${UP}/il2p_codec.c ${UP}/il2p_scramble.c ${UP}/il2p_payload.c ${UP}/il2p_init.c ${UP}/il2p_header.c
    ${SRC}/dw_console.c
    ${UP}/misc/strsep.c ${UP}/misc/strtok_r.c ${UP}/misc/strcasestr.c ${UP}/misc/strlcpy.c ${UP}/misc/strlcat.c)
  packet_target_setup(gen_packets)
  target_compile_definitions(gen_packets PRIVATE DW_CONSOLE_DEFAULT_VERBOSE=1)
  if(WIN32)
    target_sources(gen_packets PRIVATE ${COMPAT}/getopt_win.c)
  endif()
endif()

# --- gen_damaged: the same generator with a damage shim (tools/gen_damage_shim.c) in front of the FX.25 and IL2P senders;
#     rawtool: WAV to raw samples, noise, feed in small writes.  Both are test tools only.
if(PACKET_TEST_TOOLS)
  add_library(dmg_send_objs OBJECT ${UP}/fx25_send.c ${UP}/il2p_send.c)
  packet_target_setup(dmg_send_objs)
  target_compile_definitions(dmg_send_objs PRIVATE tone_gen_put_bit=dmg_put_bit fx25_send_frame=real_fx25_send_frame il2p_send_frame=real_il2p_send_frame)
  add_executable(gen_damaged
    ${UP}/gen_packets.c ${UP}/gen_tone.c ${UP}/hdlc_send.c ${UP}/morse.c ${UP}/dtmf.c
    ${UP}/dsp.c ${UP}/fcs_calc.c ${UP}/ax25_pad.c ${UP}/ax25_pad2.c
    ${UP}/fx25_encode.c ${UP}/fx25_extract.c ${UP}/fx25_init.c
    ${UP}/il2p_codec.c ${UP}/il2p_scramble.c ${UP}/il2p_payload.c ${UP}/il2p_init.c ${UP}/il2p_header.c
    ${SRC}/dw_console.c ${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_damage_shim.c $<TARGET_OBJECTS:dmg_send_objs>
    ${UP}/misc/strsep.c ${UP}/misc/strtok_r.c ${UP}/misc/strcasestr.c ${UP}/misc/strlcpy.c ${UP}/misc/strlcat.c)
  packet_target_setup(gen_damaged)
  target_compile_definitions(gen_damaged PRIVATE DW_CONSOLE_DEFAULT_VERBOSE=1)
  if(WIN32)
    target_sources(gen_damaged PRIVATE ${COMPAT}/getopt_win.c)
  endif()
  add_executable(rawtool tools/rawtool.c)
  target_compile_definitions(rawtool PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

include(CTest)
if(BUILD_TESTING AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test/CMakeLists.txt)
  add_subdirectory(test)
endif()
