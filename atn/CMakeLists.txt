# atn - FoxSDR GPL helper: the ATN protocol stack above VDL Mode 2 AVLC (derived from dumpvdl2 2.7.0).
# GPL-3.0-or-later (see LICENSE and UPSTREAM.md). Build: MSVC (Visual Studio 2022) + CMake, x64, Release.
cmake_minimum_required(VERSION 3.16)
project(atn C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release)
endif()

# --- dumpvdl2 (GPL-3.0-or-later) -------------------------------------------------------------------------
set(DV src/dumpvdl2)
set(DUMPVDL2_SOURCES
	${DV}/ac_data.c ${DV}/asn1-format-icao-json.c ${DV}/asn1-format-icao-text.c ${DV}/asn1-util.c
	${DV}/atn.c ${DV}/avlc.c ${DV}/bitstream.c ${DV}/clnp.c ${DV}/cotp.c ${DV}/crc.c ${DV}/esis.c
	${DV}/icao.c ${DV}/idrp.c ${DV}/reassembly.c ${DV}/tlv.c ${DV}/util.c ${DV}/x25.c ${DV}/xid.c)

# --- asn1c runtime (BSD-2-Clause) and the generated ATN ASN.1 modules -------------------------------------
include(${CMAKE_CURRENT_SOURCE_DIR}/src/asn1/CMakeLists.inc)
set(ASN1_SOURCES "")
foreach(f ${ASN_MODULE_SOURCES})
	list(APPEND ASN1_SOURCES src/asn1/${f})
endforeach()

# --- libacars (MIT): the core only; no ACARS / ARINC / ADS-C / MIAM / zlib / libxml2 / jansson -----------
set(LA third_party/libacars/libacars)
set(LIBACARS_SOURCES
	${LA}/asn1-format-common.c ${LA}/asn1-util.c ${LA}/configuration.c ${LA}/dict.c ${LA}/hash.c ${LA}/json.c ${LA}/libacars.c
	${LA}/list.c ${LA}/reassembly.c ${LA}/util.c ${LA}/vstring.c)

# one object library with everything except the main programs
add_library(atn_core OBJECT src/stubs.c ${DUMPVDL2_SOURCES} ${ASN1_SOURCES} ${LIBACARS_SOURCES})
set(ATN_INCLUDES
	${CMAKE_CURRENT_SOURCE_DIR}/compat          # stand-ins for glib / pthread / sys/time / arpa/inet / unistd
	${CMAKE_CURRENT_SOURCE_DIR}/src              # "asn1/X.h"
	${CMAKE_CURRENT_SOURCE_DIR}/src/dumpvdl2
	${CMAKE_CURRENT_SOURCE_DIR}/third_party/libacars)  # <libacars/X.h>
set(ATN_FORCE_INCLUDE "/FI${CMAKE_CURRENT_SOURCE_DIR}/compat/foxsdr_win.h")
target_include_directories(atn_core PRIVATE ${ATN_INCLUDES})
target_compile_definitions(atn_core PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
if(MSVC)
	target_compile_options(atn_core PRIVATE ${ATN_FORCE_INCLUDE} /W3 /wd4244 /wd4267 /wd4018 /wd4996 /utf-8)
	# ordinary Release code generation; /fp:precise (the default) - no fast-math, results are deterministic
endif()

add_executable(atn src/main.c $<TARGET_OBJECTS:atn_core>)
target_include_directories(atn PRIVATE ${ATN_INCLUDES})
target_compile_definitions(atn PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
	target_compile_options(atn PRIVATE ${ATN_FORCE_INCLUDE} /W3 /wd4244 /wd4267 /utf-8)
endif()

# development tool that wrote tests/apdus.txt; not built by default and not part of the helper
add_executable(atn_testgen EXCLUDE_FROM_ALL tests/testgen.c $<TARGET_OBJECTS:atn_core>)
target_include_directories(atn_testgen PRIVATE ${ATN_INCLUDES})
target_compile_definitions(atn_testgen PRIVATE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
	target_compile_options(atn_testgen PRIVATE ${ATN_FORCE_INCLUDE} /W3 /wd4244 /wd4267 /utf-8)
endif()

# --- tests --------------------------------------------------------------------------------------------------
enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
	add_test(NAME atn_pinned_output
		COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_tests.py $<TARGET_FILE:atn>
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
endif()
