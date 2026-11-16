/* FoxSDR helper compatibility shim: upstream frame.c includes <endian.h> for htole64 (it only runs on little-endian
   machines in practice; Windows x64 is little-endian). Not part of tetrapol-kit. */
#pragma once
#include <stdint.h>
#define htole64(x) ((uint64_t)(x))
