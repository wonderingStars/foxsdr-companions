/* Empty stand-in for <regex.h>.  Dire Wolf's ax25_pad.c includes "regex.h" but calls nothing from it
 * (only decode_aprs.c / tt_*.c use regular expressions, and the helper does not link them). */
#ifndef DW_COMPAT_REGEX_H
#define DW_COMPAT_REGEX_H 1
#endif
