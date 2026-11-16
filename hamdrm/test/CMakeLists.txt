# End-to-end cases: the generator (tools/txgen, QSSTV's transmit chain) makes the audio, the helper decodes it.
# Expected files live in test/expected; MM_UPDATE=1 in the environment rewrites them.
add_executable(hamdrm_sigtool sigtool.cpp ../src/rs_file.cpp ../src/rs.cpp)
  set_target_properties(hamdrm_sigtool PROPERTIES OUTPUT_NAME sigtool) # unique target name in the joined build; file name unchanged
target_include_directories(hamdrm_sigtool PRIVATE ../src)
if(MSVC)
  target_compile_definitions(hamdrm_sigtool PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
  target_compile_options(hamdrm_sigtool PRIVATE /wd4244 /wd4267)
endif()

set(RUN ${CMAKE_COMMAND}
  -DHELPER=$<TARGET_FILE:hamdrm> -DTXGEN=$<TARGET_FILE:hamdrm_txgen> -DSIGTOOL=$<TARGET_FILE:hamdrm_sigtool>
  -DWORK=${CMAKE_CURRENT_BINARY_DIR} -DSRC=${CMAKE_CURRENT_SOURCE_DIR})

function(hd_case name mode)
  add_test(NAME hamdrm_${name} COMMAND ${RUN} -DMODE=${mode} -DNAME=${name} ${ARGN} -P ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
endfunction()

hd_case(info info)
hd_case(empty empty)
hd_case(garbage garbage)
hd_case(noise noise)
hd_case(wrong_format fail -DARGS=--foxsdr-helper,1,--format,cs16,--rate,12000)
hd_case(wrong_rate fail -DARGS=--foxsdr-helper,1,--format,s16,--rate,48000)
hd_case(no_protocol fail -DARGS=--format,s16,--rate,12000)
hd_case(bad_outdir fail -DARGS=--foxsdr-helper,1,--format,s16,--rate,12000,--out-dir,Z:/no/such/folder)

# pinned JSON for a small text file in mode B, and the same twice (deterministic)
hd_case(modeB_text compare -DTX=--mode,B)
hd_case(modeB_twice twice -DTX=--mode,B)
# every robustness mode, both bandwidths, all QAM sizes, protection and interleaver
hd_case(modeA_bw1_qam2 decode -DTX=--mode,A,--bw,1,--qam,2)
hd_case(modeE_bw0_qam0 decode -DTX=--mode,E,--bw,0,--qam,0)
hd_case(modeB_bw1_qam1_prot1 decode -DTX=--mode,B,--bw,1,--qam,1,--prot,1)
hd_case(modeA_long_interleaver decode -DTX=--mode,A,--interleaver,0)
# a multi-segment binary file, written to the output folder
hd_case(blob_outdir decode -DPAYLOAD=blob.bin -DSENDNAME=blob.bin -DEXPECT_NAME=blob.bin -DOUTDIR=1 -DTX=--mode,B)
# the name that came over the air is not trusted: no folders
hd_case(hostile_name decode -DSENDNAME=../../evil.txt -DEXPECT_NAME=evil.txt -DOUTDIR=1 -DTX=--mode,B)
# damage
hd_case(noisy decode -DNOISE_RMS=2500 -DTX=--mode,B)
hd_case(cut_signal partial -DPAYLOAD=blob.bin -DSENDNAME=blob.bin -DCUT=9 -DTX=--mode,B)
hd_case(dropout_then_repeat decode -DGAP=5,3 -DTX=--mode,B,--repeat,2)
# Reed-Solomon coded file, received complete (the erasure path is in the unit tests)
hd_case(rs2_file rs -DRSTYPE=rs2 -DPAYLOAD=blob.bin -DSENDNAME=pic.rs2 -DEXPECT_NAME=pic.bin -DOUTDIR=1 -DTX=--mode,B)
hd_case(hybrid_name hybrid -DSENDNAME=de_pic.jpg -DOUTDIR=1 -DTX=--mode,B)

add_test(NAME hamdrm_unit COMMAND unit_test)
add_test(NAME hamdrm_fft_shim COMMAND fft_shim_test)
