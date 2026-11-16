/* Minimal getopt_long for MSVC (test tool gen_packets only).  Written for the FoxSDR packet helper, GPL-2.0-or-later. */
#ifndef DW_COMPAT_GETOPT_H
#define DW_COMPAT_GETOPT_H 1
#ifdef __cplusplus
extern "C" {
#endif
extern char *optarg;
extern int optind, opterr, optopt;
struct option { const char *name; int has_arg; int *flag; int val; };
#define no_argument 0
#define required_argument 1
#define optional_argument 2
int getopt(int argc, char *const argv[], const char *optstring);
int getopt_long(int argc, char *const argv[], const char *optstring, const struct option *longopts, int *longindex);
#ifdef __cplusplus
}
#endif
#endif
