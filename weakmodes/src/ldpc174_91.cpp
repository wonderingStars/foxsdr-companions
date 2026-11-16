// weakmodes: LDPC(174,91) encoder and the hybrid belief-propagation / ordered-statistics decoder used by FT2L.
//
// C++ port of WSJT-X lib/ft8/decode174_91.f90, bpdecode174_91.f90 (the BP part inside decode174_91),
// osd174_91.f90 (with mrbencode91, nextpat91, boxit91, fetchit91), encode174_91.f90, encode174_91_nocrc.f90,
// get_crc14.f90, platanh.f90.
// WSJT-X, Copyright (C) 2001-2025 by Joe Taylor, K1JT, and the WSJT Development Group; GPL-3.0-or-later (see LICENSE).
//
// Changes: Fortran 1-based arrays are 0-based here; the sort inside osd174_91 (indexx) is a stable sort, so equal
// reliabilities are ordered by position (indexx's order for ties is unspecified); bit and index arrays are plain
// vectors. Nothing else differs.
#include "ldpc174_91.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#ifdef _MSC_VER
#include <intrin.h>
#else
#define __popcnt64 __builtin_popcountll
#endif
#include <vector>

#include "ldpc174_91_tables.h"

namespace wm {

namespace {

const int N = 174, K = 91, M = 83;
const int S = 176; // row stride (bytes) of the OSD matrices: a multiple of 8 so rows XOR as 64-bit words

struct GenMatrix {
    uint8_t gen[M][K]; // gen(M,K) of encode174_91
    GenMatrix() {
        memset(gen, 0, sizeof gen);
        for (int i = 0; i < M; i++) {
            for (int j = 0; j < 23; j++) {
                char ch = kGen[i][j];
                int istr = ch >= 'a' ? ch - 'a' + 10 : ch >= 'A' ? ch - 'A' + 10 : ch - '0';
                int ibmax = (j == 22) ? 3 : 4;
                for (int jj = 1; jj <= ibmax; jj++) {
                    int icol = j * 4 + jj - 1;
                    if ((istr >> (4 - jj)) & 1) gen[i][icol] = 1;
                }
            }
        }
    }
};
const GenMatrix &genmat() {
    static GenMatrix g;
    return g;
}

// platanh: piecewise-linear approximation of atanh
float platanh(float x) {
    int isign = +1;
    float z = x;
    if (x < 0) { isign = -1; z = std::fabs(x); }
    if (z <= 0.664f) return x / 0.83f;
    if (z <= 0.9217f) return isign * (z - 0.4064f) / 0.322f;
    if (z <= 0.9951f) return isign * (z - 0.8378f) / 0.0524f;
    if (z <= 0.9998f) return isign * (z - 0.9914f) / 0.0012f;
    return isign * 7.0f;
}

// mrbencode91: XOR of the rows of g2 (row i = column i of the Fortran g2) selected by me. The rows are packed into three
// 64-bit words (gp[3*i..3*i+2], bit j of the row in word j/64 bit j%64); the codeword comes back as 174 bytes.
void mrbencode91(const uint8_t *me, uint8_t *codeword, const uint64_t *gp, int k) {
    uint64_t a0 = 0, a1 = 0, a2 = 0;
    for (int i = 0; i < k; i++)
        if (me[i] == 1) {
            a0 ^= gp[3 * i];
            a1 ^= gp[3 * i + 1];
            a2 ^= gp[3 * i + 2];
        }
    const uint64_t acc[3] = {a0, a1, a2};
    for (int j = 0; j < N; j++) codeword[j] = (uint8_t)((acc[j >> 6] >> (j & 63)) & 1);
}

// nextpat91: next test error pattern; iflag is 1-based like the Fortran (negative when finished)
void nextpat91(uint8_t *mi, int k, int iorder, int *iflag) {
    int ind = -1;
    for (int i = 1; i <= k - 1; i++)
        if (mi[i - 1] == 0 && mi[i] == 1) ind = i;
    if (ind < 0) { *iflag = ind; return; }
    std::vector<uint8_t> ms(k, 0);
    for (int i = 1; i <= ind - 1; i++) ms[i - 1] = mi[i - 1];
    ms[ind - 1] = 1;
    ms[ind] = 0;
    if (ind + 1 < k) {
        int sum = 0;
        for (int i = 0; i < k; i++) sum += ms[i];
        int nz = iorder - sum;
        for (int i = k - nz + 1; i <= k; i++) ms[i - 1] = 1;
    }
    memcpy(mi, ms.data(), k);
    for (int i = 1; i <= k; i++)
        if (mi[i - 1] == 1) { *iflag = i; break; }
}

// boxit91 / fetchit91: hash boxes of syndrome patterns (common /boxes/ in the Fortran)
struct Boxes {
    int indexes[5001][2];
    std::vector<int> fp; // 0..525000
    int np[5001];
    int lastpat = -1, inext = -1;
    Boxes() : fp(525001, -1) { reset_boxes(); }
    void reset_boxes() {
        std::fill(fp.begin(), fp.end(), -1);
        for (int i = 0; i <= 5000; i++) { np[i] = -1; indexes[i][0] = indexes[i][1] = -1; }
    }
    // boxit91 with the pattern number ipat already formed from e2(1:ntau): ipat = sum of e2(i) * 2^(ntau-i)
    void boxit(int ipat, int npindex, int i1, int i2) {
        indexes[npindex][0] = i1;
        indexes[npindex][1] = i2;
        int ip = fp[ipat];
        if (ip == -1) fp[ipat] = npindex;
        else {
            while (np[ip] != -1) ip = np[ip];
            np[ip] = npindex;
        }
    }
    void fetchit(bool *reset, int ipat, int *i1, int *i2) {
        if (*reset) { lastpat = -1; *reset = false; }
        int index = fp[ipat];
        if (lastpat != ipat && index > 0) {
            *i1 = indexes[index][0];
            *i2 = indexes[index][1];
            inext = np[index];
        } else if (lastpat == ipat && inext > 0) {
            *i1 = indexes[inext][0];
            *i2 = indexes[inext][1];
            inext = np[inext];
        } else {
            *i1 = -1;
            *i2 = -1;
            inext = -1;
        }
        lastpat = ipat;
    }
};

// osd174_91 for k = 91 (the only value FT2L uses). llr: channel values; returns nhardmin (negative if the CRC fails)
void osd174_91(const float *llr, int k, const uint8_t *apmask, int ndeep, uint8_t *message91, uint8_t *cw,
               int *nhardmin, float *dmin_out) {
    // generator matrix of the LDPC code with the CRC cascaded (computed once)
    static std::vector<uint8_t> gen; // k x S, row i = codeword of unit message i
    static int gen_k = 0;
    if (gen_k != k) {
        gen.assign((size_t)k * S, 0);
        for (int i = 1; i <= k; i++) {
            uint8_t m91[K] = {0};
            m91[i - 1] = 1;
            if (i <= 77) {
                uint8_t m96[96] = {0};
                memcpy(m96, m91, 91);
                int ncrc14;
                get_crc14(m96, 96, &ncrc14);
                for (int b = 0; b < 14; b++) m91[77 + b] = (ncrc14 >> (13 - b)) & 1;
                for (int b = 77; b < k; b++) m91[b] = 0; // message91(78:k)=0
            }
            encode174_91_nocrc(m91, &gen[(size_t)(i - 1) * S]);
        }
        gen_k = k;
    }
    float rx[N], absrx[N];
    uint8_t apmaskr[N], hdec[N];
    int indices[N];
    for (int i = 0; i < N; i++) {
        rx[i] = llr[i];
        apmaskr[i] = apmask[i];
        hdec[i] = rx[i] >= 0 ? 1 : 0;
        absrx[i] = std::fabs(rx[i]);
    }
    // indx = ascending order of reliability (stable); indices(i) = indx(N+1-i): most reliable first
    std::vector<int> indx(N);
    std::iota(indx.begin(), indx.end(), 0);
    std::stable_sort(indx.begin(), indx.end(), [&](int a, int b) { return absrx[a] < absrx[b]; });
    // genmrb(k,N): columns re-ordered; stored here as columns-of-N per row i (row-major k x N)
    std::vector<uint8_t> genmrb((size_t)k * S, 0);
    for (int i = 0; i < N; i++) {
        int src = indx[N - 1 - i];
        indices[i] = src;
        for (int r = 0; r < k; r++) genmrb[(size_t)r * S + i] = gen[(size_t)r * S + src];
    }
    // Gaussian elimination so the k most reliable (independent) columns form an identity
    for (int id = 0; id < k; id++) {
        for (int icol = id; icol < k + 20 && icol < N; icol++) {
            if (genmrb[(size_t)id * S + icol] == 1) {
                if (icol != id) {
                    for (int r = 0; r < k; r++) std::swap(genmrb[(size_t)r * S + id], genmrb[(size_t)r * S + icol]);
                    std::swap(indices[id], indices[icol]);
                }
                for (int ii = 0; ii < k; ii++) {
                    if (ii != id && genmrb[(size_t)ii * S + id] == 1)
                        for (int c = 0; c < S; c += 8) {
                            uint64_t a, b;
                            memcpy(&a, &genmrb[(size_t)ii * S + c], 8);
                            memcpy(&b, &genmrb[(size_t)id * S + c], 8);
                            a ^= b;
                            memcpy(&genmrb[(size_t)ii * S + c], &a, 8);
                        }
                }
                break;
            }
        }
    }
    // g2 = transpose(genmrb): column i of g2 = row i of genmrb. g2t is genmrb itself (row i = g2(:,i)).
    const std::vector<uint8_t> &g2t = genmrb;
    std::vector<uint64_t> gpv((size_t)k * 3, 0);
    for (int i = 0; i < k; i++)
        for (int j = 0; j < N; j++)
            if (genmrb[(size_t)i * S + j]) gpv[(size_t)i * 3 + (j >> 6)] |= (uint64_t)1 << (j & 63);
    const uint64_t *gp = gpv.data();
    // g2(k+1:N, n1) = row n1 of genmrb at columns k..N-1
    uint8_t hdecp[N], apm[N];
    float absp[N];
    for (int i = 0; i < N; i++) { hdecp[i] = hdec[indices[i]]; absp[i] = absrx[indices[i]]; apm[i] = apmaskr[indices[i]]; }
    uint8_t m0[K], c0[N], cwl[N], ce[N], me[K], mi[K], misub[K], e2sub[N], e2[N];
    memcpy(m0, hdecp, k);
    mrbencode91(m0, c0, gp, k);
    int nhard = 0;
    float dmin = 0;
    for (int i = 0; i < N; i++) {
        int nx = c0[i] ^ hdecp[i];
        nhard += nx;
        dmin += nx * absp[i];
    }
    memcpy(cwl, c0, N);
    int nord = 0, npre1 = 0, npre2 = 0, nt = 40, ntheta = 12, ntau = 14;
    if (ndeep > 6) ndeep = 6;
    if (ndeep == 0) goto done;
    if (ndeep == 1) { nord = 1; npre1 = 0; npre2 = 0; nt = 40; ntheta = 12; }
    else if (ndeep == 2) { nord = 1; npre1 = 1; npre2 = 0; nt = 40; ntheta = 10; }
    else if (ndeep == 3) { nord = 1; npre1 = 1; npre2 = 1; nt = 40; ntheta = 12; ntau = 14; }
    else if (ndeep == 4) { nord = 2; npre1 = 1; npre2 = 1; nt = 40; ntheta = 12; ntau = 17; }
    else if (ndeep == 5) { nord = 3; npre1 = 1; npre2 = 1; nt = 40; ntheta = 12; ntau = 15; }
    else { nord = 4; npre1 = 1; npre2 = 1; nt = 95; ntheta = 12; ntau = 15; }
    {
        int nd1kptbest = 0;
        (void)nd1kptbest;
        for (int iorder = 1; iorder <= nord; iorder++) {
            for (int i = 0; i < k - iorder; i++) misub[i] = 0;
            for (int i = k - iorder; i < k; i++) misub[i] = 1;
            int iflag = k - iorder + 1;
            while (iflag >= 0) {
                int iend = (iorder == nord && npre1 == 0) ? iflag : 1;
                float d1 = 0;
                for (int n1 = iflag; n1 >= iend; n1--) {
                    memcpy(mi, misub, k);
                    mi[n1 - 1] = 1;
                    bool skip = false;
                    for (int i = 0; i < k; i++)
                        if ((apm[i] & mi[i]) == 1) { skip = true; break; }
                    if (skip) continue;
                    for (int i = 0; i < k; i++) me[i] = m0[i] ^ mi[i];
                    int nd1kpt;
                    if (n1 == iflag) {
                        mrbencode91(me, ce, gp, k);
                        nd1kpt = 1;
                        for (int i = 0; i < N - k; i++) {
                            e2sub[i] = ce[k + i] ^ hdecp[k + i];
                            e2[i] = e2sub[i];
                        }
                        for (int i = 0; i < nt; i++) nd1kpt += e2sub[i];
                        d1 = 0;
                        for (int i = 0; i < k; i++) d1 += (me[i] ^ hdecp[i]) * absp[i];
                    } else {
                        nd1kpt = 2;
                        for (int i = 0; i < N - k; i++) e2[i] = e2sub[i] ^ g2t[(size_t)(n1 - 1) * S + k + i];
                        for (int i = 0; i < nt; i++) nd1kpt += e2[i];
                    }
                    if (nd1kpt <= ntheta) {
                        mrbencode91(me, ce, gp, k);
                        int nxs = 0;
                        for (int i = 0; i < N; i++) nxs += ce[i] ^ hdecp[i];
                        float dd;
                        if (n1 == iflag) {
                            float s = 0;
                            for (int i = 0; i < N - k; i++) s += e2sub[i] * absp[k + i];
                            dd = d1 + s;
                        } else {
                            float s = 0;
                            for (int i = 0; i < N - k; i++) s += e2[i] * absp[k + i];
                            dd = d1 + (ce[n1 - 1] ^ hdecp[n1 - 1]) * absp[n1 - 1] + s;
                        }
                        if (dd < dmin) {
                            dmin = dd;
                            memcpy(cwl, ce, N);
                            nhard = nxs;
                            nd1kptbest = nd1kpt;
                        }
                    }
                }
                nextpat91(misub, k, iorder, &iflag);
            }
        }
        if (npre2 == 1) {
            static Boxes boxes;
            bool reset = true;
            boxes.reset_boxes();
            int ntotal = 0;
            std::vector<int> rowpat(k); // bits k..k+ntau-1 of row i as the pattern number (bit j has weight 2^(ntau-1-j))
            for (int i = 0; i < k; i++) {
                int v = 0;
                for (int j = 0; j < ntau; j++) v = (v << 1) | g2t[(size_t)i * S + k + j];
                rowpat[i] = v;
            }
            for (int i1 = k; i1 >= 1; i1--) {
                for (int i2 = i1 - 1; i2 >= 1; i2--) {
                    ntotal++;
                    boxes.boxit(rowpat[i1 - 1] ^ rowpat[i2 - 1], ntotal, i1, i2);
                }
            }
            reset = true;
            // Second pre-processing rule, with the work per candidate pair reduced: the codeword of a pattern
            // m0^misub^e(in1)^e(in2) is the codeword of m0^misub xor row(in1) xor row(in2) (the code is linear), and
            // sum(nxor*absrx) is read from per-byte tables of the reliabilities. Same decisions as the Fortran loop.
            uint64_t hw[3] = {0, 0, 0}; // hard decisions, packed
            for (int j = 0; j < N; j++) hw[j >> 6] |= (uint64_t)hdecp[j] << (j & 63);
            static float W[22][256];
            for (int b = 0; b < 22; b++) {
                W[b][0] = 0.f;
                for (int v = 1; v < 256; v++) {
                    int lo = 0;
                    while (!((v >> lo) & 1)) lo++;
                    int pos = 8 * b + lo;
                    W[b][v] = W[b][v & (v - 1)] + (pos < N ? absp[pos] : 0.f);
                }
            }
            bool anyap = false;
            for (int i = 0; i < k; i++) if (apm[i]) anyap = true;
            for (int i = 0; i < k - nord; i++) misub[i] = 0;
            for (int i = k - nord; i < k; i++) misub[i] = 1;
            int iflag = k - nord + 1;
            while (iflag >= 0) {
                for (int i = 0; i < k; i++) me[i] = m0[i] ^ misub[i];
                uint64_t cp[3] = {0, 0, 0}; // codeword of me, packed
                for (int i = 0; i < k; i++)
                    if (me[i] == 1) { cp[0] ^= gp[3 * i]; cp[1] ^= gp[3 * i + 1]; cp[2] ^= gp[3 * i + 2]; }
                int basepat = 0; // e2sub(1:ntau) = codeword bits k.. xor hard decisions, as a pattern number
                for (int i = 0; i < ntau; i++) {
                    int j = k + i;
                    basepat = (basepat << 1) | (int)(((cp[j >> 6] ^ hw[j >> 6]) >> (j & 63)) & 1);
                }
                bool apmisub = false;
                if (anyap)
                    for (int i = 0; i < k; i++) if (misub[i] && apm[i]) apmisub = true;
                for (int i2 = 0; i2 <= ntau; i2++) {
                    int ipat = i2 > 0 ? (basepat ^ (1 << (ntau - i2))) : basepat; // r2pat = e2sub xor ui, ui(i2)=1
                    for (;;) { // label 778
                        int in1, in2;
                        boxes.fetchit(&reset, ipat, &in1, &in2);
                        if (in1 > 0 && in2 > 0) {
                            int sm = nord + (misub[in1 - 1] ? 0 : 1) + (misub[in2 - 1] ? 0 : 1); // sum(mi)
                            bool skip = anyap && (apmisub || apm[in1 - 1] || apm[in2 - 1]);
                            if (sm < nord + npre1 + npre2 || skip) break; // Fortran: cycle (next i2)
                            uint64_t ch[3];
                            for (int w = 0; w < 3; w++) ch[w] = cp[w] ^ gp[3 * (in1 - 1) + w] ^ gp[3 * (in2 - 1) + w];
                            uint64_t x0 = ch[0] ^ hw[0], x1 = ch[1] ^ hw[1], x2 = ch[2] ^ hw[2];
                            float dd = 0;
                            for (int b = 0; b < 8; b++) dd += W[b][(x0 >> (8 * b)) & 255];
                            for (int b = 0; b < 8; b++) dd += W[8 + b][(x1 >> (8 * b)) & 255];
                            for (int b = 0; b < 6; b++) dd += W[16 + b][(x2 >> (8 * b)) & 255];
                            if (dd < dmin) {
                                dmin = dd;
                                for (int j = 0; j < N; j++) cwl[j] = (uint8_t)((ch[j >> 6] >> (j & 63)) & 1);
                                nhard = (int)(__popcnt64(x0) + __popcnt64(x1) + __popcnt64(x2 & ((((uint64_t)1) << 46) - 1)));
                            }
                            continue; // goto 778
                        }
                        break;
                    }
                }
                nextpat91(misub, k, nord, &iflag);
            }
        }
    }
done:
    // back to [message][parity] order
    uint8_t cwo[N];
    for (int i = 0; i < N; i++) cwo[indices[i]] = cwl[i];
    memcpy(cw, cwo, N);
    memcpy(message91, cwo, 91);
    uint8_t m96[96] = {0};
    memcpy(m96, cwo, 77);
    for (int b = 0; b < 14; b++) m96[82 + b] = cwo[77 + b];
    int nbadcrc;
    get_crc14(m96, 96, &nbadcrc);
    if (nbadcrc != 0) nhard = -nhard;
    *nhardmin = nhard;
    *dmin_out = dmin;
}

} // namespace

// get_crc14: remainder of mc(1:len) divided by the polynomial 0x6757 (14 bits); 0 for a consistent message + CRC
void get_crc14(const uint8_t *mc, int len, int *ncrc) {
    static const int p[15] = {1, 1, 0, 0, 1, 1, 1, 0, 1, 0, 1, 0, 1, 1, 1};
    int r[15];
    for (int i = 0; i < 15; i++) r[i] = mc[i];
    for (int i = 0; i <= len - 15; i++) {
        r[14] = mc[i + 14];
        int r1 = r[0];
        for (int j = 0; j < 15; j++) r[j] = (r[j] + r1 * p[j]) & 1;
        int t = r[0]; // cshift(r,1)
        for (int j = 0; j < 14; j++) r[j] = r[j + 1];
        r[14] = t;
    }
    int v = 0;
    for (int j = 0; j < 14; j++) v = (v << 1) | r[j];
    *ncrc = v;
}

void encode174_91_nocrc(const uint8_t *message91, uint8_t *codeword) {
    const GenMatrix &g = genmat();
    memcpy(codeword, message91, K);
    for (int i = 0; i < M; i++) {
        int nsum = 0;
        for (int j = 0; j < K; j++) nsum += message91[j] * g.gen[i][j];
        codeword[K + i] = nsum & 1;
    }
}

void encode174_91(const uint8_t *message77, uint8_t *codeword) {
    uint8_t m96[96] = {0};
    memcpy(m96, message77, 77); // 77 bits, then zeros: CRC of the message padded as the Fortran bytes
    int ncrc14;
    get_crc14(m96, 96, &ncrc14);
    uint8_t message[K];
    memcpy(message, message77, 77);
    for (int b = 0; b < 14; b++) message[77 + b] = (ncrc14 >> (13 - b)) & 1;
    encode174_91_nocrc(message, codeword);
}

int decode174_91(const float *llr, int Keff, int maxosd, int norder, const uint8_t *apmask, uint8_t *message91, uint8_t *cw,
                 int *ntype, int *nharderror, float *dmin) {
    const int maxiterations = 30;
    int nosd = 0;
    float zsave[3][N];
    if (maxosd > 3) maxosd = 3;
    if (maxosd == 0) {
        nosd = 1;
        for (int i = 0; i < N; i++) zsave[0][i] = llr[i];
    } else if (maxosd > 0) {
        nosd = maxosd;
    } else {
        nosd = 0;
    }
    const int ncw = 3;
    float toc[7][M], tov[3][N], tanhtoc[7][M], zn[N], zsum[N];
    memset(toc, 0, sizeof toc);
    memset(tov, 0, sizeof tov);
    memset(tanhtoc, 0, sizeof tanhtoc);
    for (int j = 0; j < M; j++)
        for (int i = 0; i < kNrw[j]; i++) toc[i][j] = llr[kNm[j][i] - 1];
    int ncnt = 0, nclast = 0;
    for (int i = 0; i < N; i++) zsum[i] = 0;
    for (int iter = 0; iter <= maxiterations; iter++) {
        for (int i = 0; i < N; i++) {
            if (apmask[i] != 1) {
                float s = 0;
                for (int k = 0; k < ncw; k++) s += tov[k][i];
                zn[i] = llr[i] + s;
            } else {
                zn[i] = llr[i];
            }
        }
        for (int i = 0; i < N; i++) zsum[i] += zn[i];
        if (iter > 0 && iter <= maxosd) {
            for (int i = 0; i < N; i++) zsave[iter - 1][i] = zsum[i];
        }
        for (int i = 0; i < N; i++) cw[i] = zn[i] > 0 ? 1 : 0;
        int ncheck = 0;
        for (int i = 0; i < M; i++) {
            int synd = 0;
            for (int k = 0; k < kNrw[i]; k++) synd += cw[kNm[i][k] - 1];
            if (synd % 2 != 0) ncheck++;
        }
        if (ncheck == 0) {
            uint8_t m96[96] = {0};
            for (int i = 0; i < 77; i++) m96[i] = cw[i];
            for (int i = 0; i < 14; i++) m96[82 + i] = cw[77 + i];
            int nbadcrc;
            get_crc14(m96, 96, &nbadcrc);
            int nh = 0;
            for (int i = 0; i < N; i++)
                if ((2 * cw[i] - 1) * llr[i] < 0.0f) nh++;
            *nharderror = nh;
            if (nbadcrc == 0) {
                memcpy(message91, cw, 91);
                float d = 0;
                for (int i = 0; i < N; i++) {
                    int hdec = llr[i] >= 0 ? 1 : 0;
                    d += (hdec ^ cw[i]) * std::fabs(llr[i]);
                }
                *dmin = d;
                *ntype = 1;
                return 0;
            }
        }
        if (iter > 0) { // early stopping criterion
            int nd = ncheck - nclast;
            if (nd < 0) ncnt = 0;
            else ncnt++;
            if (ncnt >= 5 && iter >= 10 && ncheck > 15) {
                *nharderror = -1;
                break;
            }
        }
        nclast = ncheck;
        // messages from bits to check nodes
        for (int j = 0; j < M; j++) {
            for (int i = 0; i < kNrw[j]; i++) {
                int ibj = kNm[j][i] - 1;
                toc[i][j] = zn[ibj];
                for (int kk = 0; kk < ncw; kk++)
                    if (kMn[ibj][kk] == j + 1) toc[i][j] -= tov[kk][ibj];
            }
        }
        // messages from check nodes to variable nodes
        for (int i = 0; i < M; i++)
            for (int k = 0; k < 7; k++) tanhtoc[k][i] = std::tanh(-toc[k][i] / 2);
        for (int j = 0; j < N; j++) {
            for (int i = 0; i < ncw; i++) {
                int ichk = kMn[j][i] - 1;
                float Tmn = 1.0f;
                for (int k = 0; k < kNrw[ichk]; k++)
                    if (kNm[ichk][k] != j + 1) Tmn *= tanhtoc[k][ichk];
                float y = platanh(-Tmn);
                tov[i][j] = 2 * y;
            }
        }
    }
    for (int i = 0; i < nosd; i++) {
        float zn2[N];
        for (int b = 0; b < N; b++) zn2[b] = zsave[i][b];
        float dminosd;
        osd174_91(zn2, Keff, apmask, norder, message91, cw, nharderror, &dminosd);
        if (*nharderror > 0) {
            float d = 0;
            for (int b = 0; b < N; b++) {
                int hdec = llr[b] >= 0 ? 1 : 0;
                d += (hdec ^ cw[b]) * std::fabs(llr[b]);
            }
            *dmin = d;
            *ntype = 2;
            return 0;
        }
    }
    *ntype = 0;
    *nharderror = -1;
    *dmin = 0;
    return 0;
}

} // namespace wm
