/* Stand-in for gmodule.h / glib: the four GByteArray calls used by asn1-format-icao-text.c. */
#ifndef FOXSDR_COMPAT_GMODULE_H
#define FOXSDR_COMPAT_GMODULE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
	uint8_t *data;
	unsigned int len;
	unsigned int cap;
} GByteArray;
static inline GByteArray *g_byte_array_new(void) {
	return (GByteArray *)calloc(1, sizeof(GByteArray));
}
static inline GByteArray *g_byte_array_append(GByteArray *a, const uint8_t *d, unsigned int n) {
	if(n == 0 || d == NULL) return a;
	if(a->len + n > a->cap) {
		unsigned int nc = a->cap ? a->cap : 16;
		while(nc < a->len + n) nc *= 2;
		uint8_t *p = (uint8_t *)realloc(a->data, nc);
		if(p == NULL) abort();
		a->data = p;
		a->cap = nc;
	}
	memcpy(a->data + a->len, d, n);
	a->len += n;
	return a;
}
static inline uint8_t *g_byte_array_free(GByteArray *a, int free_segment) {
	uint8_t *seg = a->data;
	if(free_segment) { free(a->data); seg = NULL; }
	free(a);
	return seg;
}
#endif
