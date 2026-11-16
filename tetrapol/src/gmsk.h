/*
 * gmsk.h -- GMSK demodulator of the FoxSDR "tetrapol" GPL helper (I/Q in, hard bits out)
 *
 * New code written for the helper. Upstream (tetrapol-kit) does this step in GNU Radio (demod/demod.py:
 * frequency translation, low-pass, digital.gmsk_demod with samples_per_symbol); this is a plain C version of the
 * same chain: low-pass, FM discriminator, slow frequency-offset removal, timing recovery, slicer, differential
 * decoding. The output is exactly the byte-per-bit stream that upstream tetrapol_dump reads.
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; version 2 of the License.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct gmsk_priv gmsk_t;

/** Create a demodulator for baseband I/Q of one TETRAPOL channel (8000 bit/s) sampled at `rate` Hz. */
gmsk_t *gmsk_create(double rate);
void gmsk_destroy(gmsk_t *g);

/**
 * Feed `n` complex samples (interleaved I,Q as floats, full scale about 1.0). Decided bits (0 or 1, one per byte) are
 * appended to `bits`; at most `cap` are written (more than `n * 8000 / rate + 2` are never produced).
 * @return number of bits written
 */
size_t gmsk_process(gmsk_t *g, const float *iq, size_t n, uint8_t *bits, size_t cap);

/** Number of samples in which the signal was too weak to demodulate (below the squelch floor). */
uint64_t gmsk_weak_samples(const gmsk_t *g);

#ifdef __cplusplus
}
#endif
