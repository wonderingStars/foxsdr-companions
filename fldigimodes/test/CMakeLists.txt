# Tests for the fldigimodes helper. Every case runs run_case.cmake (see its header for the modes).
# The test audio is made by sigtool, which compiles fldigi's own TRANSMITTERS (FOXSDR_TEST_TX): fldigi's modem makes the
# signal, the helper (receive code only) must read the text back. Pinned expectations are in expected/*.jsonl; to
# rewrite them after an intended change run
#   MM_UPDATE=1 ctest ... and review the git diff.
# Noise: sigtool gen --snr <dB> is signal power over noise power in 2500 Hz, white noise over the whole audio band,
# seeded (--seed) so the signal file is the same every run.

# fldigi's receivers again, this time with the transmitters (FOXSDR_TEST_TX): used only to make test audio.
add_executable(fldigimodes_sigtool sigtool.cpp gen_modes.cpp ${SRC}/modes.cpp ${SRC}/fm_runtime.cpp ${SRC}/rsid_detector.cpp
               ${FLDIGI_CORE})
set_target_properties(fldigimodes_sigtool PROPERTIES OUTPUT_NAME sigtool) # unique target name in the joined build; file name unchanged
fm_target_setup(fldigimodes_sigtool)
target_compile_definitions(fldigimodes_sigtool PRIVATE FOXSDR_TEST_TX)

set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)

# fm_case(<name> <mode> <helper --mode> <rate> GEN <sigtool gen args...> [ARGS <more helper args>] [EXTRA <-Dvar=value>...]
#         [EXPECT <file>])   the first GEN item is the sigtool mode name
function(fm_case NAME MODE HMODE RATE)
  cmake_parse_arguments(C "" "EXPECT" "GEN;ARGS;EXTRA" ${ARGN})
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  set(all --foxsdr-helper 1 --format s16 --rate ${RATE} --mode ${HMODE} ${C_ARGS})
  string(REPLACE ";" "|" a "${all}")
  string(REPLACE ";" "|" g "${C_GEN}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:fldigimodes> "-DARGS=${a}"
      -DSIGTOOL=$<TARGET_FILE:fldigimodes_sigtool> "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/work" -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  if(C_GEN)
    list(APPEND defs "-DGEN=${g}")
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

set(MSG "CQ DE FOXSDR 73")

# --- clean signals from fldigi's transmitters: the decoded text is pinned (with times and quality fields) ---------------
fm_case(olivia_8_500    compare olivia-8-500    8000 GEN olivia-8-500    --text ${MSG})
fm_case(olivia_4_250    compare olivia-4-250    8000 GEN olivia-4-250    --text ${MSG})
fm_case(olivia_16_500   compare olivia-16-500   8000 GEN olivia-16-500   --text ${MSG})
fm_case(olivia_32_1000  compare olivia-32-1000  8000 GEN olivia-32-1000  --text ${MSG})
fm_case(olivia_64_2000  compare olivia-64-2000  8000 GEN olivia-64-2000  --text ${MSG})
fm_case(contestia_8_500   compare contestia-8-500   8000 GEN contestia-8-500   --text ${MSG})
fm_case(contestia_4_250   compare contestia-4-250   8000 GEN contestia-4-250   --text ${MSG})
fm_case(contestia_16_1000 compare contestia-16-1000 8000 GEN contestia-16-1000 --text ${MSG})
fm_case(mfsk4    compare mfsk4    8000  GEN mfsk4    --text ${MSG})
fm_case(mfsk8    compare mfsk8    8000  GEN mfsk8    --text ${MSG})
fm_case(mfsk11   compare mfsk11   11025 GEN mfsk11   --text ${MSG})
fm_case(mfsk16   compare mfsk16   8000  GEN mfsk16   --text ${MSG})
fm_case(mfsk22   compare mfsk22   11025 GEN mfsk22   --text ${MSG})
fm_case(mfsk31   compare mfsk31   8000  GEN mfsk31   --text ${MSG})
fm_case(mfsk32   compare mfsk32   8000  GEN mfsk32   --text ${MSG})
fm_case(mfsk64   compare mfsk64   8000  GEN mfsk64   --text ${MSG})
fm_case(mfsk128  compare mfsk128  8000  GEN mfsk128  --text ${MSG})
fm_case(mt63_500s   compare mt63-500s  8000 GEN mt63-500s  --text ${MSG} --tail 8)
fm_case(mt63_500l   compare mt63-500l  8000 GEN mt63-500l  --text ${MSG} --tail 8)
fm_case(mt63_1000s  compare mt63-1000s 8000 GEN mt63-1000s --text ${MSG} --tail 8)
fm_case(mt63_1000l  compare mt63-1000l 8000 GEN mt63-1000l --text ${MSG} --tail 8)
fm_case(mt63_2000s  compare mt63-2000s 8000 GEN mt63-2000s --text ${MSG} --tail 8)
fm_case(mt63_2000l  compare mt63-2000l 8000 GEN mt63-2000l --text ${MSG} --tail 8)
fm_case(thormicro compare thormicro 8000  GEN thormicro --text ${MSG} --tail 8)
fm_case(thor4     compare thor4     8000  GEN thor4     --text ${MSG} --tail 8)
fm_case(thor5     compare thor5     11025 GEN thor5     --text ${MSG} --tail 8)
fm_case(thor8     compare thor8     8000  GEN thor8     --text ${MSG} --tail 8)
fm_case(thor11    compare thor11    11025 GEN thor11    --text ${MSG} --tail 8)
fm_case(thor16    compare thor16    8000  GEN thor16    --text ${MSG} --tail 8)
fm_case(thor22    compare thor22    11025 GEN thor22    --text ${MSG} --tail 8)
fm_case(thor25x4  compare thor25x4  8000  GEN thor25x4  --text ${MSG} --tail 8)
fm_case(thor50x1  compare thor50x1  8000  GEN thor50x1  --text ${MSG} --tail 8)
fm_case(thor50x2  compare thor50x2  8000  GEN thor50x2  --text ${MSG} --tail 8)
fm_case(thor100   compare thor100   8000  GEN thor100   --text ${MSG} --tail 8)

# --- the same text with 8-bit characters, a reversed sideband and another carrier frequency ---------------------------
# characters above 127 (here the byte E9, an e acute) go out as UTF-8, read as ISO-8859-1
fm_case(olivia_8bit  compare olivia-8-500 8000 GEN olivia-8-500 --hex 436166e920)
fm_case(olivia_reverse contains olivia-8-500 8000 GEN olivia-8-500 --text ${MSG} --reverse ARGS --reverse EXTRA "-DMESSAGE=${MSG}")
fm_case(olivia_freq800 contains olivia-8-500 8000 GEN olivia-8-500 --text ${MSG} --freq 800 ARGS --freq 800 EXTRA "-DMESSAGE=${MSG}")
fm_case(mfsk16_freq2200 contains mfsk16 8000 GEN mfsk16 --text ${MSG} --freq 2200 ARGS --freq 2200 EXTRA "-DMESSAGE=${MSG}")
fm_case(mfsk16_reverse contains mfsk16 8000 GEN mfsk16 --text ${MSG} --reverse ARGS --reverse EXTRA "-DMESSAGE=${MSG}")
fm_case(thor16_reverse contains thor16 8000 GEN thor16 --text ${MSG} --tail 8 --reverse ARGS --reverse EXTRA "-DMESSAGE=${MSG}")
fm_case(mt63_1000s_freq1200 contains mt63-1000s 8000 GEN mt63-1000s --text ${MSG} --tail 8 --freq 1200 ARGS --freq 1200 EXTRA "-DMESSAGE=${MSG}")

# --- with noise (white, seeded): the message must still decode at the level given ---------------------------------------
# (The lowest levels at which each mode decodes are in README.md; these cases sit a few dB above them.)
include(${CMAKE_CURRENT_SOURCE_DIR}/noise_cases.cmake OPTIONAL)

# --- the same signal through other doors: float samples, a pipe in small writes, a cut-off sample ---------------------
fm_case(f32_olivia_8_500 f32   olivia-8-500 8000 GEN olivia-8-500 --text ${MSG} EXPECT olivia_8_500)
fm_case(f32_mfsk16       f32   mfsk16       8000 GEN mfsk16       --text ${MSG} EXPECT mfsk16)
fm_case(f32_mt63_1000s   f32   mt63-1000s   8000 GEN mt63-1000s   --text ${MSG} --tail 8 EXPECT mt63_1000s)
fm_case(f32_thor16       f32   thor16       8000 GEN thor16       --text ${MSG} --tail 8 EXPECT thor16)
fm_case(pipe_olivia_8_500 pipe olivia-8-500 8000 GEN olivia-8-500 --text ${MSG} EXPECT olivia_8_500 EXTRA -DCHUNK=1000)
fm_case(pipe_mfsk16       pipe mfsk16       8000 GEN mfsk16       --text ${MSG} EXPECT mfsk16 EXTRA -DCHUNK=333)
fm_case(pipe_mt63_1000s   pipe mt63-1000s   8000 GEN mt63-1000s   --text ${MSG} --tail 8 EXPECT mt63_1000s EXTRA -DCHUNK=4097)
fm_case(pipe_thor16       pipe thor16       8000 GEN thor16       --text ${MSG} --tail 8 EXPECT thor16 EXTRA -DCHUNK=1)
fm_case(twice_olivia_8_500 twice olivia-8-500 8000 GEN olivia-8-500 --text ${MSG} EXPECT olivia_8_500)
fm_case(twice_mt63_500l    twice mt63-500l    8000 GEN mt63-500l    --text ${MSG} --tail 8 EXPECT mt63_500l)
fm_case(oddbyte_mfsk16 oddbyte mfsk16 8000 GEN mfsk16 --text ${MSG} EXPECT mfsk16)

# --- a signal of one mode must not decode as another ----------------------------------------------------------------
fm_case(cross_olivia_as_contestia absent contestia-8-500 8000 GEN olivia-8-500 --text ${MSG} EXTRA "-DMESSAGE=${MSG}")
fm_case(cross_mfsk16_as_olivia    absent olivia-8-500    8000 GEN mfsk16       --text ${MSG} EXTRA "-DMESSAGE=${MSG}")
fm_case(cross_thor16_as_mfsk16    absent mfsk16          8000 GEN thor16       --text ${MSG} --tail 8 EXTRA "-DMESSAGE=${MSG}")

# --- RSID: found alongside any mode, in the first table and (after ESCAPE) the second --------------------------------
fm_case(rsid_mfsk16        rsid rsid 8000 GEN mfsk16 --rsid MFSK16 --rsid-only
        EXTRA "-DMUST_CONTAIN=\"type\":\"rsid\",\"name\":\"MFSK16\"|\"mode\":\"MFSK16\"|\"extended\":false" -DMAX_RSID=1)
fm_case(rsid_olivia_8_500  rsid rsid 8000 GEN mfsk16 --rsid OLIVIA_8_500 --rsid-only
        EXTRA "-DMUST_CONTAIN=\"name\":\"OLIVIA_8_500\"|\"mode\":\"OLIVIA-8/500\"" -DMAX_RSID=1)
fm_case(rsid_contestia_16_1000 rsid rsid 8000 GEN mfsk16 --rsid CONTESTIA_16_1000 --rsid-only
        EXTRA "-DMUST_CONTAIN=\"name\":\"CONTESTIA_16_1000\"" -DMAX_RSID=1)
fm_case(rsid_mt63_1000_lg  rsid rsid 8000 GEN mfsk16 --rsid MT63_1000_LG --rsid-only
        EXTRA "-DMUST_CONTAIN=\"name\":\"MT63_1000_LG\"" -DMAX_RSID=1)
fm_case(rsid_thor_16       rsid rsid 8000 GEN mfsk16 --rsid THOR_16 --rsid-only
        EXTRA "-DMUST_CONTAIN=\"name\":\"THOR_16\"" -DMAX_RSID=1)
fm_case(rsid_thor25x4_ext  rsid rsid 8000 GEN mfsk16 --rsid THOR25x4 --rsid-only
        EXTRA "-DMUST_CONTAIN=\"name\":\"THOR25x4\"|\"extended\":true" -DMAX_RSID=1)
fm_case(rsid_mfsk64l_ext   rsid rsid 8000 GEN mfsk16 --rsid MFSK64L --rsid-only
        EXTRA "-DMUST_CONTAIN=\"name\":\"MFSK64L\"|\"extended\":true" -DMAX_RSID=1)
fm_case(rsid_freq900       rsid rsid 8000 GEN mfsk16 --rsid MFSK16 --rsid-only --freq 900
        EXTRA "-DMUST_CONTAIN=\"name\":\"MFSK16\"|\"freq\":9" -DMAX_RSID=1)
fm_case(rsid_reverse       rsid rsid 8000 GEN mfsk16 --rsid MFSK16 --rsid-only --reverse ARGS --reverse
        EXTRA "-DMUST_CONTAIN=\"name\":\"MFSK16\"" -DMAX_RSID=1)
fm_case(rsid_noisy         rsid rsid 8000 GEN mfsk16 --rsid MFSK16 --rsid-only --snr -6 --seed 5
        EXTRA "-DMUST_CONTAIN=\"name\":\"MFSK16\"")
# the RSID in front of a text signal: both come out, the RSID first
fm_case(rsid_then_text compare mfsk16 8000 GEN mfsk16 --rsid MFSK16 --text ${MSG})
# an RSID while another mode is being decoded (the detector runs alongside)
fm_case(rsid_off compare mfsk16 8000 GEN mfsk16 --rsid MFSK16 --text ${MSG} ARGS --rsid off)

# --- the RSID name tables: 145 names in the first table, 57 in the second, as fldigi lists them -------------------------
add_test(NAME rsid_tables COMMAND ${CMAKE_COMMAND} -DMODE=sigtool -DNAME=rsid_tables -DSIGTOOL=$<TARGET_FILE:fldigimodes_sigtool>
         "-DSIGARGS=rsid-tables" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/work" -DEXPECTED=${TE}/rsid_tables.txt -P ${RUN})

# --- failure paths -------------------------------------------------------------------------------------------------
fm_case(empty_olivia empty olivia-8-500 8000)
fm_case(empty_mt63 empty mt63-1000s 8000)
fm_case(empty_thor16 empty thor16 8000)
fm_case(empty_mfsk16 empty mfsk16 8000)
# 1 MB of random bytes
fm_case(garbage_olivia garbage olivia-8-500 8000 EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=400)
fm_case(garbage_contestia garbage contestia-16-1000 8000 EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=400)
fm_case(garbage_mfsk16 garbage mfsk16 8000 EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=400)
fm_case(garbage_mt63   garbage mt63-1000l 8000 EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=400)
fm_case(garbage_thor16 garbage thor16 8000 EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=400)
fm_case(garbage_rsid   garbage rsid 8000 EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=20)

function(fm_fail NAME REGEX)
  cmake_parse_arguments(C "" "" "ARGS" ${ARGN})
  string(REPLACE ";" "|" a "${C_ARGS}")
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=${NAME} -DHELPER=$<TARGET_FILE:fldigimodes>
           "-DARGS=${a}" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/work" "-DSTDERR_REGEX=${REGEX}" -P ${RUN})
endfunction()
fm_fail(fail_format_cs16 "unsupported --format" ARGS --foxsdr-helper 1 --format cs16 --rate 8000 --mode olivia-8-500)
fm_fail(fail_format_junk "unsupported --format" ARGS --foxsdr-helper 1 --format wav --rate 8000 --mode mfsk16)
fm_fail(fail_rate_48k "unsupported --rate 48000" ARGS --foxsdr-helper 1 --format s16 --rate 48000 --mode olivia-8-500)
fm_fail(fail_rate_8k_for_mfsk11 "mfsk11 runs at 11025" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode mfsk11)
fm_fail(fail_rate_11k_for_thor16 "thor16 runs at 8000" ARGS --foxsdr-helper 1 --format s16 --rate 11025 --mode thor16)
fm_fail(fail_no_rate "--rate is required" ARGS --foxsdr-helper 1 --format s16 --mode mfsk16)
fm_fail(fail_no_mode "--mode is required" ARGS --foxsdr-helper 1 --format s16 --rate 8000)
fm_fail(fail_unknown_mode "unknown mode" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode psk31)
fm_fail(fail_olivia_tones "tones must be" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode olivia-7-500)
fm_fail(fail_olivia_bw "bandwidth must be" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode olivia-8-300)
fm_fail(fail_mfsk_name "MFSK modes are" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode mfsk17)
fm_fail(fail_thor_name "THOR modes are" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode thor99)
fm_fail(fail_mt63_name "MT63 bandwidth" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode mt63-700s)
fm_fail(fail_freq "--freq must lie" ARGS --foxsdr-helper 1 --format s16 --rate 8000 --mode mfsk16 --freq 9000)
fm_fail(fail_unknown_option "unknown option" ARGS --foxsdr-helper 1 --bogus)
fm_fail(fail_no_protocol "--foxsdr-helper 1 is required" ARGS --format s16 --rate 8000 --mode mfsk16)
fm_fail(fail_protocol_2 "unsupported protocol" ARGS --foxsdr-helper 2)

# --- manifest and hello ---------------------------------------------------------------------------------------------
add_test(NAME manifest_info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=manifest_info -DHELPER=$<TARGET_FILE:fldigimodes>
         "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/work" -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../fldigimodes.helper.json -P ${RUN})
add_test(NAME hello_fields COMMAND ${CMAKE_COMMAND} -DMODE=hello -DNAME=hello_fields -DHELPER=$<TARGET_FILE:fldigimodes>
         "-DARGS=--foxsdr-helper|1|--format|s16|--rate|8000|--mode|olivia-8-500" "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/work"
         "-DMUST_CONTAIN={\"helper\":\"fldigimodes\"|\"protocol\":1|\"licence\":\"GPL-3.0-only\"|\"source\":\"https://github.com/w1hkj/fldigi\"|\"rate\":8000|\"mode\":\"olivia-8-500\""
         -P ${RUN})
