# hfmodes: FoxSDR GPL helper, HF telegraphy decoders (audio in, JSON Lines out). GPL-3.0-or-later.
# Build:  cmake -S hfmodes -B <build> -G "Visual Studio 17 2022" -A x64 && cmake --build <build> --config Release --target hfmodes hfmodes_gen --parallel 4
# Test:   ctest --test-dir <build> -C Release
cmake_minimum_required(VERSION 3.20)
project(hfmodes LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ONAIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/onair)
set(HFM_DECODER_SOURCES
    ${ONAIR}/src/hfutil/hfu_fsk.cpp
    ${ONAIR}/src/cishf/cishf_dsp.cpp
    ${ONAIR}/src/cishf/cishf_gen.cpp
    ${ONAIR}/src/marine/marine_dsp.cpp
    ${ONAIR}/src/marine/marine_navtex.cpp
    ${ONAIR}/src/sitor/sitor_arq.cpp
    src/dsp_compat.cpp
    src/frontend.cpp
    src/crowd36.cpp
    src/cis3650.cpp)

add_library(hfmodes_core STATIC ${HFM_DECODER_SOURCES})
target_include_directories(hfmodes_core PUBLIC ${ONAIR}/include src)
if(MSVC)
  # the upstream code is written for GCC/Clang; these only silence its warnings, they change no code
  target_compile_options(hfmodes_core PUBLIC /W3 /utf-8 /permissive- /Zc:__cplusplus)
  target_compile_definitions(hfmodes_core PUBLIC _USE_MATH_DEFINES NOMINMAX _CRT_SECURE_NO_WARNINGS)
  # __builtin_popcount in hfu_fsk.cpp
  target_compile_options(hfmodes_core PUBLIC /FI${CMAKE_CURRENT_SOURCE_DIR}/src/msvc_compat.h)
endif()

add_executable(hfmodes src/main.cpp)
target_link_libraries(hfmodes PRIVATE hfmodes_core)

add_executable(hfmodes_gen EXCLUDE_FROM_ALL tests/gen.cpp)
target_link_libraries(hfmodes_gen PRIVATE hfmodes_core)

add_executable(hfmodes_unit EXCLUDE_FROM_ALL tests/unit.cpp)
target_link_libraries(hfmodes_unit PRIVATE hfmodes_core)

# ---- tests ---------------------------------------------------------------------------------------------------------------------------
enable_testing()
set(HFM_SIG ${CMAKE_CURRENT_BINARY_DIR}/signals)
file(MAKE_DIRECTORY ${HFM_SIG})

# every case: generator name, audio rate, modes passed to the helper (the helper also runs all modes in the "all" test)
set(HFM_CASES
    "arqe|48000|ARQ-E|THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345"
    "arqn|44100|ARQ-N|THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345"
    "autospec|48000|AUTOSPEC|THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345"
    "spread|48000|SPREAD|THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345"
    "gtor|48000|G-TOR|G-TOR test frame at the given speed, with a CRC check."
    "efr|48000|EFR|EFR telegram ok: message 37, address 12 / 34, 3 data bytes: 414243"
    "piccolo|48000|PICCOLO-MK6|THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345"
    "coquelet8|48000|COQUELET-8|THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345"
    "coquelet13|48000|COQUELET-13|THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 12345"
    "bee|12000|CIS-36-50-ONAIR|FILE"
    "cis3650|11025|CIS-36-50|FILE"
    "cis14|12000|CIS-14|14-bit frame found"
    "cis12|12000|CIS-12|12 carriers found, pilot at 3299.7 Hz"
    "cis36|12000|CIS-36|CIS-36 (MFSK-36): 33 tones at 40.3 Hz spacing, about 20 Bd"
    "hdr|12000|CIS-OFDM-HDR|CIS-45 (40 Bd): 46 carriers"
    "c128|12000|CIS-128|CIS-128: 121 carriers"
    "crowd36|8000|CROWD36|THE QUICK BROWN FOX 1234 JUMPS OVER THE LAZY DOG"
    "sitorb|48000|SITOR-B|CQ CQ CQ DE TEST 1234 NAVTEX FEC BROADCAST"
    "sitora|48000|SITOR-A|CQ DE TEST HELLO WORLD 1234")

add_test(NAME build_tools COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --config $<CONFIG> --target hfmodes_gen hfmodes_unit)
set_tests_properties(build_tools PROPERTIES FIXTURES_SETUP tools)

add_test(NAME unit COMMAND hfmodes_unit)
set_tests_properties(unit PROPERTIES FIXTURES_REQUIRED tools)

foreach(c IN LISTS HFM_CASES)
  string(REPLACE "|" ";" c "${c}")
  list(GET c 0 name)
  list(GET c 1 rate)
  list(GET c 2 modes)
  list(GET c 3 want)
  set(wantArgs -DWANT=${want})
  if(want STREQUAL "FILE")
    set(wantArgs -DWANT_FILE=${HFM_SIG}/${name}.raw.truth)
  endif()
  add_test(NAME gen_${name} COMMAND hfmodes_gen --case ${name} --rate ${rate} --out ${HFM_SIG}/${name}.raw)
  set_tests_properties(gen_${name} PROPERTIES FIXTURES_REQUIRED tools FIXTURES_SETUP sig_${name})
  add_test(NAME run_${name}
           COMMAND ${CMAKE_COMMAND} -DHELPER=$<TARGET_FILE:hfmodes> -DINPUT=${HFM_SIG}/${name}.raw -DRATE=${rate} -DMODES=${modes}
                   ${wantArgs} -DEXPECT=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/${name}.jsonl -DACTUAL=${HFM_SIG}/${name}.out.jsonl
                   -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_case.cmake)
  set_tests_properties(run_${name} PROPERTIES FIXTURES_REQUIRED sig_${name})
endforeach()

# every decoder at once (no --modes) on the EFR signal: the combined output is pinned too
add_test(NAME run_all_modes_efr
         COMMAND ${CMAKE_COMMAND} -DHELPER=$<TARGET_FILE:hfmodes> -DINPUT=${HFM_SIG}/efr.raw -DRATE=48000 -DMODES=
                 -DWANT=EFR\ telegram\ ok -DEXPECT=${CMAKE_CURRENT_SOURCE_DIR}/tests/expected/all_modes_efr.jsonl -DACTUAL=${HFM_SIG}/all_modes_efr.out.jsonl
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_case.cmake)
set_tests_properties(run_all_modes_efr PROPERTIES FIXTURES_REQUIRED sig_efr)

# a real recording, where FOXSDR_SAMPLES_DIR holds one (optional)
add_test(NAME real_sitorb_tfc COMMAND ${CMAKE_COMMAND} -DHELPER=$<TARGET_FILE:hfmodes> -DSAMPLE=${FOXSDR_SAMPLES_DIR}/navtex-dsc-decoder/sitorb-tfc/SITOR-B_tfc.mp3
                 -DWORK=${HFM_SIG} -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/real_sitorb.cmake)
set_tests_properties(real_sitorb_tfc PROPERTIES SKIP_RETURN_CODE 77)

# failure paths
add_test(NAME fail_paths COMMAND ${CMAKE_COMMAND} -DHELPER=$<TARGET_FILE:hfmodes> -DSIG=${HFM_SIG} -DGEN=$<TARGET_FILE:hfmodes_gen>
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/fail_paths.cmake)
set_tests_properties(fail_paths PROPERTIES FIXTURES_REQUIRED tools)
add_test(NAME manifest COMMAND ${CMAKE_COMMAND} -DHELPER=$<TARGET_FILE:hfmodes> -DMANIFEST=${CMAKE_CURRENT_SOURCE_DIR}/hfmodes.helper.json
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/manifest.cmake)
