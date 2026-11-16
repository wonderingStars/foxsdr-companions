/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Test helper: copies the data chunk of a RIFF/WAVE file to standard output (the helper reads raw samples). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
int main(int argc, char **argv) {
	if (argc != 2) { fprintf(stderr, "usage: wavdata file.wav\n"); return 2; }
	FILE *f = fopen(argv[1], "rb");
	if (!f) { fprintf(stderr, "wavdata: cannot open %s\n", argv[1]); return 2; }
	unsigned char hdr[12];
	if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) { fprintf(stderr, "wavdata: not a WAV file\n"); return 2; }
	_setmode(_fileno(stdout), _O_BINARY);
	for (;;) {
		unsigned char ch[8];
		if (fread(ch, 1, 8, f) != 8) { fprintf(stderr, "wavdata: no data chunk\n"); return 2; }
		uint32_t sz = ch[4] | ch[5] << 8 | ch[6] << 16 | (uint32_t)ch[7] << 24;
		if (!memcmp(ch, "data", 4)) {
			static unsigned char buf[1 << 16];
			while (sz > 0) {
				size_t n = fread(buf, 1, sz < sizeof buf ? sz : sizeof buf, f);
				if (n == 0) break;
				fwrite(buf, 1, n, stdout);
				sz -= (uint32_t)n;
			}
			return 0;
		}
		fseek(f, (long)(sz + (sz & 1)), SEEK_CUR);
	}
}
