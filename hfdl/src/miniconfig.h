/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * FoxSDR helper: a small configuration-file reader / writer written for this helper, replacing the
 * libconfig library that upstream dumphfdl links (libconfig is LGPL and is not part of this helper).
 *
 * It implements only the subset of the libconfig C API (same function names, same meaning) that
 * dumphfdl's systable.c calls, for files in the libconfig syntax that the system table uses:
 *   name = value;   with value an integer, a float, a string, true/false, a group { ... },
 *   a list ( ... ) or an array [ ... ];  comments  # ...  // ...  and  C-style block comments.
 * Not supported: includes, @include, #-directives, string concatenation across lines.
 */
#pragma once
#include <stdint.h>
#include <stdio.h>

#define CONFIG_TRUE  1
#define CONFIG_FALSE 0

#define CONFIG_TYPE_NONE   0
#define CONFIG_TYPE_INT    1
#define CONFIG_TYPE_INT64  2
#define CONFIG_TYPE_FLOAT  3
#define CONFIG_TYPE_STRING 4
#define CONFIG_TYPE_BOOL   5
#define CONFIG_TYPE_ARRAY  6
#define CONFIG_TYPE_LIST   7
#define CONFIG_TYPE_GROUP  8

typedef enum {
	CONFIG_ERR_NONE = 0,
	CONFIG_ERR_FILE_IO = 1,
	CONFIG_ERR_PARSE = 2
} config_error_t;

typedef struct config_setting_t config_setting_t;
struct config_setting_t {
	int type;
	char *name;                     // NULL for list / array elements
	config_setting_t *parent;
	config_setting_t **elems;       // children of a group, list or array
	int elem_cnt, elem_cap;
	long long ival;                 // INT, INT64, BOOL
	double fval;                    // FLOAT
	char *sval;                     // STRING
};

typedef struct config_t {
	config_setting_t *root;
	config_error_t error_type;
	int error_line;
	char error_text[160];
} config_t;

void config_init(config_t *cfg);
void config_destroy(config_t *cfg);
int config_read_file(config_t *cfg, char const *filename);
int config_write_file(config_t *cfg, char const *filename);
char const *config_error_text(config_t const *cfg);
int config_error_line(config_t const *cfg);
config_error_t config_error_type(config_t const *cfg);

config_setting_t *config_root_setting(config_t const *cfg);
config_setting_t *config_lookup(config_t const *cfg, char const *path);
int config_lookup_int(config_t const *cfg, char const *path, int *value);

int config_setting_type(config_setting_t const *s);
int config_setting_is_group(config_setting_t const *s);
int config_setting_is_list(config_setting_t const *s);
int config_setting_is_number(config_setting_t const *s);
config_setting_t *config_setting_get_member(config_setting_t const *s, char const *name);
config_setting_t *config_setting_get_elem(config_setting_t const *s, unsigned int idx);
int config_setting_lookup_int(config_setting_t const *s, char const *name, int *value);
int config_setting_lookup_string(config_setting_t const *s, char const *name, char const **value);
int config_setting_get_int(config_setting_t const *s);
double config_setting_get_float(config_setting_t const *s);
char const *config_setting_get_string(config_setting_t const *s);
config_setting_t *config_setting_add(config_setting_t *parent, char const *name, int type);
int config_setting_set_int(config_setting_t *s, int value);
int config_setting_set_float(config_setting_t *s, double value);
int config_setting_set_string(config_setting_t *s, char const *value);
config_setting_t *config_setting_set_float_elem(config_setting_t *parent, int idx, double value);
