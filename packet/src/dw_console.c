/*
 * dw_console.c - replacement for Dire Wolf's textcolor.c in the FoxSDR "packet" helper.
 *
 * Part of the FoxSDR GPL helper "packet", which is built from Dire Wolf (wb2osz/direwolf).
 * Copyright (C) 2026 the FoxSDR helper authors.  Licence: GNU General Public License,
 * version 2 or (at your option) any later version, the same as Dire Wolf.
 *
 * Upstream textcolor.c writes ANSI colour sequences or Windows console attributes and
 * prints to stdout.  A helper's stdout carries JSON Lines and nothing else, and it must
 * never touch a console, so this file provides the same three functions (text_color_init,
 * text_color_set, dw_printf) with these rules:
 *
 *   - text_color_init / text_color_set do nothing (no colours, no console calls).
 *   - dw_printf discards its text unless the helper was started with --verbose, in which
 *     case it goes to stderr.
 *   - dw_capture_begin / dw_capture_end divert dw_printf into a buffer so the caller can
 *     obtain the text of a function that only prints (ax25_safe_print).
 */

#include "direwolf.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "textcolor.h"

#ifdef DW_CONSOLE_DEFAULT_VERBOSE
static int g_verbose = 1;
#else
static int g_verbose = 0;
#endif
static char *g_cap_buf = NULL;
static size_t g_cap_size = 0;
static size_t g_cap_len = 0;

void dw_console_set_verbose(int on)
{
	g_verbose = on;
}

void dw_capture_begin(char *buf, size_t size)
{
	g_cap_buf = buf;
	g_cap_size = size;
	g_cap_len = 0;
	if (size > 0) buf[0] = '\0';
}

size_t dw_capture_end(void)
{
	size_t n = g_cap_len;
	g_cap_buf = NULL;
	g_cap_size = 0;
	g_cap_len = 0;
	return n;
}

void text_color_init(int enable_color)
{
	(void)enable_color;
}

void text_color_set(dw_color_t c)
{
	(void)c;
}

void text_color_term(void)
{
}

int dw_printf(const char *fmt, ...)
{
	char buffer[16384];	/* ax25_safe_print can print 2048 bytes * 6 */
	va_list args;
	int len;

	if (g_cap_buf == NULL && !g_verbose) {
		return 0;
	}

	va_start(args, fmt);
	len = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);
	if (len < 0) return len;
	if ((size_t)len >= sizeof(buffer)) len = (int)sizeof(buffer) - 1;

	if (g_cap_buf != NULL) {
		size_t room = g_cap_size > g_cap_len + 1 ? g_cap_size - g_cap_len - 1 : 0;
		size_t n = (size_t)len < room ? (size_t)len : room;
		memcpy(g_cap_buf + g_cap_len, buffer, n);
		g_cap_len += n;
		g_cap_buf[g_cap_len] = '\0';
	} else {
		fputs(buffer, stderr);
	}
	return len;
}
