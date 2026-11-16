# libacars 2.2.1 (MIT), built from the vendored source as a static library.
# zlib / libxml2 / jansson are not used (MIAM decompression, XML pretty-printing and OHMA JSON parsing are off).
file(GLOB LA_CORE_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libacars/*.c")
file(GLOB LA_ASN1_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/libacars/asn1/*.c")
set(LA_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/la_gen")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/libacars_config.h.in" "${LA_GEN_DIR}/config.h" COPYONLY)
file(WRITE "${LA_GEN_DIR}/la_version.c" "char const * const LA_VERSION = \"2.2.1\";\n")
add_library(acars_static STATIC ${LA_CORE_SRC} ${LA_ASN1_SRC} "${LA_GEN_DIR}/la_version.c")
target_include_directories(acars_static PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" PRIVATE "${LA_GEN_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/libacars")
if(MSVC)
  target_compile_options(acars_static PRIVATE /W1 /wd4244 /wd4267 /wd4996 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE)
endif()
