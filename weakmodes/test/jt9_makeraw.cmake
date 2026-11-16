# Helper script for the JT9 tests: run a generator (GENEXE with GEN arguments separated by '|') and turn its WAV into
# raw s16 with wmtool. -DGENEXE -DGEN -DWMTOOL -DWAV -DRAW
string(REPLACE "|" ";" GENLIST "${GEN}")
get_filename_component(dir "${RAW}" DIRECTORY)
file(MAKE_DIRECTORY "${dir}")
file(REMOVE "${WAV}" "${RAW}")
execute_process(COMMAND "${GENEXE}" ${GENLIST} --out "${WAV}" OUTPUT_QUIET ERROR_VARIABLE err RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "generator failed (${rc}): ${err}")
endif()
execute_process(COMMAND "${WMTOOL}" wav2raw "${WAV}" "${RAW}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "wmtool wav2raw failed")
endif()
