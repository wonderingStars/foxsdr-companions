/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>                 // memcpy
#include <libacars/libacars.h>      // la_proto_tree_destroy()
#include <libacars/list.h>          // la_list_*
#include <libacars/reassembly.h>    // la_reasm_ctx, la_reasm_ctx_new()
#include "util.h"                   // NEW, ASSERT, struct octet_string
#include "crc.h"                    // crc16_ccitt
#include "mpdu.h"                   // mpdu_parse
#include "spdu.h"                   // spdu_parse
#include "statsd.h"                 // statsd_*
#include "pdu.h"                    // struct hfdl_pdu_metadata

/*
 * FoxSDR helper change: upstream queues each received PDU to a decoder thread (GAsyncQueue) which
 * runs the formatters and the output threads. Here the PDU is decoded synchronously in
 * pdu_decoder_queue_push() and every resulting LPDU tree is handed to a sink callback, so that the
 * output order is a function of the input only.
 */
static la_reasm_ctx *reasm_ctx = NULL;
static hfdl_pdu_sink_fn pdu_sink = NULL;
static void *pdu_sink_ctx = NULL;

/******************************
 * Forward declarations
 ******************************/

static struct metadata_vtable hfdl_pdu_metadata_vtable;

/******************************
 * Public methods
 ******************************/

#define IS_MPDU(buf) ((buf)[0] & 1)

void pdu_decoder_queue_push(struct metadata *metadata, struct octet_string *pdu, uint32_t flags) {
	UNUSED(flags);
	ASSERT(metadata != NULL);
	ASSERT(pdu != NULL);
	ASSERT(pdu_sink != NULL);
	struct hfdl_pdu_metadata *hm = container_of(metadata, struct hfdl_pdu_metadata, metadata);
	statsd_increment_per_channel(hm->freq, "frames.processed");
	la_list *lpdu_list = NULL;
	if(IS_MPDU(pdu->buf)) {
		lpdu_list = mpdu_parse(pdu, reasm_ctx, metadata->rx_timestamp, hm->freq);
	} else {
		lpdu_list = spdu_parse(pdu, hm->freq);
	}
	for(la_list *lpdu = lpdu_list; lpdu != NULL; lpdu = la_list_next(lpdu)) {
		ASSERT(lpdu->data != NULL);
		pdu_sink(metadata, lpdu->data, pdu_sink_ctx);
	}
	la_list_free_full(lpdu_list, la_proto_tree_destroy);
	octet_string_destroy(pdu);
	XFREE(metadata);
}

void hfdl_pdu_decoder_init(hfdl_pdu_sink_fn sink, void *sink_ctx) {
	reasm_ctx = la_reasm_ctx_new();
	pdu_sink = sink;
	pdu_sink_ctx = sink_ctx;
}

void hfdl_pdu_decoder_destroy(void) {
	la_reasm_ctx_destroy(reasm_ctx);
	reasm_ctx = NULL;
	pdu_sink = NULL;
}

// Compute FCS over the first hdr_len octets of buf.
// The received FCS value is expected to be stored just after the buffer.
bool hfdl_pdu_fcs_check(uint8_t *buf, uint32_t hdr_len) {
	uint16_t fcs_check = buf[hdr_len] | (buf[hdr_len + 1] << 8);
	uint16_t fcs_computed = crc16_ccitt(buf, hdr_len, 0xFFFFu) ^ 0xFFFFu;
	debug_print(D_PROTO, "FCS: computed: 0x%04x check: 0x%04x\n",
			fcs_computed, fcs_check);
	if(fcs_check != fcs_computed) {
		debug_print(D_PROTO, "FCS check failed\n");
		return false;
	}
	debug_print(D_PROTO, "FCS check OK\n");
	return true;
}

struct metadata *hfdl_pdu_metadata_create() {
	NEW(struct hfdl_pdu_metadata, m);
	m->metadata.vtable = &hfdl_pdu_metadata_vtable;
	return &m->metadata;
}

/****************************************
 * Private variables and methods
 ****************************************/

static struct metadata *hfdl_pdu_metadata_copy(struct metadata const *m) {
	ASSERT(m != NULL);
	struct hfdl_pdu_metadata *hm = container_of(m, struct hfdl_pdu_metadata, metadata);
	NEW(struct hfdl_pdu_metadata, copy);
	memcpy(copy, hm, sizeof(struct hfdl_pdu_metadata));
	return &copy->metadata;
}

static void hfdl_pdu_metadata_destroy(struct metadata *m) {
	if(m == NULL) {
		return;
	}
	struct hfdl_pdu_metadata *hm = container_of(m, struct hfdl_pdu_metadata, metadata);
	XFREE(hm);
}

static struct metadata_vtable hfdl_pdu_metadata_vtable = {
	.copy = hfdl_pdu_metadata_copy,
	.destroy = hfdl_pdu_metadata_destroy
};
