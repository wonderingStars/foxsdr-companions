/*
 * packet_main.c - FoxSDR GPL helper "packet": AX.25 / FX.25 / IL2P receive-only decoder.
 *
 * Built from Dire Wolf (https://github.com/wb2osz/direwolf, John Langner WB2OSZ).
 * Derived from the test fixture atest.c: the WAV file reader is replaced by raw samples on
 * standard input and the printed report by one JSON object per frame.  The demodulators,
 * the HDLC / FX.25 / IL2P decoders and the AX.25 frame code are Dire Wolf's own, unchanged
 * (see UPSTREAM.md).
 *
 * Copyright (C) 2011-2023 John Langner, WB2OSZ   (Dire Wolf, atest.c)
 * Copyright (C) 2026 the FoxSDR helper authors   (this file)
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 * Pipe protocol version 1 (docs/PIPE-PROTOCOL.md):
 *    packet --foxsdr-helper 1 --format s16|f32 --rate <Hz> [--baud 300|1200|9600] ...
 *    stdin : raw mono audio until end of file.   stdout: JSON Lines.   stderr: log (--verbose).
 */

#include "direwolf.h"

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "ax25_pad.h"
#include "demod.h"
#include "dlq.h"
#include "fx25.h"
#include "hdlc_rec.h"
#include "il2p.h"
#include "multi_modem.h"
#include "ptt.h"
#include "textcolor.h"

#define HELPER_NAME "packet"
#define HELPER_VERSION "1.0.1"
#define HELPER_LICENCE "GPL-2.0-or-later"
#define HELPER_SOURCE "https://github.com/wb2osz/direwolf"
#define UPSTREAM_TEXT "Dire Wolf 1.8.2 (eda1383f5fa9d8ba3cb27f99db1d2c79494404c9)"

void dw_console_set_verbose(int on);
void dw_capture_begin(char *buf, size_t size);
size_t dw_capture_end(void);

/* ------------------------------------------------------------------------------------------ */
/* The manifest.  Kept identical to packet.helper.json (a test compares the two).              */
/* ------------------------------------------------------------------------------------------ */

#include "helper_manifest.h"

/* ------------------------------------------------------------------------------------------ */
/* JSON output                                                                                 */
/* ------------------------------------------------------------------------------------------ */

typedef struct {
	char *p;
	size_t len, cap;
} sbuf_t;

static void sb_init(sbuf_t *b)
{
	b->cap = 4096;
	b->len = 0;
	b->p = (char *)malloc(b->cap);
	if (b->p == NULL) exit(2);
	b->p[0] = '\0';
}

static void sb_reserve(sbuf_t *b, size_t more)
{
	if (b->len + more + 1 > b->cap) {
		while (b->len + more + 1 > b->cap) b->cap *= 2;
		b->p = (char *)realloc(b->p, b->cap);
		if (b->p == NULL) exit(2);
	}
}

static void sb_puts(sbuf_t *b, const char *s)
{
	size_t n = strlen(s);
	sb_reserve(b, n);
	memcpy(b->p + b->len, s, n + 1);
	b->len += n;
}

static void sb_printf(sbuf_t *b, const char *fmt, ...)
{
	/* Never truncate: a cut-off line is not valid JSON (the hello line was cut at 255 bytes before this was fixed). */
	char tmp[256];
	va_list ap, ap2;
	int n;
	va_start(ap, fmt);
	va_copy(ap2, ap);
	n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
	va_end(ap);
	if (n >= 0 && (size_t)n < sizeof(tmp)) {
		sb_puts(b, tmp);
	} else if (n >= 0) {
		sb_reserve(b, (size_t)n);
		vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap2);
		b->len += (size_t)n;
	}
	va_end(ap2);
}

/* JSON string of arbitrary bytes.  Valid UTF-8 sequences pass through; any other byte is read
 * as Latin-1 and written as \u00XX, so the output is always valid UTF-8 JSON. */
static void sb_json_string(sbuf_t *b, const unsigned char *s, size_t n)
{
	size_t i = 0;
	sb_puts(b, "\"");
	while (i < n) {
		unsigned char c = s[i];
		char tmp[16];
		if (c == '"') { sb_puts(b, "\\\""); i++; }
		else if (c == '\\') { sb_puts(b, "\\\\"); i++; }
		else if (c == '\n') { sb_puts(b, "\\n"); i++; }
		else if (c == '\r') { sb_puts(b, "\\r"); i++; }
		else if (c == '\t') { sb_puts(b, "\\t"); i++; }
		else if (c < 0x20 || c == 0x7f) { snprintf(tmp, sizeof(tmp), "\\u%04x", c); sb_puts(b, tmp); i++; }
		else if (c < 0x80) { char one[2] = { (char)c, 0 }; sb_puts(b, one); i++; }
		else {
			int need = 0;
			if (c >= 0xc2 && c <= 0xdf) need = 1;
			else if (c >= 0xe0 && c <= 0xef) need = 2;
			else if (c >= 0xf0 && c <= 0xf4) need = 3;
			int ok = need > 0 && i + (size_t)need < n;
			if (ok) {
				for (int k = 1; k <= need; k++) {
					if (i + k >= n || (s[i + k] & 0xc0) != 0x80) { ok = 0; break; }
				}
			}
			if (ok && need == 2) {
				/* reject overlong (E0 80..9F) and surrogates (ED A0..BF) */
				if (c == 0xe0 && s[i + 1] < 0xa0) ok = 0;
				if (c == 0xed && s[i + 1] > 0x9f) ok = 0;
			}
			if (ok && need == 3) {
				if (c == 0xf0 && s[i + 1] < 0x90) ok = 0;
				if (c == 0xf4 && s[i + 1] > 0x8f) ok = 0;
			}
			if (ok) {
				sb_reserve(b, 4);
				memcpy(b->p + b->len, s + i, need + 1);
				b->len += need + 1;
				b->p[b->len] = '\0';
				i += need + 1;
			} else {
				snprintf(tmp, sizeof(tmp), "\\u%04x", c);
				sb_puts(b, tmp);
				i++;
			}
		}
	}
	sb_puts(b, "\"");
}

static void sb_json_cstr(sbuf_t *b, const char *s)
{
	sb_json_string(b, (const unsigned char *)s, strlen(s));
}

static FILE *g_out;

static void emit(sbuf_t *b)
{
	sb_puts(b, "\n");
	fwrite(b->p, 1, b->len, g_out);
	fflush(g_out);
}

/* ------------------------------------------------------------------------------------------ */
/* State                                                                                       */
/* ------------------------------------------------------------------------------------------ */

static struct audio_s g_cfg;
static int g_rate = 0;
static int g_baud = 1200;
static int64_t g_sample_number = 0;	/* samples consumed so far */
static long g_frames[3];		/* ax25, fx25, il2p */
static long g_corrected_total[3];

static const char *const retry_names[] = { "NONE", "SINGLE", "DOUBLE", "TRIPLE", "TWO_SEP", "PASSALL" };

/* ------------------------------------------------------------------------------------------ */
/* Hooks that Dire Wolf's receive chain calls (atest.c supplies the same ones)                 */
/* ------------------------------------------------------------------------------------------ */

/* demod.c calls this to read a sample from the audio device.  We feed samples ourselves. */
int audio_get(int a)
{
	(void)a;
	return -1;
}

void ptt_set(int ot, int chan, int ptt_signal)
{
	(void)ot; (void)chan; (void)ptt_signal;
}

int get_input(int it, int chan)
{
	(void)it; (void)chan;
	return -1;
}

/* Called by multi_modem.c for every frame that passes the sanity checks.  Takes the place of
 * atest.c's dlq_rec_frame, which printed "DECODED[n] ..." text; here it builds one JSON line. */
void dlq_rec_frame(int chan, int subchan, int slice, packet_t pp, alevel_t alevel, fec_type_t fec_type, retry_t retries, char *spectrum)
{
	char stemp[AX25_MAX_ADDR_LEN * 12 + 64];
	char monitor[AX25_MAX_INFO_LEN * 6 + 512];
	char desc[256];
	unsigned char *pinfo = NULL;
	int info_len;
	int naddr;
	int kind;
	const char *type;
	sbuf_t b;
	double t;
	size_t n;

	(void)chan; (void)spectrum;

	switch (fec_type) {
	case fec_type_fx25: kind = 1; type = "fx25"; break;
	case fec_type_il2p: kind = 2; type = "il2p"; break;
	default: kind = 0; type = "ax25"; break;
	}
	g_frames[kind]++;

	t = (double)g_sample_number / (double)g_rate;

	naddr = ax25_get_num_addr(pp);
	info_len = ax25_get_info(pp, &pinfo);
	if (info_len < 0 || pinfo == NULL) info_len = 0;

	/* The monitor line Dire Wolf prints: "SRC>DST,VIA*:" then the information field through
	 * ax25_safe_print (control characters shown as <0x..>). */
	stemp[0] = '\0';
	ax25_format_addrs(pp, stemp);
	snprintf(monitor, sizeof(monitor), "%s", stemp);
	dw_capture_begin(monitor + strlen(monitor), sizeof(monitor) - strlen(monitor));
	ax25_safe_print((char *)pinfo, info_len, 0);
	n = dw_capture_end();
	(void)n;

	sb_init(&b);
	sb_printf(&b, "{\"t\":%.4f,\"type\":\"%s\",\"text\":", t, type);
	sb_json_cstr(&b, monitor);

	if (naddr >= 2) {
		int heard = ax25_get_heard(pp);
		int i;
		char a[AX25_MAX_ADDR_LEN];
		cmdres_t cr;
		int pf = 0, nr = 0, ns = 0;
		int ctl, pid;

		ax25_get_addr_with_ssid(pp, AX25_SOURCE, a);
		sb_puts(&b, ",\"src\":");
		sb_json_cstr(&b, a);
		ax25_get_addr_with_ssid(pp, AX25_DESTINATION, a);
		sb_puts(&b, ",\"dst\":");
		sb_json_cstr(&b, a);
		sb_puts(&b, ",\"via\":[");
		for (i = AX25_REPEATER_1; i < naddr; i++) {
			char v[AX25_MAX_ADDR_LEN + 2];
			ax25_get_addr_with_ssid(pp, i, v);
			if (i == heard) strcat(v, "*");
			if (i > AX25_REPEATER_1) sb_puts(&b, ",");
			sb_json_cstr(&b, v);
		}
		sb_puts(&b, "]");
		if (heard >= 0) {
			ax25_get_addr_with_ssid(pp, heard, a);
			sb_puts(&b, ",\"heard\":");
			sb_json_cstr(&b, a);
		}

		ctl = ax25_get_control(pp);
		pid = ax25_get_pid(pp);
		desc[0] = '\0';
		ax25_frame_type(pp, &cr, desc, &pf, &nr, &ns);
		if (ctl >= 0) sb_printf(&b, ",\"control\":%d", ctl);
		if (pid >= 0 && (desc[0] == 'U' ? strncmp(desc, "UI", 2) == 0 : desc[0] == 'I')) {
			sb_printf(&b, ",\"pid\":%d", pid);
		}
		sb_puts(&b, ",\"frame\":");
		sb_json_cstr(&b, desc);
		sb_printf(&b, ",\"aprs\":%s", ax25_is_aprs(pp) ? "true" : "false");
	}

	/* information field: hex, and printable text (anything outside 0x20..0x7e shown as '.') */
	{
		static const char hexd[] = "0123456789abcdef";
		sbuf_t h;
		int i;
		char *pt = (char *)malloc((size_t)info_len + 1);
		sb_init(&h);
		sb_reserve(&h, (size_t)info_len * 2 + 4);
		for (i = 0; i < info_len; i++) {
			h.p[h.len++] = hexd[(pinfo[i] >> 4) & 15];
			h.p[h.len++] = hexd[pinfo[i] & 15];
			pt[i] = (pinfo[i] >= 0x20 && pinfo[i] < 0x7f) ? (char)pinfo[i] : '.';
		}
		h.p[h.len] = '\0';
		pt[info_len] = '\0';
		sb_printf(&b, ",\"info_len\":%d,\"info_hex\":\"%s\",\"info_text\":", info_len, h.p);
		sb_json_cstr(&b, pt);
		free(pt);
		free(h.p);
	}

	if (kind != 0) {
		sb_printf(&b, ",\"corrected\":%d", (int)retries);
		g_corrected_total[kind] += (int)retries;
	} else if (g_cfg.achan[0].fix_bits != RETRY_NONE || g_cfg.achan[0].passall) {
		int r = (int)retries;
		if (r < 0 || r > 5) r = 0;
		sb_printf(&b, ",\"fix\":\"%s\"", retry_names[r]);
	}

	sb_printf(&b, ",\"level\":%d,\"mark\":%d,\"space\":%d,\"subchan\":%d,\"slice\":%d}",
		alevel.rec, alevel.mark, alevel.space, subchan, slice);
	emit(&b);
	free(b.p);

	ax25_delete(pp);
}

/* ------------------------------------------------------------------------------------------ */
/* Command line                                                                                */
/* ------------------------------------------------------------------------------------------ */

static void fail(const char *fmt, ...)
{
	va_list ap;
	fputs("packet: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	exit(2);
}

static void usage(FILE *f)
{
	fputs(
	"packet: AX.25 / FX.25 / IL2P receiver (FoxSDR GPL helper, built from Dire Wolf)\n"
	"\n"
	"  packet --foxsdr-helper 1 --format s16|f32 --rate <Hz> [options] < audio.raw\n"
	"  packet --foxsdr-helper-info\n"
	"\n"
	"  --format s16|f32   mono audio, little-endian 16-bit integer or 32-bit float (-1..1)\n"
	"  --rate N           sample rate, 8000..192000 Hz (see README for the combinations tested)\n"
	"  --baud N           300 (AFSK 1600/1800 Hz), 1200 (AFSK 1200/2200 Hz, default),\n"
	"                     9600 (G3RUH baseband)\n"
	"  --profile P        Dire Wolf AFSK demodulator profile letters, e.g. A, E+, A- (default A+)\n"
	"  --fix-bits N       0..4: try to repair AX.25 frames with a bad CRC (Dire Wolf -F; default 0)\n"
	"  --sanity MODE      ax25 (default), aprs or none: what an AX.25 frame must look like\n"
	"  --verbose          Dire Wolf's own diagnostic text on stderr\n"
	"  --centre HZ        accepted and ignored (audio helper)\n", f);
}

static int read_all_bytes(unsigned char *buf, size_t n, size_t *got)
{
	size_t r = fread(buf, 1, n, stdin);
	*got = r;
	return r > 0;
}

int main(int argc, char **argv)
{
	const char *format = NULL;
	int protocol = 0;
	int info = 0;
	int verbose = 0;
	int fix_bits = 0;
	const char *profile = NULL;
	const char *sanity = "ax25";
	int rate = 0;
	int baud = 1200;
	int i;

#ifdef _WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
#define NEEDARG() do { if (i + 1 >= argc) fail("option %s needs a value", a); } while (0)
		if (strcmp(a, "--foxsdr-helper") == 0) { NEEDARG(); protocol = atoi(argv[++i]); if (protocol != 1) fail("unsupported protocol version %s", argv[i]); }
		else if (strcmp(a, "--foxsdr-helper-info") == 0) info = 1;
		else if (strcmp(a, "--format") == 0) { NEEDARG(); format = argv[++i]; }
		else if (strcmp(a, "--rate") == 0) { NEEDARG(); rate = atoi(argv[++i]); }
		else if (strcmp(a, "--centre") == 0 || strcmp(a, "--center") == 0) { NEEDARG(); i++; }
		else if (strcmp(a, "--baud") == 0) { NEEDARG(); baud = atoi(argv[++i]); }
		else if (strcmp(a, "--profile") == 0) { NEEDARG(); profile = argv[++i]; }
		else if (strcmp(a, "--fix-bits") == 0) { NEEDARG(); fix_bits = atoi(argv[++i]); if (fix_bits < 0 || fix_bits >= RETRY_MAX) fail("--fix-bits must be 0..%d", RETRY_MAX - 1); }
		else if (strcmp(a, "--sanity") == 0) { NEEDARG(); sanity = argv[++i]; }
		else if (strcmp(a, "--verbose") == 0) verbose = 1;
		else if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) { usage(stdout); return 0; }
		else { usage(stderr); fail("unknown option %s", a); }
	}

	if (info) {
		fputs(HELPER_MANIFEST_JSON, stdout);
		fflush(stdout);
		return 0;
	}
	if (protocol != 1) { usage(stderr); fail("--foxsdr-helper 1 is required"); }
	if (format == NULL) fail("--format is required (s16 or f32)");
	if (strcmp(format, "s16") != 0 && strcmp(format, "f32") != 0) fail("unsupported --format %s (use s16 or f32)", format);
	if (rate < MIN_SAMPLES_PER_SEC || rate > MAX_SAMPLES_PER_SEC) fail("--rate must be %d..%d", MIN_SAMPLES_PER_SEC, MAX_SAMPLES_PER_SEC);
	if (baud != 300 && baud != 1200 && baud != 9600) fail("--baud must be 300, 1200 or 9600");
	if (strcmp(sanity, "ax25") != 0 && strcmp(sanity, "aprs") != 0 && strcmp(sanity, "none") != 0) fail("--sanity must be ax25, aprs or none");
	if (profile != NULL) {
		if (strlen(profile) > 8) fail("--profile is too long (at most 8 characters)");
		for (const char *p = profile; *p; p++) {
			char u = (char)toupper((unsigned char)*p);
			if (u != 'A' && u != 'B' && u != 'D' && u != 'E' && *p != '+' && *p != '-') fail("bad --profile %s (letters A, B, D, E; optional + or - at the end)", profile);
		}
		if (baud == 9600) fail("--profile applies to AFSK (300 and 1200 baud) only");
	}
	/* the AFSK modems need a few samples per bit; the G3RUH one upsamples itself */
	if (baud == 1200 && rate < 8000) fail("rate too low");
	if (baud == 9600 && rate < 19200) fail("--baud 9600 needs --rate of at least 19200");

	g_out = stdout;
	dw_console_set_verbose(verbose);
	g_rate = rate;
	g_baud = baud;

	/* ---- configure Dire Wolf exactly as atest.c does for one radio channel ---- */
	memset(&g_cfg, 0, sizeof(g_cfg));
	g_cfg.adev[0].num_channels = 1;
	g_cfg.adev[0].samples_per_sec = rate;
	g_cfg.adev[0].bits_per_sample = 16;
	g_cfg.chan_medium[0] = MEDIUM_RADIO;
	g_cfg.achan[0].modem_type = MODEM_AFSK;
	g_cfg.achan[0].mark_freq = DEFAULT_MARK_FREQ;
	g_cfg.achan[0].space_freq = DEFAULT_SPACE_FREQ;
	g_cfg.achan[0].baud = baud;
	strlcpy(g_cfg.achan[0].profiles, "A", sizeof(g_cfg.achan[0].profiles));
	g_cfg.achan[0].num_freq = 1;
	g_cfg.achan[0].offset = 0;
	g_cfg.achan[0].fix_bits = (retry_t)fix_bits;
	g_cfg.achan[0].passall = 0;
	g_cfg.achan[0].sanity_test = strcmp(sanity, "aprs") == 0 ? SANITY_APRS : strcmp(sanity, "none") == 0 ? SANITY_NONE : SANITY_AX25;

	if (baud == 300) {			/* HF SSB packet, atest.c "baud < 600" branch */
		g_cfg.achan[0].mark_freq = 1600;
		g_cfg.achan[0].space_freq = 1800;
	} else if (baud == 9600) {		/* atest.c final else branch */
		g_cfg.achan[0].modem_type = MODEM_SCRAMBLE;
		g_cfg.achan[0].mark_freq = 0;
		g_cfg.achan[0].space_freq = 0;
		strlcpy(g_cfg.achan[0].profiles, " ", sizeof(g_cfg.achan[0].profiles));
	}
	if (profile != NULL) strlcpy(g_cfg.achan[0].profiles, profile, sizeof(g_cfg.achan[0].profiles));

	fx25_init(0);
	il2p_init(0);
	multi_modem_init(&g_cfg);

	{
		sbuf_t b;
		sb_init(&b);
		sb_printf(&b, "{\"helper\":\"%s\",\"version\":\"%s\",\"protocol\":1,\"licence\":\"%s\",\"source\":\"%s\","
			"\"upstream\":\"%s\",\"format\":\"%s\",\"rate\":%d,\"baud\":%d,\"modem\":\"%s\",\"profile\":\"%s\","
			"\"sanity\":\"%s\",\"fix_bits\":%d}",
			HELPER_NAME, HELPER_VERSION, HELPER_LICENCE, HELPER_SOURCE, UPSTREAM_TEXT, format, rate, baud,
			baud == 9600 ? "g3ruh" : "afsk", g_cfg.achan[0].profiles, sanity, fix_bits);
		emit(&b);
		free(b.p);
	}

	/* ---- main loop ---- */
	{
		const int is_f32 = strcmp(format, "f32") == 0;
		const size_t bytes_per = is_f32 ? 4 : 2;
		enum { CHUNK = 65536 };
		unsigned char *buf = (unsigned char *)malloc(CHUNK);
		size_t have = 0, got;

		if (buf == NULL) fail("out of memory");
		while (read_all_bytes(buf + have, CHUNK - have, &got)) {
			size_t total = have + got;
			size_t nsamp = total / bytes_per;
			size_t k;
			for (k = 0; k < nsamp; k++) {
				int sam;
				if (!is_f32) {
					sam = (int16_t)(buf[2 * k] | (buf[2 * k + 1] << 8));
				} else {
					uint32_t u = (uint32_t)buf[4 * k] | ((uint32_t)buf[4 * k + 1] << 8) |
						((uint32_t)buf[4 * k + 2] << 16) | ((uint32_t)buf[4 * k + 3] << 24);
					float f;
					memcpy(&f, &u, 4);
					if (!(f == f)) f = 0.0f;		/* NaN */
					f *= 32768.0f;
					if (f > 32767.0f) f = 32767.0f;
					if (f < -32768.0f) f = -32768.0f;
					sam = (int)f;
				}
				g_sample_number++;
				multi_modem_process_sample(0, sam);
			}
			have = total - nsamp * bytes_per;
			if (have > 0) memmove(buf, buf + nsamp * bytes_per, have);
		}
		if (have > 0) fprintf(stderr, "packet: input ended in the middle of a sample (%u stray byte(s) ignored)\n", (unsigned)have);
		free(buf);
	}

	/* A frame is released a few bit times after its last bit, and IL2P waits for one more bit
	 * ("be sure to send an extra bit to flush it out" - il2p_rec.c), so feed silence. */
	{
		int64_t extra = (int64_t)rate / 4;
		int64_t k;
		for (k = 0; k < extra; k++) {
			g_sample_number++;
			multi_modem_process_sample(0, 0);
		}
	}

	{
		sbuf_t b;
		sb_init(&b);
		sb_printf(&b, "{\"t\":%.4f,\"type\":\"stats\",\"samples\":%lld,\"frames\":%ld,\"ax25\":%ld,\"fx25\":%ld,\"il2p\":%ld,"
			"\"fx25_corrected\":%ld,\"il2p_corrected\":%ld}",
			(double)g_sample_number / (double)g_rate, (long long)g_sample_number,
			g_frames[0] + g_frames[1] + g_frames[2], g_frames[0], g_frames[1], g_frames[2],
			g_corrected_total[1], g_corrected_total[2]);
		emit(&b);
		free(b.p);
	}
	return 0;
}
