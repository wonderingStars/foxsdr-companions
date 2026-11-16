# Tests for the tetrapol helper. Every case runs run_case.cmake (see its header for the modes).
# Pinned expectations are in expected/*.jsonl; to rewrite them after an intended change run
#   TP_UPDATE=1 ctest ... and review the git diff.
#
# All signals are made by tools/tpgen.c (an encoder written from the upstream constants, checked against PAS 0001-2 and
# against the upstream encoder, see UPSTREAM.md): nothing in test/ is a recording of a real network.

add_executable(tpgen tools/tpgen.c ${UP}/bit_utils.c)
target_include_directories(tpgen PRIVATE ${UP} ${COMPAT})
if(MSVC)
  target_compile_options(tpgen PRIVATE /W3 /utf-8 /wd4200 /wd4244 /wd4267 /wd4996 /wd4018)
  target_compile_definitions(tpgen PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
  target_link_libraries(tpgen PRIVATE m)
endif()

set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(BITS --foxsdr-helper 1 --format bits --rate 8000)
set(CF32 --foxsdr-helper 1 --format cf32 --rate 48000)

# tp_case(<name> <mode> [GEN1 <tpgen args>] [GEN2 ..] [GEN3 ..] [ARGS <helper args>...] [EXPECT <file>] [EXTRA <-Dvar=value>...])
# tpgen arguments are given as one string separated by '|'.
function(tp_case NAME MODE)
  cmake_parse_arguments(C "" "EXPECT;GEN1;GEN2;GEN3" "ARGS;EXTRA" ${ARGN})
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  string(REPLACE ";" "|" a "${C_ARGS}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:tetrapol-helper> "-DARGS=${a}"
      -DTPGEN=$<TARGET_FILE:tpgen> -DWORK=${WORKDIR} -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  foreach(g GEN1 GEN2 GEN3)
    if(C_${g})
      list(APPEND defs "-D${g}=${C_${g}}")
    endif()
  endforeach()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

# --- the program and the generator -----------------------------------------------------------------------------------
add_test(NAME info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=info -DHELPER=$<TARGET_FILE:tetrapol-helper>
         -DWORK=${WORKDIR} -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../tetrapol.helper.json -P ${RUN})
tp_case(xcheck_upstream_encoder xcheck)
tp_case(empty_input empty ARGS ${BITS})

# --- a control channel: two superframes, UHF, SCR 67 -----------------------------------------------------------------
set(CCH "cch|@OUT@|67")
tp_case(cch_bits twice GEN1 ${CCH} ARGS ${BITS})
tp_case(cch_messages msgonly GEN1 ${CCH} ARGS ${BITS} --no-frames)
tp_case(cch_pipe pipe GEN1 ${CCH} ARGS ${BITS} EXPECT cch_bits EXTRA -DCHUNK=777)
tp_case(cch_pipe_odd pipe GEN1 ${CCH} ARGS ${BITS} EXPECT cch_bits EXTRA -DCHUNK=1)
tp_case(cch_scr_fixed compare GEN1 ${CCH} ARGS ${BITS} --scr 67 --no-frames)
tp_case(cch_scr_wrong contains GEN1 ${CCH} ARGS ${BITS} --scr 12
        EXTRA "-DMUST_CONTAIN=\"type\":\"stats\"" "-DMUST_NOT_CONTAIN=\"type\":\"message\"")
tp_case(cch_band_wrong contains GEN1 "cch|@OUT@|33|vhf" ARGS ${BITS} --band uhf
        EXTRA "-DMUST_CONTAIN=\"type\":\"stats\"" "-DMUST_NOT_CONTAIN=\"type\":\"message\"")
tp_case(cch_vhf compare GEN1 "cch|@OUT@|12|vhf" ARGS ${BITS} --band vhf --no-frames)
tp_case(cch_report_bad compare GEN1 ${CCH} GEN2 "damage|@IN@|@OUT@|29:*,30:*,41:*" ARGS ${BITS} --report-bad-frames)

# --- the same control channel as I/Q: same messages, other doors ------------------------------------------------------
tp_case(iq_cf32_48000 msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|1000|0|0.3|0" ARGS ${CF32} --no-frames EXPECT cch_messages)
tp_case(iq_cs16_48000 msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cs16|48000|1000|0|0.6|0"
        ARGS --foxsdr-helper 1 --format cs16 --rate 48000 --no-frames EXPECT cch_messages)
tp_case(iq_cf32_32000 msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|32000|1000|0|0.1|0"
        ARGS --foxsdr-helper 1 --format cf32 --rate 32000 --no-frames EXPECT cch_messages)
tp_case(iq_cf32_16000 msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|16000|1000|0|0.4|0"
        ARGS --foxsdr-helper 1 --format cf32 --rate 16000 --no-frames EXPECT cch_messages)
tp_case(iq_cf32_44100 msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|44100|1000|0|0.7|0"
        ARGS --foxsdr-helper 1 --format cf32 --rate 44100 --no-frames EXPECT cch_messages)
tp_case(iq_carrier_offset msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|1000|1800|0.5|0" ARGS ${CF32} --no-frames EXPECT cch_messages)
tp_case(iq_carrier_offset_neg msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|1000|-1800|0.5|0" ARGS ${CF32} --no-frames EXPECT cch_messages)
tp_case(iq_clock_error msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|1000|0|0.2|-150" ARGS ${CF32} --no-frames EXPECT cch_messages)
tp_case(iq_noise_16db msgonly GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|16|600|0.4|20" ARGS ${CF32} --no-frames EXPECT cch_messages)
# the full output (with the times) of one I/Q run is pinned, and the same signal through a pipe cut into odd pieces
# (1001 bytes: not a multiple of a sample) must give the same lines
tp_case(iq_cf32_full twice GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|1000|300|0.3|0" ARGS ${CF32})
tp_case(iq_cf32_pipe pipe GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|1000|300|0.3|0" ARGS ${CF32} EXPECT iq_cf32_full EXTRA -DCHUNK=1001)
tp_case(iq_noise_only contains GEN1 "garbage|3|400000|@OUT@" ARGS ${CF32}
        EXTRA "-DMUST_CONTAIN=\"type\":\"stats\"" "-DMUST_NOT_CONTAIN=\"type\":\"message\"")

# --- damaged frames -------------------------------------------------------------------------------------------------
# frame 8 + n is frame n of the superframe (eight idle frames lead the stream)
tp_case(damaged_single_bits compare GEN1 ${CCH} GEN2 "damage|@IN@|@OUT@|9:40,21:100,29:20,43:7,60:151" ARGS ${BITS} --no-frames)
tp_case(damaged_block_recovered compare GEN1 ${CCH} GEN2 "damage|@IN@|@OUT@|30:*" ARGS ${BITS} --no-frames)
tp_case(damaged_two_blocks compare GEN1 ${CCH} GEN2 "damage|@IN@|@OUT@|29:*,30:*" ARGS ${BITS} --no-frames)
tp_case(damaged_sync compare GEN1 ${CCH} GEN2 "damage|@IN@|@OUT@|40:s2,41:s3,90:s0" ARGS ${BITS} --no-frames)
tp_case(damaged_bch compare GEN1 ${CCH} GEN2 "damage|@IN@|@OUT@|8:*,9:*,10:*,11:*" ARGS ${BITS} --no-frames)

# --- identities --------------------------------------------------------------------------------------------------------
tp_case(hide_ids compare GEN1 ${CCH} ARGS ${BITS} --hide-ids --hide-ids-seed 7 --no-frames)
tp_case(hide_ids_other_seed compare GEN1 ${CCH} ARGS ${BITS} --hide-ids --hide-ids-seed 8 --no-frames)
tp_case(hide_ids_leak contains GEN1 ${CCH} ARGS ${BITS} --hide-ids
        EXTRA "-DMUST_CONTAIN=h:|D_REGISTRATION_ACK|D_GROUP_ACTIVATION"
        "-DMUST_NOT_CONTAIN=0x321|0x322|0x1ab|0x444|0x555|0x333|0x123|0x124|0x456|250-3-12-012|321-4-56-789|\"group_id\":1219|\"group_id\":291|\"group_id\":127")
tp_case(hide_ids_iq contains GEN1 ${CCH} GEN2 "iq|@IN@|@OUT@|cf32|48000|1000|0|0.3|0" ARGS ${CF32} --hide-ids
        EXTRA "-DMUST_CONTAIN=h:|D_REGISTRATION_ACK" "-DMUST_NOT_CONTAIN=0x321|0x1ab|250-3-12-012")

# --- what must never come out ------------------------------------------------------------------------------------------
# the generator puts recognisable bytes into a datagram (d0..d5) and into an authentication challenge (a0..a7)
tp_case(no_data_no_keys contains GEN1 ${CCH} ARGS ${BITS}
        EXTRA "-DMUST_CONTAIN=D_DATAGRAM|data message, content not decoded|D_AUTHENTICATION"
        "-DMUST_NOT_CONTAIN=d0d1|d1d2|a0a1|a1a2|\"data\":|\"valid_rt\"|key_of_call")

# --- a traffic channel: counted, not decoded ----------------------------------------------------------------------------
tp_case(tch_bits compare GEN1 "tch|@OUT@|33" ARGS ${BITS} --channel tch)
tp_case(tch_no_messages contains GEN1 "tch|@OUT@|33" ARGS ${BITS} --channel tch
        EXTRA "-DMUST_CONTAIN=traffic frame, not decoded|voice frame" "-DMUST_NOT_CONTAIN=\"type\":\"message\"|\"frame_no\"")

# --- failure paths -------------------------------------------------------------------------------------------------------
tp_case(fail_format_s16 fail ARGS --foxsdr-helper 1 --format s16 --rate 22050 EXTRA "-DSTDERR_REGEX=unsupported --format")
tp_case(fail_format_missing fail ARGS --foxsdr-helper 1 --rate 48000 EXTRA "-DSTDERR_REGEX=--format is required")
tp_case(fail_rate_missing fail ARGS --foxsdr-helper 1 --format cf32 EXTRA "-DSTDERR_REGEX=--rate is required")
tp_case(fail_rate_range fail ARGS --foxsdr-helper 1 --format cf32 --rate 8000 EXTRA "-DSTDERR_REGEX=out of range")
tp_case(fail_bits_rate fail ARGS --foxsdr-helper 1 --format bits --rate 48000 EXTRA "-DSTDERR_REGEX=must be 8000")
tp_case(fail_protocol fail ARGS --foxsdr-helper 2 --format bits EXTRA "-DSTDERR_REGEX=only pipe protocol version 1")
tp_case(fail_not_helper fail ARGS --format bits --rate 8000 EXTRA "-DSTDERR_REGEX=not started as a helper")
tp_case(fail_option fail ARGS --foxsdr-helper 1 --format bits --bogus EXTRA "-DSTDERR_REGEX=unknown option")
tp_case(fail_scr fail ARGS --foxsdr-helper 1 --format bits --scr 200 EXTRA "-DSTDERR_REGEX=--scr is an integer")

# --- garbage and truncation -------------------------------------------------------------------------------------------------
tp_case(garbage_bits garbage ARGS ${BITS} EXTRA -DGARBAGE_BYTES=300000 -DMAX_LINES=40)
tp_case(garbage_cf32 garbage ARGS ${CF32} EXTRA -DGARBAGE_BYTES=800000 -DMAX_LINES=40)
tp_case(garbage_cs16 garbage ARGS --foxsdr-helper 1 --format cs16 --rate 48000 EXTRA -DGARBAGE_BYTES=400001 -DMAX_LINES=40)
tp_case(fuzz_messages contains GEN1 "fuzzmsg|1|3000|@OUT@" ARGS ${BITS} --no-frames EXTRA "-DMUST_CONTAIN=\"type\":\"stats\"")
tp_case(fuzz_messages_2 contains GEN1 "fuzzmsg|2|3000|@OUT@" ARGS ${BITS} --no-frames --hide-ids EXTRA "-DMUST_CONTAIN=\"type\":\"stats\"")
