# Tests for the mdc helper. Every case runs run_case.cmake (see its header for the modes). Signals come from mdc-gen
# (tools/mdc_gen.c: MDC-1200 by upstream's own encoder, Fleetsync I by a generator written from upstream's decoder).
# Pinned expectations are in expected/*.jsonl; to rewrite them after an intended change run
#   MDC_UPDATE=1 ctest ... and review the git diff.
set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)

# --- upstream's own round-trip self test (encoder -> decoder, single and double packets, polled and callback) -------
add_test(NAME upstream_selftest COMMAND mdc-upstream-selftest)

# a range of MDC-1200 operation codes, one packet each, then four double packets (second block call alert,
# call alert ack, selective call); amplitude 0.68 of full scale, silence between packets
set(OPS_FULL "sil|0.2|mdc|01|80|1234|sil|0.25|mdc|01|00|1234|sil|0.25|mdc|00|80|0042|sil|0.25|mdc|20|00|0042|sil|0.25|mdc|63|85|00FF|sil|0.25|mdc|03|00|00FF|sil|0.25|mdc|22|06|BEEF|sil|0.25|mdc|06|03|BEEF|sil|0.25|mdc|07|12|0102|sil|0.25|mdc|23|00|0102|sil|0.25|mdc|11|8A|0777|sil|0.25|mdc|2B|00|0777|sil|0.25|mdc|2B|0C|0777|sil|0.25|mdc|0B|00|0777|sil|0.25|mdc|0B|0C|0777|sil|0.25|mdc|30|01|0A0A|sil|0.25|mdc|40|01|0B0B|sil|0.25|mdc|7E|55|FFFF|sil|0.25|mdc2|35|89|4321|83|0D|12|34|sil|0.25|mdc2|55|89|4321|A0|00|56|78|sil|0.25|mdc2|35|89|4321|80|15|98|76|sil|0.25|mdc2|55|89|4321|82|15|98|76|sil|0.25")
# a short sequence for the other sample rates and the noisy cases
set(OPS_SMALL "sil|0.2|mdc|01|80|1234|sil|0.25|mdc|01|00|1234|sil|0.25|mdc|00|80|0042|sil|0.25|mdc|63|85|00FF|sil|0.25|mdc|7E|55|FFFF|sil|0.25|mdc2|35|89|4321|83|0D|12|34|sil|0.25")

# mdc_case(<name> <mode> GEN <mdc-gen args '|'-separated> [RATE <Hz>] [FMT s16|f32] [ARGS <helper args>...] [EXPECT <name>]
#          [EXTRA <-Dvar=value>...])
function(mdc_case NAME MODE)
  cmake_parse_arguments(C "" "RATE;FMT;EXPECT;GEN" "ARGS;EXTRA" ${ARGN})
  if(NOT C_RATE)
    set(C_RATE 16000)
  endif()
  if(NOT C_FMT)
    set(C_FMT s16)
  endif()
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  set(all --foxsdr-helper 1 --format ${C_FMT} --rate ${C_RATE} ${C_ARGS})
  string(REPLACE ";" "|" a "${all}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:mdc-helper> "-DARGS=${a}"
      -DMDCGEN=$<TARGET_FILE:mdc-gen> -DWORK=${WORKDIR} -DEXPECTED=${TE}/${C_EXPECT}.jsonl "-DGEN=${C_GEN}")
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

# --- MDC-1200, generated, clean ----------------------------------------------------------------------------------
# 48 kHz is the main rate. On generated signals some packets are lost at 8 kHz and 11.025 kHz (see README.md, "Accepted rates").
mdc_case(mdc_ops_48k_full twice GEN "--rate|48000|${OPS_FULL}" RATE 48000)
mdc_case(mdc_ops_8k   gen   GEN "--rate|8000|${OPS_SMALL}"   RATE 8000)
mdc_case(mdc_ops_12k  gen   GEN "--rate|12000|${OPS_SMALL}"  RATE 12000)
mdc_case(mdc_ops_16k  gen   GEN "--rate|16000|${OPS_SMALL}"  RATE 16000)
mdc_case(mdc_ops_24k  gen   GEN "--rate|24000|${OPS_SMALL}"  RATE 24000)
mdc_case(mdc_ops_96k  gen   GEN "--rate|96000|${OPS_SMALL}"  RATE 96000)
mdc_case(mdc_ops_f32  gen   GEN "--rate|48000|--f32|${OPS_SMALL}" RATE 48000 FMT f32)
mdc_case(mdc_ops_pipe pipe  GEN "--rate|48000|${OPS_SMALL}" RATE 48000 EXTRA -DCHUNK=777)
mdc_case(mdc_hide_ids gen   GEN "--rate|48000|${OPS_FULL}" RATE 48000 ARGS --hide-ids)
mdc_case(mdc_only     gen   GEN "--rate|48000|${OPS_SMALL}|sil|0.2|fs1|123|4567|2345|40|sil|0.4" RATE 48000 ARGS --no-fleetsync)

# --- MDC-1200, noisy: white noise added (rms as a fraction of full scale), fixed seeds -----------------------------
mdc_case(mdc_noise_05 gen GEN "--rate|48000|--noise|0.05|--seed|11|${OPS_SMALL}" RATE 48000)
mdc_case(mdc_noise_12 gen GEN "--rate|48000|--noise|0.12|--seed|12|${OPS_SMALL}" RATE 48000)
mdc_case(mdc_noise_20 gen GEN "--rate|48000|--noise|0.20|--seed|13|${OPS_SMALL}" RATE 48000)

# --- error correction and CRC ------------------------------------------------------------------------------------
# one and two flipped bits in the coded block: the convolutional code repairs them (ecc_bits > 0), the CRC then passes
mdc_case(mdc_ecc_flip1 gen GEN "--rate|48000|sil|0.2|mdcflip|01|80|1234|1|sil|0.3" RATE 48000)
mdc_case(mdc_ecc_flip2 gen GEN "--rate|48000|sil|0.2|mdcflip|01|80|1234|2|sil|0.3" RATE 48000)
# a wrecked block: no packet, a crc_fail event with --report-bad, and only the count in stats without it
mdc_case(mdc_crc_fail      gen GEN "--rate|48000|sil|0.2|mdc|01|80|1234|sil|0.3|mdcbad|63|85|00FF|sil|0.3|mdc|01|00|1234|sil|0.3" RATE 48000 ARGS --report-bad)
mdc_case(mdc_crc_fail_quiet gen GEN "--rate|48000|sil|0.2|mdc|01|80|1234|sil|0.3|mdcbad|63|85|00FF|sil|0.3|mdc|01|00|1234|sil|0.3" RATE 48000)

# --- Fleetsync I (generator written from the decoder: see README.md) -----------------------------------------------
set(FS_SEQ "sil|0.2|fs1|123|4567|2345|40|sil|0.4|fs1t|100|1000|100|2000|HELLO|sil|0.4|fs1t|321|1500|321|1600|Status 7 ok|sil|0.4|fs1bad|7|9|11|40|sil|0.4")
mdc_case(fs1_8k   gen GEN "--rate|8000|${FS_SEQ}"   RATE 8000)
mdc_case(fs1_16k  twice GEN "--rate|16000|${FS_SEQ}")
mdc_case(fs1_48k  gen GEN "--rate|48000|${FS_SEQ}"  RATE 48000)
mdc_case(fs1_f32  gen GEN "--rate|16000|--f32|${FS_SEQ}" FMT f32)
mdc_case(fs1_noise gen GEN "--rate|16000|--noise|0.02|--seed|1|sil|0.2|fs1|123|4567|2345|40|sil|0.4")
mdc_case(fs1_hide_ids gen GEN "--rate|16000|${FS_SEQ}" ARGS --hide-ids)
mdc_case(fs1_only gen GEN "--rate|16000|${FS_SEQ}|${OPS_SMALL}" ARGS --no-mdc)
mdc_case(mixed    gen GEN "--rate|16000|${FS_SEQ}|${OPS_SMALL}")

# --- failure paths -------------------------------------------------------------------------------------------------
mdc_case(empty_input empty GEN "")
mdc_case(garbage_noise garbage GEN "--rate|16000|--noise|0.5|sil|8" EXTRA -DMAX_LINES=8)
mdc_case(garbage_noise_8k garbage GEN "--rate|8000|--noise|0.9|sil|8" RATE 8000 EXTRA -DMAX_LINES=8)
# input cut off in the middle of a packet: no packet, no crash, clean exit
mdc_case(truncated_mdc gen GEN "--rate|16000|--truncate|4500|sil|0.2|mdc|01|80|1234|sil|0.3")
# cut off inside block 3 of 4: not enough of the message to read the text, so nothing is delivered
mdc_case(truncated_fs1 gen GEN "--rate|16000|--truncate|6000|sil|0.2|fs1t|100|1000|100|2000|HELLO|sil|0.3")
# cut off inside block 4: the three complete blocks already hold the whole text, which is delivered
mdc_case(truncated_fs1_late gen GEN "--rate|16000|--truncate|7000|sil|0.2|fs1t|100|1000|100|2000|HELLO|sil|0.3")
mdc_case(odd_byte oddbyte GEN "--rate|16000|sil|0.2|mdc|01|80|1234|sil|0.3")
mdc_case(wrong_format_cs16 fail GEN "" FMT cs16 EXTRA "-DSTDERR_REGEX=unsupported --format")
mdc_case(wrong_format_junk fail GEN "" FMT junk EXTRA "-DSTDERR_REGEX=unsupported --format")
mdc_case(wrong_rate_low    fail GEN "" RATE 4000 EXTRA "-DSTDERR_REGEX=--rate must be")
mdc_case(wrong_rate_high   fail GEN "" RATE 192000 EXTRA "-DSTDERR_REGEX=--rate must be")
mdc_case(unknown_argument  fail GEN "" ARGS --nope EXTRA "-DSTDERR_REGEX=unknown argument")
mdc_case(no_decoder        fail GEN "" ARGS --no-mdc --no-fleetsync EXTRA "-DSTDERR_REGEX=no decoder selected")

# --- manifest and test tool ----------------------------------------------------------------------------------------
add_test(NAME manifest_info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=manifest_info -DHELPER=$<TARGET_FILE:mdc-helper>
         -DWORK=${WORKDIR} -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../mdc.helper.json -P ${RUN})
mdc_case(wav_writer wav GEN "--rate|16000|sil|0.1|mdc|01|80|1234|sil|0.1")
