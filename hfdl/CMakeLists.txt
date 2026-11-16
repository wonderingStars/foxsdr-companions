cmake_minimum_required(VERSION 3.15)
project(foxsdr_hfdl C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT MSVC)
  message(FATAL_ERROR "this helper is built with MSVC (Visual Studio 2022, x64)")
endif()

option(HFDL_ASAN "build with AddressSanitizer (debugging)" OFF)
if(HFDL_ASAN)
  add_compile_options(/fsanitize=address /Zi)
  add_link_options(/INCREMENTAL:NO)
endif()

add_subdirectory(third_party)

set(HFDL_C_SRC
  src/ac_cache.c src/ac_data.c src/acars.c src/cache.c src/crc.c src/fmtr-json.c src/fmtr-text.c
  src/globals.c src/hfnpdu.c src/lpdu.c src/metadata.c src/mpdu.c src/pdu.c src/position.c
  src/spdu.c src/systable.c src/miniconfig.c src/util.c src/libfec/viterbi27_port.c)

add_library(hfdl_core STATIC ${HFDL_C_SRC})
target_include_directories(hfdl_core PUBLIC src compat)
target_compile_options(hfdl_core PRIVATE /FIhfdl_compat.h /W3 /wd4244 /wd4267 /wd4018 /wd4996 /wd4090)
target_link_libraries(hfdl_core PUBLIC acars_static)

add_library(hfdl_dsp STATIC src/hfdl.cpp src/lq.cpp src/fastddc.cpp src/libcsdr.cpp src/libcsdr_gpl.cpp src/fft.cpp)
target_include_directories(hfdl_dsp PUBLIC src compat)
target_compile_options(hfdl_dsp PRIVATE /FIhfdl_compat.h /W3 /wd4244 /wd4267 /wd4018 /wd4996 /EHsc)
target_link_libraries(hfdl_dsp PUBLIC hfdl_core)

add_executable(hfdl src/helper_main.c src/redact.c)
target_include_directories(hfdl PRIVATE src compat)
target_compile_options(hfdl PRIVATE /FIhfdl_compat.h /W3 /wd4244 /wd4267 /wd4996)
target_link_libraries(hfdl PRIVATE hfdl_dsp)

# ---------------------------------------------------------------- tests
add_executable(hfdl_gen test/hfdl_gen.cpp)
target_compile_options(hfdl_gen PRIVATE /EHsc /W3 /wd4244 /wd4267 /wd4996)
add_executable(wavdata test/wavdata.c)
target_compile_options(wavdata PRIVATE /W3 /wd4996)

enable_testing()
add_subdirectory(test)
