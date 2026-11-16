// ----------------------------------------------------------------------------
// rsid_detector.cpp -- FoxSDR fldigimodes helper: the RSID (Reed-Solomon ID) detector.
//
// This is the receive side of fldigi's class cRsId (src/rsid/rsid.cxx and src/include/rsid.h), ported without the
// user interface, the mode switching and libsamplerate. What is taken from rsid.cxx, function by function and
// unchanged in its arithmetic: the tables Squares[] and indices[], Encode(), CalculateBuckets(), search(),
// search_amp(), HammingDistance(), receive() and the part of apply() that works out the frequency. The tables of
// codes and names are fldigi's src/rsid/rsid_defs.cxx, included here unchanged (the same way rsid.cxx includes it).
// libsamplerate is replaced by a small windowed-sinc resampler written for this helper (class Resampler).
//
//   rsid.cxx, rsid.h, rsid_defs.cxx: Copyright (C) 2008-2012 Dave Freese, W1HKJ; Copyright (C) 2008, 2009 Stelios
//   Bounanos, M0GLD; (C) 2014 Dave Freese, W1HKJ (rsid_defs.cxx). The tone layout is that of Pawel Jalocha's RSID.
// Distributed under the GNU General Public License, version 3 or (at your option) any later version.
// ----------------------------------------------------------------------------
#include "rsid_detector.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <cstring>
#include <vector>

#include "fm_shim.h"
#include "gfft.h"
#include "misc.h"

#define RSID_SAMPLE_RATE 11025.0

#define RSID_FFT_SAMPLES 512
#define RSID_FFT_SIZE 1024
#define RSID_ARRAY_SIZE (RSID_FFT_SIZE * 2)
#define RSID_BUFFER_SIZE (RSID_ARRAY_SIZE * 2)

#define RSID_NSYMBOLS 15
#define RSID_NTIMES (RSID_NSYMBOLS * 2)
#define RSID_PRECISION 2.7  // detected frequency precision in Hz

// each rsid symbol has a duration equal to 1024 samples at 11025 Hz smpl rate
#define RSID_SYMLEN (1024.0 / RSID_SAMPLE_RATE)

typedef double rs_fft_type;
typedef std::complex<rs_fft_type> rs_cpx_type;
typedef std::complex<double> cmplx;

struct RSIDs {
    unsigned short rs;
    trx_mode mode;
    const char *name;
};

#define RSWINDOW 1

// ---- the class, reduced from rsid.h --------------------------------------------------------------------------------
class cRsId {
public:
    cRsId();
    ~cRsId();
    void reset();
    static void Encode(int code, unsigned char *rsid);
    void receive(const float *buf, size_t len);
    void set_search(bool wide, double centre, bool reverse)
    {
        wide_ = wide;
        centre_ = centre;
        reverse_ = reverse;
    }
    void set_sink(std::function<void(const fm::RsidHit &)> cb) { sink_ = cb; }
    void set_samplerate(int r) { samplerate_ = r; }

    static const RSIDs rsid_ids_1[];
    static const int rsid_ids_size1;
    static const RSIDs rsid_ids_2[];
    static const int rsid_ids_size2;

private:
    unsigned char *pCodes1;
    unsigned char *pCodes2;

    bool found1;
    bool found2;

    static const int Squares[];
    static const int indices[];

    double rsid_secondary_time_out;

    int hamming_resolution;

    int nBinLow;
    int nBinHigh;

    float aInputSamples[RSID_ARRAY_SIZE * 2];
    rs_fft_type fftwindow[RSID_ARRAY_SIZE];
    rs_cpx_type aFFTcmplx[RSID_ARRAY_SIZE];
    rs_fft_type aFFTAmpl[RSID_FFT_SIZE];

    g_fft<rs_fft_type> *rsfft;

    int fft_buckets[RSID_NTIMES][RSID_FFT_SIZE];

    int inptr;

    bool wide_ = true;
    double centre_ = 1500;
    bool reverse_ = false;
    int samplerate_ = 8000;
    std::function<void(const fm::RsidHit &)> sink_;


    void search(void);
    void CalculateBuckets(const rs_fft_type *pSpectrum, int iBegin, int iEnd);
    inline int HammingDistance(int iBucket, unsigned char *p2);
    bool search_amp(int &bin_out, int &symbol_out, unsigned char *pcode_table);
    void apply(int iBin, int iSymbol, int extended);

    // the part of receive() that libsamplerate did: the resampled samples arrive here
    void push_resampled(const float *s, size_t n);
    friend class RsidFeeder;
};

#include "rsid_defs.cxx"

const int cRsId::Squares[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
    0, 2, 4, 6, 8,10,12,14, 9,11,13,15, 1, 3, 5, 7,
    0, 3, 6, 5,12,15,10, 9, 1, 2, 7, 4,13,14,11, 8,
    0, 4, 8,12, 9,13, 1, 5,11,15, 3, 7, 2, 6,10,14,
    0, 5,10,15,13, 8, 7, 2, 3, 6, 9,12,14,11, 4, 1,
    0, 6,12,10, 1, 7,13,11, 2, 4,14, 8, 3, 5,15, 9,
    0, 7,14, 9, 5, 2,11,12,10,13, 4, 3,15, 8, 1, 6,
    0, 8, 9, 1,11, 3, 2,10,15, 7, 6,14, 4,12,13, 5,
    0, 9,11, 2,15, 6, 4,13, 7,14,12, 5, 8, 1, 3,10,
    0,10,13, 7, 3, 9,14, 4, 6,12,11, 1, 5,15, 8, 2,
    0,11,15, 4, 7,12, 8, 3,14, 5, 1,10, 9, 2, 6,13,
    0,12, 1,13, 2,14, 3,15, 4, 8, 5, 9, 6,10, 7,11,
    0,13, 3,14, 6,11, 5, 8,12, 1,15, 2,10, 7, 9, 4,
    0,14, 5,11,10, 4,15, 1,13, 3, 8, 6, 7, 9, 2,12,
    0,15, 7, 8,14, 1, 9, 6, 5,10, 2,13,11, 4,12, 3
};

const int cRsId::indices[] = {
    2, 4, 8, 9, 11, 15, 7, 14, 5, 10, 13, 3
};

cRsId::cRsId()
{
    reset();

    rsfft = new g_fft<rs_fft_type>(RSID_ARRAY_SIZE);

    memset(fftwindow, 0, sizeof(fftwindow));

    if (RSWINDOW) {
        for (int i = 0; i < RSID_ARRAY_SIZE; i++)
            fftwindow[i] = hamming(1.0 * i / RSID_ARRAY_SIZE);
    }

    pCodes1 = new unsigned char[rsid_ids_size1 * RSID_NSYMBOLS];
    memset(pCodes1, 0, rsid_ids_size1 * RSID_NSYMBOLS);

    pCodes2 = new unsigned char[rsid_ids_size2 * RSID_NSYMBOLS];
    memset(pCodes2, 0, rsid_ids_size2 * RSID_NSYMBOLS);

    // Initialization  of assigned mode/submode IDs.
    unsigned char *c;
    for (int i = 0; i < rsid_ids_size1; i++) {
        c = pCodes1 + i * RSID_NSYMBOLS;
        Encode(rsid_ids_1[i].rs, c);
    }

    for (int i = 0; i < rsid_ids_size2; i++) {
        c = pCodes2 + i * RSID_NSYMBOLS;
        Encode(rsid_ids_2[i].rs, c);
    }

    nBinLow = 3;
    nBinHigh = RSID_FFT_SIZE - 32;

    reset();
}

cRsId::~cRsId()
{
    delete[] pCodes1;
    delete[] pCodes2;
    delete rsfft;
}

void cRsId::reset()
{
    found1 = found2 = false;
    rsid_secondary_time_out = 0;

    memset(aInputSamples, 0, (RSID_ARRAY_SIZE * 2) * sizeof(float));
    memset(aFFTAmpl, 0, RSID_FFT_SIZE * sizeof(rs_fft_type));
    memset(fft_buckets, 0, RSID_NTIMES * RSID_FFT_SIZE * sizeof(int));

    for (int n = 0; n < RSID_ARRAY_SIZE; n++)
        aFFTcmplx[n] = cmplx(0, 0);

    inptr = RSID_FFT_SIZE;
    hamming_resolution = 1;  // fldigi: progdefaults.RsID_label_type, default 1 ("medium")
}

void cRsId::Encode(int code, unsigned char *rsid)
{
    rsid[0] = code >> 8;
    rsid[1] = (code >> 4) & 0x0f;
    rsid[2] = code & 0x0f;
    for (int i = 3; i < RSID_NSYMBOLS; i++)
        rsid[i] = 0;
    for (int i = 0; i < 12; i++) {
        for (int j = RSID_NSYMBOLS - 1; j > 0; j--)
            rsid[j] = rsid[j - 1] ^ Squares[(rsid[j] << 4) + indices[i]];
        rsid[0] = Squares[(rsid[0] << 4) + indices[i]];
    }
}

void cRsId::CalculateBuckets(const rs_fft_type *pSpectrum, int iBegin, int iEnd)
{
    rs_fft_type Amp = 0.0, AmpMax = 0.0;
    int iBucketMax = iBegin - 2;
    int j;

    for (int i = iBegin; i < iEnd; i += 2) {
        if (iBucketMax == i - 2) {
            AmpMax = pSpectrum[i];
            iBucketMax = i;
            for (j = i + 2; j < i + RSID_NTIMES + 2; j += 2) {
                Amp = pSpectrum[j];
                if (Amp > AmpMax) {
                    AmpMax = Amp;
                    iBucketMax = j;
                }
            }
        } else {
            j = i + RSID_NTIMES;
            Amp = pSpectrum[j];
            if (Amp > AmpMax) {
                AmpMax = Amp;
                iBucketMax = j;
            }
        }
        fft_buckets[RSID_NTIMES - 1][i] = (iBucketMax - i) >> 1;
    }
}

// fldigi: cRsId::receive(). The samples have been brought to 11025 Hz by the caller.
void cRsId::receive(const float *buf, size_t len)
{
    if (len == 0)
        return;
    if (rsid_secondary_time_out > 0) {
        // (fldigi counts the input samples at the modem rate; the caller passes 11025 Hz samples)
        rsid_secondary_time_out -= 1.0 * len / RSID_SAMPLE_RATE;
        if (rsid_secondary_time_out <= 0)
            reset();
    }
    push_resampled(buf, len);
}

void cRsId::push_resampled(const float *s, size_t n)
{
    while (n > 0) {
        size_t room = (size_t)(RSID_ARRAY_SIZE * 2 - inptr);
        size_t take = n < room ? n : room;
        memcpy(&aInputSamples[inptr], s, take * sizeof(float));
        inptr += (int)take;
        s += take;
        n -= take;
        while (inptr >= RSID_ARRAY_SIZE) {
            search();
            memmove(&aInputSamples[0], &aInputSamples[RSID_FFT_SAMPLES], (RSID_BUFFER_SIZE - RSID_FFT_SAMPLES) * sizeof(float));
            inptr -= RSID_FFT_SAMPLES;
        }
    }
}

void cRsId::search(void)
{
    if (wide_) {
        nBinLow = 3;
        nBinHigh = RSID_FFT_SIZE - 32;
    } else {
        float centerfreq = (float)centre_;
        float bpf = 1.0 * RSID_ARRAY_SIZE / RSID_SAMPLE_RATE;
        nBinLow = (int)((centerfreq - 100.0 * 2) * bpf);
        nBinHigh = (int)((centerfreq + 100.0 * 2) * bpf);
    }
    if (nBinLow < 3)
        nBinLow = 3;
    if (nBinHigh > RSID_FFT_SIZE - 32)
        nBinHigh = RSID_FFT_SIZE - 32;

    bool bReverse = reverse_;
    if (bReverse) {
        nBinLow = RSID_FFT_SIZE - nBinHigh;
        nBinHigh = RSID_FFT_SIZE - nBinLow;
    }

    if (RSWINDOW) {
        for (int i = 0; i < RSID_ARRAY_SIZE; i++)
            aFFTcmplx[i] = cmplx(aInputSamples[i] * fftwindow[i], 0);
    } else {
        for (int i = 0; i < RSID_ARRAY_SIZE; i++)
            aFFTcmplx[i] = cmplx(aInputSamples[i], 0);
    }

    rsfft->ComplexFFT(aFFTcmplx);

    memset(aFFTAmpl, 0, sizeof(aFFTAmpl));

    static const double pscale = 4.0 / (RSID_FFT_SIZE * RSID_FFT_SIZE);

    if (bReverse) {
        for (int i = 0; i < RSID_FFT_SIZE; i++)
            aFFTAmpl[RSID_FFT_SIZE - 1 - i] = norm(aFFTcmplx[i]) * pscale;
    } else {
        for (int i = 0; i < RSID_FFT_SIZE; i++)
            aFFTAmpl[i] = norm(aFFTcmplx[i]) * pscale;
    }

    int bucket_low = 3;
    int bucket_high = RSID_FFT_SIZE - 32;
    if (bReverse) {
        bucket_low = RSID_FFT_SIZE - bucket_high;
        bucket_high = RSID_FFT_SIZE - bucket_low;
    }

    memmove(fft_buckets, &(fft_buckets[1][0]), (RSID_NTIMES - 1) * RSID_FFT_SIZE * sizeof(int));
    memset(&(fft_buckets[RSID_NTIMES - 1][0]), 0, RSID_FFT_SIZE * sizeof(int));

    CalculateBuckets(aFFTAmpl, bucket_low, bucket_high - RSID_NTIMES);
    CalculateBuckets(aFFTAmpl, bucket_low + 1, bucket_high - RSID_NTIMES);

    int symbol_out_1 = -1;
    int bin_out_1 = -1;
    int symbol_out_2 = -1;
    int bin_out_2 = -1;

    if (rsid_secondary_time_out <= 0) {
        found1 = search_amp(bin_out_1, symbol_out_1, pCodes1);
        if (found1) {
            if (symbol_out_1 != RSID_ESCAPE) {
                if (bReverse)
                    bin_out_1 = 1024 - bin_out_1 - 31;
                apply(bin_out_1, symbol_out_1, 0);
                reset();
                return;
            } else {
                // 10 rsid_gap + 15 symbols + 2 for timing errors
                rsid_secondary_time_out = 27 * RSID_SYMLEN;
                return;
            }
        } else
            return;
    }

    found2 = search_amp(bin_out_2, symbol_out_2, pCodes2);
    if (found2) {
        if (symbol_out_2 != RSID_NONE2) {
            if (bReverse)
                bin_out_2 = 1024 - bin_out_2 - 31;
            apply(bin_out_2, symbol_out_2, 1);
        }
        reset();
    }
}

// fldigi: cRsId::apply(). The frequency is worked out as fldigi does; instead of switching the modem the hit is reported.
void cRsId::apply(int iBin, int iSymbol, int extended)
{
    double rsidfreq = 0;
    int n;
    int tblsize;
    const RSIDs *p_rsid;

    if (extended) {
        tblsize = rsid_ids_size2;
        p_rsid = rsid_ids_2;
    } else {
        tblsize = rsid_ids_size1;
        p_rsid = rsid_ids_1;
    }

    rsidfreq = (iBin + RSID_NSYMBOLS - 0.5) * RSID_SAMPLE_RATE / 2048.0;

    for (n = 0; n < tblsize; n++)
        if (p_rsid[n].rs == iSymbol)
            break;

    fm::RsidHit h;
    h.code = iSymbol;
    h.extended = extended != 0;
    h.freq_hz = rsidfreq;
    if (n < tblsize) {
        h.name = p_rsid[n].name;
        h.known_to_fldigi = (p_rsid[n].mode != NUM_MODES);
        h.eot = (p_rsid[n].mode == MODE_EOT);
        if (h.known_to_fldigi && !h.eot) {
            static const struct {
                int id;
                const char *sname;
                const char *name;
            } names[] = {
#include "fm_mode_names.inc"
            };
            for (const auto &e : names)
                if (e.id == p_rsid[n].mode)
                    h.fldigi_mode = e.sname;
        }
    } else {
        h.name = "unknown";
    }
    if (sink_)
        sink_(h);
}

inline int cRsId::HammingDistance(int iBucket, unsigned char *p2)
{
    int dist = 0;
    for (int i = 0, j = 1; i < RSID_NSYMBOLS; i++, j += 2) {
        if (fft_buckets[j][iBucket] != p2[i]) {
            ++dist;
            if (dist > hamming_resolution)
                break;
        }
    }
    return dist;
}

bool cRsId::search_amp(int &bin_out, int &symbol_out, unsigned char *pcode)
{
    int i, j;
    int iDistanceMin = 1000;  // infinity
    int iDistance = 1000;
    int iBin = -1;
    int iSymbol = -1;

    int tblsize;
    const RSIDs *prsid;

    if (pcode == pCodes1) {
        tblsize = rsid_ids_size1;
        prsid = rsid_ids_1;
    } else {
        tblsize = rsid_ids_size2;
        prsid = rsid_ids_2;
    }

    unsigned char *pc = 0;
    for (i = 0; i < tblsize; i++) {
        pc = pcode + i * RSID_NSYMBOLS;
        for (j = nBinLow; j < nBinHigh - RSID_NTIMES; j++) {
            iDistance = HammingDistance(j, pc);
            if (iDistance < iDistanceMin) {
                iDistanceMin = iDistance;
                iSymbol = prsid[i].rs;
                iBin = j;
                if (iDistanceMin == 0)
                    break;
            }
        }
    }

    if (iDistanceMin <= hamming_resolution) {
        symbol_out = iSymbol;
        bin_out = iBin;
        return true;
    }

    return false;
}

// ---- the resampler (written for this helper; replaces libsamplerate) -------------------------------------------------
namespace fm {

namespace {

// Streaming windowed-sinc rate converter, input rate -> RSID_SAMPLE_RATE. Output sample k lies at input position
// k * in/out. Half width 16 input samples at the cut-off (scaled when the input rate is higher than the output rate).
class Resampler {
public:
    explicit Resampler(int in_rate) : in_(in_rate)
    {
        step_ = (double)in_rate / RSID_SAMPLE_RATE;  // input samples per output sample
        fc_ = std::min(1.0, RSID_SAMPLE_RATE / (double)in_rate) * 0.94;  // cut-off, as a fraction of the input Nyquist
        half_ = (int)std::ceil(16.0 / fc_);
    }

    // append samples; produce as many output samples as the input allows
    template <class F> void push(const double *x, int n, F out)
    {
        for (int i = 0; i < n; i++)
            buf_.push_back((float)x[i]);
        // output k needs input up to floor(pos)+half_; positions are measured from the start of the stream
        for (;;) {
            double pos = (double)k_ * step_;
            long long ipos = (long long)std::floor(pos);
            if (ipos + half_ >= base_ + (long long)buf_.size())
                break;
            double acc = 0;
            for (long long j = ipos - half_ + 1; j <= ipos + half_; j++) {
                long long idx = j - base_;
                double v = (idx >= 0 && idx < (long long)buf_.size()) ? buf_[(size_t)idx] : 0.0;
                double d = pos - (double)j;
                double w = 0.5 + 0.5 * std::cos(M_PI * d / (double)half_);  // Hann window over the half width
                double s = (std::fabs(d * fc_) < 1e-12) ? 1.0 : std::sin(M_PI * d * fc_) / (M_PI * d * fc_);
                acc += v * s * w;
            }
            out((float)(acc * fc_));
            k_++;
        }
        // drop input that no later output needs
        long long keep_from = (long long)std::floor((double)k_ * step_) - half_;
        if (keep_from > base_) {
            size_t drop = (size_t)(keep_from - base_);
            if (drop > buf_.size())
                drop = buf_.size();
            buf_.erase(buf_.begin(), buf_.begin() + (long)drop);
            base_ += (long long)drop;
        }
    }

private:
    int in_;
    double step_, fc_;
    int half_;
    long long k_ = 0, base_ = 0;
    std::vector<float> buf_;
};

}  // namespace

struct RsidDetector::Impl {
    cRsId core;
    Resampler rs;
    std::vector<float> out;
    int rate;
    explicit Impl(int r) : rs(r), rate(r) {}
};

RsidDetector::RsidDetector(int rate) : impl_(new Impl(rate))
{
    impl_->core.set_samplerate(rate);
    impl_->core.set_search(true, 1500, false);
}
RsidDetector::~RsidDetector() {}

void RsidDetector::set_callback(std::function<void(const RsidHit &)> cb) { impl_->core.set_sink(cb); }

void RsidDetector::set_search(bool wide, double centre, bool reverse) { impl_->core.set_search(wide, centre, reverse); }

void RsidDetector::process(const double *buf, int n)
{
    if (n <= 0)
        return;
    std::vector<float> &o = impl_->out;
    o.clear();
    impl_->rs.push(buf, n, [&](float v) { o.push_back(v); });
    if (!o.empty())
        impl_->core.receive(o.data(), o.size());
}

void RsidDetector::encode(int code, unsigned char symbols[15]) { cRsId::Encode(code, symbols); }

int RsidDetector::code_for(const std::string &name, bool &extended)
{
    for (int i = 0; i < cRsId::rsid_ids_size1; i++)
        if (name == cRsId::rsid_ids_1[i].name) {
            extended = false;
            return cRsId::rsid_ids_1[i].rs;
        }
    for (int i = 0; i < cRsId::rsid_ids_size2; i++)
        if (name == cRsId::rsid_ids_2[i].name) {
            extended = true;
            return cRsId::rsid_ids_2[i].rs;
        }
    return -1;
}

void RsidDetector::entry(int table, int i, int &code, std::string &name, bool &known)
{
    const RSIDs &e = (table == 1) ? cRsId::rsid_ids_1[i] : cRsId::rsid_ids_2[i];
    code = e.rs;
    name = e.name;
    known = (e.mode != NUM_MODES);
}

int RsidDetector::escape_code() { return RSID_ESCAPE; }

int RsidDetector::table1_size() { return cRsId::rsid_ids_size1; }
int RsidDetector::table2_size() { return cRsId::rsid_ids_size2; }

}  // namespace fm
