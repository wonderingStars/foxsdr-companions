# stdc: FoxSDR GPL helper for the Inmarsat-C (STD-C) TDM channel, built on cropinghigh/inmarsatc (GPL-3.0).
cmake_minimum_required(VERSION 3.14)
project(stdc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(MSVC)
  add_compile_options(/W3 /utf-8 /EHsc)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES NOMINMAX)
else()
  add_compile_options(-Wall)
endif()

add_library(inmarsatc STATIC
  src/inmarsatc_demodulator.cpp
  src/inmarsatc_decoder.cpp
  src/inmarsatc_parser.cpp)
target_include_directories(inmarsatc PUBLIC src)

add_executable(stdc-helper src/stdc_main.cpp)
target_link_libraries(stdc-helper PRIVATE inmarsatc)
set_target_properties(stdc-helper PROPERTIES OUTPUT_NAME stdc-helper)

enable_testing()
option(STDC_BUILD_TESTS "build the test tools and register the CTest cases" ON)
if(STDC_BUILD_TESTS)
  add_subdirectory(test)
endif()
