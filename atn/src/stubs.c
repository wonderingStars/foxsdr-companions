/*
 *  atn - FoxSDR GPL helper. New code (GPL-3.0-or-later): the few symbols dumpvdl2's sources expect from parts of
 *  dumpvdl2 that this helper does not contain (its main program, the ACARS decoder, the ground-station database).
 */
#include <stdint.h>
#include <string.h>
#include <libacars/libacars.h>
#include <libacars/reassembly.h>
#include "dumpvdl2.h"
#include "acars.h"

/* ------------------------------------------------------------------------------------------------------ */
/* globals dumpvdl2's sources expect (defined in dumpvdl2.c / version.c upstream)                           */
/* ------------------------------------------------------------------------------------------------------ */
dumpvdl2_config_t Config;
int do_exit = 0;
char const * const DUMPVDL2_VERSION = "2.7.0";
char const * const LA_VERSION = "2.2.1";

/* ------------------------------------------------------------------------------------------------------ */
/* ACARS inside AVLC is not decoded here (FoxSDR has its own ACARS decoder and the libacars ACARS / ARINC /   */
/* ADS-C(FANS) / MIAM code is not part of this helper): the payload is passed on as an unknown PDU.          */
/* Stand-in for dumpvdl2's acars.c parse_acars().                                                            */
/* ------------------------------------------------------------------------------------------------------ */
la_proto_node *parse_acars(uint8_t *buf, uint32_t len, uint32_t *msg_type, la_reasm_ctx *rtables,
		struct timeval rx_time) {
	(void)msg_type; (void)rtables; (void)rx_time;
	return unknown_proto_pdu_new(buf, len);
}

/* The ground-station address database (dumpvdl2's gs_data.c, loaded from a file by --gs-file) is not part of
 * this helper: a lookup always finds nothing. */
#include "gs_data.h"
gs_data_entry *gs_data_entry_lookup(uint32_t addr) {
	(void)addr;
	return NULL;
}
