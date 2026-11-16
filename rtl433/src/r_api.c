/** @file
    Generic RF data receiver and decoder for ISM band devices using RTL-SDR and SoapySDR.

    Copyright (C) 2019 Christian W. Zuckschwerdt <zany@triq.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/


/*
 * FoxSDR GPL helper: this file is a TRIMMED copy of rtl_433's src/r_api.c.
 * Removed: mongoose / HTTP / MQTT / InfluxDB / syslog / UDP / rtl_tcp outputs,
 * the SDR device control, signal-file dumpers, CSV field discovery, data tags
 * and the LoRa chirp demodulator hooks (stubbed). Every removal is listed in
 * UPSTREAM.md. The decoding path is unchanged.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "r_api.h"
#include "r_util.h"
#include "rtl_433.h"
#include "r_private.h"
#include "rtl_433_devices.h"
#include "r_device.h"
#include "pulse_slicer.h"
#include "pulse_detect_fsk.h"
#include "bitbuffer.h"
#include "decoder_util.h"
#include "data.h"
#include "list.h"
#include "raw_output.h"
#include "compat_time.h"
#include "logger.h"
#include "fatal.h"

/* general */

void r_init_cfg(r_cfg_t *cfg)
{
    cfg->out_block_size  = DEFAULT_BUF_LENGTH;
    cfg->samp_rate       = DEFAULT_SAMPLE_RATE;
    cfg->conversion_mode = CONVERT_NATIVE;
    cfg->fsk_pulse_detect_mode = FSK_PULSE_DETECT_AUTO;
    // Default log level is to show all LOG_FATAL, LOG_ERROR, LOG_WARNING
    // abnormal messages and LOG_CRITICAL information.
    cfg->verbosity = LOG_WARNING;

    list_ensure_size(&cfg->in_files, 100);
    list_ensure_size(&cfg->output_handler, 16);

    // collect devices list, this should be a module
    r_device r_devices[] = {
#define DECL(name) name,
            DEVICES
#undef DECL
    };

    cfg->num_r_devices = sizeof(r_devices) / sizeof(*r_devices);
    for (unsigned i = 0; i < cfg->num_r_devices; i++) {
        r_devices[i].protocol_num = i + 1;
    }
    cfg->devices = malloc(sizeof(r_devices));
    if (!cfg->devices)
        FATAL_CALLOC("r_init_cfg()");

    memcpy(cfg->devices, r_devices, sizeof(r_devices));

    cfg->demod = calloc(1, sizeof(*cfg->demod));
    if (!cfg->demod)
        FATAL_CALLOC("r_init_cfg()");

    cfg->demod->level_limit = 0.0f;
    cfg->demod->min_level = -12.1442f;
    cfg->demod->min_snr = 9.0f;
    // Pulse detect will only print LOG_NOTICE and lower.
    cfg->demod->detect_verbosity = LOG_WARNING;

    // note: this should be optional
    cfg->demod->pulse_detect = pulse_detect_create();
    for (unsigned i = 0; i < FSK_PULSE_DETECTOR_COUNT; ++i) {
        pulse_detect_fsk_init(&cfg->demod->pulse_detect_fsk[i]);
    }
    // FoxSDR: no LoRa demodulator is built (patent / scope), lora_fm_demod stays NULL.
    // initialize tables
    baseband_init();

    time(&cfg->demod->running_since);
    time(&cfg->demod->frames_since);
    get_time_now(&cfg->demod->now);

    list_ensure_size(&cfg->demod->r_devs, 100);
    list_ensure_size(&cfg->demod->dumper, 32);
}

r_cfg_t *r_create_cfg(void)
{
    r_cfg_t *cfg = calloc(1, sizeof(*cfg));
    if (!cfg)
        FATAL_CALLOC("r_create_cfg()");

    r_init_cfg(cfg);

    return cfg;
}

void r_free_cfg(r_cfg_t *cfg)
{
    free(cfg->gain_str);
    cfg->gain_str = NULL;

    list_free_elems(&cfg->demod->dumper, free);

    list_free_elems(&cfg->demod->r_devs, (list_elem_free_fn)free_protocol);

    pulse_detect_free(cfg->demod->pulse_detect);
    cfg->demod->pulse_detect = NULL;

    list_free_elems(&cfg->raw_handler, (list_elem_free_fn)raw_output_free);

    r_logger_set_log_handler(NULL, NULL);

    list_free_elems(&cfg->output_handler, (list_elem_free_fn)data_output_free);

    list_free_elems(&cfg->in_files, NULL);

    free(cfg->demod);
    cfg->demod = NULL;

    free(cfg->devices);
    cfg->devices = NULL;
}

/* device decoder protocols */

void register_protocol(r_cfg_t *cfg, r_device const *r_dev, char *arg)
{
    // use arg of 'v', 'vv', 'vvv' as device verbosity
    int dev_verbose = 0;
    if (arg && *arg == 'v') {
        for (; *arg == 'v'; ++arg) {
            dev_verbose++;
        }
        if (*arg) {
            arg++; // skip separator
        }
    }

    // use any other arg as device parameter
    r_device *p;
    if (r_dev->create_fn) {
        p = r_dev->create_fn(arg);
    }
    else {
        if (arg && *arg) {
            fprintf(stderr, "Protocol [%u] \"%s\" does not take arguments \"%s\"!\n", r_dev->protocol_num, r_dev->name, arg);
        }
        p  = malloc(sizeof(*p));
        if (!p)
            FATAL_CALLOC("register_protocol()");
        *p = *r_dev; // copy
    }

    p->verbose      = dev_verbose ? dev_verbose : (cfg->verbosity > 4 ? cfg->verbosity - 5 : 0);
    p->verbose_bits = cfg->verbose_bits;
    p->log_fn       = log_device_handler;

    p->output_fn  = data_acquired_handler;
    p->output_ctx = cfg;

    list_push(&cfg->demod->r_devs, p);

    if (cfg->verbosity >= LOG_INFO) {
        fprintf(stderr, "Registering protocol [%u] \"%s\"\n", r_dev->protocol_num, r_dev->name);
    }
}

void free_protocol(r_device *r_dev)
{
    free(r_dev->decode_ctx);
    free(r_dev);
}

void unregister_protocol(r_cfg_t *cfg, r_device *r_dev)
{
    for (size_t i = 0; i < cfg->demod->r_devs.len; ++i) { // list might contain NULLs
        r_device *p = cfg->demod->r_devs.elems[i];
        if (!strcmp(p->name, r_dev->name)) {
            list_remove(&cfg->demod->r_devs, i, (list_elem_free_fn)free_protocol);
            i--; // so we don't skip the next elem now shifted down
        }
    }
}

void register_all_protocols(r_cfg_t *cfg, unsigned disabled)
{
    for (int i = 0; i < cfg->num_r_devices; i++) {
        // register all device protocols that are not disabled
        if (cfg->devices[i].disabled <= disabled) {
            register_protocol(cfg, &cfg->devices[i], NULL);
        }
    }
}

/* output helper */

/* output helper */

char *time_pos_str(r_cfg_t *cfg, unsigned samples_ago, char *buf)
{
    if (cfg->report_time == REPORT_TIME_SAMPLES) {
        double s_per_sample = 1.0f / cfg->samp_rate;
        return sample_pos_str(cfg->demod->sample_file_pos - samples_ago * s_per_sample, buf);
    }
    else {
        struct timeval ago = cfg->demod->now;
        double us_per_sample = 1e6 / cfg->samp_rate;
        unsigned usecs_ago   = samples_ago * us_per_sample;
        while (ago.tv_usec < (int)usecs_ago) {
            ago.tv_sec -= 1;
            ago.tv_usec += 1000000;
        }
        ago.tv_usec -= usecs_ago;

        char const *format = NULL;
        if (cfg->report_time == REPORT_TIME_UNIX)
            format = "%s";
        else if (cfg->report_time == REPORT_TIME_ISO)
            format = "%Y-%m-%dT%H:%M:%S";

        if (cfg->report_time_hires)
            return usecs_time_str(buf, format, cfg->report_time_tz, &ago);
        else
            return format_time_str(buf, format, cfg->report_time_tz, ago.tv_sec);
    }
}

int run_ook_demods(list_t *r_devs, pulse_data_t *pulse_data)
{
    int p_events = 0;

    unsigned next_priority = 0; // next smallest on each loop through decoders
    // run all decoders of each priority, stop if an event is produced
    for (unsigned priority = 0; !p_events && priority < UINT_MAX; priority = next_priority) {
        next_priority = UINT_MAX;
        for (void **iter = r_devs->elems; iter && *iter; ++iter) {
            r_device *r_dev = *iter;

            // Find next smallest priority
            if (r_dev->priority > priority && r_dev->priority < next_priority)
                next_priority = r_dev->priority;
            // Run only current priority
            if (r_dev->priority != priority)
                continue;

            switch (r_dev->modulation) {
            case OOK_PULSE_PCM:
            // case OOK_PULSE_RZ:
                p_events += pulse_slicer_pcm(pulse_data, r_dev);
                break;
            case OOK_PULSE_PPM:
                p_events += pulse_slicer_ppm(pulse_data, r_dev);
                break;
            case OOK_PULSE_PWM:
                p_events += pulse_slicer_pwm(pulse_data, r_dev);
                break;
            case OOK_PULSE_MANCHESTER_ZEROBIT:
                p_events += pulse_slicer_manchester_zerobit(pulse_data, r_dev);
                break;
            case OOK_PULSE_PIWM_RAW:
                p_events += pulse_slicer_piwm_raw(pulse_data, r_dev);
                break;
            case OOK_PULSE_PIWM_DC:
                p_events += pulse_slicer_piwm_dc(pulse_data, r_dev);
                break;
            case OOK_PULSE_DMC:
                p_events += pulse_slicer_dmc(pulse_data, r_dev);
                break;
            case OOK_PULSE_PWM_OSV1:
                p_events += pulse_slicer_osv1(pulse_data, r_dev);
                break;
            case OOK_PULSE_NRZS:
                p_events += pulse_slicer_nrzs(pulse_data, r_dev);
                break;
            case OOK_PULSE_RZI:
                p_events += pulse_slicer_rzi(pulse_data, r_dev);
                break;
            // FSK decoders
            case FSK_PULSE_PCM:
            case FSK_PULSE_PWM:
            case FSK_PULSE_MANCHESTER_ZEROBIT:
            case LORA:
                break;
            default:
                fprintf(stderr, "Unknown modulation %u in protocol!\n", r_dev->modulation);
            }
        }
    }

    return p_events;
}

int run_fsk_demods(list_t *r_devs, pulse_data_t *fsk_pulse_data)
{
    int p_events = 0;

    unsigned next_priority = 0; // next smallest on each loop through decoders
    // run all decoders of each priority, stop if an event is produced
    for (unsigned priority = 0; !p_events && priority < UINT_MAX; priority = next_priority) {
        next_priority = UINT_MAX;
        for (void **iter = r_devs->elems; iter && *iter; ++iter) {
            r_device *r_dev = *iter;

            // Find next smallest priority
            if (r_dev->priority > priority && r_dev->priority < next_priority)
                next_priority = r_dev->priority;
            // Run only current priority
            if (r_dev->priority != priority)
                continue;

            switch (r_dev->modulation) {
            // OOK decoders
            case OOK_PULSE_PCM:
            // case OOK_PULSE_RZ:
            case OOK_PULSE_PPM:
            case OOK_PULSE_PWM:
            case OOK_PULSE_MANCHESTER_ZEROBIT:
            case OOK_PULSE_PIWM_RAW:
            case OOK_PULSE_PIWM_DC:
            case OOK_PULSE_DMC:
            case OOK_PULSE_PWM_OSV1:
            case OOK_PULSE_NRZS:
            case OOK_PULSE_RZI:
                break;
            case FSK_PULSE_PCM:
                p_events += pulse_slicer_pcm(fsk_pulse_data, r_dev);
                break;
            case FSK_PULSE_PWM:
                p_events += pulse_slicer_pwm(fsk_pulse_data, r_dev);
                break;
            case FSK_PULSE_MANCHESTER_ZEROBIT:
                p_events += pulse_slicer_manchester_zerobit(fsk_pulse_data, r_dev);
                break;
            case LORA:
                break;
            default:
                fprintf(stderr, "Unknown modulation %u in protocol!\n", r_dev->modulation);
            }
        }
    }

    return p_events;
}

/* FoxSDR: LoRa chirp demodulation is not built; these keep the interface. */
int get_lora_params(list_t *r_devs, unsigned *spreading_factor,
        uint32_t *bandwidth, unsigned *sync_word)
{
    (void)r_devs; (void)spreading_factor; (void)bandwidth; (void)sync_word;
    return 0;
}

int run_lora_demods(list_t *r_devs, uint8_t const *payload,
        unsigned payload_len, unsigned spreading_factor,
        uint32_t bandwidth, unsigned sync_word)
{
    (void)r_devs; (void)payload; (void)payload_len; (void)spreading_factor;
    (void)bandwidth; (void)sync_word;
    return 0;
}

/* handlers */

static void log_handler(log_level_t level, char const *src, char const *msg, void *userdata)
{
    r_cfg_t *cfg = userdata;

    if (cfg->verbosity < (int)level) {
        return;
    }
    /* clang-format off */
    data_t *data = data_make(
            "src",     "",     DATA_STRING, src,
            "lvl",      "",     DATA_INT,    level,
            "msg",      "",     DATA_STRING, msg,
            NULL);
    /* clang-format on */

    // prepend "time" if requested
    if (cfg->report_time != REPORT_TIME_OFF) {
        char time_str[LOCAL_TIME_BUFLEN];
        time_pos_str(cfg, 0, time_str);
        data = data_prepend(data,
                data_str(NULL, "time", "", NULL, time_str));
    }

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_t *output = cfg->output_handler.elems[i];
        if (output && output->log_level >= (int)level) {
            data_output_print(output, data);
        }
    }
    data_free(data);
}

void r_redirect_logging(r_cfg_t *cfg)
{
    r_logger_set_log_handler(log_handler, cfg);
}

/** Pass the data structure to all output handlers. Frees data afterwards. */
void event_occurred_handler(r_cfg_t *cfg, data_t *data)
{
    // prepend "time" if requested
    if (cfg->report_time != REPORT_TIME_OFF) {
        char time_str[LOCAL_TIME_BUFLEN];
        time_pos_str(cfg, 0, time_str);
        data = data_prepend(data,
                data_str(NULL, "time", "", NULL, time_str));
    }

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_t *output = cfg->output_handler.elems[i];
        data_output_print(output, data);
    }
    data_free(data);
}

/// Samples since the start of the package a decoder decoded from.
/// An FSK package can start later than its OOK envelope package, e.g. after a lead-in.
static unsigned package_start_ago(r_cfg_t const *cfg, r_device const *r_dev)
{
    if (r_dev->modulation >= LORA_DEMOD_MIN_VAL) {
        return cfg->demod->lora_start_ago;
    }
    if (r_dev->modulation >= FSK_DEMOD_MIN_VAL) {
        return cfg->demod->fsk_pulse_data.start_ago;
    }
    return cfg->demod->pulse_data.start_ago;
}

/** Pass the data structure to all output handlers. Frees data afterwards. */
void log_device_handler(r_device *r_dev, int level, data_t *data)
{
    r_cfg_t *cfg = r_dev->output_ctx;

    // prepend "time" if requested
    if (cfg->report_time != REPORT_TIME_OFF) {
        char time_str[LOCAL_TIME_BUFLEN];
        time_pos_str(cfg, package_start_ago(cfg, r_dev), time_str);
        data = data_prepend(data,
                data_str(NULL, "time", "", NULL, time_str));
    }

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_t *output = cfg->output_handler.elems[i];
        if (output && output->log_level >= level) {
            data_output_print(output, data);
        }
    }
    data_free(data);
}

/** Pass the data structure to all output handlers. Frees data afterwards. */
void data_acquired_handler(r_device *r_dev, data_t *data)
{
    r_cfg_t *cfg = r_dev->output_ctx;

#ifndef NDEBUG
    // check for undeclared csv fields
    for (data_t *d = data; d; d = d->next) {
        int found = 0;
        for (char const *const *p = r_dev->fields; *p; ++p) {
            if (!strcmp(d->key, *p)) {
                found = 1;
                break;
            }
        }
        if (!found) {
            fprintf(stderr, "WARNING: Undeclared field \"%s\" in [%u] \"%s\"\n", d->key, r_dev->protocol_num, r_dev->name);
        }
    }
#endif

    if (cfg->conversion_mode == CONVERT_SI) {
        for (data_t *d = data; d; d = d->next) {
            // Convert double type fields ending in _F to _C
            if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_F")) {
                d->value.v_dbl = fahrenheit2celsius(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_F", "_C");
                free(d->key);
                d->key = new_label;
                char *pos;
                if (d->format && (pos = strrchr(d->format, 'F'))) {
                    *pos = 'C';
                }
            }
            // Convert double type fields ending in _mi_h to _km_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mi_h")) {
                d->value.v_dbl = mph2kmph(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_mi_h", "_km_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mi/h", "km/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _in to _mm
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_in")) {
                d->value.v_dbl = inch2mm(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_in", "_mm");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "in", "mm");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _in_h to _mm_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_in_h")) {
                d->value.v_dbl = inch2mm(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_in_h", "_mm_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "in/h", "mm/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _inHg to _hPa
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_inHg")) {
                d->value.v_dbl = inhg2hpa(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_inHg", "_hPa");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "inHg", "hPa");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _PSI to _kPa
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_PSI")) {
                d->value.v_dbl = psi2kpa(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_PSI", "_kPa");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "PSI", "kPa");
                free(d->format);
                d->format = new_format_label;
            }
        }
    }
    if (cfg->conversion_mode == CONVERT_CUSTOMARY) {
        for (data_t *d = data; d; d = d->next) {
            // Convert double type fields ending in _C to _F
            if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_C")) {
                d->value.v_dbl = celsius2fahrenheit(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_C", "_F");
                free(d->key);
                d->key = new_label;
                char *pos;
                if (d->format && (pos = strrchr(d->format, 'C'))) {
                    *pos = 'F';
                }
            }
            // Convert double type fields ending in _km_h to _mi_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_km_h")) {
                d->value.v_dbl = kmph2mph(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_km_h", "_mi_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "km/h", "mi/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _m_s to _mi_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_m_s")) {
                d->value.v_dbl = mps2mph(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_m_s", "_mi_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "m/s", "mi/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _mm to _in
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mm")) {
                d->value.v_dbl = mm2inch(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_mm", "_in");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mm", "in");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _mm_h to _in_h
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_mm_h")) {
                d->value.v_dbl = mm2inch(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_mm_h", "_in_h");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "mm/h", "in/h");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _hPa to _inHg
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_hPa")) {
                d->value.v_dbl = hpa2inhg(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_hPa", "_inHg");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "hPa", "inHg");
                free(d->format);
                d->format = new_format_label;
            }
            // Convert double type fields ending in _kPa to _PSI
            else if ((d->type == DATA_DOUBLE) && str_endswith(d->key, "_kPa")) {
                d->value.v_dbl = kpa2psi(d->value.v_dbl);
                char *new_label = str_replace(d->key, "_kPa", "_PSI");
                free(d->key);
                d->key = new_label;
                char *new_format_label = str_replace(d->format, "kPa", "PSI");
                free(d->format);
                d->format = new_format_label;
            }
        }
    }

    // prepend "description" if requested
    if (cfg->report_description) {
        data = data_prepend(data,
                data_str(NULL, "description", "Description", NULL, r_dev->name));
    }

    // prepend "protocol" if requested
    if (cfg->report_protocol && r_dev->protocol_num) {
        data = data_prepend(data,
                data_int(NULL, "protocol", "Protocol", NULL, r_dev->protocol_num));
    }

    if (cfg->report_meta && cfg->demod->lora_spreading_factor) {
        char coding_rate[4];
        snprintf(coding_rate, sizeof(coding_rate), "4/%u",
                cfg->demod->lora_coding_rate + 4);
        data = data_str(data, "mod",  "Modulation",       NULL,         "LoRa");
        data = data_dbl(data, "freq", "Freq",             "%.1f MHz",   cfg->demod->center_frequency / 1000000.0);
        data = data_int(data, "sf",   "Spreading factor", NULL,         cfg->demod->lora_spreading_factor);
        data = data_dbl(data, "bw",   "Bandwidth",        "%.1f kHz",   cfg->demod->lora_bandwidth / 1000.0);
        data = data_str(data, "cr",   "Coding rate",      NULL,         coding_rate);
        data = data_int(data, "sync", "Sync word",        "0x%02x",     cfg->demod->lora_sync_word);
    }
    else if (cfg->report_meta && cfg->demod->fsk_pulse_data.fsk_f2_est) {
        data = data_str(data, "mod",   "Modulation",  NULL,         "FSK");
        data = data_dbl(data, "freq1", "Freq1",       "%.1f MHz",   cfg->demod->fsk_pulse_data.freq1_hz / 1000000.0);
        data = data_dbl(data, "freq2", "Freq2",       "%.1f MHz",   cfg->demod->fsk_pulse_data.freq2_hz / 1000000.0);
        data = data_dbl(data, "rssi",  "RSSI",        "%.1f dB",    cfg->demod->fsk_pulse_data.rssi_db);
        data = data_dbl(data, "snr",   "SNR",         "%.1f dB",    cfg->demod->fsk_pulse_data.snr_db);
        data = data_dbl(data, "noise", "Noise",       "%.1f dB",    cfg->demod->fsk_pulse_data.noise_db);
    }
    else if (cfg->report_meta) {
        data = data_str(data, "mod",   "Modulation",  NULL,         "ASK");
        data = data_dbl(data, "freq",  "Freq",        "%.1f MHz",   cfg->demod->pulse_data.freq1_hz / 1000000.0);
        data = data_dbl(data, "rssi",  "RSSI",        "%.1f dB",    cfg->demod->pulse_data.rssi_db);
        data = data_dbl(data, "snr",   "SNR",         "%.1f dB",    cfg->demod->pulse_data.snr_db);
        data = data_dbl(data, "noise", "Noise",       "%.1f dB",    cfg->demod->pulse_data.noise_db);
    }

    // prepend "time" if requested
    if (cfg->report_time != REPORT_TIME_OFF) {
        char time_str[LOCAL_TIME_BUFLEN];
        time_pos_str(cfg, package_start_ago(cfg, r_dev), time_str);
        data = data_prepend(data,
                data_str(NULL, "time", "", NULL, time_str));
    }

    for (size_t i = 0; i < cfg->output_handler.len; ++i) { // list might contain NULLs
        data_output_t *output = cfg->output_handler.elems[i];
        data_output_print(output, data);
    }
    data_free(data);
}

// level 0: do not report (don't call this), 1: report successful devices, 2: report active devices, 3: report all
data_t *create_report_data(r_cfg_t *cfg, int level)
{
    list_t *r_devs = &cfg->demod->r_devs;
    data_t *data;
    list_t dev_data_list = {0};
    list_ensure_size(&dev_data_list, r_devs->len);

    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (level <= 2 && r_dev->decode_events == 0)
            continue;
        if (level <= 1 && r_dev->decode_ok == 0)
            continue;
        if (level <= 0)
            continue;

        data = data_make(
                "device",       "", DATA_INT, r_dev->protocol_num,
                "name",         "", DATA_STRING, r_dev->name,
                "events",       "", DATA_INT, r_dev->decode_events,
                "ok",           "", DATA_INT, r_dev->decode_ok,
                "messages",     "", DATA_INT, r_dev->decode_messages,
                NULL);

        if (r_dev->decode_fails[-DECODE_FAIL_OTHER])
            data = data_int(data, "fail_other",   "", NULL, r_dev->decode_fails[-DECODE_FAIL_OTHER]);
        if (r_dev->decode_fails[-DECODE_ABORT_LENGTH])
            data = data_int(data, "abort_length", "", NULL, r_dev->decode_fails[-DECODE_ABORT_LENGTH]);
        if (r_dev->decode_fails[-DECODE_ABORT_EARLY])
            data = data_int(data, "abort_early",  "", NULL, r_dev->decode_fails[-DECODE_ABORT_EARLY]);
        if (r_dev->decode_fails[-DECODE_FAIL_MIC])
            data = data_int(data, "fail_mic",     "", NULL, r_dev->decode_fails[-DECODE_FAIL_MIC]);
        if (r_dev->decode_fails[-DECODE_FAIL_SANITY])
            data = data_int(data, "fail_sanity",  "", NULL, r_dev->decode_fails[-DECODE_FAIL_SANITY]);

        list_push(&dev_data_list, data);
    }

    data = data_make(
            "count",            "", DATA_INT, cfg->demod->frames_ook,
            "fsk",              "", DATA_INT, cfg->demod->frames_fsk,
            "events",           "", DATA_INT, cfg->demod->frames_events,
            NULL);

    char since_str[LOCAL_TIME_BUFLEN];
    format_time_str(since_str, "%Y-%m-%dT%H:%M:%S", cfg->report_time_tz, cfg->demod->frames_since);

    data = data_make(
            "enabled",          "", DATA_INT, r_devs->len,
            "since",            "", DATA_STRING, since_str,
            "frames",           "", DATA_DATA, data,
            "stats",            "", DATA_ARRAY, data_array((int)dev_data_list.len, DATA_DATA, dev_data_list.elems),
            NULL);

    list_free_elems(&dev_data_list, NULL);
    return data;
}

void flush_report_data(r_cfg_t *cfg)
{
    list_t *r_devs = &cfg->demod->r_devs;

    time(&cfg->demod->frames_since);
    cfg->demod->frames_ook = 0;
    cfg->demod->frames_fsk = 0;
    cfg->demod->frames_events = 0;

    for (void **iter = r_devs->elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;

        r_dev->decode_events = 0;
        r_dev->decode_ok = 0;
        r_dev->decode_messages = 0;
        r_dev->decode_fails[0] = 0;
        r_dev->decode_fails[1] = 0;
        r_dev->decode_fails[2] = 0;
        r_dev->decode_fails[3] = 0;
        r_dev->decode_fails[4] = 0;
    }
}
