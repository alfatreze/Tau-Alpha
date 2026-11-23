#pragma GCC optimize ("Os")
#include "meters_settings.h"
#include "../cores/tune_core.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#define MF_FN P2_COLD_TEXT static
#include "../cores/mset_file_core.h"

/* ---- the save-file backend (W2, docs/features/meters/METER_SAVE_FILE_SPEC.md): chosen at RUN TIME, so one firmware serves a bitstream with and without the save RAM (no CORE_VERSION interlock) ----
 * Used when the RAM is built in (SAVE_ST bit 31) AND the Pocket loaded the whole 8 KB file into it (words written [15:0] = 2,048) AND no newer file version is present. Otherwise (no RAM, no file, a file of another size, a newer
 * version) the pool of two tunings (below) stays in use exactly as before and nothing is written to the RAM. Registers: docs/MMIO_ALLOCATION.md (defined here, not in mmio.h: the WebAssembly modules hash that header). */
#define R_SAVE_ST    0x800001ECu
#define R_SAVE_IDX   0x800001F0u
#define R_SAVE_DATA  0x800001F4u
#define MS_SLOT_ID   11u                  /* the save slot's id in data.json (tools/tau_data_slots.py) */
#define MS_FILE_BYTES 8192u
#ifdef P2_HOST
uint32_t ms_host_save_st; uint32_t ms_host_save_ram[2048]; static uint32_t ms_host_idx;        /* the tests' model of the save RAM (ms_host_save_st 0 = not built in) */
P2_COLD_TEXT static uint32_t sv_status(void) { return ms_host_save_st; }
P2_COLD_TEXT static void sv_seek(uint32_t w) { ms_host_idx = w; }
P2_COLD_TEXT static uint32_t sv_rd(void) { return ms_host_save_ram[ms_host_idx++ & 2047u]; }
P2_COLD_TEXT static void sv_wr(uint32_t v) { ms_host_save_ram[ms_host_idx++ & 2047u] = v; }
#else
P2_COLD_TEXT static uint32_t sv_status(void) { return REG_RD(R_SAVE_ST); }
P2_COLD_TEXT static void sv_seek(uint32_t w) { REG_WR(R_SAVE_IDX, w); (void)REG_RD(R_SAVE_ST); }     /* the status read gives the RAM's output register its cycle */
P2_COLD_TEXT static uint32_t sv_rd(void) { return REG_RD(R_SAVE_DATA); }
P2_COLD_TEXT static void sv_wr(uint32_t v) { REG_WR(R_SAVE_DATA, v); }
#endif
/* The slot's entry in APF's Dataslot ID/Size Table (words: id, size, id, size, ...): the Pocket writes a nonvolatile slot back with the SIZE in this entry (docs core-boot-process "Shutdown", KB-001). APF fills it when it loads a
 * file, so with the seed on the card it already reads 8,192 and this does nothing. If the RAM looks loaded but the entry is still 0 (a file that does not exist: data-slot parameter bit 5 fills the RAM with 0xFF), set it to
 * 8,192 so the Pocket has something to write back: probe 2b of docs/features/meters/RELEASE_075_PLUMBING_PLAN.md (DEV 232). Done after boot, never while APF delivers slots (KB-002). */
#ifdef P2_HOST
uint32_t ms_host_dt[64];
P2_COLD_TEXT static uint32_t dt_rd(uint32_t a) { return ms_host_dt[a & 63u]; }
P2_COLD_TEXT static void dt_wr(uint32_t a, uint32_t v) { ms_host_dt[a & 63u] = v; }
#else
P2_COLD_TEXT static uint32_t dt_rd(uint32_t a) { REG_WR(R_DT_ADDR, a); (void)REG_RD(R_STAT0); return REG_RD(R_DT_DATA); }
P2_COLD_TEXT static void dt_wr(uint32_t a, uint32_t v) { REG_WR(R_DT_ADDR, a); REG_WR(R_DT_DATA, v); }
#endif
P2_COLD_TEXT static void size_entry(void)
{
    for (uint32_t i = 0; i < 32u; i++) {
        if (dt_rd(2u * i) != MS_SLOT_ID) continue;
        if (dt_rd(2u * i + 1u) == 0u) dt_wr(2u * i + 1u, MS_FILE_BYTES);
        return;
    }
}
P2_COLD_TEXT static int file_on(mf_io_t *io)           /* 1 = the file backend serves this call (io is filled) */
{
    const uint32_t st = sv_status();
    if (!(st >> 31) || (st & 0xFFFFu) != 2048u) return 0;
    io->seek = sv_seek; io->rd = sv_rd; io->wr = sv_wr;
    mf_scan_t sc; mf_scan(io, &sc);
    return !sc.locked;
}
/* The settings interface over the pool of two tunings (see meters_settings.h for the contract). The format is cores/tune_core.h's, unchanged: the persisted words are bit for bit what the older code wrote, so a card
 * written by 0.7.1 is read by this and the other way round. */
MS_STATE static const uint16_t *ms_max; MS_STATE static uint32_t ms_np, ms_key;
static const uint16_t *ms_max_of(uint32_t key, uint32_t *np) { if (key != ms_key || !ms_max) return 0; *np = ms_np; return ms_max; }       /* tune_unpack asks for the schema of the key it reads */
static uint32_t pool_put(uint32_t id_hash, uint32_t params_rev, const meter_settings_t *s, const uint16_t *max, uint32_t np)
{
    const uint32_t key = tune_key(id_hash, params_rev);
    uint8_t idx[MS_MAX_ENTRIES]; uint16_t val[MS_MAX_ENTRIES]; uint32_t n = 0;
    for (uint32_t k = 0; k < s->n && k < MS_MAX_ENTRIES; k++) if (s->idx[k] < np && s->val[k] <= max[s->idx[k]]) { idx[n] = s->idx[k]; val[n] = s->val[k]; n++; }
    uint32_t w[TUNE_WORDS];
    const uint32_t kept = tune_build(key, s->preset, idx, val, n, max, w);
    if (!kept) { mset_pool_drop(key); return 0u; }
    mset_pool_store(key, w);
    return kept;
}
static int pool_get(uint32_t id_hash, uint32_t params_rev, const uint16_t *max, uint32_t np, meter_settings_t *out)
{
    const uint32_t key = tune_key(id_hash, params_rev); const int sl = mset_pool_find(key);
    out->n = 0; out->preset = 0;
    if (sl < 0) return 0;
    ms_max = max; ms_np = np; ms_key = key;
    uint32_t mt = 0, pr = 0; uint8_t ix[TUNE_MAX]; uint16_t vl[TUNE_MAX];
    const uint32_t n = tune_unpack(mset_pool_words((uint32_t)sl), ms_max_of, &mt, &pr, ix, vl);
    if (!n || mt != key) return 0;
    out->preset = (uint8_t)pr; out->n = (uint8_t)n;
    for (uint32_t k = 0; k < n; k++) { out->idx[k] = ix[k]; out->val[k] = vl[k]; }
    return 1;
}
static int pool_preset(uint32_t id_hash, uint32_t params_rev, uint32_t *preset)
{
    const int sl = mset_pool_find(tune_key(id_hash, params_rev));
    if (sl < 0) return 0;
    uint8_t b[16]; uint32_t pos = 28u; tune_from_words(mset_pool_words((uint32_t)sl), b); *preset = tune_get(b, &pos, 7u);
    return 1;
}
static void pool_drop(uint32_t id_hash, uint32_t params_rev) { mset_pool_drop(tune_key(id_hash, params_rev)); }

uint32_t meter_settings_state(void)
{
    const uint32_t st = sv_status();
    if (!(st >> 31)) return 3u;
    if ((st & 0xFFFFu) != 2048u) return 2u;
    mf_io_t io; io.seek = sv_seek; io.rd = sv_rd; io.wr = sv_wr;
    mf_scan_t sc; mf_scan(&io, &sc);
    return sc.locked ? 1u : 0u;
}
/* ---- the public interface: the file when it serves, else the pool ---- */
static int fits(const meter_settings_t *r, const uint16_t *max, uint32_t np)
{
    for (uint32_t k = 0; k < r->n; k++) if (r->idx[k] >= np || r->val[k] > max[r->idx[k]]) return 0;
    return 1;
}
uint32_t meter_settings_put(uint32_t id_hash, uint32_t params_rev, const meter_settings_t *s, const uint16_t *max, uint32_t np)
{
    mf_io_t io;
    if (!file_on(&io)) return pool_put(id_hash, params_rev, s, max, np);
    uint8_t idx[MS_MAX_ENTRIES]; uint16_t val[MS_MAX_ENTRIES]; uint32_t n = 0;
    for (uint32_t k = 0; k < s->n && k < MS_MAX_ENTRIES; k++) if (s->idx[k] < np && s->val[k] <= max[s->idx[k]]) { idx[n] = s->idx[k]; val[n] = s->val[k]; n++; }
    pool_drop(id_hash, params_rev);                                    /* the file wins from now on: an old pool record must not come back after a removal */
    size_entry();
    mf_put(&io, id_hash, params_rev, s->preset, n, idx, val);          /* n = 0 removes the meter's record */
    return n;
}
int meter_settings_get(uint32_t id_hash, uint32_t params_rev, const uint16_t *max, uint32_t np, meter_settings_t *out)
{
    mf_io_t io;
    if (!file_on(&io)) return pool_get(id_hash, params_rev, max, np, out);
    mf_scan_t sc; mf_scan(&io, &sc); mf_rec_t r; int rev_ok = 0;
    out->n = 0; out->preset = 0;
    if (mf_find(&io, &sc, id_hash, params_rev, &r, &rev_ok)) {
        if (!rev_ok) return 0;                                         /* another parameter revision: never applied (dropped at the meter's next write) */
        out->preset = r.preset; out->n = r.n > MS_MAX_ENTRIES ? MS_MAX_ENTRIES : r.n;
        for (uint32_t k = 0; k < out->n; k++) { out->idx[k] = r.idx[k]; out->val[k] = r.val[k]; }
        if (!fits(out, max, np)) { out->n = 0; out->preset = 0; return 0; }
        return 1;
    }
    /* Migration (spec 4.4): a tuning kept in the old pool moves into the file the first time its meter is looked up; from then on the file wins. */
    if (!pool_get(id_hash, params_rev, max, np, out)) return 0;
    uint8_t idx[MS_MAX_ENTRIES]; uint16_t val[MS_MAX_ENTRIES];
    for (uint32_t k = 0; k < out->n; k++) { idx[k] = out->idx[k]; val[k] = out->val[k]; }
    mf_put(&io, id_hash, params_rev, out->preset, out->n, idx, val);
    pool_drop(id_hash, params_rev);
    return 1;
}
int meter_settings_preset(uint32_t id_hash, uint32_t params_rev, uint32_t *preset)
{
    mf_io_t io;
    if (!file_on(&io)) return pool_preset(id_hash, params_rev, preset);
    mf_scan_t sc; mf_scan(&io, &sc); mf_rec_t r; int rev_ok = 0;
    if (mf_find(&io, &sc, id_hash, params_rev, &r, &rev_ok)) { if (!rev_ok) return 0; *preset = r.preset; return 1; }
    return pool_preset(id_hash, params_rev, preset);                   /* not migrated yet */
}
void meter_settings_drop(uint32_t id_hash, uint32_t params_rev)
{
    mf_io_t io;
    pool_drop(id_hash, params_rev);
    if (file_on(&io)) mf_put(&io, id_hash, params_rev, 0, 0, (const uint8_t *)0, (const uint16_t *)0);
}
