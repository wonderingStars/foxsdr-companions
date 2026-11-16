# FT2L cases. Signals come from ft2l_gen (a port of ft2lsim from wsjtx-ft2libre), written while the tests run.
# SNR is in 2500 Hz as ft2lsim defines it. The "snr" the decoder prints is the upstream formula and reads several dB
# low on these signals (see README.md); the pinned lines record it as it is.
set(F2 --mode ft2)

wm_case(ft2l_clean      gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --freq 1500 --dt 0 --snr 99 --periods 2 ARGS ${F2})
wm_case(ft2l_noisy      gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --freq 1500 --dt 0.1 --snr -10 --seed 11 --periods 2 ARGS ${F2})
wm_case(ft2l_weak       gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --freq 1500 --dt 0.1 --snr -14 --seed 3 --periods 3 ARGS ${F2})
wm_case(ft2l_report     gen GENEXE ft2l_gen GEN --msg "K1ABC W9XYZ -11" --freq 800 --dt -0.15 --snr -8 --seed 5 --periods 2 ARGS ${F2})
wm_case(ft2l_rr73       gen GENEXE ft2l_gen GEN --msg "W9XYZ K1ABC RR73" --freq 3100 --dt 0.2 --snr -6 --seed 6 --periods 2 ARGS ${F2})
wm_case(ft2l_freetext   gen GENEXE ft2l_gen GEN --msg "TNX BOB 73 GL" --freq 2200 --dt 0 --snr -6 --seed 7 --periods 2 ARGS ${F2})
wm_case(ft2l_two        gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --freq 1000 --dt 0 --snr -8 --msg2 "CQ DL1XYZ JO62" --freq2 2100 --dt2 0.1 --snr2 -9 --seed 8 --periods 2 ARGS ${F2})
wm_case(ft2l_noise      gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --noise-only --seed 9 --periods 8 ARGS ${F2})
# range limits: the signal at 1500 Hz is outside 2000..4000 Hz
wm_case(ft2l_range_out  gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --freq 1500 --dt 0 --snr -6 --seed 10 --periods 2 ARGS ${F2} --fmin 2000 --fmax 4000)
# --offset: the audio starts 1.25 s before the first period boundary (so the first period is the second 3.75 s)
wm_case(ft2l_offset     gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --freq 1500 --dt 0 --snr -8 --seed 12 --periods 2 --lead 1.25 ARGS ${F2} --offset 1.25)
# depth 2 (no OSD) still decodes a strong signal
wm_case(ft2l_depth2     gen GENEXE ft2l_gen GEN --msg "CQ K1ABC FN42" --freq 1500 --dt 0 --snr -8 --seed 13 --periods 2 ARGS ${F2} --depth 2)

# the same file in different delivery: whole file, 777-byte pipe writes, 100-sample reads, float samples
wm_case(ft2l_noisy_twice  twice  INPUT ${CMAKE_CURRENT_BINARY_DIR}/work/ft2l_noisy.raw EXPECT ft2l_noisy ARGS ${F2})
wm_case(ft2l_noisy_pipe   pipe   INPUT ${CMAKE_CURRENT_BINARY_DIR}/work/ft2l_noisy.raw EXPECT ft2l_noisy ARGS ${F2})
wm_case(ft2l_noisy_chunk  chunk  INPUT ${CMAKE_CURRENT_BINARY_DIR}/work/ft2l_noisy.raw EXPECT ft2l_noisy ARGS ${F2})
wm_case(ft2l_noisy_f32    f32    INPUT ${CMAKE_CURRENT_BINARY_DIR}/work/ft2l_noisy.raw EXPECT ft2l_noisy ARGS ${F2})
set_tests_properties(ft2l_noisy_twice ft2l_noisy_pipe ft2l_noisy_chunk ft2l_noisy_f32 PROPERTIES DEPENDS ft2l_noisy)
