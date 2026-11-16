// ----------------------------------------------------------------------------
// fm_runtime.cpp -- FoxSDR fldigimodes helper: definitions of the names declared in shim/fm_shim.h and
// shim/modem.h. Written for this helper; GPL-3.0-only.
// ----------------------------------------------------------------------------
#include "fm_shim.h"
#include "modem.h"

fm_rx_char_sink_t fm_rx_char_sink = 0;
fm_progdefaults progdefaults;
fm_progstatus progStatus;
static fm_waterfall g_wf;
fm_waterfall *wf = &g_wf;
modem *active_modem = 0;

void MilliSleep(long) {}

double modem::frequency = 1500;
double modem::tx_frequency = 1500;
bool modem::freqlock = false;
unsigned long modem::tx_sample_count = 0;

modem::modem()
{
    mode = 0;
    stopflag = false;
    fragmentsize = 0;
    samplerate = 8000;
    reverse = false;
    sigsearch = 0;
    sig_start = sig_stop = false;
    bandwidth = 0.0;
    freqerr = 0.0;
    rx_corr = tx_corr = 0.0;
    squelch = 0.0;
    metric = 0.0;
    syncpos = 0.0;
    s2n_ncount = s2n_sum = s2n_sum2 = s2n_metric = 0.0;
    s2n_valid = false;
    cap = CAP_RX | CAP_TX;
    for (int i = 0; i < QUALITYDEPTH; i++)
        quality[i] = 0;
    frequency = tx_frequency = wf->Carrier();
}

void modem::init()
{
    stopflag = false;
}

void modem::set_freq(double freq)
{
    // fldigi clamps to its configured low/high cutoff; the helper's audio band is 0..rate/2
    frequency = CLAMP(freq, 100.0 + bandwidth / 2, samplerate / 2.0 - 100.0 - bandwidth / 2);
    if (freqlock == false)
        tx_frequency = frequency;
}

// ---- copied from fldigi's src/trx/modem.cxx (Copyright (C) 2006-2010 Dave Freese, W1HKJ and others; GPL-3.0-only) ----
// Averages a given [0-100] integer value over the last QUALITYDEPTH calls.
// By default, returns ONLY values of:  100, 90, 75, 50, 25, 10, & 0
int modem::get_quality(int mode)
{
    int average = 0;
    for (int i = 0; i < QUALITYDEPTH; i++)
        average += quality[i];
    average /= QUALITYDEPTH;
    if (mode != 0)
        return average;
    if (average > 90) average = 100;
    else if (average > 80) average = 90;
    else if (average > 60) average = 75;
    else if (average > 40) average = 50;
    else if (average > 20) average = 25;
    else if (average > 9) average = 10;
    else average = 0;
    return average;
}

int modem::update_quality(int value, int mode)
{
    static int index = 0;
    if (value > 100)
        value = 100;
    else if (value < 0)
        value = 0;
    quality[index++] = value;
    if (QUALITYDEPTH == index)
        index = 0;
    return get_quality(mode);
}
