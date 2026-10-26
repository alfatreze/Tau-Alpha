#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "settings.h"
#include "../platform/mmio.h"
#define R_SET_IDX 0x8000006Cu
#define R_SET_DAT 0x80000070u

#define LAZY_STREAMING_CYC (CLK_HZ * 8u)
settings_stats_t settings_stats;
static uint32_t pub[SF_WORDS];           /* the words as last published or adopted */
static uint32_t pub_vals[SF_COUNT];
static uint32_t next_poll, dirty_t0, lazy_t0;
static uint8_t was_dirty, lazy_dirty; static uint32_t lazy_snap[SF_COUNT - SF_LAZY_FIRST];

static uint32_t rd(uint32_t w) { REG_WR(R_SET_IDX, w); return REG_RD(R_SET_DAT); }
static void     wr(uint32_t w, uint32_t v) { REG_WR(R_SET_IDX, w); REG_WR(R_SET_DAT, v); }

/* Words 16.. exist only on the 32-word register file; on an older one an index of 16 aliases onto word 0 (Volume) and corrupts it. The test cannot be "write and read back" (both alias to
 * the same word): word 0 must survive a write to word 16. (fw/settings.inc, B-346.) */
static int wide_probe(void)
{
    uint32_t save0 = rd(0u), save16 = rd(16u), probe0 = save0 ^ 0x5A5A5A5Au;      /* on the narrow file word 16 IS word 0 (save16 == save0) */
    wr(0u, probe0); wr(16u, ~probe0);
    int ok = rd(0u) == probe0;
    wr(16u, save16); wr(0u, save0);                                              /* word 16 holds a tuning now: it is put back too (restoring 16 before 0 is right on both files) */
    return ok;
}

static void read_words(uint32_t *w)
{
    for (uint32_t i = 0; i < SF_WORDS; i++) w[i] = 0;
    for (uint32_t i = 0; i < SF_COUNT; i++) { uint32_t k = sf_table[i].word; if (k < 16u || settings_stats.wide) w[k] = rd(k); }
}

/* Word 0 holds the layout version and goes last: a power loss in between leaves the legacy marker, and the next boot resumes the migration from the words that carry the new-layout mark and the legacy words that are still there. */
static void write_words(const uint32_t *w)
{
    for (uint32_t n = 0; n < sizeof sf_write_order; n++) {      /* the order matters for a migration (cores/settings_core.h sf_write_order) */
        const uint32_t k = sf_write_order[n];
        if (k >= 16u && !settings_stats.wide) continue;      /* never write an index that would alias onto word 0 */
        if (rd(k) != w[k]) wr(k, w[k]);
    }
    if (rd(0u) != w[0]) wr(0u, w[0]);
}

void settings_init(uint32_t *vals)
{
    settings_stats.wide = (uint32_t)wide_probe();
    read_words(pub);
    if (sf_is_legacy(pub)) {                                 /* a card saved by TAU DEV 181 or older: convert it once */
        uint32_t nw[SF_WORDS]; sf_migrate_resume(pub, nw);
        write_words(nw);
        for (uint32_t k = 0; k < SF_WORDS; k++) pub[k] = nw[k];
        settings_stats.migrated = 1;
    }
    sf_unpack(pub, vals);
    settings_stats.layout = (pub[0] >> 27) & 15u;
    for (uint32_t i = 0; i < SF_COUNT; i++) pub_vals[i] = vals[i];
    settings_stats.loads++;
}

void settings_seed(uint32_t *vals) { for (uint32_t i = 0; i < SF_COUNT; i++) vals[i] = pub_vals[i]; }

static void publish(const uint32_t *vals)
{
    uint32_t w[SF_WORDS]; read_words(w);                     /* read-modify-write: bits that are not ours stay */
    sf_pack(w, vals);
    write_words(w);
    for (uint32_t i = 0; i < SF_COUNT; i++) pub[sf_table[i].word] = w[sf_table[i].word];
    for (uint32_t i = 0; i < SF_COUNT; i++) pub_vals[i] = vals[i];
    lazy_dirty = 0;
    settings_stats.stores++;
}

int settings_service(uint32_t *vals, int streaming)
{
    int changed = 0, lazy = 0;
    for (uint32_t i = 0; i < SF_LAZY_FIRST; i++) if (vals[i] != pub_vals[i]) changed = 1;
    for (uint32_t i = SF_LAZY_FIRST; i < SF_COUNT; i++) if (vals[i] != pub_vals[i]) lazy = 1;
    settings_stats.dirty = (uint32_t)(changed | lazy);
    /* The last played track changes with every track: it is saved when nothing is streaming (paused or stopped), or after LAZY_STREAMING_CYC of playing the same track, never with the 1 s urgency of a setting. */
    if (lazy && !changed) {
        int moved = !lazy_dirty; for (uint32_t i = SF_LAZY_FIRST; i < SF_COUNT; i++) if (lazy_snap[i - SF_LAZY_FIRST] != vals[i]) moved = 1;
        if (moved) { lazy_dirty = 1; lazy_t0 = cycles(); for (uint32_t i = SF_LAZY_FIRST; i < SF_COUNT; i++) lazy_snap[i - SF_LAZY_FIRST] = vals[i]; }      /* a new track restarts the wait */
        if (!streaming || (int32_t)(cycles() - lazy_t0) >= (int32_t)LAZY_STREAMING_CYC) { publish(vals); lazy_dirty = 0; }
        return 0;
    }
    if (!lazy) lazy_dirty = 0;
    /* A change is published at once when nothing is streaming, and after one second of staying unpublished while it is (a register-file write costs nothing audible, and a setting changed during playback was lost
     * when the core was quit before the music stopped: the Appearance settings did not save). A button change wins over the menu until it is published. */
    if (changed) {
        if (!was_dirty) { was_dirty = 1; dirty_t0 = cycles(); }
        if (!streaming || (int32_t)(cycles() - dirty_t0) >= (int32_t)CLK_HZ) { publish(vals); was_dirty = 0; }
        return 0;
    }
    was_dirty = 0;
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
