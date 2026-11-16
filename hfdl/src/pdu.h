/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "metadata.h"               // struct metadata
#include "util.h"                   // struct octet string

struct hfdl_pdu_metadata {
	struct metadata metadata;
	int32_t version;
	int32_t freq;
	int32_t bit_rate;
	float freq_err_hz;
	float rssi;
	float noise_floor;
	char slot;                      // 'S' - single slot frame, 'D' - double slot frame
};

enum hfdl_pdu_direction {
	UPLINK_PDU = 0,
	DOWNLINK_PDU = 1
};

// Useful fields extracted from MPDU/SPDU header or PDU metadata
// that need to be passed down below the MPDU layer
struct hfdl_pdu_hdr_data {
	int32_t freq;           // For ac_cache lookups during LPDU formatting
	uint8_t src_id;         // GS ID for uplinks, AC ID for downlinks
	uint8_t dst_id;         // AC ID for uplinks, GS ID for downlinks
	enum hfdl_pdu_direction direction;
	bool crc_ok;
};

// FoxSDR helper: called once for every decoded LPDU / SPDU protocol tree. The tree and the
// metadata are owned by the decoder and are only valid during the call.
#include <libacars/libacars.h>      // la_proto_node
typedef void (*hfdl_pdu_sink_fn)(struct metadata *metadata, la_proto_node *lpdu, void *ctx);
void hfdl_pdu_decoder_init(hfdl_pdu_sink_fn sink, void *sink_ctx);
void hfdl_pdu_decoder_destroy(void);
bool hfdl_pdu_fcs_check(uint8_t *buf, uint32_t hdr_len);
void pdu_decoder_queue_push(struct metadata *metadata, struct octet_string *pdu, uint32_t flags);
struct metadata *hfdl_pdu_metadata_create();
