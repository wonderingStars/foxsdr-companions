# Test driver for the tetrapol helper: one script, several modes (-DMODE=...), run by CTest as
#   cmake -DMODE=... -DHELPER=... -DTPGEN=... -DWORK=... [-DARGS=a|b|c] [-DGEN=...] -P run_case.cmake
# ARGS and every GEN step are lists separated by '|' (CTest cannot pass lists with ';' reliably).
# Set the environment variable TP_UPDATE=1 to rewrite EXPECTED from the current output instead of comparing
# (review the git diff before committing it).
#
# A signal is made from steps:  GEN1, GEN2, GEN3 = tpgen arguments, with @IN@ (output of the previous step) and
# @OUT@ (this step's output file) replaced. The last output is the helper's stdin.
#
# MODE
#   compare   stdout must equal EXPECTED byte for byte; exit code 0
#   twice     same, run two times: the two outputs must be identical (determinism) and equal EXPECTED
#   msgonly   like compare, but only the "message" lines, without their "t" (for the same signal through other doors)
#   pipe      the signal fed through a pipe in small writes (tpgen feed, CHUNK bytes): must equal EXPECTED
#   contains  MUST_CONTAIN (|-separated strings) all appear in the output, MUST_NOT_CONTAIN (|-separated) none does
#   fail      exit code must be 2, stdout empty, stderr must match STDERR_REGEX
#   empty     empty stdin: stdout must be exactly the hello line (EXPECTED), exit code 0
#   garbage   GARBAGE_BYTES of pseudo-random input: exit code 0, at most MAX_LINES lines out, no crash
#   info      --foxsdr-helper-info output must equal the manifest file EXPECTED
#   xcheck    tpgen xcheck must exit 0 (generator against the upstream encoder)

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

# Compare as text with the line ends normalised: git stores the pinned files with LF, and a Windows checkout or a file
# written by this script may carry CR LF.
function(read_normalised path var)
  file(READ "${path}" t)
  string(REPLACE "\r" "" t "${t}")
  set(${var} "${t}" PARENT_SCOPE)
endfunction()

function(finish_compare out expected)
  if(DEFINED ENV{TP_UPDATE})
    read_normalised("${out}" got)
    file(WRITE "${expected}" "${got}")
    message(STATUS "updated ${expected}")
    return()
  endif()
  read_normalised("${out}" got)
  read_normalised("${expected}" want)
  if(NOT got STREQUAL want)
    string(LENGTH "${got}" gl)
    string(LENGTH "${want}" wl)
    string(SUBSTRING "${got}" 0 3000 got3)
    string(SUBSTRING "${want}" 0 3000 want3)
    message(FATAL_ERROR "output differs from ${expected} (${gl} vs ${wl} characters)
--- got (first 3000) ---
${got3}
--- expected (first 3000) ---
${want3}")
  endif()
endfunction()

function(run_helper in_file out_file err_file)
  execute_process(COMMAND "${HELPER}" ${ARGLIST} ${ARGN}
                  INPUT_FILE "${in_file}" OUTPUT_FILE "${out_file}" ERROR_FILE "${err_file}"
                  RESULT_VARIABLE rc)
  set(RC ${rc} PARENT_SCOPE)
endfunction()

# keep the "message" lines and drop their "t": the same decoded messages from bits and from I/Q differ only in time
function(messages_only in out)
  file(STRINGS "${in}" lines)
  set(res "")
  foreach(l IN LISTS lines)
    if(l MATCHES "\"type\":\"message\"")
      string(REGEX REPLACE "^\\{\"t\":[0-9.]+," "{" l "${l}")
      string(APPEND res "${l}\n")
    endif()
  endforeach()
  file(WRITE "${out}" "${res}")
endfunction()

# make the signal
set(prev "")
foreach(step GEN1 GEN2 GEN3)
  if(DEFINED ${step})
    set(o "${WORK}/${NAME}.${step}")
    file(REMOVE "${o}")
    string(REPLACE "|" ";" tokens "${${step}}")
    set(cmdline "")
    foreach(t IN LISTS tokens)
      string(REPLACE "@IN@" "${prev}" t "${t}")
      string(REPLACE "@OUT@" "${o}" t "${t}")
      list(APPEND cmdline "${t}")
    endforeach()
    execute_process(COMMAND "${TPGEN}" ${cmdline} RESULT_VARIABLE grc ERROR_VARIABLE generr OUTPUT_QUIET)
    if(NOT grc EQUAL 0 OR NOT EXISTS "${o}")
      message(FATAL_ERROR "tpgen ${cmdline} failed (${grc}): ${generr}")
    endif()
    set(prev "${o}")
    set(IN "${o}")
  endif()
endforeach()

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

elseif(MODE STREQUAL "msgonly")
  run_helper("${IN}" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    file(READ "${ERR}" e)
    message(FATAL_ERROR "helper exited with ${RC}\n${e}")
  endif()
  messages_only("${OUT}" "${OUT}.msgs")
  finish_compare("${OUT}.msgs" "${EXPECTED}")

elseif(MODE STREQUAL "pipe")
  execute_process(COMMAND "${TPGEN}" feed "${IN}" ${CHUNK}
                  COMMAND "${HELPER}" ${ARGLIST}
                  OUTPUT_FILE "${OUT}" ERROR_FILE "${ERR}" RESULTS_VARIABLE rcs)
  list(GET rcs 1 rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "helper exited with ${rc} (feed: ${rcs})")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "contains")
  run_helper("${IN}" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    file(READ "${ERR}" e)
    message(FATAL_ERROR "helper exited with ${RC}\n${e}")
  endif()
  file(READ "${OUT}" text)
  string(REPLACE "|" ";" must "${MUST_CONTAIN}")
  foreach(m IN LISTS must)
    string(FIND "${text}" "${m}" pos)
    if(pos EQUAL -1)
      message(FATAL_ERROR "output lacks: ${m}")
    endif()
  endforeach()
  if(DEFINED MUST_NOT_CONTAIN)
    string(REPLACE "|" ";" mustnot "${MUST_NOT_CONTAIN}")
    foreach(m IN LISTS mustnot)
      string(FIND "${text}" "${m}" pos)
      if(NOT pos EQUAL -1)
        message(FATAL_ERROR "output must not contain: ${m}")
      endif()
    endforeach()
  endif()

elseif(MODE STREQUAL "fail")
  if(NOT IN)
    file(WRITE "${WORK}/empty.raw" "")
    set(IN "${WORK}/empty.raw")
  endif()
  run_helper("${IN}" "${OUT}" "${ERR}")
  file(READ "${OUT}" o)
  file(READ "${ERR}" e)
  if(NOT RC EQUAL 2)
    message(FATAL_ERROR "expected exit code 2, got ${RC}\nstderr: ${e}")
  endif()
  if(NOT o STREQUAL "")
    message(FATAL_ERROR "expected nothing on stdout, got: ${o}")
  endif()
  if(NOT e MATCHES "${STDERR_REGEX}")
    message(FATAL_ERROR "stderr does not match '${STDERR_REGEX}':\n${e}")
  endif()

elseif(MODE STREQUAL "empty")
  file(WRITE "${WORK}/empty.raw" "")
  run_helper("${WORK}/empty.raw" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC} on empty input")
  endif()
  # the hello line and the closing stats line (all zero) are the whole output
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "garbage")
  execute_process(COMMAND "${TPGEN}" garbage 12345 ${GARBAGE_BYTES} "${WORK}/${NAME}.raw" RESULT_VARIABLE grc)
  run_helper("${WORK}/${NAME}.raw" "${OUT}" "${ERR}")
  if(NOT RC EQUAL 0)
    message(FATAL_ERROR "helper exited with ${RC} on garbage input")
  endif()
  file(STRINGS "${OUT}" lines)
  list(LENGTH lines nlines)
  if(nlines GREATER MAX_LINES)
    message(FATAL_ERROR "garbage input produced ${nlines} lines (limit ${MAX_LINES})")
  endif()
  file(SIZE "${ERR}" esize)
  if(esize GREATER 4096)
    message(FATAL_ERROR "garbage input produced ${esize} bytes on stderr")
  endif()
  message(STATUS "garbage input: ${nlines} lines out, ${esize} bytes on stderr")

elseif(MODE STREQUAL "info")
  execute_process(COMMAND "${HELPER}" --foxsdr-helper-info OUTPUT_FILE "${OUT}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "--foxsdr-helper-info exited with ${rc}")
  endif()
  finish_compare("${OUT}" "${EXPECTED}")

elseif(MODE STREQUAL "xcheck")
  execute_process(COMMAND "${TPGEN}" xcheck RESULT_VARIABLE rc OUTPUT_VARIABLE o)
  message(STATUS "${o}")
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "the generator and the upstream encoder disagree:\n${o}")
  endif()

else()
  message(FATAL_ERROR "unknown MODE ${MODE}")
endif()
