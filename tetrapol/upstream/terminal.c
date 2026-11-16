#define LOG_PREFIX "terminal"

#include <tetrapol/log.h>
#include <tetrapol/terminal.h>
#include <tetrapol/link.h>
#include <stdlib.h>
#include <string.h>

/*
 * FoxSDR helper change: upstream keeps the terminals in a glib GTree. glib is not available here, so the
 * terminal list is a small array searched linearly, capped at TERMINAL_LIST_MAX entries (every terminal owns
 * about 30 kB of reassembly buffers). When the list is full the least recently used terminal is dropped.
 * Behaviour for up to TERMINAL_LIST_MAX terminals is the same as upstream.
 */
enum {
    TERMINAL_LIST_MAX = 512,
};

struct terminal_priv_t {
    link_t *link;
};

typedef struct {
    addr_t addr;
    terminal_t *term;
    uint64_t last_use;
} terminal_entry_t;

struct terminal_list_priv_t {
    terminal_entry_t *entries;
    int n;
    uint64_t use_counter;
    tpol_t *tpol;
    int log_ch;
};

static terminal_t* terminal_create(tpol_t *tpol, int log_ch)
{
    terminal_t *term = malloc(sizeof(terminal_t));
    if (!term) {
        return NULL;
    }

    term->link = link_create(tpol, log_ch);
    if (!term->link) {
        free(term);
        return NULL;
    }

    return term;
}

static void terminal_destroy(terminal_t *term)
{
    if (!term) {
        return;
    }

    link_destroy(term->link);
    free(term);
}

int terminal_push_hdlc_frame(terminal_t* term, const hdlc_frame_t *hdlc_fr)
{
    return link_push_hdlc_frame(term->link, hdlc_fr);
}

static bool addr_equal(const addr_t *a1, const addr_t *a2)
{
    return a1->z == a2->z && a1->y == a2->y && a1->x == a2->x;
}

terminal_list_t *terminal_list_create(tpol_t * tpol, int log_ch)
{
    terminal_list_t *tlist = calloc(1, sizeof(terminal_list_t));
    if (!tlist) {
        return NULL;
    }

    tlist->entries = calloc(TERMINAL_LIST_MAX, sizeof(terminal_entry_t));
    if (!tlist->entries) {
        free(tlist);
        return NULL;
    }

    tlist->tpol = tpol;
    tlist->log_ch = log_ch;

    return tlist;
}

void terminal_list_destroy(terminal_list_t *tlist)
{
    if (!tlist) {
        return;
    }
    for (int i = 0; i < tlist->n; ++i) {
        terminal_destroy(tlist->entries[i].term);
    }
    free(tlist->entries);
    free(tlist);
}

static terminal_entry_t *terminal_list_find(const terminal_list_t* tlist, const addr_t *addr)
{
    for (int i = 0; i < tlist->n; ++i) {
        if (addr_equal(&tlist->entries[i].addr, addr)) {
            return &tlist->entries[i];
        }
    }
    return NULL;
}

terminal_t* terminal_list_lookup(const terminal_list_t* tlist, const addr_t *addr)
{
    terminal_entry_t *e = terminal_list_find(tlist, addr);
    return e ? e->term : NULL;
}

terminal_t* terminal_list_insert(terminal_list_t* tlist, const addr_t *addr)
{
    terminal_entry_t *e = terminal_list_find(tlist, addr);
    if (e) {
        return e->term;
    }

    terminal_t *term = terminal_create(tlist->tpol, tlist->log_ch);
    if (!term) {
        return NULL;
    }

    if (tlist->n == TERMINAL_LIST_MAX) {
        int oldest = 0;
        for (int i = 1; i < tlist->n; ++i) {
            if (tlist->entries[i].last_use < tlist->entries[oldest].last_use) {
                oldest = i;
            }
        }
        terminal_destroy(tlist->entries[oldest].term);
        tlist->entries[oldest] = tlist->entries[tlist->n - 1];
        --tlist->n;
    }

    e = &tlist->entries[tlist->n++];
    e->addr = *addr;
    e->term = term;
    e->last_use = ++tlist->use_counter;

    return term;
}

void terminal_list_erase(terminal_list_t* tlist, const addr_t *addr)
{
    terminal_entry_t *e = terminal_list_find(tlist, addr);
    if (!e) {
        return;
    }
    terminal_destroy(e->term);
    *e = tlist->entries[tlist->n - 1];
    --tlist->n;
}

int terminal_list_push_hdlc_frame(terminal_list_t* tlist,
        const hdlc_frame_t *hdlc_fr)
{
    terminal_entry_t *e = terminal_list_find(tlist, &hdlc_fr->addr);
    terminal_t *term;
    if (e) {
        term = e->term;
        e->last_use = ++tlist->use_counter;
    } else {
        term = terminal_list_insert(tlist, &hdlc_fr->addr);
        if (!term) {
            LOG(ERR, "Terminal allocation failed");
            return -1;
        }
    }

    return terminal_push_hdlc_frame(term, hdlc_fr);
}

void terminal_list_rx_glitch(terminal_list_t* tlist)
{
    for (int i = 0; i < tlist->n; ++i) {
        link_rx_glitch(tlist->entries[i].term->link);
    }
}

void terminal_list_tick(terminal_list_t* tlist, time_evt_t *te)
{
    for (int i = 0; i < tlist->n; ++i) {
        link_tick(te, tlist->entries[i].term->link);
    }
}
