/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FoxSDR helper front end for dumphfdl (written for this helper; replaces upstream's main.c, options.c,
 * input-*.c and output-*.c).
 *
 *   hfdl.exe --foxsdr-helper 1 --format cs16|cf32 --rate <Hz> [--centre <Hz>] --channel-offset <Hz> [...] [options]
 *
 * Reads raw interleaved I/Q on standard input until end of file and writes JSON Lines on standard output
 * (protocol version 1, see docs/PIPE-PROTOCOL.md of the helpers repository). Everything else goes to standard error.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <errno.h>
#include <io.h>
#include <fcntl.h>
#include <windows.h>
#include <libacars/libacars.h>
#include <libacars/vstring.h>
#include <libacars/list.h>
#include "util.h"
#include "globals.h"
#include "metadata.h"
#include "pdu.h"
#include "hfdl.h"
#include "fmtr-json.h"
#include "fmtr-text.h"
#include "systable.h"
#include "ac_cache.h"
#include "ac_data.h"

#define HELPER_NAME "hfdl"
#define HELPER_VERSION "1.7.0"
#define HELPER_SOURCE "https://github.com/szpajder/dumphfdl"
#define HELPER_UPSTREAM "dumphfdl 1.7.0 (bb7234bdf12ff1ca5f1dc99a5710555f968c4e6e) + libacars 2.2.1"
#define MAX_CHANNELS 64
#define MIN_RATE 10800          // below this the 3 kHz channel (carrier at +1440 Hz) does not fit in the stream; 5400 and 7200 decoded nothing
#define DEFAULT_CHUNK_SAMPLES 32768

// The manifest (also shipped as hfdl.helper.json); printed by --foxsdr-helper-info
static char const manifest_json[] =
"{\n"
"  \"name\": \"" HELPER_NAME "\",\n"
"  \"version\": \"" HELPER_VERSION "\",\n"
"  \"licence\": \"GPL-3.0-or-later\",\n"
"  \"source\": \"" HELPER_SOURCE "\",\n"
"  \"executable\": \"hfdl.exe\",\n"
"  \"description\": \"HFDL (aircraft HF data link, ARINC 635) decoder: M-PSK demodulation at 1800 baud, slot framing, FEC, MPDU/LPDU/HFNPDU/SPDU and ACARS content, several channels inside one I/Q stream\",\n"
"  \"input\": \"iq\",\n"
"  \"format\": [\"cs16\", \"cf32\"],\n"
"  \"rate\": { \"min\": 10800, \"max\": null },\n"
"  \"options\": [\n"
"    { \"name\": \"--channel-offset\", \"value\": \"Hz\", \"repeat\": true, \"required\": true, \"help\": \"assigned HFDL channel frequency minus the I/Q centre, in Hz; the signal is the 3 kHz USB channel whose carrier is 1440 Hz above it\" },\n"
"    { \"name\": \"--centre\", \"value\": \"Hz\", \"help\": \"I/Q centre frequency; only used to report absolute frequencies\" },\n"
"    { \"name\": \"--hide-ids\", \"help\": \"blank aircraft identifiers (ICAO address, registration, flight id, aircraft data) in the output\" },\n"
"    { \"name\": \"--system-table\", \"value\": \"file\", \"help\": \"ground station table (dumphfdl systable.conf) for station names\" },\n"
"    { \"name\": \"--start-time\", \"value\": \"unix seconds\", \"help\": \"wall-clock time of the first sample; frame timestamps count from it (default 0)\" },\n"
"    { \"name\": \"--output-mpdus\", \"help\": \"also report MPDU headers\" },\n"
"    { \"name\": \"--output-corrupted-pdus\", \"help\": \"also report PDUs that failed their CRC\" },\n"
"    { \"name\": \"--stats\", \"help\": \"print a stats line at the end\" },\n"
"    { \"name\": \"--chunk\", \"value\": \"samples\", \"help\": \"read size (does not change the output)\" }\n"
"  ],\n"
"  \"events\": [\n"
"    { \"type\": \"hfdl\", \"fields\": [\"hfdl\", \"text\"], \"help\": \"one decoded LPDU / SPDU; hfdl is dumphfdl's own JSON object\" },\n"
"    { \"type\": \"text\", \"fields\": [\"text\"] },\n"
"    { \"type\": \"status\", \"fields\": [\"text\"] },\n"
"    { \"type\": \"stats\", \"fields\": [\"samples\", \"pdus\"] }\n"
"  ]\n"
"}\n";

static FILE *jout = NULL;       // protocol channel (a duplicate of the original standard output)
static bool hide_ids = false;
static uint64_t pdu_count = 0;
static uint64_t line_count = 0;
static long long start_time_s = 0;

/***************************** JSON helpers *****************************/

static void json_put_string(la_vstring *v, char const *s, size_t n) {
	la_vstring_append_sprintf(v, "%s", "\"");
	for(size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		switch(c) {
		case '"': la_vstring_append_sprintf(v, "%s", "\\\""); break;
		case '\\': la_vstring_append_sprintf(v, "%s", "\\\\"); break;
		case '\n': la_vstring_append_sprintf(v, "%s", "\\n"); break;
		case '\r': la_vstring_append_sprintf(v, "%s", "\\r"); break;
		case '\t': la_vstring_append_sprintf(v, "%s", "\\t"); break;
		default:
			if(c < 0x20) {
				la_vstring_append_sprintf(v, "\\u%04x", c);
			} else {
				la_vstring_append_sprintf(v, "%c", c);
			}
		}
	}
	la_vstring_append_sprintf(v, "%s", "\"");
}

static double elapsed_s(void) {
	return (double)(helper_now_us() - start_time_s * 1000000LL) / 1e6;
}

static void emit_line(char const *s, size_t n) {
	fwrite(s, 1, n, jout);
	fputc('\n', jout);
	fflush(jout);
	line_count++;
}

static void emit_simple(char const *type, char const *text) {
	la_vstring *v = la_vstring_new();
	la_vstring_append_sprintf(v, "{\"t\":%.3f,\"type\":\"%s\",\"text\":", elapsed_s(), type);
	json_put_string(v, text, strlen(text));
	la_vstring_append_sprintf(v, "%s", "}");
	emit_line(v->str, v->len);
	la_vstring_destroy(v, true);
}

/***************************** --hide-ids *****************************/

// Identifier redaction: see redact.c
extern void hfdl_redact_json(la_vstring *json);
extern char *hfdl_redact_text(char const *text);

/***************************** decoded PDU sink *****************************/

// One-line form of the multi-line text output: lines trimmed and joined with " | "
static char *text_one_line(char const *text, size_t len) {
	char *out = (char *)malloc(3 * len + 4);     // a one character line gains a 3 character separator
	size_t n = 0;
	bool pending_sep = false;
	size_t i = 0;
	while(i < len) {
		size_t j = i;
		while(j < len && text[j] != '\n') {
			j++;
		}
		size_t a = i, b = j;
		while(a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r')) a++;
		while(b > a && (text[b - 1] == ' ' || text[b - 1] == '\t' || text[b - 1] == '\r')) b--;
		if(b > a) {
			if(pending_sep) {
				out[n++] = ' ';
				out[n++] = '|';
				out[n++] = ' ';
			}
			memcpy(out + n, text + a, b - a);
			n += b - a;
			pending_sep = true;
		}
		i = j + 1;
	}
	out[n] = '\0';
	return out;
}

static void pdu_sink(struct metadata *metadata, la_proto_node *lpdu, void *ctx) {
	(void)ctx;
	struct octet_string *js = fmtr_DEF_json.format_decoded_msg(metadata, lpdu);
	struct octet_string *tx = fmtr_DEF_text.format_decoded_msg(metadata, lpdu);
	if(js == NULL || tx == NULL) {
		octet_string_destroy(js);
		octet_string_destroy(tx);
		return;
	}
	// js->buf is {"hfdl":{...}}\n : strip the outer braces and the newline, put our fields around it
	size_t jl = js->len;
	while(jl > 0 && (js->buf[jl - 1] == '\n' || js->buf[jl - 1] == '\r')) {
		jl--;
	}
	la_vstring *obj = la_vstring_new();
	la_vstring_append_buffer(obj, (char const *)js->buf, jl);
	if(hide_ids) {
		hfdl_redact_json(obj);
	}
	size_t tl = tx->len;
	char *one = text_one_line((char const *)tx->buf, tl);
	if(hide_ids) {
		char *red = hfdl_redact_text(one);
		free(one);
		one = red;
	}
	la_vstring *line = la_vstring_new();
	la_vstring_append_sprintf(line, "{\"t\":%.3f,\"type\":\"hfdl\",", elapsed_s());
	// obj->str is "{...}" with a single top-level member "hfdl"
	la_vstring_append_buffer(line, obj->str + 1, obj->len - 2);
	la_vstring_append_sprintf(line, "%s", ",\"text\":");
	json_put_string(line, one, strlen(one));
	la_vstring_append_sprintf(line, "%s", "}");
	emit_line(line->str, line->len);
	pdu_count++;
	free(one);
	la_vstring_destroy(line, true);
	la_vstring_destroy(obj, true);
	octet_string_destroy(js);
	octet_string_destroy(tx);
}

/***************************** command line *****************************/

static void usage(void) {
	fprintf(stderr,
		"FoxSDR helper: hfdl, HFDL decoder (dumphfdl 1.7.0 core, GPL-3.0-or-later)\n"
		"\n"
		"usage: hfdl --foxsdr-helper 1 --format cs16|cf32 --rate <Hz> --channel-offset <Hz> [--channel-offset <Hz> ...]\n"
		"            [--centre <Hz>] [--hide-ids] [--system-table <file>] [--start-time <unix s>] [--output-mpdus]\n"
		"            [--output-corrupted-pdus] [--stats] [--chunk <samples>]  < samples\n"
		"       hfdl --foxsdr-helper-info\n"
		"\n"
		"Reads raw interleaved I/Q (cs16 = int16 little endian, cf32 = float32) from standard input until end of\n"
		"file; prints JSON Lines on standard output. --channel-offset is the assigned HFDL channel frequency minus\n"
		"the I/Q centre frequency in Hz (the signal is the 3 kHz USB channel whose carrier sits 1440 Hz above it).\n");
}

static bool parse_ll(char const *s, long long lo, long long hi, long long *out) {
	char *end = NULL;
	errno = 0;
	long long v = strtoll(s, &end, 10);
	if(s[0] == '\0' || *end != '\0' || errno != 0 || v < lo || v > hi) {
		return false;
	}
	*out = v;
	return true;
}

static int die(char const *msg, char const *arg) {
	if(arg != NULL) {
		fprintf(stderr, "hfdl: %s: %s\n", msg, arg);
	} else {
		fprintf(stderr, "hfdl: %s\n", msg);
	}
	return 2;
}

int main(int argc, char **argv) {
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

	int protocol = 0;
	char const *format = NULL;
	long long rate = 0, centre = 0, chunk = DEFAULT_CHUNK_SAMPLES;
	long long offsets[MAX_CHANNELS];
	int offset_cnt = 0;
	char const *systable_file = NULL;
	bool want_stats = false;
	Config.output_mpdus = false;
	Config.output_corrupted_pdus = false;

	for(int i = 1; i < argc; i++) {
		char const *a = argv[i];
		#define NEXT() (++i < argc ? argv[i] : NULL)
		if(strcmp(a, "--foxsdr-helper-info") == 0) {
			_setmode(_fileno(stdout), _O_BINARY);      // the manifest is LF text, byte for byte the file hfdl.helper.json
			fputs(manifest_json, stdout);
			return 0;
		} else if(strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
			usage();
			return 0;
		} else if(strcmp(a, "--foxsdr-helper") == 0) {
			char const *v = NEXT();
			long long p;
			if(v == NULL || !parse_ll(v, 0, 1000, &p)) return die("--foxsdr-helper needs a protocol number", v);
			if(p != 1) return die("unsupported protocol version (this helper speaks 1)", v);
			protocol = (int)p;
		} else if(strcmp(a, "--format") == 0) {
			format = NEXT();
			if(format == NULL) return die("--format needs a value", NULL);
		} else if(strcmp(a, "--rate") == 0) {
			char const *v = NEXT();
			if(v == NULL || !parse_ll(v, 1, 100000000LL, &rate)) return die("bad --rate", v);
		} else if(strcmp(a, "--centre") == 0 || strcmp(a, "--center") == 0) {
			char const *v = NEXT();
			if(v == NULL || !parse_ll(v, 0, 2000000000LL, &centre)) return die("bad --centre", v);
		} else if(strcmp(a, "--channel-offset") == 0) {
			char const *v = NEXT();
			long long o;
			if(v == NULL || !parse_ll(v, -100000000LL, 100000000LL, &o)) return die("bad --channel-offset", v);
			if(offset_cnt >= MAX_CHANNELS) return die("too many --channel-offset values", NULL);
			offsets[offset_cnt++] = o;
		} else if(strcmp(a, "--hide-ids") == 0) {
			hide_ids = true;
		} else if(strcmp(a, "--system-table") == 0) {
			systable_file = NEXT();
			if(systable_file == NULL) return die("--system-table needs a file", NULL);
		} else if(strcmp(a, "--start-time") == 0) {
			char const *v = NEXT();
			if(v == NULL || !parse_ll(v, 0, 4000000000LL, &start_time_s)) return die("bad --start-time", v);
		} else if(strcmp(a, "--output-mpdus") == 0) {
			Config.output_mpdus = true;
		} else if(strcmp(a, "--output-corrupted-pdus") == 0) {
			Config.output_corrupted_pdus = true;
		} else if(strcmp(a, "--stats") == 0) {
			want_stats = true;
		} else if(strcmp(a, "--chunk") == 0) {
			char const *v = NEXT();
			if(v == NULL || !parse_ll(v, 1, 1 << 24, &chunk)) return die("bad --chunk", v);
		} else {
			usage();
			return die("unknown option", a);
		}
	}

	// Validate everything before the hello line is printed (a bad command line prints no hello).
	if(protocol != 1) {
		usage();
		return die("missing --foxsdr-helper 1", NULL);
	}
	bool is_cs16 = false;
	if(format == NULL) {
		return die("missing --format", NULL);
	} else if(strcmp(format, "cs16") == 0) {
		is_cs16 = true;
	} else if(strcmp(format, "cf32") == 0) {
		is_cs16 = false;
	} else {
		return die("unsupported --format (use cs16 or cf32)", format);
	}
	if(rate < MIN_RATE) {
		return die("--rate is missing or too low (minimum 10800 S/s: a 3 kHz channel at 1440 Hz does not fit below)", NULL);
	}
	if(offset_cnt < 1) {
		return die("at least one --channel-offset is required", NULL);
	}
	int32_t freqs[MAX_CHANNELS];
	for(int i = 0; i < offset_cnt; i++) {
		long long f = centre + offsets[i];
		if(f < 0 || f > INT_MAX) {
			return die("channel frequency out of range (check --centre and --channel-offset)", NULL);
		}
		freqs[i] = (int32_t)f;
	}

	// Binary stdin; protocol on a private duplicate of stdout, so that nothing printed by library code can
	// ever corrupt it (fd 1 is pointed at stderr).
	_setmode(_fileno(stdin), _O_BINARY);
	int proto_fd = _dup(_fileno(stdout));
	if(proto_fd < 0) {
		return die("cannot duplicate standard output", NULL);
	}
	_dup2(_fileno(stderr), _fileno(stdout));
	_setmode(proto_fd, _O_BINARY);
	jout = _fdopen(proto_fd, "wb");
	if(jout == NULL) {
		return die("cannot open the protocol channel", NULL);
	}

	// decoder state
	Config.utc = true;
	Config.milliseconds = true;
	Config.ac_cache_ttl = AC_CACHE_TTL_DEFAULT;
	Config.ac_data_details = AC_DETAILS_NORMAL;
	Config.ac_data_available = false;
	helper_clock_set((time_t)start_time_s);
	Systable = systable_create(NULL);
	if(systable_file != NULL) {
		if(!systable_read_from_file(Systable, systable_file)) {
			fprintf(stderr, "hfdl: could not load system table from %s:", systable_file);
			if(systable_error_type(Systable) == SYSTABLE_ERR_FILE_PARSE) {
				fprintf(stderr, " line %d:", systable_file_error_line(Systable));
			}
			fprintf(stderr, " %s\n", systable_error_text(Systable));
			return 2;
		}
		fprintf(stderr, "hfdl: system table loaded from %s\n", systable_file);
	}
	if((AC_cache = ac_cache_create()) == NULL) {
		return die("could not create the aircraft cache", NULL);
	}

	hfdl_rx *rx = hfdl_rx_create((int32_t)rate, (int32_t)centre, offset_cnt, freqs);
	if(rx == NULL) {
		return 2;
	}
	hfdl_pdu_decoder_init(pdu_sink, NULL);

	// hello
	{
		la_vstring *v = la_vstring_new();
		la_vstring_append_sprintf(v,
			"{\"helper\":\"%s\",\"version\":\"%s\",\"protocol\":1,\"licence\":\"GPL-3.0-or-later\",\"source\":\"%s\","
			"\"format\":\"%s\",\"rate\":%lld,\"centre\":%lld,\"upstream\":\"%s\",\"channels\":[",
			HELPER_NAME, HELPER_VERSION, HELPER_SOURCE, format, rate, centre, HELPER_UPSTREAM);
		for(int i = 0; i < offset_cnt; i++) {
			la_vstring_append_sprintf(v, "%s%d", i ? "," : "", freqs[i]);
		}
		la_vstring_append_sprintf(v, "%s", "]}");
		emit_line(v->str, v->len);
		la_vstring_destroy(v, true);
	}
	line_count = 0;

	// main loop
	size_t const sample_bytes = is_cs16 ? 4 : 8;
	size_t const chunk_bytes = (size_t)chunk * sample_bytes;
	unsigned char *raw = (unsigned char *)malloc(chunk_bytes + sample_bytes);
	float *fbuf = (float *)malloc((size_t)chunk * 2 * sizeof(float));
	if(raw == NULL || fbuf == NULL) {
		return die("out of memory", NULL);
	}
	size_t carry = 0;
	uint64_t samples_in = 0;
	for(;;) {
		size_t got = fread(raw + carry, 1, chunk_bytes - carry, stdin);
		size_t have = carry + got;
		size_t nsamp = have / sample_bytes;
		if(nsamp > 0) {
			if(is_cs16) {
				int16_t const *p = (int16_t const *)raw;
				for(size_t i = 0; i < nsamp * 2; i++) {
					fbuf[i] = (float)p[i] / 32767.5f;       // upstream: full scale = SHRT_MAX + 0.5
				}
			} else {
				memcpy(fbuf, raw, nsamp * 2 * sizeof(float));
			}
			hfdl_rx_push(rx, fbuf, nsamp);
			samples_in += nsamp;
		}
		carry = have - nsamp * sample_bytes;
		if(carry > 0) {
			memmove(raw, raw + nsamp * sample_bytes, carry);
		}
		if(got == 0) {          // end of file (a trailing partial sample is dropped)
			break;
		}
	}
	if(want_stats) {
		la_vstring *v = la_vstring_new();
		la_vstring_append_sprintf(v, "{\"t\":%.3f,\"type\":\"stats\",\"samples\":%llu,\"pdus\":%llu}",
			elapsed_s(), (unsigned long long)samples_in, (unsigned long long)pdu_count);
		emit_line(v->str, v->len);
		la_vstring_destroy(v, true);
	}
	hfdl_pdu_decoder_destroy();
	hfdl_rx_destroy(rx);
	free(raw);
	free(fbuf);
	fclose(jout);
	(void)emit_simple;
	return 0;
}
