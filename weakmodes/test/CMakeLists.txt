# Tests for the weakmodes helper. Every case runs run_case.cmake (see its header for the modes).
# Pinned expectations are in expected/*.jsonl; to rewrite them after an intended change run
#   WM_UPDATE=1 ctest ... and review the git diff.
set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(PROTO --foxsdr-helper 1 --format s16 --rate 12000)

# wm_case(<name> <mode> [INPUT <file>] [ARGS <helper args>...] [GENEXE <tgt>] [GEN <args>...] [EXTRA <-Dvar=value>...])
function(wm_case NAME MODE)
  cmake_parse_arguments(C "" "INPUT;EXPECT;GENEXE" "ARGS;GEN;EXTRA" ${ARGN})
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  set(all ${PROTO} ${C_ARGS})
  string(REPLACE ";" "|" a "${all}")
  string(REPLACE ";" "|" g "${C_GEN}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:weakmodes-helper> "-DARGS=${a}"
      -DWMTOOL=$<TARGET_FILE:wmtool> -DWORK=${WORKDIR} -DEXPECTED=${TE}/${C_EXPECT}.jsonl "-DGEN=${g}")
  if(C_GENEXE)
    list(APPEND defs -DGENEXE=$<TARGET_FILE:${C_GENEXE}>)
  endif()
  if(C_INPUT)
    list(APPEND defs -DINPUT=${C_INPUT})
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

# --- protocol / failure paths (no decoder needed) ------------------------------------------------------------------
wm_case(info   info  EXTRA -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../weakmodes.helper.json)
wm_case(empty  empty)
wm_case(hello  hello EXTRA "-DMUST_CONTAIN=\"helper\":\"weakmodes\"|\"protocol\":1|\"licence\":\"GPL-3.0-or-later\"|reverse-engineered")
wm_case(garbage garbage EXTRA -DGARBAGE_BYTES=200000 -DMAX_LINES=40)
wm_case(fail_format fail ARGS --format\;cf32 EXTRA "-DSTDERR_REGEX=--format must be s16 or f32")
wm_case(fail_rate   fail ARGS --rate\;8000 EXTRA "-DSTDERR_REGEX=--rate must be 12000")

include(ft2l.cmake)
include(msk144.cmake)
include(jt9.cmake)

# --- the three modes at once (the program default), and a stray byte at the end of the input -------------------------
if(EXISTS ${FOXSDR_SAMPLES_DIR}/weakmodes/msk144_181211_120800.raw)
  wm_case(all_modes  compare INPUT ${FOXSDR_SAMPLES_DIR}/weakmodes/msk144_181211_120800.raw)
  wm_case(oddbyte    oddbyte  INPUT ${FOXSDR_SAMPLES_DIR}/weakmodes/msk144_181211_120800.raw EXPECT msk144_real_2 ARGS --mode msk144)
endif()

# garbage long enough for the minute-long JT9 period and for many MSK144 blocks (no crash, no flood of lines)
wm_case(garbage_jt9    garbage ARGS --mode jt9    EXTRA -DGARBAGE_BYTES=1500000 -DMAX_LINES=40)
wm_case(garbage_msk144 garbage ARGS --mode msk144 EXTRA -DGARBAGE_BYTES=1500000 -DMAX_LINES=40)
