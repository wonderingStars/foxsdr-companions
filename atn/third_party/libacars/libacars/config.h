/*
 *  Replaces libacars's CMake-generated config.h (config.h.in, MIT, Copyright (c) 2018-2023 Tomasz Lemiech)
 *  for the MSVC build of this helper: no zlib, libxml2 or jansson; Windows lfind() prototype.
 */
#ifndef _CONFIG_H
#define _CONFIG_H
#define LFIND_NMEMB_SIZE_UINT 1
#endif
