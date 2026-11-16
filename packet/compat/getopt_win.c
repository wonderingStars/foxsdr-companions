/* Minimal getopt / getopt_long for MSVC, used only by the gen_packets test tool.
 * Written for the FoxSDR packet helper.  Licence: GPL-2.0-or-later.  Supports clustered short options,
 * short options with a required argument (attached or separate), long options with a required argument
 * (--name=value or --name value), and stops at the first non-option (POSIX behaviour). */
#include <stdio.h>
#include <string.h>
#include "getopt.h"

char *optarg = NULL;
int optind = 1, opterr = 1, optopt = 0;
static int nextchar_pos = 0;

int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts, int *longindex)
{
	const char *a;
	optarg = NULL;
	if (optind >= argc) return -1;
	a = argv[optind];
	if (a == NULL || a[0] != '-' || a[1] == '\0') return -1;
	if (strcmp(a, "--") == 0) { optind++; return -1; }

	if (a[1] == '-' && longopts != NULL) {
		const char *name = a + 2;
		const char *eq = strchr(name, '=');
		size_t n = eq ? (size_t)(eq - name) : strlen(name);
		int i;
		for (i = 0; longopts[i].name != NULL; i++) {
			if (strlen(longopts[i].name) == n && strncmp(longopts[i].name, name, n) == 0) {
				if (longindex) *longindex = i;
				optind++;
				if (longopts[i].has_arg == required_argument) {
					if (eq) optarg = (char *)(eq + 1);
					else if (optind < argc) optarg = argv[optind++];
					else return '?';
				}
				if (longopts[i].flag) { *longopts[i].flag = longopts[i].val; return 0; }
				return longopts[i].val;
			}
		}
		if (opterr) fprintf(stderr, "unrecognized option '%s'\n", a);
		optind++;
		return '?';
	}

	if (nextchar_pos == 0) nextchar_pos = 1;
	{
		int c = (unsigned char)a[nextchar_pos];
		const char *p = strchr(optstring, c);
		nextchar_pos++;
		if (p == NULL || c == ':') {
			optopt = c;
			if (opterr) fprintf(stderr, "invalid option -- '%c'\n", c);
			if (a[nextchar_pos] == '\0') { optind++; nextchar_pos = 0; }
			return '?';
		}
		if (p[1] == ':') {
			if (a[nextchar_pos] != '\0') optarg = (char *)(a + nextchar_pos);
			else if (optind + 1 < argc) optarg = argv[++optind];
			else { optind++; nextchar_pos = 0; return '?'; }
			optind++; nextchar_pos = 0;
			return c;
		}
		if (a[nextchar_pos] == '\0') { optind++; nextchar_pos = 0; }
		return c;
	}
}

int getopt(int argc, char *const argv[], const char *optstring)
{
	return getopt_long(argc, argv, optstring, NULL, NULL);
}
