# trunkfmt - FoxSDR GPL helper (port of parts of sdrtrunk).  GPL-3.0-or-later, see LICENSE.
cmake_minimum_required(VERSION 3.20)
project(trunkfmt CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /utf-8 /fp:precise)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()

# manifest.h is generated from trunkfmt.helper.json so that --foxsdr-helper-info prints the file exactly.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/trunkfmt.helper.json" TRUNKFMT_MANIFEST)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/trunkfmt.helper.json")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gen/manifest.h"
     "// generated from trunkfmt.helper.json\nstatic const char* const MANIFEST_JSON = R\"TFMT(${TRUNKFMT_MANIFEST})TFMT\";\n")

add_executable(trunkfmt
  src/main.cpp src/framer.cpp src/afsk1200.cpp src/lj1200.cpp src/tait1200.cpp src/dmr_connectplus.cpp src/fleetsync2.cpp src/mpt1327.cpp
  src/ltrdsp.cpp src/ltr.cpp src/passport.cpp)
target_include_directories(trunkfmt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/gen")

# test signal generator (not shipped): builds the test inputs from the upstream sync words and CRC constants
add_executable(trunkfmt_gen tools/gen_signals.cpp)

enable_testing()
add_test(NAME trunkfmt_regression
  COMMAND ${CMAKE_COMMAND}
    -DHELPER=$<TARGET_FILE:trunkfmt> -DGEN=$<TARGET_FILE:trunkfmt_gen>
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR} -DWORK=${CMAKE_CURRENT_BINARY_DIR}/testwork
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.cmake)
