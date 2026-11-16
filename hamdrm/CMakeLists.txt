cmake_minimum_required(VERSION 3.16)
project(hamdrm LANGUAGES C CXX)
# GPL helper: HamDRM (digital SSTV) receive modem and file reassembly, from QSSTV. See README.md, UPSTREAM.md, COPYING.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(HAMDRM_SRC
  src/main.cpp src/drmrx.cpp src/sourcedecoder.cpp src/rs_file.cpp src/rs.cpp
  src/drm.cpp src/demodulator.cpp src/channeldecode.cpp src/getmode.cpp src/getofdm.cpp src/getofdmsync.cpp
  src/getfoffsint.cpp src/getsymbolidx.cpp src/mkfacmap.cpp src/mkmscmap.cpp src/deinterleaver.cpp
  src/msdhardfac.cpp src/msdhardmsc.cpp src/viterbi_decode.cpp src/bits2bytes.cpp src/crc16_bytewise.cpp src/crc8_c.cpp
  src/filter1.cpp src/filter1c.cpp)

add_executable(hamdrm ${HAMDRM_SRC})
target_include_directories(hamdrm PRIVATE src)
if(MSVC)
  target_compile_options(hamdrm PRIVATE /W3 /utf-8 /wd4244 /wd4267 /wd4305 /wd4101 /wd4996 /wd4018 /wd4700)
  target_compile_definitions(hamdrm PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
endif()

option(HAMDRM_ASAN "build hamdrm with the MSVC address sanitiser (test runs only)" OFF)
if(MSVC AND HAMDRM_ASAN)
  target_compile_options(hamdrm PRIVATE /fsanitize=address /Zi)
  target_link_options(hamdrm PRIVATE /INCREMENTAL:NO)
endif()

add_executable(unit_test test/unit_test.cpp src/rs_file.cpp src/rs.cpp)
target_include_directories(unit_test PRIVATE src)
if(MSVC)
  target_compile_definitions(unit_test PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
  target_compile_options(unit_test PRIVATE /wd4244 /wd4267)
endif()

add_executable(fft_shim_test test/fft_shim_test.cpp src/fftw3.h)
target_include_directories(fft_shim_test PRIVATE src)

enable_testing()
add_subdirectory(tools/txgen)
add_subdirectory(test)
