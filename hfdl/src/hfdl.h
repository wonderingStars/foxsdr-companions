/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FoxSDR helper: replaces upstream's hfdl.h. The demodulator is now one synchronous object that owns the shared
 * forward FFT and all channel decoders (upstream: one thread per channel plus an FFT thread, connected by
 * block.c). Decoded PDUs are delivered through pdu_decoder_queue_push() as before.
 */
#pragma once
#include <stdint.h>
#include <stddef.h>

#define SPS 3
#define HFDL_SYMBOL_RATE 1800
#define HFDL_CHANNEL_TRANSITION_BW_HZ 250

#ifdef __cplusplus
extern "C" {
#endif

typedef struct hfdl_rx hfdl_rx;

void hfdl_init_globals(void);

// sample_rate: Hz of the complex input stream; centerfreq: Hz of the stream's centre (the offset of a channel is
// frequency - centerfreq); frequencies: assigned HFDL channel frequencies in Hz, in decoding order.
// Returns NULL (with a message on stderr) if the rate is too low or a channel lies outside the stream.
hfdl_rx *hfdl_rx_create(int32_t sample_rate, int32_t centerfreq, int32_t channel_cnt, int32_t const *frequencies);

// Feed `samples` interleaved float I/Q pairs. Decoded frames are delivered synchronously from inside this call.
void hfdl_rx_push(hfdl_rx *rx, float const *iq, size_t samples);

// Number of input samples that have been through the channelizer so far (for the status / stats lines).
uint64_t hfdl_rx_samples_processed(hfdl_rx const *rx);

void hfdl_rx_destroy(hfdl_rx *rx);

#ifdef __cplusplus
}
#endif
