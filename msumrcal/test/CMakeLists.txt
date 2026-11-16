# Each case runs test/run_case.cmake. Expected files live in test/expected; MM_UPDATE=1 in the environment rewrites them.
set(RUN ${CMAKE_COMMAND}
  -DHELPER=$<TARGET_FILE:msumrcal> -DGEN=$<TARGET_FILE:gen_msumr_lines>
  -DWORK=${CMAKE_CURRENT_BINARY_DIR} -DSRC=${CMAKE_CURRENT_SOURCE_DIR})

function(msumr_case name mode)
  add_test(NAME msumrcal_${name} COMMAND ${RUN} -DMODE=${mode} -DNAME=${name} ${ARGN} -P ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
endfunction()

msumr_case(info info)
msumr_case(pass_pinned compare)
msumr_case(pass_truth truth)
msumr_case(pass_twice twice)
msumr_case(pass_coeffs_only compare -DEXTRA=--emit,coeffs)
msumr_case(pass_cold_offset compare -DEXTRA=--emit,coeffs,--cold-offset-k,5)
msumr_case(tlm_per_line compare -DEXTRA=--lines-per-tlm,1,--emit,coeffs)
msumr_case(empty empty)
msumr_case(garbage garbage)
msumr_case(bad_records compare -DINPUT=bad_records.txt)
msumr_case(wrong_format fail -DEXTRA=--format,s16)
msumr_case(no_protocol fail -DNOPROTO=1)
msumr_case(bad_option fail -DEXTRA=--lines-per-tlm,0)
