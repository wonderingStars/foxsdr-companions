# Test driver for the multimon helper: one script, several modes (-DMODE=...), run by CTest as
#   cmake -DMODE=... -DHELPER=... [-DARGS=a|b|c] [-DINPUT=...] [-DEXPECTED=...] -P run_case.cmake
# ARGS are helper arguments separated by '|' (CTest cannot pass lists with ';' reliably).
# Set the environment variable MM_UPDATE=1 to rewrite EXPECTED from the current output instead of comparing
# (review the diff before committing it).
#
# MODE
#   compare   stdin from INPUT; stdout must equal EXPECTED byte for byte; exit code 0
#   twice     same, run two times: the two outputs must be identical (determinism) and equal EXPECTED
#   pipe      INPUT fed through a pipe in small writes (SIGTOOL feed, CHUNK bytes): must equal EXPECTED
#   chunk     --chunk 100 with INPUT: events equal EXPECTED once the "t" and the FLEX clock stamp are ignored
#   f32       INPUT converted to float samples, run with --format f32: must equal EXPECTED
#   gen       GEN (gen-ng arguments separated by '|') makes WORK/NAME.raw, then as compare
#   sig       SIGTOOL_ARGS (separated by '|') makes WORK/NAME.raw, then as compare
#   fail      run with ARGS (stdin from INPUT if given): exit code must be 2, stdout empty, stderr must match
#             STDERR_REGEX
#   empty     empty stdin: stdout must be exactly the hello line (EXPECTED), exit code 0
#   garbage   GARBAGE_BYTES of pseudo-random input: exit code 0, at most MAX_LINES lines out, no crash
#   oddbyte   INPUT plus one stray byte: stdout equals EXPECTED, stderr mentions the cut-off sample
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
set(IN "${INPUT}")

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

function(strip_times in out)
  # drop the "t" field and the FLEX_NEXT virtual clock stamp, which follow the read chunk size
  file(STRINGS "${in}" lines)
  set(res "")
  foreach(l IN LISTS lines)
    string(REGEX REPLACE "^\\{\"t\":[0-9.]+," "{" l "${l}")
    string(REGEX REPLACE "FLEX_NEXT\\|1970-01-01 00:[0-9][0-9]:[0-9][0-9]\\+00:00\\|" "FLEX_NEXT|" l "${l}")
    string(APPEND res "${l}\n")
  endforeach()
  file(WRITE "${out}" "${res}")
endfunction()

function(run_helper in_file out_file err_file)
  execute_process(COMMAND "${HELPER}" ${ARGLIST} ${ARGN}
                  INPUT_FILE "${in_file}" OUTPUT_FILE "${out_file}" ERROR_FILE "${err_file}"
                  RESULT_VARIABLE rc)
  set(RC ${rc} PARENT_SCOPE)
endfunction()

if(MODE STREQUAL "gen")
  string(REPLACE "|" ";" GENLIST "${GEN}")
  file(REMOVE "${WORK}/${NAME}.raw") # gen-ng will not overwrite a file
  execute_process(COMMAND "${GENNG}" -t raw ${GENLIST} "${WORK}/${NAME}.raw"
                  OUTPUT_QUIET ERROR_VARIABLE generr RESULT_VARIABLE grc)
  if(NOT grc EQUAL 0 OR NOT EXISTS "${WORK}/${NAME}.raw")
    message(FATAL_ERROR "gen-ng failed (${grc}): ${generr}")
  endif()
  set(IN "${WORK}/${NAME}.raw")
  set(MODE compare)
elseif(MODE STREQUAL "sig")
  string(REPLACE "|" ";" SIGLIST "${SIGTOOL_ARGS}")
  execute_process(COMMAND "${SIGTOOL}" ${SIGLIST} "${WORK}/${NAME}.raw" RESULT_VARIABLE grc ERROR_VARIABLE generr)
  if(NOT grc EQUAL 0)
    message(FATAL_ERROR "sigtool failed (${grc}): ${generr}")
  endif()
  set(IN "${WORK}/${NAME}.raw")
  set(MODE compare)
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

elseif(MODE STREQUAL "chunk")
  run_helper("${IN}" "${OUT}" "${ERR}" --chunk 100)
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC}")
  endif()
  strip_times("${OUT}" "${OUT}.stripped")
  # expected is from the default chunk: strip it the same way
  run_helper("${IN}" "${OUT}.default" "${ERR}")
  strip_times("${OUT}.default" "${OUT}.default.stripped")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${OUT}.stripped" "${OUT}.default.stripped" RESULT_VARIABLE same)
  if(NOT same EQUAL 0)
    file(READ "${OUT}.stripped" a)
    file(READ "${OUT}.default.stripped" b)
    message(FATAL_ERROR "decoded events depend on the read chunk size\n--- chunk 100 ---\n${a}\n--- default ---\n${b}")
  endif()
  # and the default-chunk output is the pinned one
  finish_compare("${OUT}.default" "${EXPECTED}")

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
  # the hello line names the format, so compare from line 2 on
  file(STRINGS "${OUT}" lines)
  list(REMOVE_AT lines 0)
  list(JOIN lines "\n" body)
  file(WRITE "${OUT}.body" "${body}\n")
  file(STRINGS "${EXPECTED}" elines)
  list(REMOVE_AT elines 0)
  list(JOIN elines "\n" ebody)
  file(WRITE "${OUT}.ebody" "${ebody}\n")
  finish_compare("${OUT}.body" "${OUT}.ebody")

elseif(MODE STREQUAL "fail")
  if(NOT IN)
    file(WRITE "${WORK}/empty.raw" "")
    set(IN "${WORK}/empty.raw")
  endif()
  run_helper("${IN}" "${OUT}" "${ERR}")
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
  execute_process(COMMAND "${SIGTOOL}" oddbyte "${IN}" "${WORK}/${NAME}.raw" RESULT_VARIABLE grc)
  run_helper("${WORK}/${NAME}.raw" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC}")
  endif()
  file(READ "${ERR}" e)
  if(NOT e MATCHES "middle of a sample")
    message(FATAL_ERROR "no warning about the cut-off sample on stderr:\n${e}")
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
