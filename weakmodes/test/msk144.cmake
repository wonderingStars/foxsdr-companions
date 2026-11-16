# MSK144 cases. Pinned expectations: expected/msk144_*.jsonl (rewrite with WM_UPDATE=1, then read the diff).
# Signals: the real recordings that ship with WSJT-X (not in this repository: ${FOXSDR_SAMPLES_DIR}/weakmodes) and signals made by
# msk144_gen (a port of WSJT-X's msk144sim). SNR in the generator is in 2500 Hz at the peak of the ping.
set(MD ${FOXSDR_SAMPLES_DIR}/weakmodes)
set(M144 --mode msk144)

# --- real recordings --------------------------------------------------------------------------------------------
if(EXISTS ${MD}/msk144_181211_120500.raw AND EXISTS ${MD}/msk144_181211_120800.raw)
wm_case(msk144_real_1 twice INPUT ${MD}/msk144_181211_120500.raw ARGS ${M144})
wm_case(msk144_real_2 twice INPUT ${MD}/msk144_181211_120800.raw ARGS ${M144})
# the decoded events must not depend on the size of the reads (only the emission time t does)
wm_case(msk144_real_2_chunk chunk INPUT ${MD}/msk144_181211_120800.raw EXPECT msk144_real_2 ARGS ${M144})
wm_case(msk144_real_2_pipe  pipe  INPUT ${MD}/msk144_181211_120800.raw EXPECT msk144_real_2 ARGS ${M144})
wm_case(msk144_real_2_f32   f32   INPUT ${MD}/msk144_181211_120800.raw EXPECT msk144_real_2 ARGS ${M144})
# depth 1 = short-ping decoder only (no frame averaging): same two messages
wm_case(msk144_real_2_depth1 compare INPUT ${MD}/msk144_181211_120800.raw ARGS ${M144} --depth 1)
endif()

# --- generated signals ----------------------------------------------------------------------------------------------
# A ping 3 s into the period (the shape of makepings.f90, width 0.12 s), clean to marginal.
wm_case(msk144_gen_clean   gen GENEXE msk144_gen ARGS ${M144} GEN --msg "K1ABC W9XYZ EN37" --snr 10 --ping-times 3 --seed 1)
wm_case(msk144_gen_report  gen GENEXE msk144_gen ARGS ${M144} GEN --msg "W9XYZ K1ABC -11" --snr 5 --ping-times 4 --seed 2)
wm_case(msk144_gen_cq      gen GENEXE msk144_gen ARGS ${M144} GEN --msg "CQ K1ABC FN42" --snr 3 --freq 1530 --ping-times 3 --seed 3)
wm_case(msk144_gen_rr73    gen GENEXE msk144_gen ARGS ${M144} GEN --msg "K1ABC W9XYZ RR73" --snr 0 --freq 1450 --ping-times 5 --seed 4)
wm_case(msk144_gen_free    gen GENEXE msk144_gen ARGS ${M144} GEN --msg "TNX BOB 73 GL" --snr 2 --ping-times 2 --seed 5)
wm_case(msk144_gen_low     gen GENEXE msk144_gen ARGS ${M144} GEN --msg "K1ABC W9XYZ EN37" --snr -3 --ping-times 3 --seed 6)
# a continuous carrier (the frame-averaging path is reached on weak ones)
wm_case(msk144_gen_cw      gen GENEXE msk144_gen ARGS ${M144} GEN --msg "CQ K1ABC FN42" --snr 6 --shape cw --length 3 --seed 7)
# only noise: nothing may be decoded in 60 s (four periods)
wm_case(msk144_gen_noise   gen GENEXE msk144_gen ARGS ${M144} GEN --noise-only --length 60 --seed 8)
# the decoder must not need the signal to be on a period boundary or the carrier at 1500 Hz exactly
wm_case(msk144_gen_offset  gen GENEXE msk144_gen ARGS ${M144} --offset 0.5 GEN --msg "K1ABC W9XYZ EN37" --snr 6 --ping-times 3 --delay 4321 --seed 9)
# a weak continuous carrier (-8 dB): only the 4-to-7-frame averaging decoder gets it; depth 1 has no averaging
wm_case(msk144_gen_cw_weak        gen GENEXE msk144_gen ARGS ${M144} GEN --msg "K1ABC W9XYZ EN37" --snr -8 --shape cw --length 3 --seed 1)
wm_case(msk144_gen_cw_weak_depth1 gen GENEXE msk144_gen ARGS ${M144} --depth 1 GEN --msg "K1ABC W9XYZ EN37" --snr -8 --shape cw --length 3 --seed 1)
