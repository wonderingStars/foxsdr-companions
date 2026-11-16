/*
 * rawtool.c - TEST TOOL ONLY (never part of the helper): signal-file utilities for the packet helper's tests.
 * Copyright (C) 2026 the FoxSDR helper authors.  Licence: GPL-2.0-or-later.
 *
 *   rawtool wav2raw [-f32] in.wav out.raw   strip the header of a 16-bit mono WAV written by gen_packets; -f32 writes
 *                                           32-bit float samples instead of s16.  Prints "<rate> <samples>".
 *   rawtool feed in.raw CHUNK               copy the file to stdout in CHUNK byte writes, flushing each one
 *   rawtool garbage SEED NBYTES out.raw     NBYTES of pseudo-random bytes (fixed generator, same on every machine)
 *   rawtool noise SEED NSAMPLES out.raw     NSAMPLES s16 samples of loud pseudo-random "noise" (wide-band)
 *   rawtool oddbyte in.raw out.raw          the file plus one stray byte
 *   rawtool lines file                      number of lines in the file
 *   rawtool silence NSAMPLES out.raw        NSAMPLES zero s16 samples
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static uint32_t rd32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint16_t rd16(const unsigned char *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t lcg_state;
static uint32_t lcg(void)
{
	lcg_state = lcg_state * 1664525u + 1013904223u;
	return lcg_state >> 8;
}

static int wav2raw(int argc, char **argv)
{
	int f32 = 0, a = 2;
	FILE *in, *out;
	unsigned char hdr[12], ch[8];
	uint32_t rate = 0;
	unsigned bits = 0, chans = 0;
	long samples = 0;

	if (argc > a && strcmp(argv[a], "-f32") == 0) { f32 = 1; a++; }
	if (argc - a != 2) { fprintf(stderr, "usage: rawtool wav2raw [-f32] in.wav out.raw\n"); return 2; }
	in = fopen(argv[a], "rb");
	if (!in) { fprintf(stderr, "cannot open %s\n", argv[a]); return 1; }
	if (fread(hdr, 1, 12, in) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { fprintf(stderr, "not a WAV file\n"); return 1; }
	out = fopen(argv[a + 1], "wb");
	if (!out) { fprintf(stderr, "cannot write %s\n", argv[a + 1]); return 1; }
	while (fread(ch, 1, 8, in) == 8) {
		uint32_t size = rd32(ch + 4);
		if (memcmp(ch, "fmt ", 4) == 0) {
			unsigned char f[40];
			uint32_t n = size < sizeof(f) ? size : (uint32_t)sizeof(f);
			if (fread(f, 1, n, in) != n) return 1;
			if (size > n) fseek(in, (long)(size - n), SEEK_CUR);
			rate = rd32(f + 4); chans = rd16(f + 2); bits = rd16(f + 14);
		} else if (memcmp(ch, "data", 4) == 0) {
			uint32_t i;
			if (chans != 1 || bits != 16) { fprintf(stderr, "need 16-bit mono\n"); return 1; }
			for (i = 0; i + 1 < size; i += 2) {
				unsigned char s[2];
				if (fread(s, 1, 2, in) != 2) break;
				if (f32) {
					float v = (float)(int16_t)rd16(s) / 32768.0f;
					fwrite(&v, 4, 1, out);
				} else fwrite(s, 1, 2, out);
				samples++;
			}
			break;
		} else fseek(in, (long)size + (size & 1), SEEK_CUR);
	}
	fclose(out); fclose(in);
	printf("%u %ld\n", rate, samples);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: rawtool wav2raw|feed|garbage|noise|oddbyte|lines|silence ...\n"); return 2; }
	if (strcmp(argv[1], "wav2raw") == 0) return wav2raw(argc, argv);

	if (strcmp(argv[1], "feed") == 0 && argc == 4) {
		FILE *in = fopen(argv[2], "rb");
		size_t chunk = (size_t)atol(argv[3]), n;
		unsigned char *buf;
		if (!in || chunk == 0) return 1;
#ifdef _WIN32
		_setmode(_fileno(stdout), _O_BINARY);
#endif
		buf = (unsigned char *)malloc(chunk);
		while ((n = fread(buf, 1, chunk, in)) > 0) { fwrite(buf, 1, n, stdout); fflush(stdout); }
		free(buf); fclose(in);
		return 0;
	}
	if (strcmp(argv[1], "garbage") == 0 && argc == 5) {
		FILE *out = fopen(argv[4], "wb");
		long n = atol(argv[3]), i;
		lcg_state = (uint32_t)atol(argv[2]);
		if (!out) return 1;
		for (i = 0; i < n; i++) fputc((int)(lcg() & 0xff), out);
		fclose(out);
		return 0;
	}
	if (strcmp(argv[1], "noise") == 0 && argc == 5) {
		FILE *out = fopen(argv[4], "wb");
		long n = atol(argv[3]), i;
		lcg_state = (uint32_t)atol(argv[2]);
		if (!out) return 1;
		for (i = 0; i < n; i++) {
			int16_t v = (int16_t)((int)(lcg() & 0xffff) - 32768) / 2;
			fwrite(&v, 2, 1, out);
		}
		fclose(out);
		return 0;
	}
	if (strcmp(argv[1], "silence") == 0 && argc == 4) {
		FILE *out = fopen(argv[3], "wb");
		long n = atol(argv[2]), i;
		int16_t z = 0;
		if (!out) return 1;
		for (i = 0; i < n; i++) fwrite(&z, 2, 1, out);
		fclose(out);
		return 0;
	}
	if (strcmp(argv[1], "oddbyte") == 0 && argc == 4) {
		FILE *in = fopen(argv[2], "rb"), *out = fopen(argv[3], "wb");
		int c;
		if (!in || !out) return 1;
		while ((c = fgetc(in)) != EOF) fputc(c, out);
		fputc(0x55, out);
		fclose(in); fclose(out);
		return 0;
	}
	if (strcmp(argv[1], "lines") == 0 && argc == 3) {
		FILE *in = fopen(argv[2], "rb");
		long n = 0;
		int c;
		if (!in) return 1;
		while ((c = fgetc(in)) != EOF) if (c == '\n') n++;
		printf("%ld\n", n);
		fclose(in);
		return 0;
	}
	fprintf(stderr, "rawtool: bad arguments\n");
	return 2;
}
