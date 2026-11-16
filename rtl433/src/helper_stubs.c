/** @file
    FoxSDR GPL helper: stubs for the rtl_433 parts that are NOT built.

    This file is new (written for this helper); it is released under the same
    licence as the rest of the helper (GPL-2.0-or-later).

    Not built, and why:
    - LoRa chirp demodulation (src/lora.c, src/lora_fm.c, src/lorawan.c and the
      three LoRa / LoRaWAN device decoders): on the "do not build" list of the
      FoxSDR helper rules (patent scope). r_flow.c checks demod->lora_fm_demod
      and demod->lora_fft_demod for NULL before using them, so these stubs are
      only ever called with NULL.
    - Signal-file grabber (src/samp_grab.c) and AM/OOK analyser
      (src/am_analyze.c): they write captured I/Q to files, which is a replay
      aid. r_flow.c only uses them when demod->samp_grab / demod->am_analyze is
      set, and the helper never sets them.
    - Pulse analyser (src/pulse_analyzer.c): prints suggested "flex" decoder
      lines meant to be copied into a transmitter-side replay. Not built.
*/

#include <stdint.h>
#include <stddef.h>

#include "lora.h"
#include "lora_fm.h"
#include "am_analyze.h"
#include "samp_grab.h"
#include "pulse_analyzer.h"

/* LoRa: never created, so every handle is NULL. */

lora_fm_demod_t *lora_fm_demod_create(void) { return NULL; }
void lora_fm_demod_free(lora_fm_demod_t *demod) { (void)demod; }
void lora_fm_demod_reset(lora_fm_demod_t *demod) { (void)demod; }

int lora_fm_demod_process(lora_fm_demod_t *demod, int16_t const *fm,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    (void)demod; (void)fm; (void)sample_count; (void)sample_offset; (void)sample_rate;
    (void)spreading_factor; (void)bandwidth; (void)sync_word; (void)packets; (void)max_packets;
    return 0;
}

int lora_fft_demod_process_cu8(lora_fft_demod_t *demod, uint8_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    (void)demod; (void)iq_buf; (void)sample_count; (void)sample_offset; (void)sample_rate;
    (void)spreading_factor; (void)bandwidth; (void)sync_word; (void)packets; (void)max_packets;
    return 0;
}

int lora_fft_demod_process_cs16(lora_fft_demod_t *demod, int16_t const *iq_buf,
        size_t sample_count, uint64_t sample_offset, uint32_t sample_rate,
        unsigned spreading_factor, uint32_t bandwidth, unsigned sync_word,
        lora_packet_t *packets, unsigned max_packets)
{
    (void)demod; (void)iq_buf; (void)sample_count; (void)sample_offset; (void)sample_rate;
    (void)spreading_factor; (void)bandwidth; (void)sync_word; (void)packets; (void)max_packets;
    return 0;
}

lora_fft_demod_t *lora_fft_demod_create(void) { return NULL; }
void lora_fft_demod_free(lora_fft_demod_t *demod) { (void)demod; }
void lora_fft_demod_reset(lora_fft_demod_t *demod) { (void)demod; }

/* Analysers and grabbers: never created. */

am_analyze_t *am_analyze_create(void) { return NULL; }
void am_analyze_free(am_analyze_t *a) { (void)a; }
void am_analyze_skip(am_analyze_t *a, unsigned n_samples) { (void)a; (void)n_samples; }
void am_analyze(am_analyze_t *a, int16_t *am_buf, unsigned n_samples, int debug_output, samp_grab_t *g)
{
    (void)a; (void)am_buf; (void)n_samples; (void)debug_output; (void)g;
}
void am_analyze_classify(am_analyze_t *aa) { (void)aa; }

samp_grab_t *samp_grab_create(unsigned size, int sg_fileformat) { (void)size; (void)sg_fileformat; return NULL; }
void samp_grab_free(samp_grab_t *g) { (void)g; }
void samp_grab_push(samp_grab_t *g, unsigned char *iq_buf, uint32_t len) { (void)g; (void)iq_buf; (void)len; }
void samp_grab_reset(samp_grab_t *g) { (void)g; }
void samp_grab_write(samp_grab_t *g, unsigned grab_len, unsigned grab_end) { (void)g; (void)grab_len; (void)grab_end; }

int pulse_analyzer_check(pulse_data_t *data, int package_type, struct r_device *device)
{
    (void)data; (void)package_type; (void)device;
    return 0;
}
void pulse_analyzer(pulse_data_t *data, int package_type, struct r_device *device)
{
    (void)data; (void)package_type; (void)device;
}

/* Placeholders for the decoders that are left out of this helper. They keep the protocol numbers of
   every other decoder the same as in upstream rtl_433 (numbers are positions in the DEVICES list).
   disabled = 3 ("disabled and hidden"): never registered, see helper_main.c.

   - Itron ERT SCM, SCM+, IDM, NetIDM: patent US7830874 (shown active to 2029-02-09), not built.
   - LoRa (Cotech 36-9231, LoRaWAN frames, ELSYS ERS): need the LoRa chirp demodulator, not built.
   - Decoders that decrypt their payload (IKEA Sparsnas finds its key by brute force; Govee H5059 / H5112 /
     H5310 apply a fixed XOR key table; Apator Metra ERM30 / EITN30 undo a nibble-substitution "encryption";
     ELV ESA / Gira EHZ undo a salted XOR chain): the helper rule is "no decryption, no key recovery". */
#include "r_device.h"

#define REMOVED(id, why) \
    r_device const removed_##id = { \
        .name       = "(not built: " why ") " #id, \
        .modulation = OOK_PULSE_PPM, \
        .disabled   = 3, \
    }

REMOVED(ert_scm, "Itron ERT patent");
REMOVED(scmplus, "Itron ERT patent");
REMOVED(ert_idm, "Itron ERT patent");
REMOVED(ert_netidm, "Itron ERT patent");
REMOVED(cotech_36_9231, "LoRa chirp demodulator");
REMOVED(lorawan, "LoRa chirp demodulator");
REMOVED(elsys_ers, "LoRa chirp demodulator");
REMOVED(ikea_sparsnas, "decrypts, brute-forces the key");
REMOVED(govee_h5059, "decrypts, fixed XOR key");
REMOVED(govee_h5112, "decrypts, fixed XOR key");
REMOVED(govee_h5310, "decrypts, fixed XOR key");
REMOVED(apator_metra_erm30, "decrypts, substitution cipher");
REMOVED(apator_metra_eitn30, "decrypts, substitution cipher");
REMOVED(esa_energy, "decrypts, salted XOR");
