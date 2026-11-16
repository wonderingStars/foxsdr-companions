# FoxSDR GPL helpers: top-level build of every helper in this repository.
#
# This file only adds the helper folders as subdirectories. Each helper keeps its own CMakeLists.txt, its own licence
# and its own tests, and still builds alone exactly as before (cmake -S <folder>). One option per helper, all ON by
# default: -DFOXSDR_HELPER_<NAME>=OFF leaves a helper out. Only MSVC (Visual Studio 2022, x64) is required.
#
# Every helper's own CTest cases are registered (they are added by the helper's folder), and this file adds one
# more case per helper, "conformance_<folder>", which runs tools/conformance.py against the built executable.
#
# This file is build glue for the aggregate and contains no upstream code; every helper folder carries the licence
# of its own upstream (see README.md, "Licences").

cmake_minimum_required(VERSION 3.20)
project(foxsdr_gpl_helpers NONE)

enable_testing()

# folder  option-name  executable target (as named in the helper's own CMakeLists.txt)
set(FOXSDR_HELPERS
  "multimon|MULTIMON|multimon-helper"
  "hfmodes|HFMODES|hfmodes"
  "trunkfmt|TRUNKFMT|trunkfmt"
  "atn|ATN|atn"
  "rtl433|RTL433|rtl433-helper"
  "packet|PACKET|packet-helper"
  "js8|JS8|js8-helper"
  "tetrapol|TETRAPOL|tetrapol-helper"
  "stdc|STDC|stdc-helper"
  "sondes|SONDES|sondes"
  "trunkctl|TRUNKCTL|trunkctl"
  "weakmodes|WEAKMODES|weakmodes-helper"
  "weakmodes2|WEAKMODES2|weakmodes2-helper"
  "fldigimodes|FLDIGIMODES|fldigimodes"
  "hfdl|HFDL|hfdl"
  "hamdrm|HAMDRM|hamdrm"
  "msumrcal|MSUMRCAL|msumrcal"
  "mdc|MDC|mdc-helper"
)

set(FOXSDR_SAMPLES_DIR "" CACHE PATH "optional folder of real recordings for the tests that use them (they are skipped when it is empty)")
option(FOXSDR_CONFORMANCE "register the protocol conformance test (tools/conformance.py) for every helper" ON)

find_package(Python3 COMPONENTS Interpreter)
# dumpbin lives beside link.exe in the Visual Studio installation.
get_filename_component(_foxsdr_linkdir "${CMAKE_LINKER}" DIRECTORY)
find_program(FOXSDR_DUMPBIN dumpbin HINTS "${_foxsdr_linkdir}")

foreach(_entry IN LISTS FOXSDR_HELPERS)
  string(REPLACE "|" ";" _entry "${_entry}")
  list(GET _entry 0 _dir)
  list(GET _entry 1 _opt)
  list(GET _entry 2 _target)
  option(FOXSDR_HELPER_${_opt} "build the ${_dir} helper" ON)
  if(FOXSDR_HELPER_${_opt})
    add_subdirectory(${_dir})
    if(FOXSDR_CONFORMANCE AND Python3_EXECUTABLE AND TARGET ${_target})
      set(_cmd ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/conformance.py
               --exe $<TARGET_FILE:${_target}>
               --manifest ${CMAKE_CURRENT_SOURCE_DIR}/${_dir}/${_dir}.helper.json)
      if(FOXSDR_DUMPBIN)
        list(APPEND _cmd --dumpbin ${FOXSDR_DUMPBIN})
      endif()
      add_test(NAME conformance_${_dir} COMMAND ${_cmd})
      set_tests_properties(conformance_${_dir} PROPERTIES TIMEOUT 300)
    endif()
  endif()
endforeach()
