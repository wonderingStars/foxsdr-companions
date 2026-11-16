/*
 * gen_damage_shim.c - TEST TOOL ONLY (never part of the helper): damaged-frame cases for the FoxSDR "packet" helper.
 *
 * Copyright (C) 2026 the FoxSDR helper authors.  Licence: GPL-2.0-or-later (same as Dire Wolf).
 *
 * Dire Wolf's generator (gen_packets) has no option to corrupt a frame on purpose.  This file is linked into a second
 * copy of the generator ("gen_damaged") in place of the two functions that send FX.25 and IL2P frames:
 *
 *    fx25_send.c and il2p_send.c are compiled UNCHANGED, with  -Dfx25_send_frame=real_fx25_send_frame
 *    -Dil2p_send_frame=real_il2p_send_frame -Dtone_gen_put_bit=dmg_put_bit  so their output bits land here first.
 *    The functions below call the real ones, flip bits in a chosen number of bytes (symbols), and pass the bits on
 *    to the real modulator, tone_gen_put_bit() in gen_tone.c.
 *
 * What to damage comes from the environment variable DW_DAMAGE:
 *    x<N>   FX.25: flip one bit in each of N different bytes of the codeblock data area
 *    h<N>   IL2P : flip one bit in each of N different bytes of the 15 byte header (13 + 2 parity)
 *    p<N>   IL2P : flip one bit in each of N different bytes of the first payload block
 * Absent or 0: the frames pass through untouched.  The positions are fixed, so a file is reproducible.
 * Nothing here transmits: the output is a WAV file written by gen_packets.
 */
#include "direwolf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ax25_pad.h"
#include "fx25.h"
#include "gen_tone.h"
#include "il2p.h"

int real_fx25_send_frame(int chan, unsigned char *fbuf, int flen, int fx_mode);
int real_il2p_send_frame(int chan, packet_t pp, int max_fec, int polarity);

#define MAXBITS (8 * 4096)
static unsigned char g_bits[MAXBITS];
static int g_nbits;
static int g_fx_state[MAX_RADIO_CHANS];	/* NRZI level fx25_send.c itself ended its last frame on (its private state) */

void dmg_put_bit(int chan, int dat)
{
	if (g_nbits < MAXBITS) g_bits[g_nbits++] = (unsigned char)(dat & 1);
}

static void parse(char *kind, int *n)
{
	const char *e = getenv("DW_DAMAGE");
	*kind = 0;
	*n = 0;
	if (e != NULL && e[0] != '\0') {
		*kind = e[0];
		*n = atoi(e + 1);
	}
}

static void flush_plain(int chan)
{
	int i;
	for (i = 0; i < g_nbits; i++) {
		tone_gen_put_bit(chan, g_bits[i]);
	}
	g_nbits = 0;
}

int fx25_send_frame(int chan, unsigned char *fbuf, int flen, int fx_mode)
{
	char kind; int n, i, ret;
	int prev = g_fx_state[chan];
	g_nbits = 0;
	ret = real_fx25_send_frame(chan, fbuf, flen, fx_mode);
	if (g_nbits > 0) g_fx_state[chan] = g_bits[g_nbits - 1];	/* before any damage */
	parse(&kind, &n);
	if (ret > 0 && kind == 'x' && n > 0) {
		/* NRZI levels -> data bits (same level = 1), flip, re-encode.  Data area starts after the 64 bit tag. */
		int total = g_nbits;
		unsigned char *data = (unsigned char *)malloc((size_t)total);
		int lv = prev;
		for (i = 0; i < total; i++) { data[i] = (g_bits[i] == lv); lv = g_bits[i]; }
		for (i = 0; i < n; i++) {
			int byte = 1 + 3 * i;		/* inside the smallest (32 byte) data area for n <= 9 */
			int bit = 64 + 8 * byte + 3;
			if (bit < total) data[bit] ^= 1;
		}
		lv = prev;
		for (i = 0; i < total; i++) { if (!data[i]) lv = !lv; g_bits[i] = (unsigned char)lv; }
		free(data);
	}
	flush_plain(chan);
	return ret;
}

int il2p_send_frame(int chan, packet_t pp, int max_fec, int polarity)
{
	char kind; int n, i, ret;
	g_nbits = 0;
	ret = real_il2p_send_frame(chan, pp, max_fec, polarity);
	parse(&kind, &n);
	if (ret > 0 && (kind == 'h' || kind == 'p') && n > 0) {
		/* layout in bits: 8 preamble, 24 sync word, 15 header bytes, then payload blocks; no NRZI */
		int start = kind == 'h' ? 8 + 24 : 8 + 24 + 8 * 15;
		int stride = kind == 'h' ? 1 : 2;
		for (i = 0; i < n; i++) {
			int bit = start + 8 * (stride * i) + 3;
			if (bit < g_nbits) g_bits[bit] ^= 1;
		}
	}
	flush_plain(chan);
	return ret;
}
