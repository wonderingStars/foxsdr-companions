/*
 *  atn_testgen - development tool (GPL-3.0-or-later, new code written for this helper). NOT part of the helper.
 *
 *  Builds a few ATN application PDUs (CPDLC, CM) with the asn1c PER encoder that is bundled with dumpvdl2, and
 *  prints them as "name hex" lines. The output is pinned in tests/apdus.txt; tests/make_frames.py wraps those
 *  PDUs in ISO 8473 / COTP / X.25 / AVLC to make the test frames. Everything it makes is synthetic.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "asn1/asn_application.h"
#include "asn1/per_encoder.h"
#include "asn1/Fully-encoded-data.h"
#include "asn1/ProtectedAircraftPDUs.h"
#include "asn1/ProtectedGroundPDUs.h"
#include "asn1/ATCDownlinkMessage.h"
#include "asn1/ATCUplinkMessage.h"
#include "asn1/ATCDownlinkMsgElementId.h"
#include "asn1/ATCUplinkMsgElementId.h"
#include "asn1/CMAircraftMessage.h"
#include "asn1/CMGroundMessage.h"
#include "asn1/ACSE-apdu.h"
#include "asn1/NULL.h"
#include "asn1/ADSAircraftPDUs.h"
#include "asn1/AARE-apdu.h"
#include "asn1/OBJECT_IDENTIFIER.h"
#include "asn1/per_decoder.h"

static size_t enc(asn_TYPE_descriptor_t *td, void *p, uint8_t *out, size_t outlen, int *bits_unused) {
	asn_enc_rval_t r = uper_encode_to_buffer(td, p, out, outlen);
	if(r.encoded < 0) {
		fprintf(stderr, "encode of %s failed\n", td->name);
		exit(1);
	}
	size_t n = ((size_t)r.encoded + 7) / 8;
	if(bits_unused) *bits_unused = (int)(n * 8 - (size_t)r.encoded);
	return n;
}

static void put(char const *name, uint8_t const *b, size_t n) {
	printf("%s ", name);
	for(size_t i = 0; i < n; i++) printf("%02x", b[i]);
	printf("\n");
	/* self-check: names without "_inner" must decode as Fully-encoded-data */
	if(strstr(name, "_inner") == NULL && strstr(name, "x225") == NULL) {
		Fully_encoded_data_t *f = NULL;
		asn_dec_rval_t r = uper_decode_complete(0, &asn_DEF_Fully_encoded_data, (void **)&f, b, n);
		fprintf(stderr, "%s: decode code=%d consumed=%ld of %lu octets\n", name, (int)r.code, (long)r.consumed,
				(unsigned long)n);
	}
}

static void set_header(ATCMessageHeader_t *h, long id, long ref) {
	static MsgReferenceNumber_t r;
	h->messageIdNumber = id;
	r = ref;
	h->messageRefNumber = &r;
	h->dateTime.date.year = 2026;
	h->dateTime.date.month = 11;
	h->dateTime.date.day = 12;
	h->dateTime.timehhmmss.hoursminutes.hours = 9;
	h->dateTime.timehhmmss.hoursminutes.minutes = 41;
	h->dateTime.timehhmmss.seconds = 7;
	h->logicalAck = NULL;
}

/* wrap an application PDU (already PER encoded) as Fully-encoded-data */
static size_t wrap_fed(long pcid, uint8_t const *apdu, size_t n, int unused, uint8_t *out, size_t outlen) {
	Fully_encoded_data_t fed;
	memset(&fed, 0, sizeof(fed));
	fed.data.presentation_context_identifier = pcid;
	fed.data.presentation_data_values.present = PDV_list__presentation_data_values_PR_arbitrary;
	fed.data.presentation_data_values.choice.arbitrary.buf = (uint8_t *)apdu;
	fed.data.presentation_data_values.choice.arbitrary.size = (int)n;
	fed.data.presentation_data_values.choice.arbitrary.bits_unused = unused;
	return enc(&asn_DEF_Fully_encoded_data, &fed, out, outlen, NULL);
}

int main(void) {
	uint8_t a[512], b[512], c[512];
	int unused = 0;
	size_t n, pn, fn;
	long const PC = Presentation_context_identifier_user_ase_apdu;

	/* 1. CPDLC downlink: DM0 WILCO, message id 5, referring to message 4 (protected mode, 32 integrity bits) */
	{
		ATCDownlinkMsgElementId_t el;
		memset(&el, 0, sizeof(el));
		el.present = ATCDownlinkMsgElementId_PR_dM0NULL;
		ATCDownlinkMessage_t m;
		memset(&m, 0, sizeof(m));
		set_header(&m.header, 5, 4);
		ASN_SEQUENCE_ADD(&m.messageData.elementIds.list, &el);
		n = enc(&asn_DEF_ATCDownlinkMessage, &m, a, sizeof(a), &unused);
		uint8_t integ[4] = { 0xde, 0xad, 0xbe, 0xef };
		ProtectedAircraftPDUs_t p;
		memset(&p, 0, sizeof(p));
		p.present = ProtectedAircraftPDUs_PR_send;
		CPDLCMessage_t inner;
		memset(&inner, 0, sizeof(inner));
		inner.buf = a; inner.size = (int)n; inner.bits_unused = unused;
		p.choice.send.protectedMessage = &inner;
		p.choice.send.integrityCheck.buf = integ;
		p.choice.send.integrityCheck.size = 4;
		pn = enc(&asn_DEF_ProtectedAircraftPDUs, &p, b, sizeof(b), &unused);
		fn = wrap_fed(PC, b, pn, unused, c, sizeof(c));
		put("cpdlc_downlink_dm0_wilco", c, fn);
	}
	/* 2. CPDLC uplink: UM0 UNABLE, message id 3 referring to message 5 */
	{
		ATCUplinkMsgElementId_t el;
		memset(&el, 0, sizeof(el));
		el.present = ATCUplinkMsgElementId_PR_uM0NULL;
		ATCUplinkMessage_t m;
		memset(&m, 0, sizeof(m));
		set_header(&m.header, 3, 5);
		ASN_SEQUENCE_ADD(&m.messageData.elementIds.list, &el);
		n = enc(&asn_DEF_ATCUplinkMessage, &m, a, sizeof(a), &unused);
		uint8_t integ[4] = { 0x01, 0x23, 0x45, 0x67 };
		ProtectedGroundPDUs_t p;
		memset(&p, 0, sizeof(p));
		p.present = ProtectedGroundPDUs_PR_send;
		CPDLCMessage_t inner;
		memset(&inner, 0, sizeof(inner));
		inner.buf = a; inner.size = (int)n; inner.bits_unused = unused;
		p.choice.send.protectedMessage = &inner;
		p.choice.send.integrityCheck.buf = integ;
		p.choice.send.integrityCheck.size = 4;
		pn = enc(&asn_DEF_ProtectedGroundPDUs, &p, b, sizeof(b), &unused);
		fn = wrap_fed(PC, b, pn, unused, c, sizeof(c));
		put("cpdlc_uplink_um0_unable", c, fn);
	}
	/* 3. CPDLC abort by user (aircraft side), reason 5 = commanded termination */
	{
		ProtectedAircraftPDUs_t p;
		memset(&p, 0, sizeof(p));
		p.present = ProtectedAircraftPDUs_PR_abortUser;
		p.choice.abortUser = PMCPDLCUserAbortReason_commanded_termination;
		pn = enc(&asn_DEF_ProtectedAircraftPDUs, &p, b, sizeof(b), &unused);
		put("cpdlc_abort_user_inner", b, pn);
		fn = wrap_fed(PC, b, pn, unused, c, sizeof(c));
		put("cpdlc_abort_user", c, fn);
	}
	/* 4. CM logon request: flight id TEST123, long TSAP (RDP 5 octets, local system + NSEL + TSEL 10 octets) */
	{
		CMAircraftMessage_t m;
		memset(&m, 0, sizeof(m));
		m.present = CMAircraftMessage_PR_cmLogonRequest;
		CMLogonRequest_t *r = &m.choice.cmLogonRequest;
		char const *fid = "TEST123";
		r->aircraftFlightIdentification.buf = (uint8_t *)fid;
		r->aircraftFlightIdentification.size = 7;
		uint8_t rdp5[5] = { 0x47, 0x00, 0x27, 0x81, 0x01 };
		uint8_t sel[10] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09 };
		r->cMLongTSAP.rDP.buf = rdp5; r->cMLongTSAP.rDP.size = 5;
		r->cMLongTSAP.shortTsap.locSysNselTsel.buf = sel; r->cMLongTSAP.shortTsap.locSysNselTsel.size = 10;
		n = enc(&asn_DEF_CMAircraftMessage, &m, a, sizeof(a), &unused);
		put("cm_logon_request_inner", a, n);
		fn = wrap_fed(PC, a, n, unused, c, sizeof(c));
		put("cm_logon_request", c, fn);
	}
	/* 5. CM abort (aircraft side), reason 1 = undefined error */
	{
		CMAircraftMessage_t m;
		memset(&m, 0, sizeof(m));
		m.present = CMAircraftMessage_PR_cmAbortReason;
		m.choice.cmAbortReason = CMAbortReason_undefined_error;
		n = enc(&asn_DEF_CMAircraftMessage, &m, a, sizeof(a), &unused);
		put("cm_abort_inner", a, n);
		fn = wrap_fed(PC, a, n, unused, c, sizeof(c));
		put("cm_abort", c, fn);
	}
	/* 6. ADS-C v2 aircraft PDU: user abort, reason 3 = unable to decode message (stamped 2026-11-12 09:41:07) */
	{
		ADSAircraftPDUs_t m;
		memset(&m, 0, sizeof(m));
		m.timestamp.date.year = 2026;
		m.timestamp.date.month = 11;
		m.timestamp.date.day = 12;
		m.timestamp.time.timeHours = 9;
		m.timestamp.time.timeMinutes = 41;
		static TimeSeconds_t secs;
		secs = 7;
		m.timestamp.time.timeSeconds = &secs;
		m.adsAircraftPdu.present = ADSAircraftPDU_PR_aDS_user_abort_PDU;
		m.adsAircraftPdu.choice.aDS_user_abort_PDU = UserAbortReason_unable_to_decode_message;
		n = enc(&asn_DEF_ADSAircraftPDUs, &m, a, sizeof(a), &unused);
		put("adsc_user_abort_inner", a, n);
		fn = wrap_fed(PC, a, n, unused, c, sizeof(c));
		put("adsc_user_abort", c, fn);
	}
	/* 7. ACSE AARE (association rejected, permanent; service user: no reason given) as X.225 Short Accept SPDU:
	 *    f0 = SAC, 02 = presentation control octet "PER unaligned", then the ACSE APDU */
	{
		ACSE_apdu_t acse;
		memset(&acse, 0, sizeof(acse));
		acse.present = ACSE_apdu_PR_aare;
		AARE_apdu_t *r = &acse.choice.aare;
		static unsigned long arcs[] = { 1, 0, 9835, 2 };
		OBJECT_IDENTIFIER_set_arcs(&r->application_context_name, arcs, sizeof(arcs[0]), 4);
		r->result = Associate_result_rejected_permanent;
		r->result_source_diagnostic.present = Associate_source_diagnostic_PR_acse_service_user;
		r->result_source_diagnostic.choice.acse_service_user = Associate_source_diagnostic__acse_service_user_no_reason_given;
		n = enc(&asn_DEF_ACSE_apdu, &acse, a, sizeof(a), &unused);
		b[0] = 0xf0; b[1] = 0x02;
		memcpy(b + 2, a, n);
		put("x225_sac_aare_rejected", b, n + 2);
	}
	return 0;
}
