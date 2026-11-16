/* SPDX-License-Identifier: GPL-3.0-or-later */
/* FoxSDR helper: reduced from upstream's output-common.h. Only the formatter descriptor types that
 * fmtr-json.c and fmtr-text.c define themselves with remain; the output threads, queues, network and
 * file outputs (and their glib / pthread / kvargs dependencies) are not part of this helper. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <libacars/libacars.h>          // la_proto_node
#include "metadata.h"                   // struct metadata

// Data type on formatter input
typedef enum {
	FMTR_INTYPE_UNKNOWN           = 0,
	FMTR_INTYPE_DECODED_FRAME     = 1,
	FMTR_INTYPE_RAW_FRAME         = 2
} fmtr_input_type_t;

// Output formats
typedef enum {
	OFMT_UNKNOWN    = 0,
	OFMT_TEXT       = 1,
	OFMT_BASESTATION = 2,
	OFMT_JSON       = 3
} output_format_t;

struct octet_string;

typedef struct octet_string* (fmt_decoded_fun_t)(struct metadata *, la_proto_node *);
typedef struct octet_string* (fmt_raw_fun_t)(struct metadata *, struct octet_string *);
typedef bool (intype_check_fun_t)(fmtr_input_type_t);

// Frame formatter descriptor
typedef struct {
	char *name;
	char *description;
	fmt_decoded_fun_t *format_decoded_msg;
	fmt_raw_fun_t *format_raw_msg;
	intype_check_fun_t *supports_data_type;
	output_format_t output_format;
} fmtr_descriptor_t;
