#include "settings.h"
#include "../platform/mmio.h"
#define R_SET_IDX 0x8000006Cu
#define R_SET_DAT 0x80000070u

settings_stats_t settings_stats;
static uint32_t pub[SF_WORDS];           /* the words as last published or adopted */
static uint32_t pub_vals[SF_COUNT];
static uint32_t next_poll;

static uint32_t rd(uint32_t w) { REG_WR(R_SET_IDX, w); return REG_RD(R_SET_DAT); }
static void     wr(uint32_t w, uint32_t v) { REG_WR(R_SET_IDX, w); REG_WR(R_SET_DAT, v); }

/* Words 16.. exist only on the 32-word register file; on an older one an index of 16 aliases onto word 0 (Volume) and corrupts it. The test cannot be "write and read back" (both alias to
 * the same word): word 0 must survive a write to word 16. (fw/settings.inc, B-346.) */
static int wide_probe(void)
{
    uint32_t save0 = rd(0u), probe0 = save0 ^ 0x5A5A5A5Au;
    wr(0u, probe0); wr(16u, ~probe0);
    int ok = rd(0u) == probe0;
    wr(0u, save0);
    return ok;
}

static void read_words(uint32_t *w)
{
    for (uint32_t i = 0; i < SF_WORDS; i++) w[i] = 0;
    for (uint32_t i = 0; i < SF_COUNT; i++) { uint32_t k = sf_table[i].word; if (k < 16u || settings_stats.wide) w[k] = rd(k); }
}

void settings_init(uint32_t *vals)
{
    settings_stats.wide = (uint32_t)wide_probe();
    read_words(pub);
    sf_unpack(pub, vals);
    for (uint32_t i = 0; i < SF_COUNT; i++) pub_vals[i] = vals[i];
    settings_stats.loads++;
}

static void publish(const uint32_t *vals)
{
    uint32_t w[SF_WORDS]; read_words(w);                     /* read-modify-write: bits that are not ours stay */
    sf_pack(w, vals);
    for (uint32_t i = 0; i < SF_COUNT; i++) {
        uint32_t k = sf_table[i].word;
        if (k >= 16u && !settings_stats.wide) continue;      /* never write an index that would alias onto the volume */
        if (rd(k) != w[k]) wr(k, w[k]);
        pub[k] = w[k];
    }
    for (uint32_t i = 0; i < SF_COUNT; i++) pub_vals[i] = vals[i];
    settings_stats.stores++;
}

int settings_service(uint32_t *vals, int streaming)
{
    int changed = 0;
    for (uint32_t i = 0; i < SF_COUNT; i++) if (vals[i] != pub_vals[i]) changed = 1;
    settings_stats.dirty = (uint32_t)changed;
    if (changed) { if (!streaming) publish(vals); return 0; }          /* a button change wins over the menu until it is published */
    if ((int32_t)(cycles() - next_poll) < 0) return 0;
    next_poll = cycles() + CLK_HZ / 2u;
    for (uint32_t i = 0; i < SF_COUNT; i++) {                          /* has Core Settings moved one of our words? */
        uint32_t k = sf_table[i].word;
        if ((k < 16u || settings_stats.wide) && rd(k) != pub[k]) {
            uint32_t w[SF_WORDS]; read_words(w); sf_unpack(w, vals);
            for (uint32_t j = 0; j < SF_COUNT; j++) pub_vals[j] = vals[j];
            for (uint32_t j = 0; j < SF_WORDS; j++) pub[j] = w[j];
            settings_stats.menu_changes++;
            return 1;
        }
    }
    return 0;
}
