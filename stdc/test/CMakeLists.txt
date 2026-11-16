# Tests for the stdc helper (CTest). Signals come from the test encoder stdc-gen, which builds Inmarsat-C frames
# from the constants upstream holds. run_case.cmake does the work; its header lists the modes.
add_executable(stdc-gen stdc_gen.cpp)

set(TE ${CMAKE_CURRENT_SOURCE_DIR}/expected)
set(RUN ${CMAKE_CURRENT_SOURCE_DIR}/run_case.cmake)
set(WORKDIR ${CMAKE_CURRENT_BINARY_DIR}/work)
set(PROTO --foxsdr-helper 1 --format s16 --rate 48000)

# stdc_case(<name> <mode> [GEN <scenario> <signal mode> <gen options...>] [ARGS <helper args...>] [EXPECT <file>] [EXTRA -Dv=x...])
function(stdc_case NAME MODE)
  cmake_parse_arguments(C "" "EXPECT;SCENARIO;SIGMODE" "GENOPTS;ARGS;EXTRA" ${ARGN})
  if(NOT C_EXPECT)
    set(C_EXPECT ${NAME})
  endif()
  set(a "${C_ARGS}")
  string(REPLACE ";" "|" a "${a}")
  string(REPLACE ";" "|" g "${C_GENOPTS}")
  set(defs -DMODE=${MODE} -DNAME=${NAME} -DHELPER=$<TARGET_FILE:stdc-helper> -DGEN=$<TARGET_FILE:stdc-gen>
      "-DARGS=${a}" "-DGENOPTS=${g}" -DSCENARIO=${C_SCENARIO} -DSIGMODE=${C_SIGMODE} -DWORK=${WORKDIR}
      -DEXPECTED=${TE}/${C_EXPECT}.jsonl)
  add_test(NAME ${NAME} COMMAND ${CMAKE_COMMAND} ${defs} ${C_EXTRA} -P ${RUN})
endfunction()

include(${CMAKE_CURRENT_SOURCE_DIR}/cases.cmake)
