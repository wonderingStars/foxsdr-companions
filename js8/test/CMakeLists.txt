# Tests for the js8 helper. Every case runs run_case.cmake (see its header for the modes). Pinned expectations are in
# expected/*.jsonl; to rewrite them after an intended change run
#   set JS8_UPDATE=1 (any value) and  ctest --test-dir <build> -C Release
# and review the git diff.

set(TD ${FOXSDR_SAMPLES_DIR}/js8)
set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(PROTO --foxsdr-helper 1 --format s16 --rate 12000)
file(MAKE_DIRECTORY ${WORKDIR})
file(WRITE ${WORKDIR}/empty.raw "")

# js8_case(<name> <mode> [WAV <file in ${FOXSDR_SAMPLES_DIR}/js8>] [GEN <js8gen options>...] [RAW <file>] [GARBAGE <bytes>]
#          [ARGS <helper args>...] [EXPECT <pinned name>] [CHUNK n] [CONTAIN regex] [NOPROTO])
function(js8_case NAME MODE)
  cmake_parse_arguments(C "NOPROTO" "WAV;EXPECT;RAW;GARBAGE;CHUNK;CONTAIN" "GEN;ARGS" ${ARGN})
  if(C_WAV AND NOT EXISTS "${TD}/${C_WAV}")
    return() # upstream sample not present (see README.md, FOXSDR_SAMPLES_DIR)
  endif()
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  if(C_NOPROTO)
    set(all ${C_ARGS})
  else()
    set(all ${PROTO} ${C_ARGS})
  endif()
  string(REPLACE ";" "|" a "${all}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:js8-helper> -DGEN=$<TARGET_FILE:js8gen>
      "-DARGS=${a}" -DWORK=${WORKDIR} -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  if(C_WAV)
    list(APPEND defs -DWAV=${TD}/${C_WAV})
  endif()
  if(C_GEN)
    string(REPLACE ";" "|" g "${C_GEN}")
    list(APPEND defs "-DGENARGS=${g}")
  endif()
  if(C_RAW)
    list(APPEND defs -DRAW=${C_RAW})
  endif()
  if(C_GARBAGE)
    list(APPEND defs -DGARBAGE=${C_GARBAGE})
  endif()
  if(C_CHUNK)
    list(APPEND defs -DCHUNK=${C_CHUNK})
  endif()
  if(C_CONTAIN)
    list(APPEND defs "-DCONTAIN=${C_CONTAIN}")
  endif()
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} -P ${RUN})
endfunction()

# --- JS8Call's own sample recordings (see data/README.md) -------------------------------------------------------------
js8_case(real_A_1_4 twice WAV A_1_4.wav ARGS --modes A)
js8_case(real_A_2_3 compare WAV A_2_3.wav ARGS --modes A)
js8_case(real_A_2_6 compare WAV A_2_6.wav ARGS --modes A)
js8_case(real_A_2_9 twice WAV A_2_9.wav ARGS --modes A)
js8_case(real_A_2_1_nothing compare WAV A_2_1.wav ARGS --modes A)
js8_case(real_E_1_1 twice WAV E_1_1.wav ARGS --modes E)

# --- generated signals (js8gen: JS8Call's encoder and modulation, seeded noise) -------------------------------------
# every frame kind, Normal speed, no noise (decoded as sent: heartbeat/CQ, directed with and without number, /P calls,
# compound, compound directed, legacy Normal data frames with Huffman and JSC packing)
set(KINDS_A
  --clean --slots 2
  --signal "mode=A,freq=600,kind=hb,call=K1ABC,grid=FN42"
  --signal "mode=A,freq=900,kind=cq,call=W1AW,grid=FN31,cq=CQ DX"
  --signal "mode=A,freq=1200,kind=directed,from=K1ABC,to=W1AW,cmd=SNR,num=-12"
  --signal "mode=A,freq=1500,kind=directed,from=K1ABC/P,to=W1AW,cmd=ACK"
  --signal "mode=A,freq=1800,kind=compound,call=VE3/K1ABC,grid=FN03"
  --signal "mode=A,freq=2100,kind=cmpdir,call=VE3/K1ABC,cmd=SNR,num=5"
  --signal "mode=A,freq=2400,kind=data,text=HELLO WORLD THIS IS A TEST"
  --signal "mode=A,freq=2700,kind=directed,from=K1ABC,to=@ALLCALL,cmd=GRID?"
  --signal "mode=A,slot=1,freq=1000,kind=data,text=CQ CQ DE W1AW THE QUICK BROWN FOX")
js8_case(gen_kinds_clean_A compare GEN ${KINDS_A} ARGS --modes A)

# one signal per speed, no noise, all speeds decoded in one run (frame bits 7 = data flag set for the fast speeds)
set(MODES_CLEAN
  --clean --slots 1
  --signal "mode=A,freq=600,kind=hb,call=K1ABC,grid=FN42"
  --signal "mode=B,freq=1200,kind=hb,call=K2DEF,grid=EM73"
  --signal "mode=C,freq=1900,kind=directed,from=K3GHI,to=K1ABC,cmd=SNR,num=-3"
  --signal "mode=E,freq=2600,kind=data,text=SLOW MODE TEST MESSAGE"
  --signal "mode=I,freq=3300,kind=hb,call=K4JKL,grid=IO91")
js8_case(gen_modes_clean twice GEN ${MODES_CLEAN} ARGS --modes ABCEI)
# the same file with the default speeds: Ultra is not decoded (JS8Call itself ships it switched off)
js8_case(gen_modes_default_without_ultra compare GEN ${MODES_CLEAN})
js8_case(gen_fast_data compare GEN --clean --slots 1
  --signal "mode=B,freq=1500,kind=data,text=FAST DATA FRAME TEXT" ARGS --modes B)

# noisy: eight stations of several speeds below -10 dB in 2500 Hz, white noise of rms 1000
set(NOISY_MIXED
  --slots 1 --noise-rms 1000 --seed 11
  --signal "mode=A,freq=700,snr=-12,kind=hb,call=K1ABC,grid=FN42"
  --signal "mode=A,freq=1900,snr=-14,kind=directed,from=W1AW,to=K1ABC,cmd=SNR,num=-9"
  --signal "mode=B,freq=1200,snr=-10,kind=hb,call=K2DEF,grid=EM73"
  --signal "mode=C,freq=2500,snr=-8,kind=directed,from=K3GHI,to=K1ABC,cmd=ACK"
  --signal "mode=E,freq=3100,snr=-16,kind=data,text=NOISY SLOW MESSAGE")
js8_case(gen_noisy_mixed twice GEN ${NOISY_MIXED} ARGS --modes ABCE)

# the decode threshold: a Normal-speed frame 3 dB above where 11 of 12 decode, and 7 dB below it
js8_case(gen_threshold_A_decodes compare GEN --slots 1 --noise-rms 1000 --seed 5 --signal "mode=A,freq=1500,snr=-17,kind=hb,call=K1ABC,grid=FN42" ARGS --modes A)
js8_case(gen_below_threshold_A compare GEN --slots 1 --noise-rms 1000 --seed 5 --signal "mode=A,freq=1500,snr=-27,kind=hb,call=K1ABC,grid=FN42" ARGS --modes A)
js8_case(gen_noise_only compare GEN --slots 2 --noise-rms 3000 --seed 9 ARGS --modes ABCE)

# --- the same signal through other doors -----------------------------------------------------------------------------
js8_case(f32_noisy_mixed f32 GEN ${NOISY_MIXED} ARGS --modes ABCE)
js8_case(feed_noisy_mixed_chunk1001 feed RAW ${WORKDIR}/gen_noisy_mixed.raw EXPECT gen_noisy_mixed ARGS --modes ABCE CHUNK 1001)
set_tests_properties(feed_noisy_mixed_chunk1001 PROPERTIES DEPENDS gen_noisy_mixed)

# a one-minute stream: frames in several periods of two speeds (the helper drops audio it no longer needs, so this
# exercises the buffer), once from a file and once through a pipe in 9999-byte pieces; both give the same lines
set(LONG_STREAM
  --slots 4 --noise-rms 1000 --seed 21
  --signal "mode=A,slot=0,freq=800,snr=-10,kind=hb,call=K1ABC,grid=FN42"
  --signal "mode=A,slot=2,freq=1600,snr=-10,kind=directed,from=W1AW,to=K1ABC,cmd=ACK"
  --signal "mode=A,slot=3,freq=2400,snr=-10,kind=data,text=THIRD PERIOD DATA"
  --signal "mode=B,slot=1,freq=1200,snr=-8,kind=hb,call=K2DEF,grid=EM73"
  --signal "mode=B,slot=5,freq=2000,snr=-8,kind=cq,call=K2DEF,grid=EM73")
js8_case(long_stream compare GEN ${LONG_STREAM} ARGS --modes AB)
js8_case(long_stream_feed feed RAW ${WORKDIR}/long_stream.raw EXPECT long_stream ARGS --modes AB CHUNK 9999)
set_tests_properties(long_stream_feed PROPERTIES DEPENDS long_stream)

# Periods are aligned to UTC: 12 s of lead-in, stream starting 12 s before 2023-11-14T22:13:30Z (a multiple of 15, 10,
# 6 and 30 s); the frames must be found in the period that starts at 22:13:30 and timestamped so.
js8_case(start_time_aligned compare GEN --lead 12 --slots 1 --noise-rms 1000 --seed 4
  --signal "mode=A,freq=1500,snr=-10,kind=hb,call=K1ABC,grid=FN42" ARGS --modes A --start-time 1699999998)
# without the right start time the same file is not aligned and nothing is decoded
js8_case(start_time_missing compare RAW ${WORKDIR}/start_time_aligned.raw ARGS --modes A)
set_tests_properties(start_time_missing PROPERTIES DEPENDS start_time_aligned)

# the frequency window and the text events
js8_case(window_excludes compare GEN --clean --slots 1 --signal "mode=A,freq=3000,kind=hb,call=K1ABC,grid=FN42" ARGS --modes A --nfb 2000)
js8_case(text_events compare GEN --clean --slots 1 --signal "mode=A,freq=1500,kind=hb,call=K1ABC,grid=FN42" ARGS --modes A --text-events)

# --- failure paths -----------------------------------------------------------------------------------------------------
js8_case(empty_input compare RAW ${WORKDIR}/empty.raw ARGS --modes A)
js8_case(garbage_input compare GARBAGE 1440000 ARGS --modes ABCE)
js8_case(fail_format fail RAW ${WORKDIR}/empty.raw ARGS --format cs16 CONTAIN "unsupported --format")
js8_case(fail_rate fail RAW ${WORKDIR}/empty.raw ARGS --rate 48000 CONTAIN "unsupported --rate")
js8_case(fail_mode fail RAW ${WORKDIR}/empty.raw ARGS --modes AZ CONTAIN "unknown mode")
js8_case(fail_protocol fail RAW ${WORKDIR}/empty.raw NOPROTO ARGS --foxsdr-helper 2 --format s16 --rate 12000 CONTAIN "unsupported protocol")
js8_case(fail_no_protocol fail RAW ${WORKDIR}/empty.raw NOPROTO ARGS --format s16 --rate 12000 CONTAIN "missing --foxsdr-helper")
js8_case(fail_unknown_option fail RAW ${WORKDIR}/empty.raw ARGS --bogus CONTAIN "unknown option")

# the manifest the helper prints is the shipped file
add_test(NAME helper_info COMMAND ${CMAKE_COMMAND} -DMODE=info -DNAME=helper_info -DHELPER=$<TARGET_FILE:js8-helper>
         -DWORK=${WORKDIR} -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/../js8.helper.json -P ${RUN})
