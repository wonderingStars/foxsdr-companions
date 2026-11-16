# JT9 cases. Signals are made by jt9_gen (port of WSJT-X jt9sim) and pinned in expected/jt9_*.jsonl.
# SNR values are jt9sim's nominal ones (noise sigma 1, signal amplitude 10^(snr/20)); see notes-jt9.md for the sweep.
set(JT9_ARGS --mode jt9)

# jt9_raw(<name> GEN <jt9_gen arguments>): a test that writes WORK/<name>.raw, for cases that need a file input
function(jt9_raw NAME)
  cmake_parse_arguments(R "" "" "GEN" ${ARGN})
  string(REPLACE ";" "|" g "${R_GEN}")
  add_test(NAME ${NAME}_make
           COMMAND ${CMAKE_COMMAND} -DGENEXE=$<TARGET_FILE:jt9_gen> "-DGEN=${g}" -DWMTOOL=$<TARGET_FILE:wmtool>
                   -DWAV=${WORKDIR}/${NAME}.wav -DRAW=${WORKDIR}/${NAME}.raw -P ${CMAKE_CURRENT_SOURCE_DIR}/jt9_makeraw.cmake)
  set_tests_properties(${NAME}_make PROPERTIES FIXTURES_SETUP ${NAME}_raw)
endfunction()

# --- one signal, generated and decoded ------------------------------------------------------------------------------
wm_case(jt9_clean     gen GENEXE jt9_gen GEN --snr -5 --seed 1 --freq 1500 --msg "CQ K1ABC FN42" ARGS ${JT9_ARGS})
wm_case(jt9_noisy     gen GENEXE jt9_gen GEN --snr -24 --seed 2 --freq 1234.5 --dt 0.3 --msg "K1ABC W9XYZ -17" ARGS ${JT9_ARGS})
wm_case(jt9_freetext  gen GENEXE jt9_gen GEN --snr -15 --seed 3 --freq 800 --msg "TNX 73 GL" ARGS ${JT9_ARGS})
wm_case(jt9_report    gen GENEXE jt9_gen GEN --snr -20 --seed 4 --freq 2100 --msg "W9XYZ K1ABC RR73" ARGS ${JT9_ARGS})
# --- two stations in one file ---------------------------------------------------------------------------------------
wm_case(jt9_two       gen GENEXE jt9_gen
        GEN --seed 5 --sig "1000:-18:0:CQ DX K1ABC FN42" --sig "2000:-21:0.4:K1ABC W9XYZ R-12"
        ARGS ${JT9_ARGS})
# --- noise only: no decode at all -------------------------------------------------------------------------------------
wm_case(jt9_noise     gen GENEXE jt9_gen GEN --seed 6 --nosignal ARGS ${JT9_ARGS})
wm_case(jt9_noise2    gen GENEXE jt9_gen GEN --seed 7 --nosignal ARGS ${JT9_ARGS})
# --- period handling --------------------------------------------------------------------------------------------------
wm_case(jt9_offset    gen GENEXE jt9_gen GEN --snr -20 --seed 8 --freq 1700 --lead 7.25 --msg "CQ K1ABC FN42" ARGS ${JT9_ARGS} --offset 7.25)
wm_case(jt9_nooffset  gen GENEXE jt9_gen GEN --snr -20 --seed 8 --freq 1700 --lead 7.25 --msg "CQ K1ABC FN42" ARGS ${JT9_ARGS})
wm_case(jt9_partial   gen GENEXE jt9_gen GEN --snr -20 --seed 9 --freq 1700 --length 52 --msg "CQ K1ABC FN42" ARGS ${JT9_ARGS})
wm_case(jt9_short     gen GENEXE jt9_gen GEN --snr -10 --seed 9 --freq 1700 --length 30 --msg "CQ K1ABC FN42" ARGS ${JT9_ARGS})
# --- frequency range -------------------------------------------------------------------------------------------------
wm_case(jt9_range_out gen GENEXE jt9_gen GEN --snr -15 --seed 10 --freq 1500 --msg "CQ K1ABC FN42" ARGS ${JT9_ARGS} --fmin 2000 --fmax 3000)

# --- the same signal through other doors: small reads, float samples, a pipe ------------------------------------------
jt9_raw(jt9_noisy2 GEN --snr -23 --seed 11 --freq 1111 --msg "CQ K1ABC FN42")
wm_case(jt9_chunk chunk INPUT ${WORKDIR}/jt9_noisy2.raw ARGS ${JT9_ARGS} EXPECT jt9_noisy2)
wm_case(jt9_f32   f32   INPUT ${WORKDIR}/jt9_noisy2.raw ARGS ${JT9_ARGS} EXPECT jt9_noisy2)
wm_case(jt9_twice twice INPUT ${WORKDIR}/jt9_noisy2.raw ARGS ${JT9_ARGS} EXPECT jt9_noisy2)
set_tests_properties(jt9_chunk jt9_f32 jt9_twice PROPERTIES FIXTURES_REQUIRED jt9_noisy2_raw)

# --- a real recording: the JT9 sample that ships with WSJT-X (not in this repository) -----------------------------------
if(EXISTS ${FOXSDR_SAMPLES_DIR}/weakmodes/jt9_130418_1742.raw)
  wm_case(jt9_real compare INPUT ${FOXSDR_SAMPLES_DIR}/weakmodes/jt9_130418_1742.raw ARGS ${JT9_ARGS})
endif()
