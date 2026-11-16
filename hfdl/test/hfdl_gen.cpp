// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * hfdl_gen: HFDL signal generator for the hfdl helper's tests (written for this helper).
 *
 * Builds complete HFDL transmissions from the same constants the decoder uses (A and M1 sequences, M1 shifts,
 * training sequence, scrambler polynomial, interleaver geometry, the K=7 r=1/2 convolutional code, CRC-16/CCITT
 * frame check) and writes them as complex baseband I/Q: an MPDU / SPDU is built byte by byte, convolutionally
 * encoded, interleaved, scrambled, mapped to BPSK / QPSK / 8PSK, framed with prekey + A1 + A2 + M1 + M2 + training
 * sequences, pulse shaped (root raised cosine, 1800 baud), mixed to the channel (carrier 1440 Hz above the
 * assigned frequency, as for an SSB-tuned receiver), a frequency error and noise are added.
 *
 *   hfdl_gen --scenario all|noise --format cs16|cf32 --rate 21600 -o file
 *
 * The generator shares no code with the decoder: only the constants are written out a second time, so a wrong
 * constant on either side makes the decode test fail.
 */
#define _USE_MATH_DEFINES 1
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <complex>
#include <vector>
#include <string>

typedef std::complex<double> cd;

// ---------------------------------------------------------------- constants (HFDL, as in dumphfdl's hfdl.c)
static const int A_LEN = 127, M1_LEN = 127, M2_LEN = 15, T_LEN = 15, DATA_FRAME_LEN = 30, PREKEY_LEN = 448;
static const uint8_t A_OCTETS[16] = {0x5b, 0xbc, 0x74, 0x57, 0x03, 0xd9, 0x89, 0x39,
                                     0xf2, 0x08, 0xd5, 0x36, 0x94, 0x2c, 0x32, 0xfe};
static const int M1_BITS[M1_LEN] = {
	0,1,1,1,0,1,1,0,1,1,1,1,0,1,0,0,0,1,0,1,1,0,0,
	1,0,1,1,1,1,1,0,0,0,1,0,0,0,0,0,0,1,1,0,0,1,1,0,1,1,
	0,0,0,1,1,1,0,0,1,1,1,0,1,0,1,1,1,0,0,0,0,1,0,0,1,1,
	0,0,0,0,0,1,0,1,0,1,0,1,1,0,1,0,0,1,0,0,1,0,1,0,0,1,
	1,1,1,0,0,1,0,0,0,1,1,0,1,0,1,0,0,0,0,1,1,1,1,1,1,1
};
static const int M_SHIFTS[8] = {72, 82, 113, 123, 61, 103, 93, 9};
static const int T_SEQ[T_LEN] = {1, 1, 1, -1, 1, 1, -1, -1, 1, -1, 1, -1, -1, -1, -1};   // 0x9AF, 15 bits, 0 -> +1

struct FrameParams { int arity; int segments; int code_rate; int push_shift; };
static const FrameParams FP[8] = {
	{1, 72, 4, 17}, {1, 72, 2, 17}, {2, 72, 2, 17}, {3, 72, 2, 17},      // 300 / 600 / 1200 / 1800 bps, single slot
	{1, 168, 4, 23}, {1, 168, 2, 23}, {2, 168, 2, 23}, {3, 168, 2, 23}   // the same, double slot
};
static const int ROWS = 40, POP_ROW_SHIFT = 9;

// ---------------------------------------------------------------- helpers
static uint8_t reverse_byte(uint8_t b) {
	uint8_t r = 0;
	for (int i = 0; i < 8; i++) if (b & (1 << i)) r |= 1 << (7 - i);
	return r;
}
static int parity32(unsigned x) { int p = 0; while (x) { p ^= 1; x &= x - 1; } return p; }

static uint16_t crc16_ccitt_x25(const uint8_t *d, size_t n) {       // bitwise, reflected 0x1021 (0x8408), init 0xFFFF, xorout 0xFFFF
	uint16_t crc = 0xFFFF;
	for (size_t i = 0; i < n; i++) {
		crc ^= d[i];
		for (int b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
	}
	return crc ^ 0xFFFF;
}
static void put_fcs(std::vector<uint8_t> &v, size_t at) {           // FCS of v[0..at) stored little endian at v[at]
	uint16_t f = crc16_ccitt_x25(v.data(), at);
	v[at] = f & 0xFF;
	v[at + 1] = f >> 8;
}

// ---------------------------------------------------------------- PDU builders
// Downlink MPDU carrying the given LPDUs (each without its FCS)
static std::vector<uint8_t> build_downlink_mpdu(int src_ac, int dst_gs, const std::vector<std::vector<uint8_t>> &lpdus) {
	int n = (int)lpdus.size();
	int hdr = 6 + n;
	std::vector<uint8_t> v(hdr + 2, 0);
	v[0] = 0x01 | 0x02 | (uint8_t)(n << 2);
	v[1] = dst_gs & 0x7f;
	v[2] = src_ac;
	for (int i = 0; i < n; i++) v[6 + i] = (uint8_t)(lpdus[i].size() + 2 - 1);
	put_fcs(v, hdr);
	for (auto &l : lpdus) {
		std::vector<uint8_t> x = l;
		x.resize(l.size() + 2);
		put_fcs(x, l.size());
		v.insert(v.end(), x.begin(), x.end());
	}
	return v;
}
// Uplink MPDU to one aircraft
static std::vector<uint8_t> build_uplink_mpdu(int src_gs, int dst_ac, const std::vector<std::vector<uint8_t>> &lpdus) {
	int n = (int)lpdus.size();
	int hdr = 2 + 2 + n;
	std::vector<uint8_t> v(hdr + 2, 0);
	v[0] = 0x01 | (0 << 4);                    // MPDU, uplink, 1 aircraft
	v[1] = src_gs & 0x7f;
	v[2] = dst_ac;
	v[3] = (uint8_t)(n << 4);
	for (int i = 0; i < n; i++) v[4 + i] = (uint8_t)(lpdus[i].size() + 2 - 1);
	put_fcs(v, hdr);
	for (auto &l : lpdus) {
		std::vector<uint8_t> x = l;
		x.resize(l.size() + 2);
		put_fcs(x, l.size());
		v.insert(v.end(), x.begin(), x.end());
	}
	return v;
}
static std::vector<uint8_t> icao_bytes(uint32_t icao) {                 // lpdu.c: parse_icao_hex() reverses every byte
	return {reverse_byte((icao >> 16) & 0xFF), reverse_byte((icao >> 8) & 0xFF), reverse_byte(icao & 0xFF)};
}
static std::vector<uint8_t> lpdu_logon_request(uint32_t icao) {
	std::vector<uint8_t> l = {0x8F};
	auto i = icao_bytes(icao); l.insert(l.end(), i.begin(), i.end());
	return l;
}
static std::vector<uint8_t> lpdu_logon_confirm(uint32_t icao, int ac_id) {
	std::vector<uint8_t> l = {0x9F};                  // LOGON_CONFIRM_LPDU_LEN is 8 octets: type, ICAO[3], aircraft id, 3 more
	auto i = icao_bytes(icao); l.insert(l.end(), i.begin(), i.end());
	l.push_back((uint8_t)ac_id);
	l.resize(8, 0);
	return l;
}
static std::vector<uint8_t> lpdu_logon_resume(uint32_t icao) {
	std::vector<uint8_t> l = {0x4F};
	auto i = icao_bytes(icao); l.insert(l.end(), i.begin(), i.end());
	return l;
}
static std::vector<uint8_t> lpdu_logoff(uint32_t icao, int reason) {
	std::vector<uint8_t> l = {0x3F};
	auto i = icao_bytes(icao); l.insert(l.end(), i.begin(), i.end());
	l.push_back((uint8_t)reason);
	return l;
}
// Squitter (SPDU) from ground station gs_id
static std::vector<uint8_t> build_spdu(int gs_id, int frame_index, int frame_offset) {
	std::vector<uint8_t> v(66, 0);
	v[0] = 0x02 | (0 << 2) | 0x20;                 // RLS in use, version 0, ISO 8208 supported, change note 0
	v[1] = (gs_id & 0x7f) | 0x80;                  // UTC sync
	v[2] = frame_index & 0xFF;
	v[3] = ((frame_index >> 8) & 0xF) | (frame_offset << 4);
	v[52] = 3;                                     // minimum priority
	v[53] = 52;                                    // system table version 52
	v[54] = 0x05 << 4;                             // freqs in use (low nibble of version high part is 0)
	v[55] = 0x01;
	v[57] = 2;                                     // second GS: id 2, no UTC sync
	v[58] = 0x21;
	put_fcs(v, 64);
	return v;
}

// ---------------------------------------------------------------- transmitter
static std::vector<int> make_symbol_stream_bits(const FrameParams &p, const std::vector<uint8_t> &pdu_in, int &n_symbols) {
	int num_symbols = p.segments * DATA_FRAME_LEN;
	int coded_bits_total = num_symbols * p.arity;
	int vit_in_len = coded_bits_total / (p.code_rate == 4 ? 2 : 1);
	int n_info = vit_in_len / 2;
	int cap_bytes = (n_info + 7) / 8;
	std::vector<uint8_t> pdu = pdu_in;
	// at least 12 zero bits must follow the PDU, so that the code is flushed
	if ((int)pdu.size() * 8 + 12 > n_info) { fprintf(stderr, "PDU of %zu bytes does not fit a frame of %d bytes\n", pdu.size(), cap_bytes); exit(2); }
	pdu.resize(cap_bytes, 0);
	// info bits: every byte reversed, then MSB first
	std::vector<int> info;
	for (uint8_t b : pdu) { uint8_t r = reverse_byte(b); for (int i = 7; i >= 0; i--) info.push_back((r >> i) & 1); }
	info.resize(n_info);
	// K=7 r=1/2 convolutional code, polynomials 0x6d and 0x4f (libfec V27POLYA / V27POLYB), flushed by the zero padding
	std::vector<int> coded;
	unsigned reg = 0;
	for (int b : info) {
		reg = ((reg << 1) | b) & 0x7f;
		coded.push_back(parity32(reg & 0x6d));
		coded.push_back(parity32(reg & 0x4f));
	}
	// order in which the decoder pops from its deinterleaver; at 300 bps every coded bit is sent twice
	std::vector<int> pops;
	if (p.code_rate == 4) { for (int c : coded) { pops.push_back(c); pops.push_back(c); } } else pops = coded;
	// deinterleaver geometry
	int cols = coded_bits_total / ROWS;
	std::vector<int> table((size_t)ROWS * cols, -1);
	{   // push positions
		int row = 0, col = 0;
		for (int k = 0; k < coded_bits_total; k++) {
			table[(size_t)row * cols + col] = k;
			row++;
			if (row == ROWS) { row = 0; col++; }
			col -= p.push_shift;
			if (col < 0) col += cols;
		}
	}
	std::vector<int> tx(coded_bits_total, -1);
	{   // pop positions
		int row = 0, col = 0;
		for (int j = 0; j < (int)pops.size(); j++) {
			int k = table[(size_t)row * cols + col];
			if (k < 0 || tx[k] != -1) { fprintf(stderr, "interleaver is not a permutation\n"); exit(2); }
			tx[k] = pops[j];
			row = (row + POP_ROW_SHIFT) % ROWS;
			if (row == 0) col++;
		}
	}
	n_symbols = num_symbols;
	return tx;
}

struct Lfsr15 {      // x^15 + x^14 + 1, 15-bit, as the decoder's descrambler (generator 0x4001, state 0x4d4b), reset every 120 bits
	unsigned state = 0x4d4b; int pos = 0;
	int next() {
		if (pos == 120) { pos = 0; state = 0x4d4b; }
		pos++;
		unsigned b = parity32(state & 0x4001);
		state = ((state << 1) | b) & 0x7fff;
		return (int)b;
	}
};

static cd psk_point(int arity, unsigned sym) {
	if (arity == 1) return sym ? cd(-1, 0) : cd(1, 0);
	unsigned M = 1u << arity;
	unsigned g = sym; for (unsigned s = sym >> 1; s; s >>= 1) g ^= s;     // gray decode
	return std::polar(1.0, 2.0 * M_PI * g / M);
}

struct Burst {
	double start_s;
	int channel_offset_hz;
	int m1_index;
	std::vector<uint8_t> pdu;
	double freq_error_hz;
	double phase0;
	double amp;
};

static std::vector<cd> frame_symbols(const Burst &b) {
	const FrameParams &p = FP[b.m1_index];
	int n_sym = 0;
	std::vector<int> tx = make_symbol_stream_bits(p, b.pdu, n_sym);
	std::vector<cd> s;
	// prekey: pseudo-random BPSK
	unsigned st = 0x1234;
	for (int i = 0; i < PREKEY_LEN; i++) { unsigned bit = parity32(st & 0x4001); st = ((st << 1) | bit) & 0x7fff; s.push_back(bit ? cd(-1, 0) : cd(1, 0)); }
	auto bpsk = [&](int bit) { return bit ? cd(-1, 0) : cd(1, 0); };
	for (int rep = 0; rep < 2; rep++)                                      // A1, A2
		for (int i = 0; i < A_LEN; i++) s.push_back(bpsk((A_OCTETS[i / 8] >> (7 - i % 8)) & 1));
	for (int j = 0; j < M1_LEN; j++) s.push_back(bpsk(M1_BITS[(M_SHIFTS[b.m1_index] + j) % M1_LEN]));    // M1
	for (int j = 0; j < M2_LEN; j++) s.push_back(bpsk(M1_BITS[(M_SHIFTS[b.m1_index] + j) % M1_LEN]));    // M2
	for (int t = 0; t < 9; t++) for (int i = 0; i < T_LEN; i++) s.push_back(cd(T_SEQ[i], 0));
	Lfsr15 scr;
	size_t bitpos = 0;
	for (int f = 0; f < p.segments; f++) {
		for (int i = 0; i < DATA_FRAME_LEN; i++) {
			unsigned sym = 0;
			for (int k = 0; k < p.arity; k++) sym = (sym << 1) | (unsigned)tx[bitpos++];
			cd pt = psk_point(p.arity, sym);
			if (scr.next()) pt = -pt;
			s.push_back(pt);
		}
		for (int i = 0; i < T_LEN; i++) s.push_back(cd(T_SEQ[i], 0));
	}
	(void)n_sym;
	return s;
}

static double srrc(double t, double beta) {      // t in symbol periods
	if (fabs(t) < 1e-9) return 1.0 - beta + 4.0 * beta / M_PI;
	if (fabs(fabs(4.0 * beta * t) - 1.0) < 1e-9)
		return beta / sqrt(2.0) * ((1 + 2 / M_PI) * sin(M_PI / (4 * beta)) + (1 - 2 / M_PI) * cos(M_PI / (4 * beta)));
	return (sin(M_PI * t * (1 - beta)) + 4 * beta * t * cos(M_PI * t * (1 + beta))) / (M_PI * t * (1 - (4 * beta * t) * (4 * beta * t)));
}

struct Rng { uint64_t s = 0x9E3779B97F4A7C15ull; double u() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return ((s >> 11) + 0.5) / 9007199254740992.0; }
             double gauss() { double a = u(), b = u(); return sqrt(-2 * log(a)) * cos(2 * M_PI * b); } };

int main(int argc, char **argv) {
	std::string scenario = "all", format = "cs16", out;
	double rate = 21600.0, noise_sigma = 0.03;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--scenario" && i + 1 < argc) scenario = argv[++i];
		else if (a == "--format" && i + 1 < argc) format = argv[++i];
		else if (a == "--rate" && i + 1 < argc) rate = atof(argv[++i]);
		else if (a == "--noise" && i + 1 < argc) noise_sigma = atof(argv[++i]);
		else if (a == "-o" && i + 1 < argc) out = argv[++i];
		else { fprintf(stderr, "usage: hfdl_gen --scenario all|noise|one --format cs16|cf32 --rate Hz [--noise sigma] -o file\n"); return 2; }
	}
	if (out.empty()) { fprintf(stderr, "no output file\n"); return 2; }

	std::vector<Burst> bursts;
	const uint32_t ICAO = 0xABC123;
	double total_s = 17.0;
	if (scenario == "all") {
		// ground station squitter, 300 bps single slot, channel 0
		bursts.push_back({0.30, 0, 0, build_spdu(4, 2397, 1), 7.0, 0.4, 0.5});
		// uplink MPDU, 1800 bps single slot (8PSK): logon confirm (aircraft 225 -> ICAO ABC123) + logoff of another aircraft
		bursts.push_back({0.60, 3000, 3, build_uplink_mpdu(2, 225, {lpdu_logon_confirm(ICAO, 225), lpdu_logoff(0x4B1234, 6)}), -11.0, 2.0, 0.5});
		// downlink MPDU, 1200 bps single slot (QPSK), from aircraft 225: logon request + logon resume
		bursts.push_back({3.60, -3000, 2, build_downlink_mpdu(225, 2, {lpdu_logon_request(ICAO), lpdu_logon_resume(ICAO)}), 3.0, -1.0, 0.5});
		// the same station on the channel where aircraft 225 was confirmed: its address is now shown next to the aircraft id
		bursts.push_back({5.20, 3000, 1, build_downlink_mpdu(225, 2, {lpdu_logon_request(ICAO)}), 0.0, 0.0, 0.5});
		// double slot 600 bps (BPSK, rate 1/2) and 300 bps double slot, back to back on the same channel set
		bursts.push_back({7.50, 0, 5, build_downlink_mpdu(226, 4, {lpdu_logon_request(0x00F00D)}), -4.0, 1.0, 0.5});
		bursts.push_back({10.60, -3000, 4, build_downlink_mpdu(227, 1, {lpdu_logoff(0x123456, 3)}), 5.0, 2.5, 0.5});
		// a frame with a damaged frame check sequence (ignored unless --output-corrupted-pdus)
		{ auto bad = build_downlink_mpdu(228, 2, {lpdu_logon_request(0xDEAD01)}); bad[bad.size() - 3] ^= 0x55;
		  bursts.push_back({12.00, 3000, 2, bad, 0.0, 0.0, 0.5}); }
	} else if (scenario == "one") {
		bursts.push_back({0.30, 0, 0, build_spdu(4, 2397, 1), 0.0, 0.0, 0.5});
		total_s = 3.5;
	} else if (scenario != "noise") { fprintf(stderr, "unknown scenario\n"); return 2; }
	if (scenario == "noise") total_s = 3.0;

	size_t N = (size_t)(total_s * rate);
	std::vector<cd> x(N, cd(0, 0));
	const double beta = 0.6;
	for (const Burst &b : bursts) {
		std::vector<cd> sym = frame_symbols(b);
		const double sps = rate / 1800.0;
		size_t start = (size_t)(b.start_s * rate);
		int span = 8;     // symbols each side
		size_t len = (size_t)(sym.size() * sps) + (size_t)(2 * span * sps);
		for (size_t n = 0; n < len && start + n < N; n++) {
			double t = (double)n / sps - span;           // in symbols, relative to symbol 0 centre
			int k0 = (int)floor(t) - span, k1 = (int)floor(t) + span;
			cd acc(0, 0);
			for (int k = k0; k <= k1; k++) if (k >= 0 && k < (int)sym.size()) acc += sym[k] * srrc(t - k, beta);
			double f = b.channel_offset_hz + 1440.0 + b.freq_error_hz;
			double ph = 2 * M_PI * f * ((double)(start + n) / rate) + b.phase0;
			x[start + n] += b.amp * 0.25 * acc * cd(cos(ph), sin(ph));
		}
	}
	Rng rng;
	FILE *f = fopen(out.c_str(), "wb");
	if (!f) { perror("open"); return 2; }
	for (size_t n = 0; n < N; n++) {
		double re = x[n].real() + noise_sigma * rng.gauss(), im = x[n].imag() + noise_sigma * rng.gauss();
		if (format == "cs16") {
			int16_t s[2] = {(int16_t)lrint(re * 32000.0), (int16_t)lrint(im * 32000.0)};
			fwrite(s, sizeof s, 1, f);
		} else {
			float s[2] = {(float)re, (float)im};
			fwrite(s, sizeof s, 1, f);
		}
	}
	fclose(f);
	fprintf(stderr, "hfdl_gen: %s, %zu samples at %.0f S/s, %zu bursts\n", scenario.c_str(), N, rate, bursts.size());
	return 0;
}
