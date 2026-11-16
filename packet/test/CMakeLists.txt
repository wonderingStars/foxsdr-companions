# Tests for the packet helper. Every case runs run_case.cmake (see its header for the modes).
# Signals are made at test time by Dire Wolf's own generator (gen_packets) or by gen_damaged (the same generator with a
# damage shim, tools/gen_damage_shim.c) and are deterministic. Pinned expectations are in expected/*.jsonl; to rewrite
# them after an intended change run   PK_UPDATE=1 ctest ...   and review the git diff.

set(TD ${CMAKE_CURRENT_SOURCE_DIR}/data)
set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(MSGS ${TD}/msgs.txt)

# pk_case(<name> <mode> RATE <Hz> [GEN <generator args>...] [ARGS <helper args>...] [DAMAGE <spec>] [EXPECT <name>] [EXTRA <-D...>...])
function(pk_case NAME MODE)
  cmake_parse_arguments(C "" "RATE;DAMAGE;EXPECT;TOOL" "GEN;ARGS;EXTRA" ${ARGN})
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  if(NOT C_RATE)
    set(C_RATE 44100)
  endif()
  if(NOT C_TOOL)
    set(C_TOOL gen_packets)
  endif()
  if(C_DAMAGE)
    set(C_TOOL gen_damaged)
  endif()
  set(all --foxsdr-helper 1 --format s16 --rate ${C_RATE} ${C_ARGS})
  string(REPLACE ";" "|" a "${all}")
  set(genargs -r ${C_RATE} ${C_GEN})
  string(REPLACE ";" "|" g "${genargs}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=${a}" "-DGENARGS=${g}"
      -DGENTOOL=$<TARGET_FILE:${C_TOOL}> -DRAWTOOL=$<TARGET_FILE:rawtool> -DMSGS=${MSGS} -DWORK=${WORKDIR}
      -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  if(C_DAMAGE)
    list(APPEND defs -DDAMAGE=${C_DAMAGE})
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

# --- plain AX.25 -----------------------------------------------------------------------------------------------
pk_case(ax25_1200_44100  gen RATE 44100  GEN -B 1200)
pk_case(ax25_1200_48000  gen RATE 48000  GEN -B 1200)
pk_case(ax25_1200_22050  gen RATE 22050  GEN -B 1200)
pk_case(ax25_1200_11025  gen RATE 11025  GEN -B 1200)
pk_case(ax25_1200_8000   gen RATE 8000   GEN -B 1200)
pk_case(ax25_1200_192000 gen RATE 192000 GEN -B 1200)
pk_case(ax25_300_44100   gen RATE 44100  GEN -B 300  ARGS --baud 300)
pk_case(ax25_300_22050   gen RATE 22050  GEN -B 300  ARGS --baud 300)
pk_case(ax25_300_8000    gen RATE 8000   GEN -B 300  ARGS --baud 300)
pk_case(ax25_9600_44100  gen RATE 44100  GEN -B 9600 ARGS --baud 9600)
pk_case(ax25_9600_48000  gen RATE 48000  GEN -B 9600 ARGS --baud 9600)
pk_case(ax25_9600_19200  gen RATE 19200  GEN -B 9600 ARGS --baud 9600)
pk_case(ax25_9600_192000 gen RATE 192000 GEN -B 9600 ARGS --baud 9600)
pk_case(ax25_1200_profile_e gen RATE 44100 GEN -B 1200 ARGS --profile E+)

# --- FX.25 (-X n: n check bytes; 16 corrects 8 bytes per block, 32 corrects 16, 64 corrects 32) -----------------------
pk_case(fx25_1200_x16  gen RATE 44100 GEN -B 1200 -X 16)
pk_case(fx25_1200_x32  gen RATE 44100 GEN -B 1200 -X 32)
pk_case(fx25_1200_x64  gen RATE 44100 GEN -B 1200 -X 64)
pk_case(fx25_300_x16   gen RATE 44100 GEN -B 300  -X 16 ARGS --baud 300)
pk_case(fx25_9600_x16  gen RATE 44100 GEN -B 9600 -X 16 ARGS --baud 9600)

# --- IL2P (-I 1: 16 parity symbols per payload block, corrects 8; -I 0: fewer, by size; -i: inverted polarity) --------
pk_case(il2p_1200_max      gen RATE 44100 GEN -B 1200 -I 1)
pk_case(il2p_1200_weak     gen RATE 44100 GEN -B 1200 -I 0)
pk_case(il2p_1200_inverted gen RATE 44100 GEN -B 1200 -i 1)
pk_case(il2p_300_max       gen RATE 44100 GEN -B 300  -I 1 ARGS --baud 300)
pk_case(il2p_9600_max      gen RATE 44100 GEN -B 9600 -I 1 ARGS --baud 9600)

# --- damaged frames: bit errors in N bytes (symbols) up to the correction limit, and one past it ---------------------
# FX.25 with 16 check bytes: 1 and 8 bytes are corrected, 9 are beyond the limit (nothing decodes)
pk_case(fx25_x16_dmg1   gen RATE 44100 GEN -B 1200 -X 16 DAMAGE x1)
pk_case(fx25_x16_dmg8   gen RATE 44100 GEN -B 1200 -X 16 DAMAGE x8)
pk_case(fx25_x16_dmg9   gen RATE 44100 GEN -B 1200 -X 16 DAMAGE x9)
# FX.25 with 32 check bytes: 16 corrected, 17 beyond the limit
pk_case(fx25_x32_dmg16  gen RATE 44100 GEN -B 1200 -X 32 DAMAGE x16)
pk_case(fx25_x32_dmg17  gen RATE 44100 GEN -B 1200 -X 32 DAMAGE x17)
# FX.25 with 64 check bytes: 32 corrected, 33 beyond the limit
pk_case(fx25_x64_dmg32  gen RATE 44100 GEN -B 1200 -X 64 DAMAGE x32)
pk_case(fx25_x64_dmg33  gen RATE 44100 GEN -B 1200 -X 64 DAMAGE x33)
# the same over G3RUH 9600
pk_case(fx25_9600_x16_dmg8 gen RATE 44100 GEN -B 9600 -X 16 DAMAGE x8 ARGS --baud 9600)
pk_case(fx25_9600_x16_dmg9 gen RATE 44100 GEN -B 9600 -X 16 DAMAGE x9 ARGS --baud 9600)
# IL2P header: RS(15,13) corrects 1 symbol, 2 are beyond the limit
pk_case(il2p_hdr_dmg1  gen RATE 44100 GEN -B 1200 -I 1 DAMAGE h1)
pk_case(il2p_hdr_dmg2  gen RATE 44100 GEN -B 1200 -I 1 DAMAGE h2)
# IL2P payload with 16 parity symbols: 8 corrected, 9 beyond the limit
pk_case(il2p_pay_dmg1  gen RATE 44100 GEN -B 1200 -I 1 DAMAGE p1)
pk_case(il2p_pay_dmg8  gen RATE 44100 GEN -B 1200 -I 1 DAMAGE p8)
pk_case(il2p_pay_dmg9  gen RATE 44100 GEN -B 1200 -I 1 DAMAGE p9)
# IL2P with automatic (smaller) parity: the 63 byte payload has 4 parity symbols, so 2 are corrected and 4 are not
pk_case(il2p_weak_dmg2 gen RATE 44100 GEN -B 1200 -I 0 DAMAGE p2)
pk_case(il2p_weak_dmg4 gen RATE 44100 GEN -B 1200 -I 0 DAMAGE p4)
pk_case(il2p_9600_pay_dmg8 gen RATE 44100 GEN -B 9600 -I 1 DAMAGE p8 ARGS --baud 9600)
pk_case(il2p_9600_pay_dmg9 gen RATE 44100 GEN -B 9600 -I 1 DAMAGE p9 ARGS --baud 9600)

# --- AX.25 with noise: gen_packets -n makes frames with increasing noise; --fix-bits tries to repair bad CRCs --------
pk_case(ax25_noise         gen RATE 44100 GEN -B 1200 -n 12 -a 25)
pk_case(ax25_noise_fix1    gen RATE 44100 GEN -B 1200 -n 12 -a 25 ARGS --fix-bits 1 EXPECT ax25_noise_fix1)

# --- the three kinds in one stream -----------------------------------------------------------------------------------
add_test(NAME mixed_stream COMMAND ${CMAKE_COMMAND} -DMODE=concat -DNAME=mixed_stream
         -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|44100"
         -DGENTOOL=$<TARGET_FILE:gen_damaged> -DRAWTOOL=$<TARGET_FILE:rawtool> -DMSGS=${MSGS} -DWORK=${WORKDIR}
         "-DPARTS=a|b|c|d" "-Da_ARGS=-r|44100|-B|1200" "-Db_ARGS=-r|44100|-B|1200|-X|16" "-Dc_ARGS=-r|44100|-B|1200|-I|1"
         "-Dd_ARGS=-r|44100|-B|1200|-X|16" -Dd_DAMAGE=x5 -DEXPECTED=${TE}/mixed_stream.jsonl -P ${RUN})

# --- the same signal through other doors: float samples, a pipe in small writes, a cut-off sample --------------------
pk_case(f32_ax25_1200 f32 RATE 44100 GEN -B 1200       EXPECT ax25_1200_44100)
pk_case(f32_fx25      f32 RATE 44100 GEN -B 1200 -X 16 EXPECT fx25_1200_x16)
pk_case(f32_il2p      f32 RATE 44100 GEN -B 1200 -I 1  EXPECT il2p_1200_max)
pk_case(pipe_ax25_small  pipe RATE 44100 GEN -B 1200       EXPECT ax25_1200_44100 EXTRA -DCHUNK=1)
pk_case(pipe_fx25_1000   pipe RATE 44100 GEN -B 1200 -X 16 EXPECT fx25_1200_x16   EXTRA -DCHUNK=1000)
pk_case(pipe_il2p_odd    pipe RATE 44100 GEN -B 1200 -I 1  EXPECT il2p_1200_max   EXTRA -DCHUNK=4097)
pk_case(oddbyte_ax25  oddbyte RATE 44100 GEN -B 1200 EXPECT ax25_1200_44100)

# --- failure paths ---------------------------------------------------------------------------------------------
add_test(NAME empty_input COMMAND ${CMAKE_COMMAND} -DMODE=empty -DNAME=empty_input
         -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|44100"
         -DWORK=${WORKDIR} -DEXPECTED=${TE}/empty_input.jsonl -P ${RUN})
# 1 MB of random bytes and 20 s of loud wide-band noise: no crash, exit 0, bounded output
foreach(baud 300 1200 9600)
  add_test(NAME garbage_${baud} COMMAND ${CMAKE_COMMAND} -DMODE=garbage -DNAME=garbage_${baud}
           -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|44100|--baud|${baud}"
           -DRAWTOOL=$<TARGET_FILE:rawtool> -DWORK=${WORKDIR} -DGARBAGE_BYTES=1000000 -DMAX_LINES=40 -P ${RUN})
  add_test(NAME noise_${baud} COMMAND ${CMAKE_COMMAND} -DMODE=noise -DNAME=noise_${baud}
           -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|44100|--baud|${baud}"
           -DRAWTOOL=$<TARGET_FILE:rawtool> -DWORK=${WORKDIR} -DNOISE_SAMPLES=882000 -DMAX_LINES=40 -P ${RUN})
endforeach()
add_test(NAME silence COMMAND ${CMAKE_COMMAND} -DMODE=silence -DNAME=silence
         -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|44100"
         -DRAWTOOL=$<TARGET_FILE:rawtool> -DWORK=${WORKDIR} -DSILENCE_SAMPLES=441000 -DEXPECTED=${TE}/silence.jsonl -P ${RUN})

function(pk_fail NAME REGEX)
  string(REPLACE ";" "|" a "${ARGN}")
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=${NAME} -DHELPER=$<TARGET_FILE:packet-helper>
           "-DARGS=${a}" -DWORK=${WORKDIR} "-DSTDERR_REGEX=${REGEX}" -P ${RUN})
endfunction()
pk_fail(fail_format_cs16     "unsupported --format"  --foxsdr-helper 1 --format cs16 --rate 44100)
pk_fail(fail_format_missing  "--format is required"  --foxsdr-helper 1 --rate 44100)
pk_fail(fail_rate_low        "--rate must be"        --foxsdr-helper 1 --format s16 --rate 4000)
pk_fail(fail_rate_high       "--rate must be"        --foxsdr-helper 1 --format s16 --rate 384000)
pk_fail(fail_rate_missing    "--rate must be"        --foxsdr-helper 1 --format s16)
pk_fail(fail_baud            "--baud must be"        --foxsdr-helper 1 --format s16 --rate 44100 --baud 2400)
pk_fail(fail_9600_low_rate   "at least 19200"        --foxsdr-helper 1 --format s16 --rate 16000 --baud 9600)
pk_fail(fail_profile_letter  "bad --profile"         --foxsdr-helper 1 --format s16 --rate 44100 --profile Z)
pk_fail(fail_profile_9600    "applies to AFSK"       --foxsdr-helper 1 --format s16 --rate 44100 --baud 9600 --profile A)
pk_fail(fail_sanity          "--sanity must be"      --foxsdr-helper 1 --format s16 --rate 44100 --sanity maybe)
pk_fail(fail_fix_bits        "--fix-bits must be"    --foxsdr-helper 1 --format s16 --rate 44100 --fix-bits 9)
pk_fail(fail_unknown_option  "unknown option"        --foxsdr-helper 1 --format s16 --rate 44100 --bogus)
pk_fail(fail_no_protocol     "--foxsdr-helper 1 is required" --format s16 --rate 44100)
pk_fail(fail_protocol_2      "unsupported protocol"  --foxsdr-helper 2 --format s16 --rate 44100)

# --- manifest and hello ----------------------------------------------------------------------------------------
add_test(NAME manifest_info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=manifest_info
         -DHELPER=$<TARGET_FILE:packet-helper> -DWORK=${WORKDIR}
         -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../packet.helper.json -P ${RUN})
add_test(NAME hello_fields COMMAND ${CMAKE_COMMAND} -DMODE=hello -DNAME=hello_fields
         -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|44100"
         -DWORK=${WORKDIR}
         "-DMUST_CONTAIN={\"helper\":\"packet\"|\"protocol\":1|\"licence\":\"GPL-2.0-or-later\"|\"source\":\"https://github.com/wb2osz/direwolf\"|\"rate\":44100|\"baud\":1200"
         -P ${RUN})

# --- a real off-air recording (WA8LMF TNC Test CD v2, track 1, 144.39 MHz APRS, Los Angeles): local only -------------------
# Skipped (CTest "Not Run") when the file or ffmpeg is missing. Pinned: frame counts only, for the first 180 s, left channel.
foreach(prof "" "E+")
  if(prof STREQUAL "")
    set(rn real_wa8lmf_t1_180s)
    set(pa "")
  else()
    set(rn real_wa8lmf_t1_180s_profile_e)
    set(pa "|--profile|E+")
  endif()
  add_test(NAME ${rn} COMMAND ${CMAKE_COMMAND} -DMODE=real -DNAME=${rn}
           -DHELPER=$<TARGET_FILE:packet-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|44100${pa}"
           -DSRC=${FOXSDR_SAMPLES_DIR}/aprs-decoder/wa8lmf-tnc-test-cd-v2/extracted/01_40_MI.FLA -DSECONDS=180 -DRATE=44100
           -DWORK=${WORKDIR} -DEXPECTED=${TE}/${rn}.counts -P ${RUN})
  set_tests_properties(${rn} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
