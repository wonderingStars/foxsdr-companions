/*
 * sondes_stream.h - the per-decoder sample stream shared by the helper's main thread and a decoder thread.
 *
 * Part of the FoxSDR "sondes" GPL helper. Licence: GNU GPL v3 (see ../LICENSE).
 *
 * The main thread reads the raw samples from stdin and pushes each chunk to EVERY decoder's stream. A decoder
 * thread (an unmodified-as-far-as-possible upstream main()) pulls from its stream through sondes_fread(). The main
 * thread waits after each chunk until every decoder has used the chunk up and is blocked waiting for more (or has
 * finished), then prints the events they produced, ordered by (t, decoder number). That barrier makes the output
 * independent of thread scheduling: the same input always gives the same lines in the same order.
 */
#ifndef SONDES_STREAM_H
#define SONDES_STREAM_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct sondes_hub {
    std::mutex m;
    std::condition_variable cv;
};

typedef std::shared_ptr<const std::vector<uint8_t> > sondes_chunk;

struct sondes_event {
    double t;
    std::string line;
};

struct sondes_stream {
    sondes_hub *hub = nullptr;
    int index = 0;               /* decoder number, the tie-break for equal t */
    std::string name;            /* decoder name, e.g. "lms6" */
    int rate = 0;                /* sample rate of the s16 samples in the chunks */

    /* decoder-thread side, no lock needed */
    sondes_chunk held;
    const uint8_t *cur = nullptr;
    size_t left = 0;
    uint64_t consumed = 0;       /* bytes handed to the decoder so far */
    bool dry = false;            /* end of input seen */
    std::string linebuf;         /* captured stdout text, up to the next newline */

    /* shared, guarded by hub->m */
    std::deque<sondes_chunk> q;
    bool eof = false;
    bool waiting = false;        /* decoder is blocked in fread with nothing to read */
    bool finished = false;       /* decoder main() has returned */
    std::vector<sondes_event> out;
    unsigned long frames = 0;    /* events produced */
    unsigned long text_lines = 0;/* non-JSON lines the decoder printed (dropped) */
    int exit_code = 0;
};

extern thread_local sondes_stream *sondes_tls;

#endif
