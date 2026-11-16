# The CTest cases for the stdc helper. See run_case.cmake for the modes. To rewrite pinned expectations after an
# intended change:  STDC_UPDATE=1 ctest -C Release   and review the git diff.

set(SYMA   --foxsdr-helper 1 --format sym  --rate 1200)
set(S16A   --foxsdr-helper 1 --format s16  --rate 48000)
set(F32A   --foxsdr-helper 1 --format f32  --rate 48000)
set(CS16A  --foxsdr-helper 1 --format cs16 --rate 48000)
set(CF32A  --foxsdr-helper 1 --format cf32 --rate 48000)
set(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/../stdc.helper.json)

# --- symbols in: the whole decoder and parser, every packet type, a BD/BE chain, message texts ------------------------
stdc_case(all_sym          twice   SCENARIO all SIGMODE sym ARGS ${SYMA})
stdc_case(all_sym_frames   compare SCENARIO all SIGMODE sym ARGS ${SYMA} --frames)
stdc_case(all_sym_invert   compare SCENARIO all SIGMODE sym GENOPTS --invert ARGS ${SYMA} EXPECT all_sym)
stdc_case(all_sym_lead0    compare SCENARIO all SIGMODE sym GENOPTS --lead|0 ARGS ${SYMA} EXPECT all_sym_lead0)
stdc_case(all_sym_errors   compare SCENARIO all SIGMODE sym GENOPTS --errors|600 ARGS ${SYMA})
stdc_case(all_sym_hide     compare SCENARIO all SIGMODE sym ARGS ${SYMA} --hide-ids)
stdc_case(all_sym_pipe     pipe    SCENARIO all SIGMODE sym ARGS ${SYMA} EXPECT all_sym EXTRA -DCHUNK=1013)

# --- privacy: no message text, with and without --hide-ids ------------------------------------------------------------
stdc_case(privacy_default  forbid  SCENARIO all SIGMODE sym ARGS ${SYMA}
          EXPECT all_sym
          EXTRA "-DFORBID=PRIVATE-|SECRET-|shortMessage\"|\"payload\"|data8Bit"
                "-DMUST=Individual Poll|Confirmation|\"name\":\"Message\"|EGC double header, part 1|\"payloadBytes\":20")
stdc_case(privacy_hide_ids forbid  SCENARIO all SIGMODE sym ARGS ${SYMA} --hide-ids
          EXPECT all_sym_hide
          EXTRA "-DFORBID=PRIVATE-|SECRET-|\"mesId\":12657851|\"mesId\":440847|\"mesId\":8473448|\"mesId\":1193046|\"mesId\":4660|\"mesId\":305419|782904"
                "-DMUST=\"mesId\":\"#1\"|\"les\":\"#")

# --- damaged signals: no crash, no false packets --------------------------------------------------------------------------
stdc_case(damaged_data     nopackets SCENARIO all SIGMODE sym GENOPTS --damage|30 ARGS ${SYMA})
stdc_case(damaged_uw       nopackets SCENARIO all SIGMODE sym GENOPTS --kill-uw ARGS ${SYMA})
stdc_case(truncated        nopackets SCENARIO all SIGMODE sym GENOPTS --truncate|9000 ARGS ${SYMA})
stdc_case(fuzz_frames      twice     SCENARIO fuzz SIGMODE sym ARGS ${SYMA})
stdc_case(garbage_sym      garbage   ARGS ${SYMA} EXTRA -DGARBAGE_BYTES=600000 -DMAX_LINES=40)
stdc_case(garbage_s16      garbage   ARGS ${S16A} EXTRA -DGARBAGE_BYTES=3000000 -DMAX_LINES=40)

# --- audio and I/Q in: the demodulator on a synthetic 1200 baud BPSK carrier ---------------------------------------------
stdc_case(audio_s16        twice     SCENARIO all SIGMODE s16 ARGS ${S16A})
stdc_case(audio_s16_2700   counts    SCENARIO all SIGMODE s16 GENOPTS --carrier|2700 ARGS ${S16A}
          EXTRA "-DSTATS_REGEX=\"frames\":3,\"packets\":26,\"packets_bad\":0")
stdc_case(audio_cs16       twice     SCENARIO all SIGMODE cs16 GENOPTS --carrier|-100 ARGS ${CS16A})
stdc_case(audio_f32        skiphello SCENARIO all SIGMODE f32 ARGS ${F32A} EXPECT audio_s16)
# float and 16-bit samples differ by rounding, enough to change a unique-word bit error here and there ("ber"): own pin
stdc_case(audio_cf32       twice     SCENARIO all SIGMODE cf32 GENOPTS --carrier|-100 ARGS ${CF32A})
stdc_case(audio_noise      counts    SCENARIO all SIGMODE s16 GENOPTS --noise|0.25 ARGS ${S16A}
          EXTRA "-DSTATS_REGEX=\"frames\":3,\"packets\":26,\"packets_bad\":0")
stdc_case(audio_pipe       pipe      SCENARIO all SIGMODE s16 ARGS ${S16A} EXPECT audio_s16 EXTRA -DCHUNK=4097)
stdc_case(audio_oddbyte    oddbyte   SCENARIO all SIGMODE s16 ARGS ${S16A} EXPECT audio_s16)

# --- the protocol: manifest, empty input, wrong arguments ----------------------------------------------------------------
stdc_case(info             info      ARGS --foxsdr-helper-info EXTRA "-DEXPECTED=${MANIFEST}")
stdc_case(empty            empty     ARGS ${S16A})
stdc_case(fail_format      fail      ARGS --foxsdr-helper 1 --format u8 --rate 48000 EXTRA "-DSTDERR_REGEX=unsupported --format")
stdc_case(fail_rate        fail      ARGS --foxsdr-helper 1 --format s16 --rate 44100 EXTRA "-DSTDERR_REGEX=needs --rate 48000")
stdc_case(fail_rate_sym    fail      ARGS --foxsdr-helper 1 --format sym --rate 48000 EXTRA "-DSTDERR_REGEX=needs --rate 1200")
stdc_case(fail_protocol    fail      ARGS --foxsdr-helper 2 --format s16 --rate 48000 EXTRA "-DSTDERR_REGEX=unsupported protocol version")
stdc_case(fail_no_flag     fail      ARGS --format s16 --rate 48000 EXTRA "-DSTDERR_REGEX=--foxsdr-helper 1 is required")
stdc_case(fail_option      fail      ARGS --foxsdr-helper 1 --bogus EXTRA "-DSTDERR_REGEX=unknown option")

# --- a real L-band recording, when FOXSDR_SAMPLES_DIR holds one (optional, never committed): counts only -----------------------------
set(REAL_WAV "${FOXSDR_SAMPLES_DIR}/stdc-decoder/sigid-inmc-egc/Inmarsat-C TDM EGC.wav")
if(EXISTS "${REAL_WAV}")
  set(REAL_RAW ${WORKDIR}/real_iq.raw)
  file(MAKE_DIRECTORY ${WORKDIR}) # real_recording_prepare may run before any other case has made the folder
  add_test(NAME real_recording_prepare COMMAND stdc-gen wavdata "${REAL_WAV}" "${REAL_RAW}")
  stdc_case(real_recording   counts    ARGS ${CS16A}
            EXTRA "-DINPUT_FILE=${REAL_RAW}" "-DSTATS_REGEX=\"frames\":4,\"packets\":55,\"packets_bad\":0")
  set_tests_properties(real_recording PROPERTIES DEPENDS real_recording_prepare)
endif()
