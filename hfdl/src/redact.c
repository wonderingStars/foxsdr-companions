/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FoxSDR helper: --hide-ids. Written for this helper (not upstream code).
 *
 * Blanks the fields that identify an individual aircraft in the decoded output:
 *   JSON: the string values of "icao" (24-bit ICAO address), "reg" (registration), "flight" and "flight_id";
 *   text: the same values wherever dumphfdl prints them ("ICAO: ", "Reg: ", "Flight: ", "Flight ID: " and the
 *         "(XXXXXX)" ICAO address after an aircraft id).
 * Not touched: the HFDL aircraft id (a number the ground station assigns for the duration of a logon), positions,
 * and the free text of ACARS messages, which is the message itself and may contain anything.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <libacars/vstring.h>

static bool key_is_identifier(char const *k, size_t n) {
	static char const *const keys[] = { "icao", "reg", "flight", "flight_id" };
	for(size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		if(strlen(keys[i]) == n && strncmp(keys[i], k, n) == 0) {
			return true;
		}
	}
	return false;
}

// Scans the JSON text with string-literal awareness. A string literal followed by ':' is a key; if the key names an
// identifier and its value is a string literal, the content of the value is removed.
void hfdl_redact_json(la_vstring *json) {
	char const *s = json->str;
	size_t n = json->len;
	la_vstring *out = la_vstring_new();
	size_t i = 0;
	while(i < n) {
		if(s[i] != '"') {
			la_vstring_append_buffer(out, s + i, 1);
			i++;
			continue;
		}
		// string literal [i, j]
		size_t j = i + 1;
		while(j < n && s[j] != '"') {
			j += (s[j] == '\\' && j + 1 < n) ? 2 : 1;
		}
		if(j >= n) {        // unterminated: copy the rest
			la_vstring_append_buffer(out, s + i, n - i);
			break;
		}
		la_vstring_append_buffer(out, s + i, j - i + 1);
		size_t after = j + 1;
		if(after < n && s[after] == ':' && key_is_identifier(s + i + 1, j - i - 1) && after + 1 < n && s[after + 1] == '"') {
			// value literal [after+1, k]
			size_t k = after + 2;
			while(k < n && s[k] != '"') {
				k += (s[k] == '\\' && k + 1 < n) ? 2 : 1;
			}
			if(k < n) {
				la_vstring_append_buffer(out, ":\"\"", 3);
				i = k + 1;
				continue;
			}
		}
		i = after;
	}
	free(json->str);
	json->str = out->str;
	json->len = out->len;
	json->allocated_size = out->allocated_size;
	free(out);          // the buffer now belongs to json
}

char *hfdl_redact_text(char const *text) {
	static char const *const prefixes[] = { "Reg: ", "Flight ID: ", "Flight: ", "ICAO: " };
	size_t n = strlen(text);
	char *out = (char *)malloc(n + 16 * (n / 4 + 1));
	size_t o = 0;
	for(size_t i = 0; i < n;) {
		bool done = false;
		for(size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]) && !done; p++) {
			size_t pl = strlen(prefixes[p]);
			if(strncmp(text + i, prefixes[p], pl) == 0) {
				memcpy(out + o, prefixes[p], pl);
				o += pl;
				out[o++] = '-';
				i += pl;
				while(i < n && text[i] != ' ' && text[i] != '|') {
					i++;
				}
				done = true;
			}
		}
		if(done) {
			continue;
		}
		if(text[i] == '(' && i + 7 < n && text[i + 7] == ')') {
			bool hex = true;
			for(size_t k = 1; k <= 6; k++) {
				if(!isxdigit((unsigned char)text[i + k])) {
					hex = false;
				}
			}
			if(hex) {
				memcpy(out + o, "(-)", 3);
				o += 3;
				i += 8;
				continue;
			}
		}
		out[o++] = text[i++];
	}
	out[o] = '\0';
	return out;
}
