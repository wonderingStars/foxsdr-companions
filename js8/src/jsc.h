#ifndef JSC_H
#define JSC_H

/**
 * This file is part of JS8Call.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * (C) 2018 Jordan Sherer <kn4crd@gmail.com> - All Rights Reserved
 *
 * FoxSDR "js8" GPL helper: upstream jsc.h reduced to the table declaration that jsc_map.cpp (upstream, unchanged
 * apart from line endings) defines. The Qt-based compress/decompress code is ported in js8_varicode.cpp. The
 * 262144-entry "list" table (jsc_list.cpp upstream) is not used: the lookup the transmit side needs is derived from
 * this table at run time by the test tool, and the receive side only needs this table.
 **/

#include <cstdint>

typedef struct Tuple{
    char const * str;
    int size;
    int index;
} Tuple;

class JSC
{
public:
    static const std::uint32_t size = 262144;
    static const Tuple map[262144];
};

#endif // JSC_H
