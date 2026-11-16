# CTest cases for the hfdl helper
set(H $<TARGET_FILE:hfdl>)
set(DATA ${PROJECT_BINARY_DIR}/testdata)
set(RUN ${CMAKE_COMMAND} -DHELPER=${H} -DWORKDIR=${DATA})
set(EXPECTED ${CMAKE_CURRENT_SOURCE_DIR}/expected)
file(MAKE_DIRECTORY ${DATA})

# generated signals (fixtures)
add_test(NAME hfdl.gen.all_cs16 COMMAND hfdl_gen --scenario all --format cs16 --rate 21600 -o ${DATA}/all.cs16)
add_test(NAME hfdl.gen.all_cf32 COMMAND hfdl_gen --scenario all --format cf32 --rate 21600 -o ${DATA}/all.cf32)
add_test(NAME hfdl.gen.noise COMMAND hfdl_gen --scenario noise --format cs16 --rate 21600 -o ${DATA}/noise.cs16)
add_test(NAME hfdl.gen.garbage COMMAND hfdl_gen --scenario noise --format cs16 --rate 21600 --noise 1.5 -o ${DATA}/garbage.cs16)
foreach(R 10800 48000 250000)
  add_test(NAME hfdl.gen.one_${R} COMMAND hfdl_gen --scenario one --format cs16 --rate ${R} -o ${DATA}/one_${R}.cs16)
  set_tests_properties(hfdl.gen.one_${R} PROPERTIES FIXTURES_SETUP signals)
endforeach()
set_tests_properties(hfdl.gen.all_cs16 hfdl.gen.all_cf32 hfdl.gen.noise hfdl.gen.garbage PROPERTIES FIXTURES_SETUP signals)

set(BASE --foxsdr-helper 1 --rate 21600 --centre 10000000 --channel-offset 0 --channel-offset 3000 --channel-offset -3000)
set(EMPTY ${CMAKE_CURRENT_SOURCE_DIR}/empty.bin)

function(hfdl_case name)
  cmake_parse_arguments(C "" "MODE;INPUT;EXPECT;EXPECT_PDUS" "ARGS;FORBIDDEN;REQUIRED" ${ARGN})
  add_test(NAME hfdl.${name} COMMAND ${RUN} -DCASE=${name} -DMODE=${C_MODE} "-DINPUT=${C_INPUT}" "-DEXPECT=${C_EXPECT}" "-DEXPECT_PDUS=${C_EXPECT_PDUS}"
           "-DARGS=${C_ARGS}" "-DFORBIDDEN=${C_FORBIDDEN}" "-DREQUIRED=${C_REQUIRED}" -P ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
  set_tests_properties(hfdl.${name} PROPERTIES FIXTURES_REQUIRED signals)
endfunction()

# decoding of the generated transmissions, pinned
hfdl_case(decode_cs16 MODE compare INPUT ${DATA}/all.cs16 EXPECT ${EXPECTED}/all_cs16.jsonl ARGS ${BASE} --format cs16 --start-time 1700000000 --stats --output-mpdus)
hfdl_case(decode_cf32 MODE compare INPUT ${DATA}/all.cf32 EXPECT ${EXPECTED}/all_cf32.jsonl ARGS ${BASE} --format cf32 --start-time 1700000000 --stats --output-mpdus)
hfdl_case(decode_corrupted MODE compare INPUT ${DATA}/all.cs16 EXPECT ${EXPECTED}/all_corrupted.jsonl ARGS ${BASE} --format cs16 --start-time 1700000000 --output-corrupted-pdus)
hfdl_case(system_table MODE compare INPUT ${DATA}/all.cs16 EXPECT ${EXPECTED}/all_systable.jsonl ARGS ${BASE} --format cs16 --start-time 1700000000 --system-table ${CMAKE_CURRENT_SOURCE_DIR}/../etc/systable.conf)
hfdl_case(hide_ids MODE compare INPUT ${DATA}/all.cs16 EXPECT ${EXPECTED}/all_hide_ids.jsonl ARGS ${BASE} --format cs16 --start-time 1700000000 --hide-ids)
hfdl_case(hide_ids_absent MODE absent INPUT ${DATA}/all.cs16 ARGS ${BASE} --format cs16 --hide-ids FORBIDDEN ABC123 4B1234 F00D REQUIRED "Logon" "(-)")
hfdl_case(reproducible MODE twice INPUT ${DATA}/all.cs16 ARGS ${BASE} --format cs16 --start-time 1700000000)

# other sample rates (one 300 bps SPDU each): the front end decimates in the frequency domain, so each rate takes a different path
foreach(R 10800 48000 250000)
  hfdl_case(rate_${R} MODE count INPUT ${DATA}/one_${R}.cs16 EXPECT_PDUS 1 ARGS --foxsdr-helper 1 --format cs16 --rate ${R} --channel-offset 0)
endforeach()

# failure paths
hfdl_case(empty_input MODE empty INPUT ${EMPTY} ARGS ${BASE} --format cs16)
hfdl_case(noise_only MODE empty INPUT ${DATA}/noise.cs16 ARGS ${BASE} --format cs16)
hfdl_case(garbage_input MODE garbage INPUT ${DATA}/garbage.cs16 ARGS ${BASE} --format cs16)
hfdl_case(bad_format MODE badformat INPUT ${EMPTY} ARGS --foxsdr-helper 1 --format wav --rate 21600 --channel-offset 0)
hfdl_case(missing_channel MODE badformat INPUT ${EMPTY} ARGS --foxsdr-helper 1 --format cs16 --rate 21600)
hfdl_case(rate_too_low MODE badformat INPUT ${EMPTY} ARGS --foxsdr-helper 1 --format cs16 --rate 7200 --channel-offset 0)
hfdl_case(channel_outside MODE badformat INPUT ${EMPTY} ARGS --foxsdr-helper 1 --format cs16 --rate 21600 --channel-offset 15000)
hfdl_case(bad_protocol MODE badformat INPUT ${EMPTY} ARGS --foxsdr-helper 2 --format cs16 --rate 21600 --channel-offset 0)
hfdl_case(manifest MODE info EXPECT ${CMAKE_CURRENT_SOURCE_DIR}/../hfdl.helper.json)

# a real off-air recording, counts only (sigidwiki "Skip.land" capture; kept outside the repository)
set(REAL_WAV "${FOXSDR_SAMPLES_DIR}/hfdl-decoder/sigid-hfdl-skipland/skip.land_2024-11-05T21_18_09Z_21931.00_iq.wav")
add_test(NAME hfdl.real_recording COMMAND ${RUN} -DCASE=real -DMODE=real "-DWAV=${REAL_WAV}" -DWAVDATA=$<TARGET_FILE:wavdata> -DEXPECT_PDUS=50
         "-DARGS=--foxsdr-helper;1;--format;cf32;--rate;20250;--centre;21931000;--channel-offset;-3000;--channel-offset;0;--channel-offset;3000;--channel-offset;6000"
         -P ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
