/*
 *  atn - FoxSDR GPL helper: the ATN / ISO 8208 / CLNP / COTP / CPDLC stack above VDL Mode 2 AVLC.
 *
 *  This program is a derivative work of dumpvdl2 2.7.0 (Copyright (c) 2017-2026 Tomasz Lemiech
 *  <szpajder@gmail.com>) and of libacars (MIT, same author). It is free software: you can redistribute it
 *  and/or modify it under the terms of the GNU General Public License as published by the Free Software
 *  Foundation, either version 3 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public
 *  License for more details.
 *
 *  This file (main.c) is new code written for the FoxSDR helper protocol version 1. It replaces dumpvdl2's
 *  demodulator, decoder thread, formatters and outputs: it reads AVLC frames as hex text lines on stdin,
 *  calls dumpvdl2's own avlc_parse() on each, and prints the decoded protocol tree as JSON Lines (the JSON
 *  and text renderings are dumpvdl2's own). Receive only; no network code; nothing is stored or forwarded.
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <crtdbg.h>
#include <stdlib.h>
#endif
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <libacars/libacars.h>
#include <libacars/vstring.h>
#include <libacars/reassembly.h>
#include "dumpvdl2.h"
#include "avlc.h"
#include "reassembly.h"
#include "output-common.h"

#define UPSTREAM_VERSION "2.7.0"

#define HELPER_NAME      "atn"
#define HELPER_VERSION   "1.0.0"
#define HELPER_SOURCE    "https://github.com/szpajder/dumpvdl2"
#define HELPER_LICENCE   "GPL-3.0-or-later"


/* longest accepted input line, in characters (4096 octets = 8192 hex digits, VDL2 frames are far shorter) */
#define MAX_LINE_CHARS   9000
#define MAX_FRAME_OCTETS 4096
#define MIN_AVLC_OCTETS  11      /* same constant as avlc.c */
#define MAX_ERROR_EVENTS 50      /* after this many error events the rest are only counted */

static void usage(FILE *f) {
	fprintf(f,
		"atn " HELPER_VERSION " (dumpvdl2 " UPSTREAM_VERSION " protocol stack above AVLC) - FoxSDR GPL helper\n"
		"\n"
		"usage: atn --format avlc-hex [options] < frames.txt\n"
		"       atn --foxsdr-helper 1 --format avlc-hex [options]\n"
		"       atn --foxsdr-helper-info\n"
		"\n"
		"stdin: one AVLC frame per line, as hex (no spaces): the whole frame after bit de-stuffing, i.e.\n"
		"       destination address (4 octets), source address (4 octets), control field, information field,\n"
		"       and - unless --fcs none - the 2-octet frame check sequence, low octet first.\n"
		"       Optional: a decimal timestamp in seconds and a space before the hex, '#' starts a comment line.\n"
		"stdout: JSON Lines. Events: frame, text, error, stats (after the hello line).\n"
		"\n"
		"options:\n"
		"  --fcs check|none     frames end with the 2-octet FCS, which is verified (default), or carry no FCS\n"
		"                       (the demodulator already verified it); with none a valid FCS is appended.\n"
		"  --no-text            do not print the dumpvdl2 text rendering (type \"text\" events)\n"
		"  --decode-fragments   decode higher layers of incomplete fragmented packets too (dumpvdl2 option)\n"
		"  --rate <Hz>, --centre <Hz>   accepted and ignored (there are no samples)\n"
		"  --version, --help\n");
}

static void print_manifest(void) {
	/* the same content as atn.helper.json, kept in the program so that the store can ask the binary */
	fputs("{\"name\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,"
		"\"licence\":\"" HELPER_LICENCE "\",\"source\":\"" HELPER_SOURCE "\","
		"\"upstream\":\"dumpvdl2 " UPSTREAM_VERSION "\",\"executable\":\"atn.exe\","
		"\"input\":\"avlc\",\"format\":\"avlc-hex\",\"rate\":null,"
		"\"events\":{"
		"\"frame\":[\"t\",\"line\",\"octets\",\"layers\",\"msg\"],"
		"\"text\":[\"t\",\"line\",\"text\"],"
		"\"error\":[\"t\",\"line\",\"error\"],"
		"\"stats\":[\"lines\",\"frames\",\"decoded\",\"bad_fcs\",\"too_short\",\"bad_input\"]}}\n", stdout);
}

/* JSON string escaping for the few fields this file writes itself */
static void json_str(la_vstring *v, char const *s, size_t n) {
	static char const hex[] = "0123456789abcdef";
	la_vstring_append_buffer(v, "\"", 1);
	for(size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if(c == '"') la_vstring_append_buffer(v, "\\\"", 2);
		else if(c == '\\') la_vstring_append_buffer(v, "\\\\", 2);
		else if(c == '\n') la_vstring_append_buffer(v, "\\n", 2);
		else if(c == '\r') la_vstring_append_buffer(v, "\\r", 2);
		else if(c == '\t') la_vstring_append_buffer(v, "\\t", 2);
		else if(c < 0x20 || c == 0x7f) {
			char e[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
			la_vstring_append_buffer(v, e, 6);
		} else if(c >= 0x80) {
			/* the text renderings can carry arbitrary octets: keep the output valid UTF-8 */
			char e[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15] };
			la_vstring_append_buffer(v, e, 6);
		} else {
			la_vstring_append_buffer(v, (char const *)&c, 1);
		}
	}
	la_vstring_append_buffer(v, "\"", 1);
}

static void emit(la_vstring *v) {
	la_vstring_append_buffer(v, "}\n", 2);
	fwrite(v->str, 1, v->len, stdout);
	fflush(stdout);
	la_vstring_destroy(v, true);
}

static la_vstring *event_start(double t, char const *type) {
	la_vstring *v = la_vstring_new();
	la_vstring_append_sprintf(v, "{\"t\":%.6f,\"type\":\"%s\"", t, type);
	return v;
}

static unsigned long n_lines, n_frames, n_decoded, n_bad_fcs, n_short, n_bad_input, n_err_suppressed;
static unsigned long n_errors_emitted;

static void emit_error(double t, unsigned long line, char const *what) {
	if(n_errors_emitted >= MAX_ERROR_EVENTS) {
		n_err_suppressed++;
		return;
	}
	n_errors_emitted++;
	la_vstring *v = event_start(t, "error");
	la_vstring_append_sprintf(v, ",\"line\":%lu,\"error\":", line);
	json_str(v, what, strlen(what));
	emit(v);
}

static int hexval(int c) {
	if(c >= '0' && c <= '9') return c - '0';
	if(c >= 'a' && c <= 'f') return c - 'a' + 10;
	if(c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* strict decimal seconds: digits [ '.' digits ], at most 15 characters; false if anything else */
static bool parse_time(char const *s, size_t n, double *out) {
	if(n == 0 || n > 15) return false;
	size_t i = 0, digits = 0;
	double v = 0.0, scale = 1.0;
	for(; i < n && isdigit((unsigned char)s[i]); i++, digits++) v = v * 10.0 + (s[i] - '0');
	if(i < n && s[i] == '.') {
		for(i++; i < n && isdigit((unsigned char)s[i]); i++, digits++) {
			scale /= 10.0;
			v += (s[i] - '0') * scale;
		}
	}
	if(i != n || digits == 0) return false;
	*out = v;
	return true;
}

/* walk the protocol tree for the names of the layers (the JSON keys dumpvdl2 itself uses) */
static void append_layers(la_vstring *v, la_proto_node const *root) {
	la_vstring_append_sprintf(v, ",\"layers\":[");
	bool first = true;
	for(la_proto_node const *n = root; n != NULL; n = n->next) {
		if(n->td != NULL && n->td->json_key != NULL) {
			la_vstring_append_sprintf(v, "%s\"%s\"", first ? "" : ",", n->td->json_key);
			first = false;
		}
	}
	la_vstring_append_buffer(v, "]", 1);
}

static void process_frame(uint8_t *frame, size_t len, double t, unsigned long line,
		reasm_contexts *rc, bool with_text, bool fcs_present) {
	uint8_t *buf = (uint8_t *)malloc(len + 2);
	if(buf == NULL) {
		emit_error(t, line, "out of memory");
		return;
	}
	memcpy(buf, frame, len);
	size_t total = len;
	if(!fcs_present) {
		uint16_t crc = (uint16_t)~crc16_ccitt(buf, (uint32_t)len, 0xFFFFu);
		buf[total++] = (uint8_t)(crc & 0xff);
		buf[total++] = (uint8_t)(crc >> 8);
	}
	if(total < MIN_AVLC_OCTETS) {
		n_short++;
		emit_error(t, line, "frame too short for AVLC (need 11 octets with FCS)");
		free(buf);
		return;
	}
	if(crc16_ccitt(buf, (uint32_t)total, 0xFFFFu) != 0xF0B8u) {
		n_bad_fcs++;
		emit_error(t, line, "bad FCS");
		free(buf);
		return;
	}
	n_frames++;

	vdl2_msg_metadata *md = (vdl2_msg_metadata *)calloc(1, sizeof(vdl2_msg_metadata));
	octet_string_t *os = (octet_string_t *)calloc(1, sizeof(octet_string_t));
	avlc_frame_qentry_t *q = (avlc_frame_qentry_t *)calloc(1, sizeof(avlc_frame_qentry_t));
	if(md == NULL || os == NULL || q == NULL) {
		emit_error(t, line, "out of memory");
		free(md); free(os); free(q); free(buf);
		return;
	}
	md->burst_timestamp.tv_sec = (long)t;
	md->burst_timestamp.tv_usec = (long)((t - (double)(long)t) * 1e6);
	md->datalen_octets = (uint32_t)total;
	md->idx = (int)line;
	os->buf = buf;
	os->len = total;
	q->metadata = md;
	q->frame = os;
	q->flags = 0;

	uint32_t msg_type = 0;
	la_proto_node *root = avlc_parse(q, &msg_type, rc);
	if(root == NULL) {
		emit_error(t, line, "AVLC frame not decoded");
	} else {
		n_decoded++;
		la_vstring *json = la_proto_tree_format_json(NULL, root);
		la_vstring *v = event_start(t, "frame");
		la_vstring_append_sprintf(v, ",\"line\":%lu,\"octets\":%lu", line, (unsigned long)total);
		append_layers(v, root);
		/* json is dumpvdl2's own JSON tree, {"avlc":{...,"x25":{...}}}: it becomes the "msg" member */
		la_vstring_append_buffer(v, ",\"msg\":", 7);
		la_vstring_append_buffer(v, json->str, json->len);
		la_vstring_destroy(json, true);
		emit(v);
		if(with_text) {
			la_vstring *txt = la_proto_tree_format_text(NULL, root);
			la_vstring *tv = event_start(t, "text");
			la_vstring_append_sprintf(tv, ",\"line\":%lu,\"text\":", line);
			json_str(tv, txt->str, txt->len);
			la_vstring_destroy(txt, true);
			emit(tv);
		}
		la_proto_tree_destroy(root);
	}
	free(buf);
	free(os);
	free(md);
	free(q);
}

int main(int argc, char **argv) {
#ifdef _WIN32
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
	_set_error_mode(_OUT_TO_STDERR);
	_setmode(_fileno(stdin), _O_BINARY);
	_setmode(_fileno(stdout), _O_BINARY);
#endif
	char const *format = NULL;
	bool fcs_present = true, with_text = true, info = false;

	for(int i = 1; i < argc; i++) {
		char const *a = argv[i];
		if(!strcmp(a, "--foxsdr-helper")) {
			if(i + 1 >= argc || strcmp(argv[i + 1], "1")) {
				fprintf(stderr, "atn: unsupported helper protocol (only version 1)\n");
				return 2;
			}
			i++;
		} else if(!strcmp(a, "--foxsdr-helper-info")) {
			info = true;
		} else if(!strcmp(a, "--format")) {
			if(i + 1 >= argc) { fprintf(stderr, "atn: --format needs a value\n"); return 2; }
			format = argv[++i];
		} else if(!strcmp(a, "--rate") || !strcmp(a, "--centre") || !strcmp(a, "--center")) {
			if(i + 1 >= argc) { fprintf(stderr, "atn: %s needs a value\n", a); return 2; }
			i++;
		} else if(!strcmp(a, "--fcs")) {
			if(i + 1 >= argc) { fprintf(stderr, "atn: --fcs needs check or none\n"); return 2; }
			a = argv[++i];
			if(!strcmp(a, "check")) fcs_present = true;
			else if(!strcmp(a, "none")) fcs_present = false;
			else { fprintf(stderr, "atn: --fcs must be check or none, not '%s'\n", a); return 2; }
		} else if(!strcmp(a, "--no-text")) {
			with_text = false;
		} else if(!strcmp(a, "--decode-fragments")) {
			Config.decode_fragments = true;
		} else if(!strcmp(a, "--version")) {
			printf("atn %s (dumpvdl2 %s, libacars 2.2.1)\n", HELPER_VERSION, UPSTREAM_VERSION);
			return 0;
		} else if(!strcmp(a, "--help") || !strcmp(a, "-h")) {
			usage(stdout);
			return 0;
		} else {
			fprintf(stderr, "atn: unknown option '%s'\n", a);
			usage(stderr);
			return 2;
		}
	}
	if(info) {
		print_manifest();
		return 0;
	}
	if(format == NULL) {
		fprintf(stderr, "atn: --format avlc-hex is required\n");
		usage(stderr);
		return 2;
	}
	if(strcmp(format, "avlc-hex")) {
		fprintf(stderr, "atn: unsupported --format '%s': this helper decodes AVLC frames, only 'avlc-hex' is accepted\n", format);
		return 2;
	}

	/* dumpvdl2 defaults (dumpvdl2.c main()): all message classes on, no address databases */
	Config.msg_filter = MSGFLT_ALL;
	Config.addrinfo_verbosity = ADDRINFO_NORMAL;

	printf("{\"helper\":\"" HELPER_NAME "\",\"version\":\"" HELPER_VERSION "\",\"protocol\":1,"
		"\"licence\":\"" HELPER_LICENCE "\",\"source\":\"" HELPER_SOURCE "\","
		"\"upstream\":\"dumpvdl2 " UPSTREAM_VERSION "\",\"format\":\"avlc-hex\"}\n");
	fflush(stdout);

	la_reasm_ctx *seq_ctx = la_reasm_ctx_new();
	reasm_ctx *off_ctx = reasm_ctx_new();
	reasm_contexts rc = { .seqbased = seq_ctx, .offsetbased = off_ctx };

	static char line[MAX_LINE_CHARS + 2];
	static uint8_t frame[MAX_FRAME_OCTETS + 2];
	double last_t = 0.0;
	bool skipping_long = false;

	while(fgets(line, sizeof(line), stdin) != NULL) {
		size_t n = strlen(line);
		bool complete = n > 0 && line[n - 1] == '\n';
		if(skipping_long) {                    /* the tail of an over-long line */
			if(complete) skipping_long = false;
			continue;
		}
		if(!complete && n >= MAX_LINE_CHARS + 1) {
			/* line longer than the buffer: report once, drop the rest of it */
			n_lines++; n_bad_input++;
			emit_error(last_t, n_lines, "line too long");
			skipping_long = true;
			continue;
		}
		n_lines++;
		while(n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) n--;
		size_t s = 0;
		while(s < n && (line[s] == ' ' || line[s] == '\t')) s++;
		if(s >= n || line[s] == '#') {
			n_lines--;                         /* blank and comment lines are not frames */
			continue;
		}
		/* tokens */
		size_t t1s = s, t1e = s;
		while(t1e < n && line[t1e] != ' ' && line[t1e] != '\t') t1e++;
		size_t t2s = t1e;
		while(t2s < n && (line[t2s] == ' ' || line[t2s] == '\t')) t2s++;
		size_t t2e = t2s;
		while(t2e < n && line[t2e] != ' ' && line[t2e] != '\t') t2e++;
		double t = last_t;
		size_t hs = t1s, he = t1e;
		if(t2s < n) {                          /* two tokens: <seconds> <hex> */
			if(t2e != n) { n_bad_input++; emit_error(t, n_lines, "too many fields (expected [seconds] hex)"); continue; }
			if(!parse_time(line + t1s, t1e - t1s, &t)) { n_bad_input++; emit_error(last_t, n_lines, "bad timestamp"); continue; }
			hs = t2s; he = t2e;
			last_t = t;
		}
		size_t hl = he - hs;
		if(hl == 0 || (hl & 1)) { n_bad_input++; emit_error(t, n_lines, "hex has an odd number of digits"); continue; }
		if(hl / 2 > MAX_FRAME_OCTETS) { n_bad_input++; emit_error(t, n_lines, "frame too long"); continue; }
		size_t fl = hl / 2;
		bool ok = true;
		for(size_t k = 0; k < fl; k++) {
			int hi = hexval(line[hs + 2 * k]), lo = hexval(line[hs + 2 * k + 1]);
			if(hi < 0 || lo < 0) { ok = false; break; }
			frame[k] = (uint8_t)((hi << 4) | lo);
		}
		if(!ok) { n_bad_input++; emit_error(t, n_lines, "not hex"); continue; }
		process_frame(frame, fl, t, n_lines, &rc, with_text, fcs_present);
	}

	la_vstring *v = event_start(last_t, "stats");
	la_vstring_append_sprintf(v, ",\"lines\":%lu,\"frames\":%lu,\"decoded\":%lu,\"bad_fcs\":%lu,\"too_short\":%lu,"
		"\"bad_input\":%lu,\"errors_suppressed\":%lu", n_lines, n_frames, n_decoded, n_bad_fcs, n_short,
		n_bad_input, n_err_suppressed);
	emit(v);
	return 0;
}
