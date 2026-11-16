# Tests for the multimon helper. Every case runs run_case.cmake (see its header for the modes).
# Pinned expectations are in expected/*.jsonl; to rewrite them after an intended change run
#   MM_UPDATE=1 ctest ... and review the git diff.

add_executable(multimon_sigtool tools/sigtool.c)
  set_target_properties(multimon_sigtool PROPERTIES OUTPUT_NAME sigtool) # unique target name in the joined build; file name unchanged
# narrow-band FM demodulator for checks against real I/Q recordings (not used by the CTest cases)
add_executable(fmdemod tools/fmdemod.c)
if(NOT MSVC)
  target_link_libraries(multimon_sigtool PRIVATE m)
  target_link_libraries(fmdemod PRIVATE m)
endif()

set(TD ${FOXSDR_SAMPLES_DIR}/multimon)
set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(PROTO --foxsdr-helper 1 --format s16 --rate 22050)

# mm_case(<name> <mode> [INPUT <file in ${FOXSDR_SAMPLES_DIR}/multimon>] [ARGS <helper args>...] [EXTRA <-Dvar=value>...])
# The expected file is expected/<expect>.jsonl, <expect> defaults to <name>.
function(mm_case NAME MODE)
  cmake_parse_arguments(C "" "INPUT;EXPECT" "ARGS;EXTRA" ${ARGN})
  if(C_INPUT AND NOT EXISTS "${TD}/${C_INPUT}")
    return() # upstream sample not present (see README.md, FOXSDR_SAMPLES_DIR)
  endif()
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  set(all ${PROTO} ${C_ARGS})
  string(REPLACE ";" "|" a "${all}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:multimon-helper> "-DARGS=${a}"
      -DSIGTOOL=$<TARGET_FILE:multimon_sigtool> -DGENNG=$<TARGET_FILE:gen-ng> -DWORK=${WORKDIR}
      -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  if(C_INPUT)
    list(APPEND defs -DINPUT=${TD}/${C_INPUT})
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

# --- real recordings (multimon-ng's own sample files, see data/README.md) ------------------------------------------
mm_case(pocsag512        twice INPUT pocsag512.raw  ARGS -a POCSAG512)
mm_case(pocsag1200       twice INPUT pocsag1200.raw ARGS -a POCSAG1200)
mm_case(pocsag2400       twice INPUT pocsag2400.raw ARGS -a POCSAG2400)
mm_case(pocsag1200_skyper  compare INPUT pocsag1200.raw ARGS -a POCSAG1200 -f skyper)
mm_case(pocsag1200_numeric compare INPUT pocsag1200.raw ARGS -a POCSAG1200 -f numeric)
mm_case(pocsag1200_auto    compare INPUT pocsag1200.raw ARGS -a POCSAG1200 -f auto)
mm_case(pocsag_bch_clean     compare INPUT bch_pocsag_clean.raw     ARGS -a POCSAG1200)
mm_case(pocsag_bch_1bit      compare INPUT bch_pocsag_1bit.raw      ARGS -a POCSAG1200)
mm_case(pocsag_bch_2bit      compare INPUT bch_pocsag_2bit.raw      ARGS -a POCSAG1200)
mm_case(pocsag_inv_clean     compare INPUT bch_pocsag_inv_clean.raw ARGS -a POCSAG1200)
mm_case(pocsag_inv_1bit      compare INPUT bch_pocsag_inv_1bit.raw  ARGS -a POCSAG1200)
mm_case(pocsag_inv_2bit      compare INPUT bch_pocsag_inv_2bit.raw  ARGS -a POCSAG1200)
mm_case(flex_bch_clean       compare INPUT bch_flex_clean.raw       ARGS -a FLEX)
mm_case(flex_bch_1bit        compare INPUT bch_flex_1bit.raw        ARGS -a FLEX)
mm_case(flex_bch_2bit        compare INPUT bch_flex_2bit.raw        ARGS -a FLEX)
mm_case(gsc                  twice   INPUT gsc_capture.raw          ARGS -a GSC)
mm_case(flex_next            twice   INPUT flex_next_sample.raw     ARGS -a FLEX_NEXT)
mm_case(flex_next_clock      compare INPUT flex_next_sample.raw      ARGS -a FLEX_NEXT --start-time 1700000000)
mm_case(ufsk1200             compare INPUT ufsk1200.raw             ARGS -a UFSK1200)
mm_case(x10                  compare INPUT x10.raw                  ARGS -a X10)
mm_case(all_demods_pocsag1200 compare INPUT pocsag1200.raw)

# --- signals made by multimon-ng's own generator gen-ng (deterministic) ----------------------------------------------
mm_case(gen_dtmf     gen ARGS -a DTMF EXTRA "-DGEN=-d|1234567890*#ABCD")
mm_case(gen_zvei1    gen ARGS -a ZVEI1 EXTRA "-DGEN=-z|12345")
mm_case(gen_pocsag512  gen ARGS -a POCSAG512  EXTRA "-DGEN=-P|Hello 512|-A|4242|-B|512")
mm_case(gen_pocsag1200 gen ARGS -a POCSAG1200 EXTRA "-DGEN=-P|Hello 1200|-A|1234567|-B|1200")
mm_case(gen_pocsag2400 gen ARGS -a POCSAG2400 EXTRA "-DGEN=-P|Hello 2400|-A|654321|-B|2400")
mm_case(gen_pocsag_numeric gen ARGS -a POCSAG1200 EXTRA "-DGEN=-P|0123456789|-A|2000000|-B|1200|-N")
mm_case(gen_pocsag_inverted gen ARGS -a POCSAG1200 EXTRA "-DGEN=-P|Inverted polarity|-A|31337|-B|1200|-I")
# Skyper text is the same bits as ordinary text with every character one higher (ROT-1): "Ifmmp" is "Hello".
mm_case(gen_pocsag_skyper gen ARGS -a POCSAG1200 -f skyper EXTRA "-DGEN=-P|Ifmmp!Tlzqfs|-A|1234568|-B|1200")
mm_case(gen_flex      gen ARGS -a FLEX EXTRA "-DGEN=-f|Hello FLEX|-F|1234567")
mm_case(gen_flex_err2 gen ARGS -a FLEX EXTRA "-DGEN=-f|Two bit errors|-F|765432|-e|2")
mm_case(gen_hdlc      gen ARGS -a AFSK1200 EXTRA "-DGEN=-p|hello hdlc")
mm_case(gen_uart      gen ARGS -a UFSK1200 EXTRA "-DGEN=-u|uart text")
mm_case(sig_eas       sig ARGS -a EAS EXTRA "-DSIGTOOL_ARGS=eas|ZCZC-WXR-RWT-012345-012346+0030-3170000-KFOX/NWS-")
mm_case(sig_morse     sig ARGS -a MORSE_CW EXTRA "-DSIGTOOL_ARGS=morse|CQ DE FOXSDR")

# --- the same signal through other doors: float samples, a pipe, small reads, a cut-off sample ---------------------
mm_case(f32_pocsag1200     f32   INPUT pocsag1200.raw  EXPECT pocsag1200 ARGS -a POCSAG1200)
mm_case(f32_gsc            f32   INPUT gsc_capture.raw EXPECT gsc        ARGS -a GSC)
mm_case(pipe_gsc           pipe  INPUT gsc_capture.raw EXPECT gsc        ARGS -a GSC)
mm_case(pipe_flex_next     pipe  INPUT flex_next_sample.raw EXPECT flex_next ARGS -a FLEX_NEXT EXTRA -DCHUNK=1000)
mm_case(chunk_pocsag1200   chunk INPUT pocsag1200.raw  EXPECT pocsag1200 ARGS -a POCSAG1200)
mm_case(chunk_gsc          chunk INPUT gsc_capture.raw EXPECT gsc        ARGS -a GSC)
mm_case(chunk_flex_next    chunk INPUT flex_next_sample.raw EXPECT flex_next ARGS -a FLEX_NEXT)
mm_case(oddbyte_pocsag1200 oddbyte INPUT pocsag1200.raw EXPECT pocsag1200 ARGS -a POCSAG1200)

# --- failure paths ---------------------------------------------------------------------------------------------
mm_case(empty_input   empty ARGS -a POCSAG1200)
# 1 MB of random bytes. With every demodulator on, UFSK1200 (a raw byte decoder) prints noise as upstream does:
# bounded by the input size. With the decoders FoxSDR rows use, noise must give (almost) nothing.
mm_case(garbage_all_demods garbage EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=1500)
mm_case(garbage_paging garbage ARGS -a POCSAG512 -a POCSAG1200 -a POCSAG2400 -a FLEX -a FLEX_NEXT -a GSC -a EAS
        EXTRA -DGARBAGE_BYTES=1000000 -DMAX_LINES=20)
mm_case(fail_format_cs16 fail ARGS --format cs16 EXTRA "-DSTDERR_REGEX=unsupported --format")
mm_case(fail_format_junk fail ARGS --format wav EXTRA "-DSTDERR_REGEX=unsupported --format")
mm_case(fail_rate        fail ARGS --rate 48000 EXTRA "-DSTDERR_REGEX=unsupported --rate")
mm_case(fail_unknown_demod fail ARGS -a NOSUCH EXTRA "-DSTDERR_REGEX=invalid mode")
mm_case(fail_unknown_option fail ARGS --bogus EXTRA "-DSTDERR_REGEX=unknown option")

# a run without --foxsdr-helper, with a protocol that does not exist
add_test(NAME fail_no_protocol COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=fail_no_protocol
         -DHELPER=$<TARGET_FILE:multimon-helper> "-DARGS=-a|POCSAG1200" -DWORK=${WORKDIR}
         "-DSTDERR_REGEX=--foxsdr-helper 1 is required" -P ${RUN})
add_test(NAME fail_protocol_2 COMMAND ${CMAKE_COMMAND} -DMODE=fail -DNAME=fail_protocol_2
         -DHELPER=$<TARGET_FILE:multimon-helper> "-DARGS=--foxsdr-helper|2" -DWORK=${WORKDIR}
         "-DSTDERR_REGEX=unsupported protocol" -P ${RUN})

# --- manifest and hello ----------------------------------------------------------------------------------------
add_test(NAME manifest_info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=manifest_info
         -DHELPER=$<TARGET_FILE:multimon-helper> -DWORK=${WORKDIR}
         -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../multimon.helper.json -P ${RUN})
add_test(NAME hello_fields COMMAND ${CMAKE_COMMAND} -DMODE=hello -DNAME=hello_fields
         -DHELPER=$<TARGET_FILE:multimon-helper> "-DARGS=--foxsdr-helper|1|--format|s16|--rate|22050|-a|GSC"
         -DWORK=${WORKDIR}
         "-DMUST_CONTAIN={\"helper\":\"multimon\"|\"protocol\":1|\"licence\":\"GPL-2.0-or-later\"|\"source\":\"https://github.com/EliasOenal/multimon-ng\"|\"rate\":22050|\"demods\":[\"GSC\"]"
         -P ${RUN})
