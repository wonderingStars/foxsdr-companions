# Test driver for the rtl433 helper: one script, several modes (-DMODE=...), run by CTest as
#   cmake -DMODE=... -DHELPER=... [-DARGS=a|b|c] [-DGEN=...] [-DEXPECTED=...] -P run_case.cmake
# ARGS are helper arguments separated by '|' (CTest cannot pass lists with ';' reliably).
# Set the environment variable RT_UPDATE=1 to rewrite EXPECTED from the current output instead of comparing
# (review the diff before committing it).
#
# Signal files are made by gen_signals (GENSIG) from GEN, its arguments separated by '|'; GENFMT is extra
# gen_signals options such as "--format|cs16".
#
# MODE
#   gen       GEN makes WORK/NAME.iq; stdout of the helper must equal EXPECTED byte for byte; exit code 0
#   twice     as gen, run two times: the two outputs must be identical (determinism)
#   pipe      as gen, the signal fed through a pipe in small writes (FEED, CHUNK bytes): must equal EXPECTED
#   oddbyte   as gen, plus one stray byte: stdout equals EXPECTED, stderr mentions the cut-off sample
#   hide      as gen; the output must not contain any |-separated string in MUST_NOT_CONTAIN, must contain every
#             one in MUST_CONTAIN, and (if EXPECTED is given) equal EXPECTED
#   hide2     as gen, but two runs without a salt: hashed ids differ between runs and agree within a run
#   fail      run with ARGS: exit code must be 2, stdout empty, stderr must match STDERR_REGEX
#   empty     empty stdin: stdout must equal EXPECTED (hello + stats lines), exit code 0
#   garbage   GARBAGE_BYTES of pseudo-random input: exit code 0, at most MAX_LINES lines out, no crash
#   info      --foxsdr-helper-info output must equal the manifest file EXPECTED
#   list      --list-protocols: output has LINES lines, the removed protocols are marked '-', MUST_CONTAIN found
#   hello     hello line (first stdout line) must contain every |-separated string in MUST_CONTAIN

if(NOT DEFINED NAME)
  set(NAME case)
endif()
if(NOT DEFINED CHUNK)
  set(CHUNK 777)
endif()
file(MAKE_DIRECTORY "${WORK}")
string(REPLACE "|" ";" ARGLIST "${ARGS}")
string(REPLACE "|" ";" GENFMTLIST "${GENFMT}")
set(OUT "${WORK}/${NAME}.out")
set(ERR "${WORK}/${NAME}.err")
set(IN "${WORK}/${NAME}.iq")

function(finish_compare out expected)
  if(DEFINED ENV{RT_UPDATE} AND NOT NO_UPDATE)
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
  if(in_file)
    execute_process(COMMAND "${HELPER}" ${ARGLIST} ${ARGN}
                    INPUT_FILE "${in_file}" OUTPUT_FILE "${out_file}" ERROR_FILE "${err_file}"
                    RESULT_VARIABLE rc)
  else()
    execute_process(COMMAND "${HELPER}" ${ARGLIST} ${ARGN}
                    OUTPUT_FILE "${out_file}" ERROR_FILE "${err_file}" RESULT_VARIABLE rc)
  endif()
  set(RC ${rc} PARENT_SCOPE)
endfunction()

if(DEFINED GEN)
  string(REPLACE "|" ";" GENLIST "${GEN}")
  file(REMOVE "${IN}")
  execute_process(COMMAND "${GENSIG}" --out "${IN}" ${GENFMTLIST} ${GENLIST}
                  OUTPUT_QUIET ERROR_VARIABLE generr RESULT_VARIABLE grc)
  if(NOT grc EQUAL 0 OR NOT EXISTS "${IN}")
    message(FATAL_ERROR "gen_signals failed (${grc}): ${generr}")
  endif()
endif()

if(MODE STREQUAL "gen")
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
  execute_process(COMMAND "${FEED}" chunks "${IN}" ${CHUNK}
                  COMMAND "${HELPER}" ${ARGLIST}
                  OUTPUT_FILE "${OUT}" ERROR_FILE "${ERR}" RESULTS_VARIABLE rcs)
  list(GET rcs 1 rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "helper exited with ${rc} (feed: ${rcs})")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "oddbyte")
  execute_process(COMMAND "${FEED}" oddbyte "${IN}"
                  COMMAND "${HELPER}" ${ARGLIST}
                  OUTPUT_FILE "${OUT}" ERROR_FILE "${ERR}" RESULTS_VARIABLE rcs)
  list(GET rcs 1 rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "helper exited with ${rc} (${rcs})")
  endif()
  file(READ "${ERR}" e)
  if(NOT e MATCHES "do not make a whole sample")
    message(FATAL_ERROR "stderr does not mention the cut-off sample:\n${e}")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "hide")
  run_helper("${IN}" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC}")
  endif()
  file(READ "${OUT}" body)
  string(REPLACE "|" ";" no "${MUST_NOT_CONTAIN}")
  foreach(m IN LISTS no)
    string(FIND "${body}" "${m}" pos)
    if(NOT pos EQUAL -1)
      message(FATAL_ERROR "output contains '${m}' although ids are hidden:\n${body}")
    endif()
  endforeach()
  string(REPLACE "|" ";" yes "${MUST_CONTAIN}")
  foreach(m IN LISTS yes)
    string(FIND "${body}" "${m}" pos)
    if(pos EQUAL -1)
      message(FATAL_ERROR "output lacks '${m}':\n${body}")
    endif()
  endforeach()
  if(DEFINED EXPECTED)
    finish_compare("${OUT}" "${EXPECTED}")
  endif()

elseif(MODE STREQUAL "hide2")
  run_helper("${IN}" "${OUT}.a" "${ERR}")
  run_helper("${IN}" "${OUT}.b" "${ERR}")
  foreach(r a b)
    file(READ "${OUT}.${r}" body)
    string(REGEX MATCHALL "\"id\":\"h[0-9a-f]+\"" ids_${r} "${body}")
    list(LENGTH ids_${r} n_${r})
    list(REMOVE_DUPLICATES ids_${r})
    list(LENGTH ids_${r} u_${r})
    if(n_${r} LESS 2 OR NOT u_${r} EQUAL 1)
      message(FATAL_ERROR "run ${r}: expected >=2 events with ONE hashed id, got ${n_${r}} events and ${u_${r}} distinct ids:\n${body}")
    endif()
  endforeach()
  if(ids_a STREQUAL ids_b)
    message(FATAL_ERROR "two runs without a salt gave the same hash ${ids_a}: the salt is not random")
  endif()

elseif(MODE STREQUAL "fail")
  run_helper("" "${OUT}" "${ERR}")
  file(READ "${ERR}" e)
  if(NOT RC EQUAL 2)
    message(FATAL_ERROR "expected exit code 2, got ${RC}\n${e}")
  endif()
  file(SIZE "${OUT}" osize)
  if(NOT osize EQUAL 0)
    message(FATAL_ERROR "a failing start must print nothing on stdout (${osize} bytes)")
  endif()
  if(NOT e MATCHES "${STDERR_REGEX}")
    message(FATAL_ERROR "stderr does not match '${STDERR_REGEX}':\n${e}")
  endif()

elseif(MODE STREQUAL "empty")
  file(WRITE "${WORK}/${NAME}.in" "")
  run_helper("${WORK}/${NAME}.in" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC} on empty input")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "garbage")
  execute_process(COMMAND "${FEED}" garbage ${GARBAGE_BYTES} 12345
                  COMMAND "${HELPER}" ${ARGLIST}
                  OUTPUT_FILE "${OUT}" ERROR_FILE "${ERR}" RESULTS_VARIABLE rcs)
  list(GET rcs 1 rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "helper exited with ${rc} on garbage input (${rcs})")
  endif()
  file(STRINGS "${OUT}" lines)
  list(LENGTH lines nlines)
  if(nlines GREATER ${MAX_LINES})
    message(FATAL_ERROR "garbage input produced ${nlines} lines (limit ${MAX_LINES})")
  endif()
  message(STATUS "garbage: ${nlines} lines")

elseif(MODE STREQUAL "info")
  execute_process(COMMAND "${HELPER}" --foxsdr-helper-info OUTPUT_FILE "${OUT}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "helper exited with ${rc}")
  endif()
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}" "${EXPECTED}" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    message(FATAL_ERROR "--foxsdr-helper-info differs from ${EXPECTED}")
  endif()

elseif(MODE STREQUAL "list")
  execute_process(COMMAND "${HELPER}" --list-protocols OUTPUT_FILE "${OUT}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "helper exited with ${rc}")
  endif()
  file(STRINGS "${OUT}" lines)
  list(LENGTH lines n)
  if(NOT n EQUAL ${LINES})
    message(FATAL_ERROR "--list-protocols gave ${n} lines, expected ${LINES}")
  endif()
  string(ASCII 9 TAB)
  set(removed 0)
  foreach(l IN LISTS lines)
    if(l MATCHES "^[0-9]+${TAB}-${TAB}")
      math(EXPR removed "${removed}+1")
    endif()
  endforeach()
  if(NOT removed EQUAL ${REMOVED})
    message(FATAL_ERROR "expected ${REMOVED} protocols marked as not built, found ${removed}")
  endif()
  file(READ "${OUT}" body)
  string(REPLACE "|" ";" yes "${MUST_CONTAIN}")
  foreach(m IN LISTS yes)
    string(FIND "${body}" "${m}" pos)
    if(pos EQUAL -1)
      message(FATAL_ERROR "--list-protocols lacks '${m}'")
    endif()
  endforeach()

elseif(MODE STREQUAL "hello")
  file(WRITE "${WORK}/${NAME}.in" "")
  run_helper("${WORK}/${NAME}.in" "${OUT}" "${ERR}")
  file(STRINGS "${OUT}" lines LIMIT_COUNT 1)
  list(GET lines 0 hello)
  string(REPLACE "|" ";" must "${MUST_CONTAIN}")
  foreach(m IN LISTS must)
    string(FIND "${hello}" "${m}" pos)
    if(pos EQUAL -1)
      message(FATAL_ERROR "hello line lacks '${m}':\n${hello}")
    endif()
  endforeach()

else()
  message(FATAL_ERROR "unknown MODE ${MODE}")
endif()
