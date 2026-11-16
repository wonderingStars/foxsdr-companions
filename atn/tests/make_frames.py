#!/usr/bin/env python3
"""Builds tests/frames.txt (input of the helper's test) from tests/apdus.txt and hand-assembled lower layers.

GPL-3.0-or-later, new code written for the FoxSDR atn helper.

Every frame is synthetic: the lower layers (AVLC, ISO 8208 / X.25, ISO 8473 CLNP incl. the ICAO LREF compressed
header, ISO 8073 COTP, ES-IS, IDRP) are assembled here, byte by byte, from the layouts in the standards (ICAO Doc
9705 / 9776, X.25, ISO 8473, 9542, 10747, 8073), NOT by running dumpvdl2's own decoder. The application PDUs
(tests/apdus.txt) come from the asn1c PER encoder via atn_testgen. Addresses are invented.

Run:  python make_frames.py > frames.txt
Line format of the output: '# <name> | <expected layers, comma separated> | <substring expected in the text>'
followed by '<seconds> <hex frame with FCS>'.
"""
import sys, os

def crc16(data, init=0xFFFF):
    crc = init
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0x8408 if crc & 1 else crc >> 1
    return crc

def with_fcs(frame):
    fcs = (~crc16(frame)) & 0xFFFF
    return frame + bytes([fcs & 0xFF, fcs >> 8])

def reverse_bits(v, n):
    r = 0
    for i in range(n):
        if v & (1 << i):
            r |= 1 << (n - 1 - i)
    return r

def avlc_addr(addr, atype, status, last):
    """4 octets, 7 data bits each, LSB of each octet is the address-extension bit (1 on the last octet)."""
    val = (addr & 0xFFFFFF) | (atype << 24) | (status << 27)
    v = reverse_bits(val, 28)
    out = []
    for i in range(4):
        out.append(((v >> (7 * i)) & 0x7F) << 1)
    out[3] |= 1 if last else 0
    return bytes(out)

AIR = 0x4CA2B1      # invented aircraft address
GND = 0x2B1A93      # invented ground station address
T_AIRCRAFT, T_GS = 1, 4

def avlc_frame(src, dst, lcf, info, air_to_gnd, cr_response=False):
    """dst field carries the A/G bit in its status position, src field carries C/R."""
    stype_s = T_AIRCRAFT if air_to_gnd else T_GS
    stype_d = T_GS if air_to_gnd else T_AIRCRAFT
    dstb = avlc_addr(dst, stype_d, 0 if air_to_gnd else 1, False)   # A/G bit: 0 = airborne (src is aircraft)
    srcb = avlc_addr(src, stype_s, 1 if cr_response else 0, False)
    return with_fcs(dstb + srcb + bytes([lcf]) + info)

def lcf_i(sseq, rseq, poll=0):
    return (rseq << 5) | (poll << 4) | (sseq << 1)

def lcf_s_rr(rseq, pf=0):
    return (rseq << 5) | (pf << 4) | 0x01      # S frame, sfunc 0 = RR

def lcf_u(mfunc, pf=0):
    # U frame: mfunc is the 6-bit function code as dumpvdl2 names it (DISC 0x10, UA 0x18, XID 0x2b ...);
    # on the wire it sits above the two type bits, with the P/F bit as its bit 2 (i.e. DISC with P=0 is 0x43)
    return ((mfunc & 0x3B) << 2) | (pf << 4) | 0x03

def x25_data(payload, sseq=0, rseq=0, more=0, chan=1):
    return bytes([0x10, chan, (rseq << 5) | (more << 4) | (sseq << 1)]) + payload

def x25_addr_block(called, calling):
    # nibble strings; layout: len byte (calling<<4|called), then called nibbles, then calling nibbles, packed
    nib = called + calling
    if len(nib) % 2:
        nib += '0'
    return bytes([(len(calling) << 4) | len(called)]) + bytes.fromhex(nib)

def nsap(tail):
    return bytes.fromhex('470027' + '81' + '0000000000' + '00000000' + '0000' + tail)   # 20 octets, invented ATN-style NSAP

def clnp_full(payload, dst, src, seg=None, ms=0):
    sp = 1 if seg is not None else 0
    addrs = bytes([len(dst)]) + dst + bytes([len(src)]) + src
    segpart = b''
    if seg:
        segpart = seg[0].to_bytes(2, 'big') + seg[1].to_bytes(2, 'big') + seg[2].to_bytes(2, 'big')
    hlen = 9 + len(addrs) + len(segpart)
    flags = (sp << 7) | (ms << 6) | 0x1C
    total = hlen + len(payload)
    hdr = bytes([0x81, hlen, 1, 0x3C, flags]) + total.to_bytes(2, 'big') + b'\x00\x00'
    return hdr + addrs + segpart + payload

def clnp_lref(payload, lref=9):
    # compressed, non-segmented data PDU: type 0, priority 0; lifetime 0x3c; flags 0; lref (7 bits) with EXP=0
    return bytes([0x00, 0x3C, 0x00, lref & 0x7F]) + payload

def cotp_dt(user, nr=0, eot=1, dst_ref=0x0102):
    # class 4 data TPDU: LI=4, code 0xF0, DST-REF (2 octets), TPDU-NR with the EOT bit
    return bytes([0x04, 0xF0]) + dst_ref.to_bytes(2, 'big') + bytes([(eot << 7) | nr]) + user

def cotp_dr(dst_ref, src_ref, reason, user=b''):
    return bytes([0x06, 0x80]) + dst_ref.to_bytes(2, 'big') + src_ref.to_bytes(2, 'big') + bytes([reason]) + user

def esis_ish(net, holdtime=30):
    body = bytes([len(net)]) + net
    hdr = bytes([0x82, 9 + len(body), 1, 0, 0x04]) + holdtime.to_bytes(2, 'big') + b'\x00\x00'
    return hdr + body

def idrp_keepalive(seq, ack):
    return bytes([0x85]) + (30).to_bytes(2, 'big') + bytes([4]) + seq.to_bytes(4, 'big') + ack.to_bytes(4, 'big') \
        + bytes([1, 1]) + bytes(16)

def load_apdus(path):
    d = {}
    for line in open(path):
        line = line.strip()
        if line and not line.startswith('#'):
            k, v = line.split()
            d[k] = bytes.fromhex(v)
    return d

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = load_apdus(os.path.join(here, 'apdus.txt'))
    out = []
    t = [100.0]
    def add(name, layers, expect, frame):
        out.append('# %s | %s | %s' % (name, layers, expect))
        out.append('%.1f %s' % (t[0], frame.hex()))
        t[0] += 1.0

    dst_n, src_n = nsap('0000000001'), nsap('0000000002')
    # --- AVLC only ----------------------------------------------------------------------------------------------
    add('avlc_s_rr', 'avlc', 'Receive Ready',
        avlc_frame(AIR, GND, lcf_s_rr(3), b'', True))
    add('avlc_u_disc', 'avlc,unknown_proto', 'DISC',
        avlc_frame(AIR, GND, lcf_u(0x10, 1), b'', True))
    xid = bytes([0x82, 0x80, 0x00, 0x00, 0xF0, 0x00, 0x03, 0x01, 0x01, 0x04])
    add('avlc_xid', 'avlc,xid', 'XID',
        avlc_frame(AIR, GND, lcf_u(0x2B, 1), xid, True))
    # --- ISO 8208 (X.25) ----------------------------------------------------------------------------------------
    callreq = bytes([0x10, 0x01, 0x0B]) + x25_addr_block('0a1b2c', '3d4e5f') + bytes([4, 0x00, 0x00, 0x01, 0x80]) \
        + bytes([0xC1, 4, 1, 0, 0, 0x02])
    add('x25_call_request_lref', 'avlc,x25', 'Call Request',
        avlc_frame(AIR, GND, lcf_i(0, 0, 1), callreq, True))
    callacc = bytes([0x10, 0x01, 0x0F]) + x25_addr_block('0a1b2c', '3d4e5f') + bytes([2, 0x00, 0x00]) + bytes([0x02])
    add('x25_call_accepted', 'avlc,x25', 'Call Accepted',
        avlc_frame(GND, AIR, lcf_i(0, 1), callacc, False, True))
    add('x25_rr', 'avlc,x25', 'Receive Ready',
        avlc_frame(AIR, GND, lcf_i(1, 1), bytes([0x10, 0x01, 0x41]), True))
    add('x25_clear_request', 'avlc,x25', 'Clear Request',
        avlc_frame(GND, AIR, lcf_i(2, 1), bytes([0x10, 0x01, 0x13, 0x00, 0x00]), False))
    # --- ES-IS and IDRP -------------------------------------------------------------------------------------------
    add('esis_ish', 'avlc,x25,esis', 'IS Hello',
        avlc_frame(GND, AIR, lcf_i(3, 1), x25_data(esis_ish(src_n), 0, 0), False))
    add('idrp_keepalive', 'avlc,x25,clnp,idrp', 'Keepalive',
        avlc_frame(GND, AIR, lcf_i(4, 1), x25_data(clnp_full(idrp_keepalive(7, 6), src_n, dst_n), 1, 0), False))
    # --- CLNP + COTP + applications -------------------------------------------------------------------------------
    def app(name, layers, expect, air_to_gnd, apdu_key, lref=False):
        cotp = cotp_dt(ap[apdu_key])
        clnp = clnp_lref(cotp) if lref else clnp_full(cotp, dst_n if air_to_gnd else src_n,
                                                       src_n if air_to_gnd else dst_n)
        src, dst = (AIR, GND) if air_to_gnd else (GND, AIR)
        add(name, layers, expect, avlc_frame(src, dst, lcf_i(5, 2), x25_data(clnp, 2, 1), air_to_gnd))
    app('cm_logon_request', 'avlc,x25,clnp,cotp,context_mgmt', 'TEST123', True, 'cm_logon_request')
    app('cm_abort_lref', 'avlc,x25,clnp,cotp,context_mgmt',
        'Context Management - Abort Reason: undefined-error', True, 'cm_abort', lref=True)
    app('cpdlc_downlink_wilco', 'avlc,x25,clnp,cotp,cpdlc', 'WILCO', True, 'cpdlc_downlink_dm0_wilco')
    app('cpdlc_uplink_unable', 'avlc,x25,clnp,cotp,cpdlc', 'UNABLE', False, 'cpdlc_uplink_um0_unable')
    app('cpdlc_abort_user_lref', 'avlc,x25,clnp,cotp,cpdlc', 'User Abort Reason: commanded-termination',
        True, 'cpdlc_abort_user', lref=True)
    app('adsc_v2_user_abort', 'avlc,x25,clnp,cotp,adsc_v2', 'ADS-C', True, 'adsc_user_abort')
    app('x225_short_accept_aare', 'avlc,x25,clnp,cotp,x225_spdu,x227_apdu', 'Short Accept', False, 'x225_sac_aare_rejected')
    # COTP DR carrying an ACSE-free abort is covered by the next one: DR with no user data
    add('cotp_dr', 'avlc,x25,clnp,cotp', 'COTP Disconnect Request',
        avlc_frame(AIR, GND, lcf_i(6, 2), x25_data(clnp_full(cotp_dr(0x0102, 0x0304, 0x80), dst_n, src_n), 3, 1), True))
    # --- reassembly: one CLNP PDU split over two X.25 data packets (M bit) ---------------------------------------------
    whole = clnp_full(cotp_dt(ap['cpdlc_downlink_dm0_wilco']), dst_n, src_n)
    h = len(whole) // 2
    add('x25_fragment_1_of_2', 'avlc,x25,unknown_proto', 'Data',
        avlc_frame(AIR, GND, lcf_i(0, 0), x25_data(whole[:h], 4, 1, more=1), True))
    add('x25_fragment_2_of_2_reassembled', 'avlc,x25,clnp,cotp,cpdlc', 'WILCO',
        avlc_frame(AIR, GND, lcf_i(1, 0), x25_data(whole[h:], 5, 1, more=0), True))
    # --- ACARS-over-AVLC is passed through undecoded --------------------------------------------------------------
    add('acars_passthrough', 'avlc,unknown_proto', 'AVLC type: I',
        avlc_frame(AIR, GND, lcf_i(7, 3), bytes([0xFF, 0xFF, 0x01, 0x32, 0x2E, 0x4E, 0x31, 0x32, 0x33]), True))
    sys.stdout.buffer.write(chr(10).join(out).encode('ascii') + bytes([10]))

if __name__ == '__main__':
    main()
