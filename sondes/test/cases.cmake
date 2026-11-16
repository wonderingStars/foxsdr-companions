# CTest cases for the sondes helper (included from ../CMakeLists.txt). See run_case.cmake for the modes.
# Pinned outputs: test/expected/<case>.jsonl; rewrite them after an intended change with SONDES_UPDATE=1.

set(TE ${CMAKE_CURRENT_SOURCE_DIR}/test/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/test/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/testwork)
set(MK2A_DATA ${FOXSDR_SAMPLES_DIR}/sondes/Mk2a_9600.bytes)

# sg_case(<name> <mode> [RATE hz] [GEN a b c] [ARGS more helper args] [INPUTS case names] [EXPECT file] [EXTRA -Dvar=value])
function(sg_case NAME MODE)
  cmake_parse_arguments(C "NOCHECK" "RATE;EXPECT" "GEN;ARGS;INPUTS;EXTRA" ${ARGN})
  if(NOT C_RATE)
    set(C_RATE 48000)
  endif()
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  set(all --foxsdr-helper 1 --format s16 --rate ${C_RATE} ${C_ARGS})
  string(REPLACE ";" "|" a "${all}")
  string(REPLACE ";" "|" g "${C_GEN}")
  string(REPLACE ";" "|" i "${C_INPUTS}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:sondes> -DGENTOOL=$<TARGET_FILE:sondes-gen>
      "-DARGS=${a}" "-DGEN_ARGS=${g}" "-DINPUTS=${i}" -DWORK=${WORKDIR} -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  if(C_NOCHECK)
    list(APPEND defs -DNOCHECK=1)
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

# --- one signal per decoder, made by sondes-gen from the upstream's constants (Mk IIa: from real recorded subframes) ---
sg_case(lms6            signal GEN lms6 --frames 6)
sg_case(lms6_noise      signal GEN lms6 --frames 6 --noise 0.12)
sg_case(lms6_inverted   signal GEN lms6 --frames 4 --invert)
sg_case(lms6_96k        signal RATE 96000 GEN lms6 --frames 4 --rate 96000)
if(EXISTS ${MK2A_DATA})
sg_case(mk2a_real_96k   signal RATE 96000 GEN mk2a --data ${MK2A_DATA} --frames 8 --baud 9616 --rate 96000)
sg_case(mk2a_real_48k   signal GEN mk2a --data ${MK2A_DATA} --frames 8 --baud 9616)
endif()
sg_case(mts01           signal GEN mts01 --frames 5)
sg_case(cf06            signal GEN cf06 --frames 5)
sg_case(mrz             signal GEN mrz --frames 20)

# --- everything in one file, one decoder only, other input paths ------------------------------------------------------
if(EXISTS ${MK2A_DATA})
sg_case(mixed           mixed INPUTS lms6 mts01 cf06 mrz mk2a_real_48k)
sg_case(mixed_lms6only  mixed NOCHECK INPUTS lms6 mts01 cf06 mrz mk2a_real_48k ARGS --types lms6)
sg_case(lms6_f32        f32 EXPECT lms6 GEN lms6 --frames 6)
sg_case(lms6_chunk      chunk GEN lms6 --frames 6)
sg_case(cf06_chunk       chunk GEN cf06 --frames 5)
set_tests_properties(mixed mixed_lms6only PROPERTIES DEPENDS "lms6;mts01;cf06;mrz;mk2a_real_48k")
endif()

# --- failure paths ---------------------------------------------------------------------------------------------------
sg_case(empty           empty)
sg_case(garbage         garbage EXTRA -DGARBAGE_BYTES=1920000 -DMAX_LINES=8)
add_test(NAME bad_format COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=bad_format -DHELPER=$<TARGET_FILE:sondes>
         "-DARGS=--foxsdr-helper|1|--format|cs16|--rate|48000" -DWORK=${WORKDIR} "-DSTDERR_REGEX=unsupported --format" -P ${RUN})
add_test(NAME bad_rate COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=bad_rate -DHELPER=$<TARGET_FILE:sondes>
         "-DARGS=--foxsdr-helper|1|--format|s16|--rate|8000" -DWORK=${WORKDIR} "-DSTDERR_REGEX=outside" -P ${RUN})
add_test(NAME bad_protocol COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=bad_protocol -DHELPER=$<TARGET_FILE:sondes>
         "-DARGS=--format|s16|--rate|48000" -DWORK=${WORKDIR} "-DSTDERR_REGEX=protocol version 1" -P ${RUN})
add_test(NAME bad_ephemeris COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=bad_ephemeris -DHELPER=$<TARGET_FILE:sondes>
         "-DARGS=--foxsdr-helper|1|--format|s16|--rate|48000|--types|rs92|--rs92-ephemeris|${CMAKE_CURRENT_BINARY_DIR}/no-such-file.n"
         -DWORK=${WORKDIR} "-DSTDERR_REGEX=cannot open the --rs92-ephemeris" -P ${RUN})
add_test(NAME bad_type COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=bad_type -DHELPER=$<TARGET_FILE:sondes>
         "-DARGS=--foxsdr-helper|1|--format|s16|--rate|48000|--types|nosuch" -DWORK=${WORKDIR} "-DSTDERR_REGEX=unknown decoder" -P ${RUN})

# --- protocol surface ------------------------------------------------------------------------------------------------
add_test(NAME info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=info -DHELPER=$<TARGET_FILE:sondes> -DWORK=${WORKDIR}
         -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/sondes.helper.json -P ${RUN})
add_test(NAME hello COMMAND ${CMAKE_COMMAND} -DMODE=hello -DNAME=hello -DHELPER=$<TARGET_FILE:sondes> -DWORK=${WORKDIR}
         "-DARGS=--foxsdr-helper|1|--format|s16|--rate|48000"
         "-DMUST_CONTAIN=sondes|protocol|GPL-3.0-only|github.com/rs1729/RS"
         -P ${RUN})
