#define LOG_PREFIX "rch"
#include <tetrapol/log.h>
#include <tetrapol/rch.h>
#include <tetrapol/addr.h>
#include <tetrapol/bit_utils.h>
#include <tetrapol/data_frame.h>
#include <tetrapol/helper_out.h>
#include <tetrapol/misc.h>
#include <tetrapol/system_config.h>

#include <stdlib.h>

typedef struct {
    int naddrs;
    addr_t addrs[3];
} rch_data_t;

struct rch_priv_t {
    data_frame_t *data_fr;
    rch_data_t rch_data;
    tpol_t *tpol;   /* FoxSDR helper: for time-stamping the reported event */
};

rch_t *rch_create(tpol_t *tpol)
{
    rch_t *rch = malloc(sizeof(rch_t));
    if (!rch) {
        return NULL;
    }

    rch->data_fr = data_frame_create();
    if (!rch->data_fr) {
        free(rch);
        return NULL;
    }

    rch->tpol = tpol;

    return rch;
}

void rch_destroy(rch_t *rch)
{
    if (rch) {
        data_frame_destroy(rch->data_fr);
    }
    free(rch);
}

bool rch_push_frame(rch_t *rch, const frame_t *fr)
{
    if (data_frame_push_frame(rch->data_fr, fr) <= 0) {
        LOG(DBG, "RCH: block fail");
        return false;
    }

    if (data_frame_blocks(rch->data_fr) != 1) {
        LOG(WTF, "block lenght != 1");
        return false;
    }

    uint8_t data[(92 + 7) / 8];
    const int size = data_frame_get_bytes(rch->data_fr, data);
    if (size != 64) {
        LOG(WTF, "invalid frame lenght");
        return false;
    }

    if (!check_fcs(data, size)) {
        LOG(DBG, "invalid FCS");
        return false;
    }

    rch->rch_data.naddrs = 0;
    for (int i = 0; i < ARRAY_LEN(rch->rch_data.addrs); ++i) {
        addr_parse(&rch->rch_data.addrs[rch->rch_data.naddrs], data + 2*i, 0);
        if (!addr_is_tti_no_st(&rch->rch_data.addrs[rch->rch_data.naddrs], true)) {
            ++rch->rch_data.naddrs;
        }
    }

    return true;
}

/* FoxSDR helper: upstream printed this as text on stderr; the helper reports it as a JSON event instead
   (tp_emit_rch in helper_out.c). */
void rch_print(const rch_t *rch)
{
    tp_emit_rch(rch->tpol, rch->rch_data.addrs, rch->rch_data.naddrs);
}
