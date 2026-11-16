# Tests for the rtl433 helper. Every case runs run_case.cmake (see its header for the modes).
# Pinned expectations are in expected/*.jsonl; to rewrite them after an intended change run
#   RT_UPDATE=1 ctest ... and review the git diff.
#
# The signals are made by gen_signals (test/gen_signals.c) from the protocol descriptions in the comments of
# three rtl_433 decoders; no recordings are committed (the rtl_433_tests recordings carry no licence).

add_executable(gen_signals gen_signals.c)
add_executable(feed feed.c)
if(NOT MSVC)
  target_link_libraries(gen_signals PRIVATE m)
else()
  target_compile_definitions(gen_signals PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_definitions(feed PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(PROTO --foxsdr-helper 1 --format cu8 --rate 250000)

# the events of the mixed signal: a remote, a temperature sensor, a tyre-pressure sensor, the sensor again
set(MIX "silvercrest|5|:|prologue|171|2|215|-1|:|toyota|f0d5aee3|165|69|1|:|prologue|171|2|218|55")

# rt_case(<name> <mode> [GEN <gen_signals args | separated>] [GENFMT <opts>] [ARGS <helper args>...]
#         [EXPECT <file>] [EXTRA <-Dvar=value>...])
function(rt_case NAME MODE)
  cmake_parse_arguments(C "NOPROTO" "GEN;GENFMT;EXPECT;PROTOFMT" "ARGS;EXTRA" ${ARGN})
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  if(C_NOPROTO)
    set(proto "")
  elseif(C_PROTOFMT)
    set(proto ${C_PROTOFMT})
  else()
    set(proto ${PROTO})
  endif()
  set(all ${proto} ${C_ARGS})
  string(REPLACE ";" "|" a "${all}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:rtl433-helper>
      "-DARGS=${a}" -DGENSIG=$<TARGET_FILE:gen_signals> -DFEED=$<TARGET_FILE:feed> -DWORK=${WORKDIR}
      -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  if(C_GEN)
    list(APPEND defs "-DGEN=${C_GEN}")
  endif()
  if(C_GENFMT)
    list(APPEND defs "-DGENFMT=${C_GENFMT}")
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

# --- one device at a time (cu8, 250 kS/s) -------------------------------------------------------------------------
rt_case(silvercrest   twice GEN "silvercrest|5")
rt_case(prologue      twice GEN "prologue|171|2|215|-1")
rt_case(toyota_tpms   twice GEN "toyota|f0d5aee3|165|69|1")
rt_case(mix           twice GEN "${MIX}")
# a damaged frame (wrong CRC-8) must give no event at all
rt_case(toyota_bad_crc twice GEN "toyota|f0d5aee3|165|69|1|1")

# --- the same signal through other doors ------------------------------------------------------------------------
rt_case(mix_cs16      gen GEN "${MIX}" GENFMT "--format|cs16" ARGS --format cs16)
rt_case(mix_cf32      gen GEN "${MIX}" GENFMT "--format|cf32" ARGS --format cf32)
rt_case(mix_cs8       gen GEN "${MIX}" GENFMT "--format|cs8"  ARGS --format cs8)
rt_case(mix_1M        gen GEN "${MIX}" GENFMT "--rate|1000000" ARGS --rate 1000000 --centre 868300000)
rt_case(mix_pipe      pipe GEN "${MIX}" EXPECT mix EXTRA -DCHUNK=777 -DNO_UPDATE=1)
rt_case(mix_pipe_odd  pipe GEN "${MIX}" EXPECT mix EXTRA -DCHUNK=4099 -DNO_UPDATE=1)
rt_case(mix_oddbyte   oddbyte GEN "${MIX}")

# --- options ----------------------------------------------------------------------------------------------------
rt_case(only_prologue gen GEN "${MIX}" ARGS -R 3)
rt_case(not_prologue  gen GEN "${MIX}" ARGS -R -3)
rt_case(no_meta       gen GEN "toyota|f0d5aee3|165|69|1" ARGS --no-meta)
rt_case(units_si      gen GEN "toyota|f0d5aee3|165|69|1" ARGS -C si)
rt_case(units_customary gen GEN "prologue|171|2|215|-1" ARGS -C customary)
rt_case(flex_user_decoder gen GEN "toyota|f0d5aee3|165|69|1" ARGS -R 88 -X "n=toyraw,m=FSK_PCM,s=52,l=52,r=150")

# --- privacy: --hide-ids ------------------------------------------------------------------------------------------
rt_case(hide_ids hide GEN "${MIX}" ARGS --hide-ids --hide-ids-salt testsalt
        EXTRA "-DMUST_NOT_CONTAIN=\"id\":171|f0d5aee3|id=171" "-DMUST_CONTAIN=\"id\":\"h|id=h")
rt_case(hide_ids_flex hide GEN "toyota|f0d5aee3|165|69|1" ARGS -R 88 --hide-ids --hide-ids-salt testsalt
        -X "n=toyraw,m=FSK_PCM,s=52,l=52,r=150"
        EXTRA "-DMUST_NOT_CONTAIN=f0d5aee3|553ccd|\"codes\"|\"rows\"" "-DMUST_CONTAIN=\"id\":\"h" EXPECT hide_ids_flex)
rt_case(hide_ids_random_salt hide2 GEN "prologue|171|2|215|-1|:|prologue|171|2|218|55" ARGS --hide-ids)

# --- failure paths --------------------------------------------------------------------------------------------------
rt_case(fail_wrong_format fail NOPROTO ARGS --foxsdr-helper 1 --format s16 --rate 250000
        EXTRA "-DSTDERR_REGEX=unsupported --format")
rt_case(fail_no_format    fail NOPROTO ARGS --foxsdr-helper 1 --rate 250000 EXTRA "-DSTDERR_REGEX=--format is required")
rt_case(fail_no_protocol  fail NOPROTO ARGS --format cu8 --rate 250000 EXTRA "-DSTDERR_REGEX=--foxsdr-helper 1 is required")
rt_case(fail_bad_protocol fail NOPROTO ARGS --foxsdr-helper 2 --format cu8 --rate 250000 EXTRA "-DSTDERR_REGEX=unsupported protocol version")
rt_case(fail_bad_rate     fail NOPROTO ARGS --foxsdr-helper 1 --format cu8 --rate 100 EXTRA "-DSTDERR_REGEX=--rate")
rt_case(fail_bad_option   fail NOPROTO ARGS --foxsdr-helper 1 --format cu8 --rate 250000 --mqtt localhost EXTRA "-DSTDERR_REGEX=unknown option")
rt_case(fail_ert_not_built fail NOPROTO ARGS --foxsdr-helper 1 --format cu8 --rate 250000 -R 149 EXTRA "-DSTDERR_REGEX=not built.*Itron ERT")
rt_case(fail_decrypt_not_built fail NOPROTO ARGS --foxsdr-helper 1 --format cu8 --rate 250000 -R 130 EXTRA "-DSTDERR_REGEX=not built.*decrypts")
rt_case(fail_lora_not_built fail NOPROTO ARGS --foxsdr-helper 1 --format cu8 --rate 250000 -R 388 EXTRA "-DSTDERR_REGEX=not built.*LoRa")
rt_case(empty_input   empty)
rt_case(garbage_cu8   garbage EXTRA -DGARBAGE_BYTES=3000000 -DMAX_LINES=20)
rt_case(garbage_cs16  garbage PROTOFMT --foxsdr-helper|1|--format|cs16|--rate|250000 EXTRA -DGARBAGE_BYTES=3000000 -DMAX_LINES=20)

# --- manifest, hello line, protocol list ---------------------------------------------------------------------------
add_test(NAME info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=info -DHELPER=$<TARGET_FILE:rtl433-helper>
         -DWORK=${WORKDIR} -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../rtl433.helper.json -P ${RUN})
add_test(NAME hello COMMAND ${CMAKE_COMMAND} -DMODE=hello -DNAME=hello -DHELPER=$<TARGET_FILE:rtl433-helper>
         "-DARGS=--foxsdr-helper|1|--format|cu8|--rate|250000" -DWORK=${WORKDIR}
         "-DMUST_CONTAIN=\"helper\":\"rtl433\"|\"protocol\":1|\"licence\":\"GPL-2.0-or-later\"|\"protocols_built\":384|\"source\":\"https://github.com/merbanan/rtl_433\""
         -P ${RUN})
add_test(NAME protocol_list COMMAND ${CMAKE_COMMAND} -DMODE=list -DNAME=protocol_list -DHELPER=$<TARGET_FILE:rtl433-helper>
         -DWORK=${WORKDIR} -DLINES=398 -DREMOVED=14
         "-DMUST_CONTAIN=ert_scm|scmplus|ert_idm|ert_netidm|cotech_36_9231|lorawan|elsys_ers|ikea_sparsnas|govee_h5112|Toyota TPMS|Honeywell" -P ${RUN})
