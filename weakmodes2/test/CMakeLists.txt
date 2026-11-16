# Tests for the weakmodes2 helper. Every case runs run_case.cmake (see its header for the modes). Pinned expectations are in
# expected/*.jsonl; after an intended change run   MM_UPDATE=1 ctest ...   and review the git diff.
# Generated signals: wm2gen (test tool, seeded noise). Real signals: the WSJT-X sample recordings, if present in ${FOXSDR_SAMPLES_DIR}/weakmodes2.

set(TD ${FOXSDR_SAMPLES_DIR}/weakmodes2)
set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(PROTO --foxsdr-helper 1 --format s16 --rate 12000)

# wm_case(<name> <mode> [INPUT <file in ${FOXSDR_SAMPLES_DIR}/weakmodes2>] [GEN <wm2gen args>...] [ARGS <helper args>...] [PATTERN <regex>] [RAWPROTO])
function(wm_case NAME MODE)
  cmake_parse_arguments(C "RAWPROTO" "INPUT;PATTERN" "ARGS;GEN" ${ARGN})
  if(C_INPUT AND NOT EXISTS "${C_INPUT}")
    return() # upstream sample not present (see README.md, FOXSDR_SAMPLES_DIR)
  endif()
  if(C_RAWPROTO)
    set(all ${C_ARGS})
  else()
    set(all ${PROTO} ${C_ARGS})
  endif()
  string(REPLACE ";" "|" a "${all}")
  string(REPLACE ";" "|" g "${C_GEN}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:weakmodes2-helper> "-DARGS=${a}"
      -DGEN=$<TARGET_FILE:wm2gen> "-DGENARGS=${g}" -DWORK=${WORKDIR} -DEXPECTED=${TE}/${NAME}.jsonl
      -DMANIFEST=${PROJECT_SOURCE_DIR}/weakmodes2.helper.json "-DPATTERN=${C_PATTERN}")
  if(C_INPUT)
    list(APPEND defs -DINPUT=${C_INPUT})
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} -P ${RUN})
endfunction()

set(Q1 "CQ K1ABC FN42")
set(Q2 "K9AN G4WJS RR73")
set(W1 "K1ABC FN42 37")

# --- the manifest ------------------------------------------------------------------------------------------------
wm_case(info info RAWPROTO)

# --- real recordings: WSJT-X's own sample files (decoded by the real WSJT-X, the stations are real) --------------------
wm_case(real_fst4_60        compare INPUT ${TD}/wsjtx_fst4_60_210115_0058.s16 ARGS --decoders fst4:60)
wm_case(real_q65_60A_eme    compare INPUT ${TD}/wsjtx_q65_60A_210106_1621.s16 ARGS --decoders q65:60A)
wm_case(real_q65_120D       compare INPUT ${TD}/wsjtx_q65_120D_210117_0920.s16 ARGS --decoders q65:120D)
wm_case(real_q65_60D_eme    compare INPUT ${TD}/wsjtx_q65_60D_201212_1838.s16 ARGS --decoders q65:60D)
# spread signals: these only decode through the fast-fading pass (WSJT-X's q65_intrinsics_fastfading)
wm_case(real_q65_60B_spread compare INPUT ${TD}/wsjtx_q65_60B_210109_0007.s16 ARGS --decoders q65:60B)
wm_case(real_q65_120E_spread compare INPUT ${TD}/wsjtx_q65_120E_210130_1442.s16 ARGS --decoders q65:120E)
wm_case(real_twice          twice   INPUT ${TD}/wsjtx_q65_60A_210106_1621.s16 ARGS --decoders q65:60A)

# --- generated: clean (well above the threshold) and noisy (near the sensitivity limit measured in README.md) -----------
wm_case(fst4_60_clean   compare GEN --mode fst4 --period 60 --sig "${Q1}@1500@0@0.2" --seed 11 ARGS --decoders fst4:60)
wm_case(fst4_60_noisy   compare GEN --mode fst4 --period 60 --sig "${Q1}@1500@-24@0.2" --seed 11 ARGS --decoders fst4:60)
wm_case(fst4_15_clean   compare GEN --mode fst4 --period 15 --sig "${Q1}@1200@-10@0.1" --seed 12 ARGS --decoders fst4:15)
wm_case(fst4_30_clean   compare GEN --mode fst4 --period 30 --sig "${Q1}@1200@-15@0.1" --seed 13 ARGS --decoders fst4:30)
wm_case(fst4_120_noisy  compare GEN --mode fst4 --period 120 --sig "${Q1}@1500@-28@0.2" --seed 14 ARGS --decoders fst4:120)
wm_case(fst4_60_f32     compare GEN --mode fst4 --period 60 --f32 --sig "${Q1}@1500@-20@0.2" --seed 31 ARGS --format f32 --decoders fst4:60)
wm_case(fst4w_120_clean compare GEN --mode fst4w --period 120 --sig "${W1}@1500@-10@0.2" --seed 15 ARGS --decoders fst4w:120)
wm_case(fst4w_120_noisy compare GEN --mode fst4w --period 120 --sig "${W1}@1500@-29@0.2" --seed 15 ARGS --decoders fst4w:120)
wm_case(q65_60A_clean   compare GEN --mode q65 --period 60 --sub A --sig "${Q1}@1500@-5@0.2" --seed 16 ARGS --decoders q65:60A)
wm_case(q65_60A_noisy   compare GEN --mode q65 --period 60 --sub A --sig "${Q1}@1500@-24@0.2" --seed 16 ARGS --decoders q65:60A)
wm_case(q65_60B_clean   compare GEN --mode q65 --period 60 --sub B --sig "${Q2}@1000@-15@0.2" --seed 17 ARGS --decoders q65:60B)
wm_case(q65_30C_clean   compare GEN --mode q65 --period 30 --sub C --sig "${Q2}@800@-12@0.2" --seed 18 ARGS --decoders q65:30C)
wm_case(q65_15A_clean   compare GEN --mode q65 --period 15 --sub A --sig "${Q1}@1500@-12@0.1" --seed 19 ARGS --decoders q65:15A)
wm_case(q65_wrongsub    grep    GEN --mode q65 --period 60 --sub A --sig "${Q1}@1500@-5@0.2" --seed 16 ARGS --decoders q65:60C PATTERN "\"decodes\":0\}")

# --- several signals, several slots, several decoders at once ---------------------------------------------------------
wm_case(fst4_60_two_signals compare GEN --mode fst4 --period 60 --sig "${Q1}@900@-12@0.1" --sig "K9AN G4WJS RR73@1900@-12@-0.2" --seed 21 ARGS --decoders fst4:60)
wm_case(fst4_60_three_slots compare GEN --mode fst4 --period 60 --sig "${Q1}@1500@-18@0.2" --slots 3 --seed 22 ARGS --decoders fst4:60)
wm_case(q65_60A_two_signals compare GEN --mode q65 --period 60 --sub A --sig "${Q1}@900@-14@0.1" --sig "${Q2}@1900@-14@-0.2" --seed 23 ARGS --decoders q65:60A)
wm_case(three_decoders      compare GEN --mode fst4 --period 60 --sig "${Q1}@1500@-10@0.2" --slots 2 --seed 24 ARGS --decoders fst4:60,q65:60A,fst4w:60)

# --- the slot clock: the stream starts 20 s before a UTC minute; the decode must carry that minute as its UTC ---------------
wm_case(utc_slots compare GEN --mode fst4 --period 60 --sig "${Q1}@1500@-10@0.2" --slots 2 --lead 20 --seed 25
        ARGS --decoders fst4:60 --start-utc 1699999960)
# 40 s of the stream belong to a slot that began before the stream: it is zero filled and nothing is lost in the next slot
wm_case(slot_offset compare GEN --mode fst4 --period 60 --sig "${Q1}@1500@-10@0.2" --slots 2 --lead 20 --seed 25
        ARGS --decoders fst4:60 --slot-offset 40)

# --- failure paths -------------------------------------------------------------------------------------------------
wm_case(fail_format_iq    fail RAWPROTO ARGS --foxsdr-helper 1 --format cs16 --rate 12000 PATTERN "format")
wm_case(fail_format_junk  fail RAWPROTO ARGS --foxsdr-helper 1 --format u8 --rate 12000 PATTERN "format")
wm_case(fail_rate_low     fail RAWPROTO ARGS --foxsdr-helper 1 --format s16 --rate 7000 PATTERN "rate")
wm_case(fail_rate_missing fail RAWPROTO ARGS --foxsdr-helper 1 --format s16 PATTERN "rate")
wm_case(fail_protocol     fail RAWPROTO ARGS --foxsdr-helper 2 --format s16 --rate 12000 PATTERN "protocol")
wm_case(fail_no_protocol  fail RAWPROTO ARGS --format s16 --rate 12000 PATTERN "foxsdr-helper")
wm_case(fail_decoder      fail ARGS --decoders fst4:61 PATTERN "period")
wm_case(fail_mode         fail ARGS --decoders ft8:15 PATTERN "unknown mode")
wm_case(fail_q65_sub      fail ARGS --decoders q65:60 PATTERN "sub-mode")
wm_case(fail_option       fail ARGS --bogus PATTERN "unknown option")
wm_case(empty_input       empty ARGS --decoders fst4:60,q65:60A)
wm_case(garbage_s16       garbage GEN --garbage 700000 --seed 3 ARGS --decoders fst4:15,q65:15A,fst4w:30)
wm_case(garbage_f32       garbage GEN --garbage 1400000 --seed 4 ARGS --format f32 --decoders fst4:15,q65:15A)
# a recording that ends in the middle of a slot (the transmission is whole, the rest of the slot is missing): decoded, marked partial
wm_case(short_recording compare GEN --mode fst4 --period 60 --sig "${Q1}@1500@-10@0.2" --trunc 55 --seed 26 ARGS --decoders fst4:60)
# ... and one that ends before the transmission is complete: a status line, no decode
wm_case(too_short grep GEN --mode fst4 --period 60 --sig "${Q1}@1500@-10@0.2" --trunc 30 --seed 26 ARGS --decoders fst4:60 PATTERN "too short for a transmission")

# --- false alarms: noise only, every decoder family (CRC-24 / CRC-12 and the Q65 likelihood test must reject everything) -------------
wm_case(noise_only_q65  grep GEN --mode q65 --period 60 --slots 6 --seed 99 ARGS --decoders q65:60A,q65:60B,q65:60D PATTERN "\"decodes\":0\}")
wm_case(noise_only_fst4 grep GEN --mode fst4 --period 60 --slots 6 --seed 98 ARGS --decoders fst4:60,fst4w:60 PATTERN "\"decodes\":0\}")
