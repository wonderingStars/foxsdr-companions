# Test driver for the fldigimodes helper: one script, several modes (-DMODE=...), run by CTest as
#   cmake -DMODE=... -DHELPER=... -DSIGTOOL=... [-DARGS=a|b|c] [-DGEN=...] [-DEXPECTED=...] -P run_case.cmake
# ARGS are helper arguments separated by '|' (CTest cannot pass lists with ';' reliably); GEN are `sigtool gen`
# arguments after the output file name, also separated by '|'. Set the environment variable MM_UPDATE=1 to rewrite
# EXPECTED from the current output instead of comparing (review the diff before committing it).
#
# Every case that has a GEN first makes WORK/NAME.wav with sigtool (fldigi's own transmitter, FOXSDR_TEST_TX), turns it
# into raw samples (WORK/NAME.raw) and feeds those to the helper.
#
# MODE
#   compare   stdout must equal EXPECTED byte for byte; exit code 0
#   twice     same, run two times: the two outputs must be identical (determinism)
#   pipe      the samples are fed through a pipe in small writes (CHUNK bytes): output must equal EXPECTED
#   f32       samples converted to float, run with --format f32: output equals EXPECTED from line 2 on
#   contains  the joined text of the run must contain MESSAGE (no pinned file: used for the noisy cases)
#   absent    the joined text must NOT contain MESSAGE (a signal of a different mode must not decode)
#   rsid      the output must contain every |-separated string of MUST_CONTAIN
#   fail      run with ARGS: exit code must be 2, stdout empty, stderr must match STDERR_REGEX
#   empty     empty stdin: stdout is the hello line and the stats line (EXPECTED), exit code 0
#   garbage   GARBAGE_BYTES of pseudo-random input: exit code 0, at most MAX_LINES lines out, no crash
#   oddbyte   the samples plus one stray byte: output equals EXPECTED and stderr mentions the cut-off sample
#   sigtool   the output of `sigtool SIGARGS` equals EXPECTED
#   info      --foxsdr-helper-info output must equal the manifest file EXPECTED
#   hello     hello line (first stdout line) must contain every |-separated string in MUST_CONTAIN

if(NOT DEFINED NAME)
  set(NAME case)
endif()
if(NOT DEFINED CHUNK)
  set(CHUNK 777)
endif()
file(MAKE_DIRECTORY "${WORK}")
string(REPLACE "|" ";" ARGLIST "${ARGS}")
set(OUT "${WORK}/${NAME}.out")
set(ERR "${WORK}/${NAME}.err")
set(IN "")

function(finish_compare out expected)
  if(DEFINED ENV{MM_UPDATE})
    configure_file("${out}" "${expected}" COPYONLY)
    message(STATUS "updated ${expected}")
    return()
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${out}" "${expected}" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    file(READ "${out}" got)
    file(READ "${expected}" want)
    string(LENGTH "${got}" gl)
    string(LENGTH "${want}" wl)
    message(FATAL_ERROR "output differs from ${expected} (${gl} vs ${wl} bytes)\n--- got (first 3000) ---\n${got}\n--- expected (first 3000) ---\n${want}")
  endif()
endfunction()

function(run_helper in_file out_file err_file)
  execute_process(COMMAND "${HELPER}" ${ARGLIST} ${ARGN}
                  INPUT_FILE "${in_file}" OUTPUT_FILE "${out_file}" ERROR_FILE "${err_file}"
                  RESULT_VARIABLE rc)
  set(RC ${rc} PARENT_SCOPE)
endfunction()

# --- make the signal ------------------------------------------------------------------------------------------------
if(DEFINED GEN)
  string(REPLACE "|" ";" GENLIST "${GEN}")
  # the first GEN item is the sigtool mode name
  list(GET GENLIST 0 GENMODE)
  list(REMOVE_AT GENLIST 0)
  file(REMOVE "${WORK}/${NAME}.wav" "${WORK}/${NAME}.raw")
  execute_process(COMMAND "${SIGTOOL}" gen "${GENMODE}" "${WORK}/${NAME}.wav" ${GENLIST}
                  OUTPUT_QUIET ERROR_VARIABLE generr RESULT_VARIABLE grc)
  if(NOT grc EQUAL 0)
    message(FATAL_ERROR "sigtool gen failed (${grc}): ${generr}")
  endif()
  execute_process(COMMAND "${SIGTOOL}" wav2raw "${WORK}/${NAME}.wav" "${WORK}/${NAME}.raw"
                  OUTPUT_QUIET ERROR_VARIABLE generr RESULT_VARIABLE grc)
  if(NOT grc EQUAL 0)
    message(FATAL_ERROR "sigtool wav2raw failed (${grc}): ${generr}")
  endif()
  set(IN "${WORK}/${NAME}.raw")
endif()

if(MODE STREQUAL "compare")
  run_helper("${IN}" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    file(READ "${ERR}" e)
    message(FATAL_ERROR "helper exited with ${RC}\n${e}")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "twice")
  run_helper("${IN}" "${OUT}" "${ERR}")
  run_helper("${IN}" "${OUT}.2" "${ERR}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${OUT}.2" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    message(FATAL_ERROR "two runs on the same input gave different output (${OUT} vs ${OUT}.2)")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "pipe")
  execute_process(COMMAND "${SIGTOOL}" feed "${IN}" ${CHUNK}
                  COMMAND "${HELPER}" ${ARGLIST}
                  OUTPUT_FILE "${OUT}" ERROR_FILE "${ERR}" RESULTS_VARIABLE rcs)
  list(GET rcs 1 rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "helper exited with ${rc} (feed: ${rcs})")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "f32")
  execute_process(COMMAND "${SIGTOOL}" s16tof32 "${IN}" "${WORK}/${NAME}.f32" RESULT_VARIABLE grc)
  if(NOT grc EQUAL 0)
    message(FATAL_ERROR "sigtool s16tof32 failed")
  endif()
  # the helper takes the last --format it is given, so appending f32 overrides an s16 in ARGS
  string(REPLACE "|" ";" ARGLIST "${ARGS}|--format|f32")
  run_helper("${WORK}/${NAME}.f32" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC}")
  endif()
  # the hello line names the format, and float samples are not bit-equal to 16-bit ones: the decoded TEXT must equal
  execute_process(COMMAND "${SIGTOOL}" jtext "${OUT}" OUTPUT_VARIABLE t32)
  execute_process(COMMAND "${SIGTOOL}" jtext "${EXPECTED}" OUTPUT_VARIABLE t16)
  if(NOT t32 STREQUAL t16)
    message(FATAL_ERROR "float input decoded differently\n--- f32 ---\n${t32}\n--- s16 ---\n${t16}")
  endif()
  if(t32 STREQUAL "")
    message(FATAL_ERROR "no text decoded")
  endif()

elseif(MODE STREQUAL "contains" OR MODE STREQUAL "absent")
  run_helper("${IN}" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC}")
  endif()
  execute_process(COMMAND "${SIGTOOL}" jtext "${OUT}" OUTPUT_VARIABLE text)
  string(FIND "${text}" "${MESSAGE}" pos)
  if(MODE STREQUAL "contains" AND pos EQUAL -1)
    file(READ "${OUT}" all)
    message(FATAL_ERROR "the decoded text does not contain '${MESSAGE}'\n--- decoded text ---\n${text}\n--- output ---\n${all}")
  endif()
  if(MODE STREQUAL "absent" AND NOT pos EQUAL -1)
    message(FATAL_ERROR "the decoded text contains '${MESSAGE}' but must not")
  endif()

elseif(MODE STREQUAL "rsid")
  run_helper("${IN}" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC}")
  endif()
  file(READ "${OUT}" all)
  string(REPLACE "|" ";" must "${MUST_CONTAIN}")
  foreach(m IN LISTS must)
    string(FIND "${all}" "${m}" pos)
    if(pos EQUAL -1)
      message(FATAL_ERROR "output lacks ${m}:\n${all}")
    endif()
  endforeach()
  if(DEFINED MAX_RSID)
    string(REGEX MATCHALL "\"type\":\"rsid\"" hits "${all}")
    list(LENGTH hits nh)
    if(nh GREATER MAX_RSID)
      message(FATAL_ERROR "${nh} rsid events (at most ${MAX_RSID} expected):\n${all}")
    endif()
  endif()

elseif(MODE STREQUAL "fail")
  file(WRITE "${WORK}/empty.raw" "")
  run_helper("${WORK}/empty.raw" "${OUT}" "${ERR}")
  file(READ "${OUT}" o)
  file(READ "${ERR}" e)
  if(NOT RC EQUAL 2)
    message(FATAL_ERROR "expected exit code 2, got ${RC}
stderr: ${e}")
  endif()
  if(NOT o STREQUAL "")
    message(FATAL_ERROR "expected nothing on stdout, got: ${o}")
  endif()
  if(NOT e MATCHES "${STDERR_REGEX}")
    message(FATAL_ERROR "stderr does not match '${STDERR_REGEX}':
${e}")
  endif()

elseif(MODE STREQUAL "empty")
  file(WRITE "${WORK}/empty.raw" "")
  run_helper("${WORK}/empty.raw" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC} on empty input")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "garbage")
  execute_process(COMMAND "${SIGTOOL}" garbage 12345 ${GARBAGE_BYTES} "${WORK}/${NAME}.raw" RESULT_VARIABLE grc)
  run_helper("${WORK}/${NAME}.raw" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC} on garbage input")
  endif()
  execute_process(COMMAND "${SIGTOOL}" lines "${OUT}" OUTPUT_VARIABLE nlines OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(nlines GREATER MAX_LINES)
    message(FATAL_ERROR "garbage input produced ${nlines} lines (limit ${MAX_LINES})")
  endif()
  message(STATUS "garbage input: ${nlines} lines out")

elseif(MODE STREQUAL "oddbyte")
  execute_process(COMMAND "${SIGTOOL}" oddbyte "${IN}" "${WORK}/${NAME}.odd.raw" RESULT_VARIABLE grc)
  run_helper("${WORK}/${NAME}.odd.raw" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC}")
  endif()
  file(READ "${ERR}" e)
  if(NOT e MATCHES "middle of a sample")
    message(FATAL_ERROR "no warning about the cut-off sample on stderr:\n${e}")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "sigtool")
  # the output of `sigtool <SIGARGS>` must equal EXPECTED (used for the RSID name tables)
  string(REPLACE "|" ";" SIGLIST "${SIGARGS}")
  execute_process(COMMAND "${SIGTOOL}" ${SIGLIST} OUTPUT_FILE "${OUT}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "sigtool ${SIGARGS} exited with ${rc}")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "info")
  execute_process(COMMAND "${HELPER}" --foxsdr-helper-info OUTPUT_FILE "${OUT}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--foxsdr-helper-info exited with ${rc}")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "hello")
  file(WRITE "${WORK}/empty.raw" "")
  run_helper("${WORK}/empty.raw" "${OUT}" "${ERR}")
  file(STRINGS "${OUT}" lines)
  list(GET lines 0 hello)
  string(REPLACE "|" ";" must "${MUST_CONTAIN}")
  foreach(m IN LISTS must)
    string(FIND "${hello}" "${m}" pos)
    if(pos EQUAL -1)
      message(FATAL_ERROR "hello line lacks ${m}: ${hello}")
    endif()
  endforeach()

else()
  message(FATAL_ERROR "unknown MODE ${MODE}")
endif()
