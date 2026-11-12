#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../cores/spectrum_core.h"
#include "../cores/audio_stats_core.h"
#include "../generated/meter_presets.h"
#include "../generated/meter_registry.h"
#include "../cores/metr_core.h"
#include "../cores/tune_core.h"
#include "../cores/meter_budget_core.h"
#include "../cores/pack_strike_core.h"
#include "../cores/helios_core.h"
#include "../cores/shipped_meter_ids.h"
#ifndef MTR_STATE
#ifdef P2_HOST
#define MTR_STATE                                                     /* a host build has no such section */
#else
#define MTR_STATE __attribute__((section(".psram_state")))      /* PSRAM state (word accesses, cleared by meters_init: the loader does not zero it): the loader's counters and the kept selection */
#endif
#endif
#ifdef P2_PACKS
#include "../cores/mpreset_host.h"                                   /* the preset files of the meter on screen (B-967) */
#endif

#define R_SCAN      0x800000E8u            /* B-267 beam position: bit 9 = present, bits 8:0 = the video line counter (helios.c) */
#define R_SPEC_IDX  0x800000DCu
#define R_SPEC_DATA 0x800000E0u
_Static_assert(MTR_BANDS == SPEC_BANDS, "the meter inputs carry one value per hardware band");

/* The module list is generated from the manifests (meter_registry.h, tools/player2/gen_meters_p2.py): a meter is its manifest plus one meter_<key>.c that provides these five functions and the live parameters. */
#define DECL(key, nm, id, lc, UC, np, cmp) extern void meter_##key##_tick(const mtr_in_t *in); extern void meter_##key##_preset(uint32_t); extern const char *meter_##key##_preset_name(uint32_t); extern uint16_t meter_##key##_mv[]; extern void meter_##key##_apply(void);
METER_LIST(DECL)
#define ROW(key, nm, id, lc, UC, np, cmp) { nm, meter_##key##_tick, np, meter_##key##_preset, meter_##key##_preset_name, meter_##key##_mv, mpd_##lc, MPD_##UC##_N, meter_##key##_apply, cmp },
const meter_def_t meter_table[] = { METER_LIST(ROW) };
uint32_t meter_count = METER_N;                                     /* the rows in use: the TOC rows (at most METER_SLOTS until P2.4), or the built-in one with no bin (rows may be absent: meters_present) */
/* Rows METER_N.. belong to pack_only meters (manifest p2.pack_only): they exist only when their pack is installed AND its descriptor (tools/player2/meter_desc.py, the last section of the pack image) checks out. The
 * descriptor stays in the PSRAM slot; only the ONE meter being edited has its Configure descriptors copied to hot RAM (dyn_pd). The dynamic code is compiled only into a PACKS build. */
#define DYN_ROWS METER_SLOTS                                         /* per-row facts of the TOC rows (a row is its TOC position; rows beyond METER_SLOTS are read and checked but cannot be selected before P2.4's registry) */
_Static_assert(METER_N == 1u, "the built-in fallback meter is Winamp Bars alone: the window loader treats row fb_row as built-in and every other row as a pack");
#ifdef P2_PACKS
static void packs_reset(void);
#else
static void packs_reset(void) { }
#endif
static const meter_def_t *mdef(uint32_t m);                         /* the row of meter m (a pack-only row builds its Configure descriptors on first use) */
static uint32_t mid_of(uint32_t m);                                  /* the meter's manifest id (tau-assets.bin METR, the reports) */
static uint32_t cmd_limit_of(uint32_t m), cyc_limit_of(uint32_t m);
static uint32_t def_npre(uint32_t m);
static void def_set_pre(uint32_t m, uint32_t p);
static const char *def_pre_name(uint32_t m, uint32_t p);
static void def_apply(uint32_t m);

#ifdef P2_PACKS
#include "meter_pack.h"
#include "../cores/meterbin_core.h"
static ps_t win_strike;                                             /* the loaded pack's overruns of its cycle limit (cores/pack_strike_core.h); a swap is a new start */
static uint32_t win_row = 0xFFu;                                    /* the row whose pack is in the window and running (0xFF none) */
static mpk_info_t win_info;                                         /* where it runs: entry, .data, .bss, pstate */
static uint8_t fb_row;                                              /* the built-in fallback row: the TOC row flagged as built-in (row 0 with no bin) */
static int dyn_register(uint32_t m);                                /* the loaded row's Configure descriptors, from the descriptor its pack ends with (below) */
static volatile uint8_t *pack_window_data(void);
#include "../cores/meter_desc_core.h"
/* Diagnostics (Info and Report rows MTOC and MWIN: registered features): all in PSRAM, word accesses, cleared by meters_init (the loader does not zero .psram_state). */
MTR_STATE uint32_t mtoc_state, mtoc_count, mtoc_err, mtoc_bad_row, mtoc_src, mtoc_cyc;
MTR_STATE uint32_t mwin_loads, mwin_last_cyc, mwin_max_cyc, mwin_last_hash, mwin_err, mwin_strikes, mwin_refused;
MTR_STATE static uint32_t toc_pv_off, toc_packs_off, toc_total, toc_coll[2];
static uint32_t pack_act = 0xFFu;                                         /* the row whose .data and .bss are in the scratch */
#define DYN_MAX_P 40u                                          /* parameters a pack meter may have (the numeric part of its Configure descriptors is copied to hot RAM while it is loaded; the label and choice strings are read from the window when a row is drawn: meters_param_label / meters_param_choice) */
#define MTR_NO_MID 0xFFFFu                                     /* a meter that is not one of the shipped seven has no manifest id: no METR section and no preset folder of its own until P2.4 */
typedef struct { uint32_t cmds, cyc, cmp, hash, off, len, pv_off; uint16_t mid, np, npre, rev, pv_len; uint8_t ok, code; char name[MD_MAX_STR + 1u]; } dyn_t;
static dyn_t dyn[DYN_ROWS];                                    /* per row, from its TOC row; off, len and cmp are valid only for the row in the window */
static meter_def_t act_def, idle_def;                          /* the loaded row's definition; and what any other row answers (its name and parameter count: the rest is not loaded) */
static uint16_t act_mv[DYN_MAX_P];                             /* the loaded meter's live parameters (the pack reads them at every tick) */
static mpd_t dyn_pd[DYN_MAX_P];
static int window_load(uint32_t m);
#endif
#ifndef P2_PACKS
#define fb_row 0u                                                     /* no loadable meters in this build: row 0 is the built-in one */
#define win_row 0xFFu
#endif
static uint32_t cur, win_seen = 0xFFFFFFFFu, frame, last_t;
static uint16_t bx, by, bw, bh;                                        /* the box on screen (16-bit: hot RAM is at its floor) */
static uint8_t force_next;
static uint8_t lvl[MTR_BANDS];
static as_onset_t onset_st;
/* The hardware statistics block: latch the window since the last picture (it starts a new one), read the sums, reduce them (audio_stats_core.h). Scaled like the peaks (3/4 of headroom). */
static int stats_take(uint16_t *rl, uint16_t *rr, int16_t *corr)
{
    REG_WR(R_STATS_CTL, 1u);
    uint32_t w[8];
    for (uint32_t i = 0; i < 8u; i++) { REG_WR(R_STATS_IDX, i); w[i] = REG_RD(R_STATS_DATA); }
    const uint32_t n = w[1];
    meter_stats.stats_win++; meter_stats.stats_n = n;
    if (w[0] & 2u) { meter_stats.stats_ovf++; return 0; }                      /* the window outgrew the sums (the meters were not drawn for a while: a menu, a pause of the UI): its numbers are wrong, so this picture has none */
    const uint64_t ll = ((uint64_t)(w[3] & 0xFFFFu) << 32) | w[2], rr2 = ((uint64_t)(w[5] & 0xFFFFu) << 32) | w[4];
    const int64_t lr = (int64_t)((uint64_t)(uint32_t)((int32_t)(w[7] << 16) >> 16) << 32 | w[6]);                  /* 48 bits, sign-extended */
    *rl = (uint16_t)((as_rms(ll, n) * 3u) / 4u); *rr = (uint16_t)((as_rms(rr2, n) * 3u) / 4u);
    *corr = (int16_t)as_corr_q8(ll, rr2, lr);
    if (n) { meter_stats.stats_rms = (uint16_t)(((uint32_t)*rl + *rr) / 2u); meter_stats.stats_corr = *corr; }
    return n != 0u;
}
static mb_t budget[METER_SLOTS];                                          /* each meter's own degrade level, from the draw commands it issues (cores/meter_budget_core.h) */
extern uint32_t mp3play_paused_now(void);          /* mp3play.c: 1 when nothing is being heard */
extern uint32_t mp3play_degrade_now(void);         /* mp3play.c: the meter degrade level 0..3 (governor_core.h gov_degrade) */

static uint8_t pre[METER_SLOTS], custom[METER_SLOTS];                                  /* the selected preset of each meter */
/* User presets (the METR section of tau-assets.bin) replace a meter's built-in ones. The section is copied to PSRAM at boot (the tag buffer it arrives in is reused); a preset is read back a word at a time
 * (the window takes word accesses only) into a small buffer and applied through the same metr_core.h that the host test checks against the Python writer. */
#ifndef METR_PSRAM
#define METR_PSRAM ((volatile uint32_t *)(uintptr_t)0xA4550000u)
#endif
#define METR_MAX_BYTES 0xF000u                                      /* the PSRAM region 0xA4550000..0xA4560000 less a margin (the Layered Wave ring starts at 0xA4560000) */
static metr_e_t ue[METER_SLOTS]; static uint8_t user_n[METER_SLOTS];
static metr_p_t sh[40];                                            /* the parameter shapes of ONE meter, from its descriptors, rebuilt when needed (all of them at once cost 2,560 B of hot RAM) */
static uint8_t is_wide(const mpd_t *p) { return p->type == 3u || (p->type == 0u && p->max > 255u); }
static const metr_p_t *shapes(uint32_t m) { const meter_def_t *d = mdef(m); for (uint32_t i = 0; i < d->np && i < 40u; i++) { sh[i].min = d->pd[i].min; sh[i].max = d->pd[i].max; sh[i].kind = d->pd[i].type; sh[i].enum_n = d->pd[i].nval; sh[i].wide = is_wide(&d->pd[i]); } return sh; }
#define TUNE_SLOTS 2u                                   /* the tuning pool: two slots of TUNE_WORDS words, one tuned meter each; the one saved least recently is evicted */
static uint32_t tune_cache[TUNE_SLOTS][TUNE_WORDS];
static uint32_t tune_k[TUNE_SLOTS] = { TUNE_KEY_NONE, TUNE_KEY_NONE };           /* the key of the tuning in each slot (tune_key: the low 16 bits of the meter's id hash and the low 4 bits of its params_rev): a tuning follows its meter, not a table position */
static uint8_t tune_newer;
MTR_STATE static uint32_t kept_hash;                                              /* the saved selection that could not be honoured at boot (bin missing, meter removed, pack refused): written back unchanged until the user picks a meter */
static void tune_clear_slot(uint32_t s) { for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[s][k] = 0; tune_k[s] = TUNE_KEY_NONE; }
static uint32_t row_key(uint32_t m);
static void tune_drop(uint32_t m) { const uint32_t key = row_key(m); for (uint32_t s = 0; s < TUNE_SLOTS; s++) if (tune_k[s] == key) tune_clear_slot(s); }       /* any tuning of meter m */
uint32_t meters_npre(uint32_t m) { return m >= meter_count || !meters_present(m) ? 1u : user_n[m] ? user_n[m] : def_npre(m); }
#ifndef P2_PACKS
#define ext_on(m) 0
#define ext_id1(p) 0u
void meters_ext_enter(uint32_t m) { (void)m; }                      /* no loadable meters in this build: nothing to switch over */
#endif
static uint32_t anchor_of(uint32_t m);
static uint8_t want_id[METER_SLOTS];                               /* the preset FILE chosen for each meter: 0 none, else id + 1 (persisted, SF_MPID*) */
#ifdef P2_PACKS
static uint8_t ext_m = 0xFFu, ext_prev, ext_seen;
#define ext_on(m) ((m) == ext_m)
#define ext_id1(p) mph_id1(p)                          /* the meter whose preset files are in use (its index is cached), the user_n it had before, which meters have been switched over */
static int ext_get(uint32_t m, uint32_t p, uint16_t *vals, char *name);
#endif
static int user_preset(uint32_t m, uint32_t p, uint16_t *vals, char *name)       /* reads preset p of meter m from the PSRAM copy (or, for the meter whose preset files are in use, from them); 0 when it cannot be read now */
{
#ifdef P2_PACKS
    if (m == ext_m) return ext_get(m, p, vals, name);
#endif
    uint8_t buf[96]; metr_e_t e = ue[m]; const uint32_t at = e.off + p * e.psize, w0 = at & ~3u, w1 = (at + e.psize + 3u) & ~3u;
    for (uint32_t w = w0; w < w1; w += 4u) { const uint32_t v = METR_PSRAM[w >> 2]; for (uint32_t k = 0; k < 4u && w - w0 + k < sizeof buf; k++) buf[w - w0 + k] = (uint8_t)(v >> (8u * k)); }
    e.off = at - w0; e.psize = 0u;
    if (name) metr_name(buf, &e, 0u, name);
    if (vals) metr_values(buf, &e, 0u, shapes(m), mdef(m)->np, vals);
    return 1;
}
void meters_init(void) {
    packs_reset();
    kept_hash = 0u; tune_clear_slot(0u); tune_clear_slot(1u); tune_newer = 0u;
    cur = 0; meter_count = METER_N;
    for (uint32_t i = 0; i < METER_SLOTS; i++) { mb_reset(&budget[i]); pre[i] = 0; custom[i] = 0; user_n[i] = 0; want_id[i] = 0; if (i < METER_N) meter_table[i].set_pre(0u); } }
uint32_t meters_metr_load(const uint8_t *src, uint32_t n)       /* returns the number of meters whose presets were replaced; the section is validated first */
{
    if (n > METR_MAX_BYTES || metr_check(src, n)) return 0;
    uint32_t done = 0;
    for (uint32_t m = 0; m < meter_count; m++) {
        metr_e_t e;
        if (!meters_present(m) || m != fb_row || !metr_find(src, n, mid_of(m), shapes(m), mdef(m)->np, &e)) continue;      /* the built-in meter only: a pack meter is not loaded at boot (its METR presets wait for P2.4) */
        ue[m] = e; user_n[m] = e.npre; done++;
    }
    if (!done) return 0;
    for (uint32_t w = 0; w < (n + 3u) / 4u; w++) METR_PSRAM[w] = (uint32_t)src[4u * w] | ((w * 4u + 1u < n ? (uint32_t)src[4u * w + 1u] : 0u) << 8) | ((w * 4u + 2u < n ? (uint32_t)src[4u * w + 2u] : 0u) << 16) | ((w * 4u + 3u < n ? (uint32_t)src[4u * w + 3u] : 0u) << 24);
    for (uint32_t m = 0; m < meter_count; m++) if (user_n[m]) meters_preset_select(m, ue[m].def);
    return done;
}
uint32_t meters_preset_of(uint32_t m) { return m < METER_SLOTS ? pre[m] : 0u; }
void meters_preset_select(uint32_t m, uint32_t p)
{
    if (m >= METER_SLOTS) return;
    if (!meters_present(m)) { pre[m] = (uint8_t)(p & 15u); return; }                  /* a pack-only meter whose pack is not there now: its saved preset is kept as it is, not lost */
    if (p >= meters_npre(m)) p = 0u;
    if (user_n[m]) { uint16_t v[40]; if (!user_preset(m, p, v, 0)) return; const meter_def_t *d = mdef(m); for (uint32_t i = 0; i < d->np; i++) d->mv[i] = v[i]; def_apply(m); }      /* a preset file that cannot be read now (busy, damaged): the meter keeps what it has */
    else def_set_pre(m, p);
    pre[m] = (uint8_t)p; custom[m] = 0u; tune_drop(m);
    want_id[m] = ext_on(m) ? (uint8_t)ext_id1(p) : 0u;                               /* a preset file is remembered by its id, a pack's own preset by its position */
    if (m == cur) force_next = 1;
}
void meters_preset_next(void) { meters_preset_select(cur, (pre[cur] + 1u) % meters_npre(cur)); }
const char *meters_preset_name(void)
{
    static char nm[METR_NAME];
    if (custom[cur]) return "CUSTOM";
    if (user_n[cur]) { user_preset(cur, pre[cur], 0, nm); return nm; }
    return def_pre_name(cur, pre[cur]);
}
/* ---- saving a tuning (cores/tune_core.h): the parameters of ONE meter that differ from its preset, in four persisted words; two slots, keyed by the meter's id hash (low 16 bits) and params_rev (low 4 bits) ---- */
static uint16_t tm_max[40];
#ifdef P2_PACKS
static uint32_t row_hash(uint32_t m) { return mtoc_state == 1u ? dyn[m].hash : shipped_meter_hash[0]; }
static uint32_t row_key(uint32_t m) { return mtoc_state == 1u ? tune_key(dyn[m].hash, dyn[m].rev) : tune_key(shipped_meter_hash[0], shipped_meter_rev[0]); }
static int row_tunable(uint32_t m) { return mtoc_state != 1u || !(toc_coll[m >> 5] >> (m & 31u) & 1u); }       /* two rows sharing the low 16 bits of their hash would share a tuning key: such a meter runs from its defaults and saves no tuning (spec section 3) */
#else
static uint32_t row_hash(uint32_t m) { (void)m; return shipped_meter_hash[0]; }
static uint32_t row_key(uint32_t m) { (void)m; return tune_key(shipped_meter_hash[0], shipped_meter_rev[0]); }
static int row_tunable(uint32_t m) { (void)m; return 1; }
#endif
static const uint16_t *tune_max_of(uint32_t key, uint32_t *np)         /* the largest value of each parameter of the loaded meter that `key` names (the built-in fallback or the pack in the window); 0 for any other key: its tuning cannot be read now */
{
    uint32_t m = 0xFFu;
    if (row_key(fb_row) == key) m = fb_row; else if (win_row < meter_count && row_key(win_row) == key) m = win_row;
    if (m == 0xFFu || !row_tunable(m)) return 0;
    const meter_def_t *d = mdef(m); *np = d->np;
    for (uint32_t i = 0; i < d->np && i < 40u; i++) tm_max[i] = d->pd[i].type == 3u ? 65535u : d->pd[i].type == 2u ? 1u : d->pd[i].type == 1u ? (uint16_t)(d->pd[i].nval ? d->pd[i].nval - 1u : 0u) : d->pd[i].max;
    return tm_max;
}
static int row_loaded(uint32_t m) { return m < meter_count && (m == fb_row || m == win_row); }
void meters_tune_save(uint32_t m)            /* the parameters of meter m were edited (or reset): store what differs from its preset, replacing any tuning of another meter */
{
    if (!row_loaded(m) || !meters_present(m)) return;
    const meter_def_t *d = mdef(m);
    if (!custom[m] || !row_tunable(m)) { tune_drop(m); return; }
    uint16_t keep[40], base[40];
    for (uint32_t i = 0; i < d->np && i < 40u; i++) keep[i] = d->mv[i];
    if (user_n[m]) { if (!user_preset(m, pre[m], base, 0)) { tune_drop(m); return; } } else { def_set_pre(m, pre[m]); for (uint32_t i = 0; i < d->np && i < 40u; i++) base[i] = d->mv[i]; }
    for (uint32_t i = 0; i < d->np && i < 40u; i++) d->mv[i] = keep[i];
    def_apply(m);
    const uint32_t key = row_key(m); uint32_t np = 0; const uint16_t *mx = tune_max_of(key, &np);
    uint32_t w[TUNE_WORDS];
    if (mx && tune_pack(key, (uint32_t)anchor_of(m), keep, base, mx, np, w)) {
        uint32_t s = TUNE_SLOTS;                                        /* the slot of this meter, else an empty one, else the one saved least recently */
        for (uint32_t i = 0; i < TUNE_SLOTS; i++) if (tune_k[i] == key) s = i;
        for (uint32_t i = 0; i < TUNE_SLOTS && s == TUNE_SLOTS; i++) if (tune_k[i] == TUNE_KEY_NONE) s = i;
        if (s == TUNE_SLOTS) s = tune_newer ? 0u : 1u;
        for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[s][k] = w[k];
        tune_k[s] = key; tune_newer = (uint8_t)s;
    } else tune_drop(m);
}
uint32_t meters_payload(uint32_t m, uint8_t *out)                  /* the SR_T_METERCFG entry: manifest meter id, schema 1, preset (0xFF = tuned), nparams, then the values (two bytes for a wide one) */
{
    if (m >= meter_count || !meters_present(m)) return 0;
    const meter_def_t *d = mdef(m); uint32_t n = 0;
    out[n++] = (uint8_t)mid_of(m); out[n++] = 1u; out[n++] = custom[m] ? 0xFFu : pre[m]; out[n++] = d->np;
    for (uint32_t i = 0; i < d->np; i++) { out[n++] = (uint8_t)d->mv[i]; if (is_wide(&d->pd[i])) out[n++] = (uint8_t)(d->mv[i] >> 8); }
    return n;
}
void meters_tune_words(uint32_t *w) { for (uint32_t s = 0; s < TUNE_SLOTS; s++) for (uint32_t k = 0; k < TUNE_WORDS; k++) w[s * TUNE_WORDS + k] = tune_cache[s][k]; }
uint32_t meters_tune_newer(void) { return tune_newer; }
static uint32_t tune_words_key(const uint32_t *w)                    /* the key a persisted record names (check 8 bits, then the 20-bit key), TUNE_KEY_NONE for an empty slot */
{
    uint8_t b[16]; uint32_t pos = 8u, any = 0u;
    for (uint32_t k = 0; k < TUNE_WORDS; k++) any |= w[k];
    if (!any) return TUNE_KEY_NONE;
    tune_from_words(w, b); return tune_get(b, &pos, 20u);
}
static void tune_reapply(uint32_t m)                                  /* meter m was just loaded (or the saved words changed): apply the tuning stored for it, when it was made from the preset now selected */
{
    if (!row_loaded(m) || !row_tunable(m)) return;
    const uint32_t key = row_key(m);
    for (uint32_t s = 0; s < TUNE_SLOTS; s++) {
        if (tune_k[s] != key) continue;
        uint32_t mt = 0, pr = 0; uint8_t ix[TUNE_MAX]; uint16_t vl[TUNE_MAX];
        const uint32_t n = tune_unpack(tune_cache[s], tune_max_of, &mt, &pr, ix, vl);
        if (!n || mt != key || pr != anchor_of(m)) continue;
        const meter_def_t *d = mdef(m);
        for (uint32_t k = 0; k < n; k++) { uint32_t v = vl[k]; const mpd_t *p = &d->pd[ix[k]]; if (p->type == 0u && v < p->min) v = p->min; d->mv[ix[k]] = (uint16_t)v; }
        custom[m] = 1u; def_apply(m); if (m == cur) force_next = 1;
    }
}
void meters_tune_apply(const uint32_t *wp, uint32_t newer)    /* the persisted words changed (boot): keep them in the pool; the tuning of the meter on screen is applied when it belongs to the preset now selected, the other slot's meter gets its tuning when it is loaded */
{
    tune_newer = (uint8_t)(newer & 1u);
    for (uint32_t s = 0; s < TUNE_SLOTS; s++) {
        const uint32_t *w = &wp[s * TUNE_WORDS];
        int same = 1; for (uint32_t k = 0; k < TUNE_WORDS; k++) if (w[k] != tune_cache[s][k]) same = 0;
        if (same) continue;
        for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[s][k] = w[k];
        tune_k[s] = tune_words_key(w);
    }
    tune_reapply(cur);
}
/* ---- what is remembered of a preset (B-968): a preset FILE by its id (7 bits, SF_MPID*; the list can be reordered or replaced and the choice follows the id), a preset of the pack's own list by its position (SF_MPRE*) ---- */
static uint32_t anchor_of(uint32_t m) { return ext_on(m) ? (uint32_t)want_id[m] : (uint32_t)pre[m]; }      /* what a saved tuning is anchored to: the file's id + 1 (0 = the defaults) while the meter runs on files, else the position */
uint32_t meters_preset_pos_saved(uint32_t m) { return m < METER_SLOTS && !want_id[m] ? pre[m] : 0u; }
uint32_t meters_preset_id_saved(uint32_t m) { return m < METER_SLOTS ? want_id[m] : 0u; }
static void apply_id(uint32_t m)                                    /* the meter runs on files: its remembered id becomes the selected preset; an id that is no longer in the list is forgotten and the meter goes to its defaults */
{
#ifdef P2_PACKS
    int pos = want_id[m] ? mph_find(want_id[m]) : 0;
    if (pos < 0) { want_id[m] = 0; pos = 0; }
    meters_preset_select(m, (uint32_t)pos);
#else
    (void)m;
#endif
}
void meters_preset_restore(uint32_t m, uint32_t id1, uint32_t pos)
{
    if (m >= METER_SLOTS) return;
    if (id1) { want_id[m] = (uint8_t)id1; if (ext_on(m)) apply_id(m); return; }           /* when the files come into use (meters_ext_enter) the id is applied; until then the pack's own first preset runs */
    want_id[m] = 0;
    if (pos != meters_preset_of(m)) meters_preset_select(m, pos);
}
void meters_params_changed(uint32_t m) { if (m >= meter_count || !meters_present(m)) return; custom[m] = 1u; def_apply(m); if (m == cur) force_next = 1; meters_tune_save(m); }
uint32_t meters_custom(uint32_t m) { return m < METER_SLOTS ? custom[m] : 0u; }
uint32_t meters_cur(void) { return cur; }
const char *meters_name(void) { return mdef(cur)->name; }

void meters_enter(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    bx = x; by = y; bw = w; bh = h; frame = 0; force_next = 1;
    win_seen = caps.spec ? (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu : 0xFFFFFFFFu;      /* the window in progress is skipped: its mean mixes in what played before the meter was shown */
    for (uint32_t b = 0; b < MTR_BANDS; b++) lvl[b] = 0;
    gfx_rect(x, y, w, h, th_role[TR_BG_BOTTOM]);
    last_t = cycles();
}
#ifdef P2_PACKS
/* Selection is the ONLY place a pack is loaded (X, the Settings list, the boot restore): never inside a tick, between pictures, with one forced repaint; the audio stream is untouched. A refusal falls back to the built-in meter. */
static void activate_params(uint32_t m)                              /* a pack was just loaded: its parameters are the defaults, so put its remembered preset and tuning back */
{
    uint32_t p = pre[m]; if (p >= meters_npre(m)) p = 0u;
    if (user_n[m]) { uint16_t v[40]; if (user_preset(m, p, v, 0)) { const meter_def_t *d = mdef(m); for (uint32_t i = 0; i < d->np && i < 40u; i++) d->mv[i] = v[i]; } }
    else def_set_pre(m, p);
    custom[m] = 0u; pre[m] = (uint8_t)p;
    tune_reapply(m);
}
static void select_row(uint32_t r)
{
    uint32_t fresh = 0u;
    if (r >= meter_count || !meters_present(r)) r = fb_row;
    cur = r; force_next = 1;
    if (r != fb_row && r != win_row) {
        int e = window_load(r);
        if (!e) { e = dyn_register(r); if (e) { win_row = 0xFFu; pack_act = 0xFFu; } }
        if (e) { dyn[r].code = (uint8_t)e; mwin_err = (uint32_t)e; mwin_refused++; cur = fb_row; }          /* refused: the built-in Winamp Bars, the row marked with the reason */
        else { dyn[r].code = 0xFFu; fresh = 1u; }
    }
    meters_ext_enter(cur);
    if (fresh) activate_params(cur);
}
#else
static void select_row(uint32_t r) { cur = r < meter_count && meters_present(r) ? r : 0u; force_next = 1; meters_ext_enter(cur); }
#endif
void meters_select(uint32_t idx) { kept_hash = 0u; select_row(idx); }
void meters_next(int dir)
{
    uint32_t t = cur;
    kept_hash = 0u;
    for (uint32_t k = 0; k < METER_SLOTS; k++) { t = dir > 0 ? (t + 1u) % meter_count : (t + meter_count - 1u) % meter_count; if (meters_present(t)) break; }      /* skipping a row that is not listed */
    select_row(t);
}
/* The saved selection is the meter's id hash (31 bits, cores/settings_core.h SF_METER_HASH; 0 = Winamp Bars), not a row: installing, removing or reordering meters never changes which meter a card remembers. */
uint32_t meters_saved_hash(void) { return kept_hash ? kept_hash : row_hash(cur) & 0x7FFFFFFFu; }
uint32_t meters_hash_is_shown(void) { return kept_hash == 0u; }
int meters_select_hash(uint32_t h31)                                  /* the boot restore: 1 when the meter is there and runs, else Winamp Bars runs and the saved hash is kept to be written back */
{
    kept_hash = 0u;
    if (h31) for (uint32_t m = 0; m < meter_count; m++) if (meters_present(m) && (row_hash(m) & 0x7FFFFFFFu) == h31) { select_row(m); if (cur == m) return 1; kept_hash = h31; return 0; }
    select_row(fb_row);
    if (h31 && (row_hash(fb_row) & 0x7FFFFFFFu) != h31) kept_hash = h31;
    return h31 == 0u || kept_hash == 0u;
}

static int fetch(uint32_t *m)                        /* a new window, read coherently (the counter must not move while the 16 means are read) */
{
    uint32_t w0 = (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu;
    if (w0 == win_seen) return 0;
    for (uint32_t b = 0; b < MTR_BANDS; b++) { REG_WR(R_SPEC_IDX, b); m[b] = REG_RD(R_SPEC_DATA) & 0xFFFFFu; }
    if (((REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu) != w0) return 0;
    win_seen = w0;
    return 1;
}

#ifdef P2_PACKS
/* The meter bin and the window (docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2 and 3, plan P2.2). At boot the v2 bin (the user's tau-meters.bin, data slot 10, else the core's tau-packs.bin, slot 9) is read ONCE:
 * the header, the TOC and the previews go to PSRAM (cores/meterbin_core.h checks toc_crc and every row from that copy); no pack is read. A pack is read from the bin only when its meter is selected, into the ONE window
 * (slot 7, 64 KiB incl. pstate: the path P0 proved on a Pocket): header against its TOC row, image through the uncached alias with the CRC summed, fence.i, pstate zeroed, run under the watchdog. A refusal or a strike
 * falls back to the built-in Winamp Bars. The row of a meter is its TOC position (P2.4 rewrites the registry; rows past METER_SLOTS are read and checked but cannot be selected before that). */
#ifndef MTR_TAU_MM
#define MTR_TAU_MM 0x0008u                                              /* this firmware as major << 8 | minor, what a row's tau_min is compared with (raise it with the release that ships) */
#endif
#define MTOC_E_NOFALLBACK 20u                                            /* a firmware rule on top of the reader's codes: a sound bin must carry the built-in fallback row */
#ifndef MTR_TOC_BASE
#define MTR_TOC_BASE ((volatile uint32_t *)(uintptr_t)0xA4900000u)        /* the header and the TOC (file offsets 0..pv_off) at 0x24900000 through the data alias ... */
#define MTR_PV_BASE  ((volatile uint32_t *)(uintptr_t)0xA4910000u)        /* ... the previews (pv_off..packs_off) at 0x24910000: up to 16 KiB + 64 KiB before the window at 0x24970000 */
#endif
#ifndef MTR_FLUSH
#define MTR_FLUSH() __asm__ volatile (".word 0x0000100f" ::: "memory")     /* fence.i, as the P0 probe: VexRiscv decodes it as the instruction cache's flush */
#endif
static uint32_t pack_stats[MTR_PACK_STATS];
static mtr_host_api_t pack_api = { MTR_PACK_ABI, gfx_rect, th_role, 0, pack_stats, gfx_text, gfx_rect_stash, gfx_blend_ready, gfx_blend_on, gfx_blend_blit, gfx_blend_off, gfx_mb_write, gfx_mb_read, gfx_fence, gfx_blit, gfx_blit_abs, gfx_sblit, gfx_bar, gfx_scroll_down, gfx_clut_load, gfx_cblit, gfx_scroll_left };
/* The places that touch the Pocket's address space can be replaced by a host test (sim/p2_meter_window_harness.c): the window (written through the data alias, executed at the instruction alias), the scratch and the call. */
#ifndef PACK_WINDOW_DATA
#define PACK_WINDOW_DATA ((volatile uint8_t *)(uintptr_t)(MB_WINDOW | 0x80000000u))
#endif
#ifndef PACK_SCRATCH
#define PACK_SCRATCH ((uint8_t *)(uintptr_t)MTR_PACK_SCRATCH_ORG)
#endif
#ifndef PACK_CALL
#define PACK_CALL(e, in, api) ((mtr_pack_entry_fn)(uintptr_t)(e))(in, api)
#endif
static volatile uint8_t *pack_window_data(void) { return PACK_WINDOW_DATA; }
/* The PSRAM copy of header + TOC + previews as the reader sees it (a file offset below pv_off is in the first region, from there on in the second): word reads only. */
static int toc_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    (void)ctx;
    if ((uint64_t)off + n > toc_packs_off) return 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t a = off + i, w = a & ~3u;
        const volatile uint32_t *p = a < toc_pv_off ? MTR_TOC_BASE + (w >> 2) : MTR_PV_BASE + ((w - toc_pv_off) >> 2);
        dst[i] = (uint8_t)(*p >> (8u * (a & 3u)));
    }
    return 1;
}
/* A data-slot bin's length: the header names it (`total`); its last byte must exist. 0 when there is no bin or it is cut short. */
static uint32_t bin_len(lib_read_fn rd, void *ctx, uint8_t *win)       /* win already holds the header */
{
    if (lib_ld32(win) != MB_MAGIC) return 0u;
    const uint32_t total = lib_ld32(win + 28);
    return total > MB_HDR && total <= MB_MAX_BIN && rd(ctx, total - 1u, win + 64u, 1u) ? total : 0u;
}
static void toc_put(uint32_t off, const uint8_t *src, uint32_t n)        /* n a multiple of 4, off 4-aligned: the bytes become words of the PSRAM copy */
{
    for (uint32_t i = 0; i < n; i += 4u) {
        const uint32_t a = off + i, v = lib_ld32(src + i);
        if (a < toc_pv_off) MTR_TOC_BASE[a >> 2] = v; else MTR_PV_BASE[(a - toc_pv_off) >> 2] = v;
    }
}
static void dyn_row_from_toc(uint32_t k, const mb_row_t *r);
static uint32_t dyn_code(uint32_t m);
static mb_info_t toc_info;                                                 /* the accepted TOC (hot: 24 bytes) */
static uint32_t fb_find(const mb_info_t *in)                              /* the row flagged as the built-in fallback, or 0xFF */
{
    mb_row_t r;
    for (uint32_t i = 0; i < in->count; i++) if (mb_row(toc_rd, 0, in, i, &r) == MB_OK && (r.flags & MB_F_FALLBACK)) return i;
    return 0xFFu;
}
/* The boot path for one bin: copy header, TOC and previews into PSRAM, check them there. 0 = accepted and installed as the registry's rows, else the code (reader codes, or MTOC_E_NOFALLBACK). Nothing is installed on a refusal. */
static int toc_open(lib_read_fn rd, void *ctx, mb_info_t *in)          /* < 0: there is no file */
{
    uint8_t *win = (uint8_t *)PACK_SCRATCH;
    if (!rd(ctx, 0u, win, MB_HDR)) return -1;                              /* no file (or under 40 bytes): not an error, MTOC stays "none" */
    const uint32_t len = bin_len(rd, ctx, win);
    if (!len) return lib_ld32(win) == MB_MAGIC ? MB_E_SIZE : MB_E_MAGIC;
    if (!rd(ctx, 0u, win, MB_HDR)) return MB_E_READ;
    const uint32_t pv_off = lib_ld32(win + 16), pv_len = lib_ld32(win + 20), packs_off = lib_ld32(win + 24);
    if (lib_ld32(win) != MB_MAGIC) return MB_E_MAGIC;
    if (lib_ld16(win + 4) != 2u) return MB_E_VERSION;
    if (pv_off < MB_HDR || pv_off > MB_HDR + MB_TOC * MB_MAX_METERS || (pv_off & 3u) || pv_len > MTR_PACK_SLOT_SIZE || (uint64_t)pv_off + pv_len != packs_off || (packs_off & 3u) || packs_off > len) return MB_E_LAYOUT;
    toc_pv_off = pv_off; toc_packs_off = packs_off; toc_total = len;
    for (uint32_t off = 0; off < packs_off; ) {                           /* one pass, a kilobyte at a time (the slot driver's landing zone): the pieces never straddle pv_off in a word */
        uint32_t n = packs_off - off; if (n > 1024u) n = 1024u;
        if (off < pv_off && off + n > pv_off) n = pv_off - off;
        if (!rd(ctx, off, win, n)) return MB_E_READ;
        const uint32_t m = (n + 3u) & ~3u; for (uint32_t i = n; i < m; i++) win[i] = 0u;
        toc_put(off, win, m); off += n;
    }
    int e = mb_open_toc(toc_rd, 0, len, in);
    if (e) return e;
    if (fb_find(in) == 0xFFu) { in->bad_row = -1; return MTOC_E_NOFALLBACK; }
    return MB_OK;
}
static lib_read_fn bin_rd; static void *bin_ctx;                           /* the bin the rows came from: a pack is read from it at selection */
static void toc_install(const mb_info_t *in, lib_read_fn rd, void *ctx)
{
    bin_rd = rd; bin_ctx = ctx;
    uint32_t h[MB_MAX_METERS]; uint8_t fl[MB_MAX_METERS];
    toc_info = *in; mtoc_count = in->count; mtoc_state = 1u; mtoc_err = 0u;
    fb_row = (uint8_t)fb_find(in);
    mb_hashes(toc_rd, 0, in, h); mb_tune_collisions(h, in->count, fl);
    toc_coll[0] = toc_coll[1] = 0u; for (uint32_t i = 0; i < in->count; i++) if (fl[i]) toc_coll[i >> 5] |= 1u << (i & 31u);
    meter_count = in->count < METER_SLOTS ? in->count : METER_SLOTS;
    for (uint32_t i = 0; i < meter_count; i++) { mb_row_t r; if (mb_row(toc_rd, 0, in, i, &r) == MB_OK) dyn_row_from_toc(i, &r); }
}
int meters_packs_load_file(lib_read_fn rd, void *ctx)                   /* a data-slot bin (the caller tries the user's first, ctx 1, then the core's, ctx 2 or 0): 1 when it was sound and is now the registry, 0 when not (the error is in MTOC) */
{
    mb_info_t in; const uint32_t t0 = cycles();
    in.bad_row = -1;
    const int e = toc_open(rd, ctx, &in);
    if (e < 0) return 0;
    mtoc_cyc = cycles() - t0;
    if (e) { mtoc_err = (uint32_t)e; mtoc_bad_row = (uint32_t)(int32_t)in.bad_row; mtoc_state = 2u; return 0; }
    mtoc_src = ctx ? (uint32_t)(uintptr_t)ctx : 2u;
    toc_install(&in, rd, ctx);
    return 1;
}
uint32_t meters_pack_source(void) { return mtoc_state == 1u ? mtoc_src : 0u; }
uint32_t meters_pack_state(uint32_t m) { return m < meter_count && !(m == fb_row) ? (win_row == m ? 0u : dyn_code(m)) : 0xFFu; }     /* 0 = meter m is the pack in the window, 0xFF = none (or built-in), else its last refusal code */
void meters_pack_info(uint32_t *running, uint32_t *first_err, uint32_t *file_err, uint32_t *seen)     /* for Info: bit m of `running` = meter m is the pack in the window; the last load error (0 none); the bin's own result (0xFF: no bin); rows in the TOC */
{
    *running = win_row < 32u ? 1u << win_row : 0u; *first_err = mwin_err; *file_err = mtoc_state == 1u ? 0u : mtoc_state == 2u ? mtoc_err : 0xFFu; *seen = mtoc_count;
}
/* Loads the pack of row m into the window. 0 = it runs; else the refusal code (and the window holds nothing that may run). The audio stream is untouched; this is called at selection only, never inside a tick. */
#ifndef MTR_TEST_WINDOW_LOAD                                              /* a host test that has no bin of packs provides its own (sim/p2_dyn_register.h) */
static int window_load(uint32_t m)
{
    mb_row_t r; int e = MB_E_COUNT;
    win_row = 0xFFu; pack_act = 0xFFu;
    if (m >= toc_info.count || (e = mb_row(toc_rd, 0, &toc_info, m, &r)) != MB_OK) return e;
    if (mb_avail(&r, MTR_PACK_ABI, MTR_TAU_MM) != MB_U_OK) return MPK_E_ABI;
    const uint32_t t0 = cycles();
    mpkb_rd_t src = { bin_rd, bin_ctx, r.pack_off };
    e = mb_check_pack(bin_rd, bin_ctx, &r);
    if (e) return e;
    e = mpk_install(mpkb_rd, &src, PACK_SCRATCH, pack_window_data(), MB_WINDOW, MTR_PACK_SLOT_SIZE, MTR_PACK_SCRATCH_ORG, MTR_PACK_SCRATCH_SIZE, r.id_hash & 0xFFFFu, &win_info);
    if (e) return e;
    MTR_FLUSH();                                                           /* BEFORE anything runs: the instruction cache may hold the previous meter's code at these addresses */
    mpk_activate(&win_info, pack_window_data(), (volatile uint8_t *)PACK_SCRATCH);
    win_row = m; pack_act = m; ps_reset(&win_strike);
    mwin_loads++; mwin_last_hash = r.id_hash; mwin_last_cyc = cycles() - t0; if (mwin_last_cyc > mwin_max_cyc) mwin_max_cyc = mwin_last_cyc;
    return MPK_OK;
}
#endif
void meters_pack_probe_done(void) { pack_act = 0xFFu; force_next = 1; }          /* the P0 probe (if linked) used the scratch: the live pack's state is gone, activate it again */
static void packs_reset(void)
{
    mtoc_state = mtoc_count = mtoc_err = mtoc_bad_row = mtoc_src = mtoc_cyc = 0u; mwin_loads = mwin_last_cyc = mwin_max_cyc = mwin_last_hash = mwin_err = mwin_strikes = mwin_refused = 0u;
    toc_pv_off = toc_packs_off = toc_total = toc_coll[0] = toc_coll[1] = 0u;
    win_row = 0xFFu; pack_act = 0xFFu; fb_row = 0u; ps_reset(&win_strike); toc_info.count = 0u;
    for (uint32_t i = 0; i < DYN_ROWS; i++) { dyn[i].ok = 0u; dyn[i].code = 0xFFu; }
}
static int pack_ok(uint32_t m) { return m == win_row; }
static void pack_struck(uint32_t m)                                      /* the pack in the window hung or kept overrunning: it is out, the built-in meter takes over (its preset files come back at the next selection: no file reads inside a tick) */
{
    dyn[m].code = MPK_E_STRIKE; mwin_strikes++; mwin_err = MPK_E_STRIKE; win_row = 0xFFu; pack_act = 0xFFu; cur = fb_row; force_next = 1; meter_stats.struck++;
}
static void pack_tick(const void *vin)                                                   /* the shape wd_run wants (platform.h) */
{
    const mtr_in_t *in = (const mtr_in_t *)vin;
    if (pack_act != cur) { mpk_activate(&win_info, pack_window_data(), (volatile uint8_t *)PACK_SCRATCH); pack_act = cur; }
    pack_api.params = mdef(cur)->mv;
    PACK_CALL(win_info.entry, in, &pack_api);
}
#endif

/* ---- the TOC rows and the loaded meter's descriptors (descriptors from the pack in the window; B-812) -------------------------------------------------------------------------------------------------------------- */
#ifdef P2_PACKS
#include "../cores/meter_desc_core.h"
static void dyn_stub_tick(const mtr_in_t *in) { if (in->force) gfx_text(in->x + 8u, in->y + in->h / 2u - 8u, "PACK NOT RUNNING", UI_RED, in->bg); }
static uint32_t dyn_code(uint32_t m) { return dyn[m].code; }
static int desc_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)       /* the loaded pack's descriptor, straight out of the window, a word at a time */
{
    const dyn_t *d = (const dyn_t *)ctx;
    if (off > d->len || n > d->len - off) return 0;
    volatile uint8_t *base = pack_window_data();
    for (uint32_t i = 0; i < n; i++) { const uint32_t a = d->off + off + i; dst[i] = (uint8_t)(*(volatile uint32_t *)(base + (a & ~3u)) >> (8u * (a & 3u))); }
    return 1;
}
static void dyn_cache_build(uint32_t m)                           /* copy the numeric Configure descriptors of the loaded meter (the strings stay in the window: dyn_str) */
{
    md_hdr_t h;
    if (!md_header(desc_rd, &dyn[m], dyn[m].len, &h)) return;
    for (uint32_t i = 0; i < h.np && i < DYN_MAX_P; i++) {
        md_param_t p; md_param(desc_rd, &dyn[m], &h, i, &p);
        mpd_t *o = &dyn_pd[i];
        o->label = 0; o->type = p.type; o->hidden = p.hidden; o->min = p.min; o->max = p.max; o->step = p.step; o->nval = p.nval; o->when_idx = p.when_idx; o->when_val = p.when_val; o->vals = 0;
    }
}
/* One Configure string of the loaded meter m, read from the descriptor in the window into `buf` (n bytes, always terminated): the label of parameter `id` (choice < 0) or the name of its choice number `choice`. Another row, a
 * parameter or a choice that does not exist gives "". A page is a few thousand cycles of PSRAM reads, once, when a key is pressed. */
static const char *dyn_str(uint32_t m, uint32_t id, int32_t choice, char *buf, uint32_t n)
{
    buf[0] = 0;
    md_hdr_t h; md_param_t p; uint32_t so;
    if (m != win_row || !md_header(desc_rd, &dyn[m], dyn[m].len, &h) || id >= h.np) return buf;
    md_param(desc_rd, &dyn[m], &h, id, &p);
    so = p.label;
    if (choice >= 0 && !md_val_off(desc_rd, &dyn[m], &p, (uint32_t)choice, &so)) return buf;
    if (md_string(desc_rd, &dyn[m], &h, so, buf, n) < 0) buf[0] = 0;
    return buf;
}
static void dyn_row_from_toc(uint32_t k, const mb_row_t *r)       /* row k of the TOC becomes a registry row: its name, limits, parameter count and preview from the TOC alone (no pack is read) */
{
    dyn_t *d = &dyn[k];
    d->cmds = r->cmds; d->cyc = r->max_cycles; d->cmp = 0u; d->hash = r->id_hash; d->off = d->len = 0u; d->pv_off = r->preview_off; d->pv_len = (uint16_t)r->preview_len; d->rev = r->params_rev;
    d->np = r->nparams; d->npre = 1u; d->code = 0xFFu; d->mid = MTR_NO_MID;
    for (uint32_t i = 0; i < SHIPPED_METERS_N; i++) if (shipped_meter_hash[i] == r->id_hash) d->mid = (uint16_t)i;
    for (uint32_t i = 0; i < sizeof d->name; i++) d->name[i] = i < sizeof r->name ? r->name[i] : 0;
    d->name[MD_MAX_STR] = 0;
    d->ok = mb_avail(r, MTR_PACK_ABI, MTR_TAU_MM) == MB_U_OK;                       /* a row this firmware cannot run (another ABI, a newer TAU) is not listed (P2.5 greys it) */
}
/* The pack in the window is the meter of row m: its descriptor must be sound and say what the TOC row says. Fills the loaded row's definition. 0 = ok, else MPK_E_DESC / MPK_E_DESC_BIG. */
static int dyn_register(uint32_t m)
{
    dyn_t *d = &dyn[m]; const uint32_t size = win_info.size;
    if (size < 16u || (size & 3u)) return MPK_E_DESC;
    volatile uint32_t *t = (volatile uint32_t *)(pack_window_data() + size - 8u); const uint32_t len = t[0], mg = t[1];
    if (mg != MD_TRAILER || len < MD_HDR + 8u || len > size || len > MD_MAX_LEN || (len & 3u)) return MPK_E_DESC;
    d->off = size - len; d->len = len;
    md_hdr_t h;
    if (md_check(desc_rd, d, len, TR_COUNT, &h) || h.np != d->np) return MPK_E_DESC;
    if (h.np > DYN_MAX_P) return MPK_E_DESC_BIG;
    d->cmp = h.cmp;
    meter_def_t *r = &act_def;
    r->name = d->name; r->tick = dyn_stub_tick; r->npre = 1u; r->set_pre = 0; r->pre_name = 0; r->mv = act_mv; r->pd = dyn_pd; r->np = (uint8_t)h.np; r->apply = 0; r->cmp = h.cmp;
    for (uint32_t i = 0; i < DYN_MAX_P; i++) act_mv[i] = 0;
    dyn_cache_build(m);
    return 0;
}
static void dyn_set_pre(uint32_t m, uint32_t p)                      /* a pack meter has one preset in itself, DEFAULT: the default of every parameter (the descriptor carries no preset table, B-970) */
{
    (void)p; md_hdr_t h; md_param_t q;
    if (m != win_row || !md_header(desc_rd, &dyn[m], dyn[m].len, &h)) return;
    for (uint32_t i = 0; i < h.np && i < DYN_MAX_P; i++) if (md_param(desc_rd, &dyn[m], &h, i, &q)) act_mv[i] = q.def;
}
static const char *dyn_pre_name(uint32_t m, uint32_t p) { (void)m; (void)p; return "DEFAULT"; }
/* ---- the preset files (B-967; fw2/art/mpreset_host.c, formats in fw2/cores/mpreset_core.h) ----------------------------------------------------------------------------------------------------------------------------- */
#ifndef MPH_REAL                                                      /* a host test that does not run the preset files (the real ones are in fw2/art/mpreset_host.c): nothing found */
__attribute__((weak)) uint32_t mph_enter(uint32_t mid) { (void)mid; return 0u; }
__attribute__((weak)) void mph_name(uint32_t pos, char *out) { (void)pos; out[0] = 0; }
__attribute__((weak)) uint32_t mph_id1(uint32_t pos) { (void)pos; return 0u; }
__attribute__((weak)) int mph_find(uint32_t id1) { (void)id1; return -1; }
__attribute__((weak)) int mph_values(uint32_t mid, uint32_t pos, const mp_param_t *ps, uint32_t np, uint16_t *vals) { (void)mid; (void)pos; (void)ps; (void)np; (void)vals; return 0; }
#endif
#define KD_X(key, nm, id, lc, UC, np, cmp) mpk_##lc,
static const mpkd_t *const meter_keydef[METER_N] = { METER_LIST(KD_X) };      /* the key hashes and defaults of the built-in meters, generated with their descriptors */
static uint32_t ext_params(uint32_t m, mp_param_t *ps)               /* the meter's parameters as the preset reader needs them (key hash, range, default): from the descriptor of the pack in the window, from the generated tables for the built-in meter */
{
    if (m == fb_row) { const meter_def_t *d = mdef(m); uint32_t n = 0; for (; n < d->np && n < 40u; n++) { ps[n].key = meter_keydef[0][n].key; ps[n].min = d->pd[n].min; ps[n].max = d->pd[n].max; ps[n].def = meter_keydef[0][n].def; } return n; }
    md_hdr_t h; uint32_t n = 0;
    if (m != win_row || !md_header(desc_rd, &dyn[m], dyn[m].len, &h)) return 0u;
    for (uint32_t i = 0; i < h.np && i < 40u; i++) { md_param_t p; if (!md_param(desc_rd, &dyn[m], &h, i, &p)) break; ps[n].key = p.key; ps[n].min = p.min; ps[n].max = p.max; ps[n].def = p.def; n++; }
    return n;
}
static int ext_get(uint32_t m, uint32_t p, uint16_t *vals, char *name)
{
    if (name) mph_name(p, name);
    if (!vals) return 1;
    mp_param_t ps[40]; const uint32_t np = ext_params(m, ps);
    return np && mph_values(mid_of(m), p, ps, np, vals);
}
static void ext_drop(void) { if (ext_m != 0xFFu) { user_n[ext_m] = ext_prev; ext_m = 0xFFu; } }
void meters_ext_enter(uint32_t m)                                    /* meter m is now the one on screen: its preset files (when it has an index) become its list: [DEFAULT] + the index */
{
    if (m >= METER_SLOTS || !meters_present(m)) { ext_drop(); return; }
    if (m == ext_m) return;
    ext_drop();
    if (mid_of(m) == MTR_NO_MID) return;                              /* not one of the shipped seven: no preset folder before P2.4 (folders by id hash) */
    const uint32_t n = mph_enter(mid_of(m));
    if (!n) return;                                                  /* no files, damaged, or the governor is busy: the meter's one preset, DEFAULT, stays */
    ext_m = (uint8_t)m; ext_prev = user_n[m]; user_n[m] = (uint8_t)(n + 1u);
    if (!(ext_seen >> m & 1u)) {                                     /* the first time this meter is switched over: its remembered position now means a place in the new list; a tuned meter keeps what it is */
        ext_seen = (uint8_t)(ext_seen | (1u << m));
        if (!custom[m]) apply_id(m);                               /* its remembered file, or the defaults */
    }
}
int meters_present(uint32_t m) { return m < meter_count && (m == fb_row || dyn[m].ok); }
static const meter_def_t *mdef(uint32_t m)
{
    if (m == fb_row || m >= meter_count) return &meter_table[0];
    if (m == win_row) return &act_def;
    idle_def.name = dyn[m].name; idle_def.tick = dyn_stub_tick; idle_def.npre = 1u; idle_def.set_pre = 0; idle_def.pre_name = 0; idle_def.mv = act_mv; idle_def.pd = dyn_pd; idle_def.np = (uint8_t)dyn[m].np; idle_def.apply = 0; idle_def.cmp = 0u;
    return &idle_def;                                                /* a row that is not in the window: its name and parameter count (from the TOC); nothing else is loaded */
}
static uint32_t mid_of(uint32_t m) { return m == fb_row ? meter_manifest_id[0] : dyn[m].mid; }
static uint32_t cmd_limit_of(uint32_t m) { return m == fb_row ? meter_cmd_budget[0] : dyn[m].cmds; }
static uint32_t cyc_limit_of(uint32_t m) { return m == fb_row ? meter_cyc_limit[0] : dyn[m].cyc; }
static uint32_t def_npre(uint32_t m) { return m == fb_row ? meter_table[0].npre : 1u; }
static void def_set_pre(uint32_t m, uint32_t p) { if (m == fb_row) meter_table[0].set_pre(p); else dyn_set_pre(m, p); }
static const char *def_pre_name(uint32_t m, uint32_t p) { return m == fb_row ? meter_table[0].pre_name(p) : dyn_pre_name(m, p); }
static void def_apply(uint32_t m) { if (m == fb_row) meter_table[0].apply(); }              /* a pack reads its parameters from the host's table at every tick */
uint32_t meters_thumb_len(uint32_t m) { return m < meter_count && m != fb_row && dyn[m].ok && dyn[m].pv_len > MB_PAL ? dyn[m].pv_len - MB_PAL : 0u; }       /* a pack meter's preview, from the TOC's preview block (palette 64 B, then the run-length stream) */
int meters_thumb_rle(uint32_t m, uint32_t i, uint8_t *v) { return i < meters_thumb_len(m) && toc_rd(0, dyn[m].pv_off + MB_PAL + i, v, 1u); }
int meters_thumb_col(uint32_t m, uint32_t c, uint16_t *fixed, uint8_t *a, uint8_t *b, uint8_t *t, uint8_t *n)       /* ... and palette entry c (the layout of generated/meter_thumbs.h mth_col_t) */
{
    uint8_t e[8]; if (!meters_thumb_len(m) || c >= 8u || !toc_rd(0, dyn[m].pv_off + 8u * c, e, 8u)) return 0;
    *fixed = (uint16_t)((uint32_t)e[0] | ((uint32_t)e[1] << 8)); *a = e[2]; *b = e[3]; *t = e[4]; *n = e[5]; return 1;
}
#else
uint32_t meters_thumb_len(uint32_t m) { (void)m; return 0u; }
int meters_thumb_rle(uint32_t m, uint32_t i, uint8_t *v) { (void)m; (void)i; (void)v; return 0; }
int meters_thumb_col(uint32_t m, uint32_t c, uint16_t *fixed, uint8_t *a, uint8_t *b, uint8_t *t, uint8_t *n) { (void)m; (void)c; (void)fixed; (void)a; (void)b; (void)t; (void)n; return 0; }
int meters_present(uint32_t m) { return m < METER_N; }
static const meter_def_t *mdef(uint32_t m) { return &meter_table[m < METER_N ? m : 0u]; }
static uint32_t mid_of(uint32_t m) { return meter_manifest_id[m]; }
static uint32_t cmd_limit_of(uint32_t m) { return meter_cmd_budget[m]; }
static uint32_t cyc_limit_of(uint32_t m) { return meter_cyc_limit[m]; }
static uint32_t def_npre(uint32_t m) { return meter_table[m].npre; }
static void def_set_pre(uint32_t m, uint32_t p) { meter_table[m].set_pre(p); }
static const char *def_pre_name(uint32_t m, uint32_t p) { return meter_table[m].pre_name(p); }
static void def_apply(uint32_t m) { meter_table[m].apply(); }
#endif
const meter_def_t *meters_def(uint32_t m) { return mdef(m); }
/* The strings of a Configure row (fw2/ui/configure_view.c): a built-in meter's are static; a pack-only meter's are read from its slot into `buf` (see dyn_str). */
const char *meters_param_label(uint32_t m, uint32_t id, char *buf, uint32_t n)
{
#ifdef P2_PACKS
    if (m != fb_row && m < METER_SLOTS) return dyn_str(m, id, -1, buf, n);
#endif
    (void)buf; (void)n; const meter_def_t *d = mdef(m); return id < d->np ? d->pd[id].label : "";
}
const char *meters_param_choice(uint32_t m, uint32_t id, uint32_t val, char *buf, uint32_t n)
{
#ifdef P2_PACKS
    if (m != fb_row && m < METER_SLOTS) return dyn_str(m, id, (int32_t)val, buf, n);
#endif
    (void)buf; (void)n; const meter_def_t *d = mdef(m); return id < d->np && d->pd[id].vals && val < d->pd[id].nval ? d->pd[id].vals[val] : "";
}
uint32_t meters_list_count(void) { uint32_t n = 0; for (uint32_t m = 0; m < METER_SLOTS; m++) n += (uint32_t)meters_present(m); return n; }
uint32_t meters_list_row(uint32_t i) { for (uint32_t m = 0; m < METER_SLOTS; m++) if (meters_present(m) && i-- == 0u) return m; return 0u; }       /* the i-th meter that is there -> its row (the saved meter index) */
uint32_t meters_list_index(uint32_t row) { uint32_t n = 0; for (uint32_t m = 0; m < row && m < METER_SLOTS; m++) n += (uint32_t)meters_present(m); return n; }       /* the other way */
meter_stats_t meter_stats;
/* The per-meter cost measurement (Talos 2 T4 gate, cores/meter_cost_core.h) lives in the Diagnostic Build's fw2/diagnostics/info_counters.c, so this module's hot RAM ceiling (tools/player2/meter_budget.py) does not
 * carry it; these two hooks are no-ops (weak) wherever that file is not linked (the normal build, the host tests). */
__attribute__((weak)) void meter_cost_mark(void) { }
__attribute__((weak)) void meter_cost_note(uint32_t meter, uint32_t cmds) { (void)meter; (void)cmds; }
/* cmp: the compose code of the registry (tools/player2/gen_meters_p2.py cmp_code). A gate is bit 15, the gating parameter in bits 14..10 and a mask of the values that enable it in bits 9..6. */
static int cmp_gate(const uint16_t *mv, uint32_t c) { if (!(c & 0x8000u)) return 1; const uint32_t v = mv[(c >> 10) & 31u]; return v < 4u && ((c >> (6u + v)) & 1u); }
static int composed(const meter_def_t *d) { const uint32_t c = d->cmp & 0xFFFFu; return c == 1u || (c > 1u && cmp_gate(d->mv, c)); }
static uint32_t shed_win = 0xFFFFFFFFu, degrade_win;                  /* the last window counted as shed */
uint32_t meters_self_degrade(uint32_t m) { return m < METER_SLOTS ? budget[m].level : 0u; }
_Static_assert(METER_SLOTS <= 8u, "meter_stats_t.max_cyc holds eight meters: widen it with the registry");
void meters_tick(int allowed)
{
    if (!caps.spec || !bw) return;
    if (!allowed) {                                                                   /* the window stays pending (drawn when the UI is allowed again); it is counted once */
        const uint32_t w0 = (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu;
        if (w0 != win_seen && w0 != shed_win) { meter_stats.shed++; shed_win = w0; }
        return;
    }
    uint32_t m[MTR_BANDS];
    if (!fetch(m)) return;
    for (uint32_t b = 0; b < MTR_BANDS; b++) lvl[b] = (uint8_t)spec_ballistics(lvl[b], spec_level(m[b], b));
    /* Standard degrade (spec 9.2, the user's rule: the meters always give way to the audio): at level 1 and 2 only every 2nd / 4th window is drawn (the ballistics above still run every window, the wave peaks keep
     * accumulating, the elapsed time adds up), whatever the meter; a forced repaint is never skipped. At level 3 the governor has already turned the meters off (meters_tick(0)). */
    const uint32_t dg = mb_effective(mp3play_degrade_now(), budget[cur].level);      /* the governor's level or the meter's own (it drew more commands than its ceiling), whichever is worse */
    if (dg && dg < 3u && !force_next) { degrade_win++; if (degrade_win & ((1u << dg) - 1u)) { meter_stats.degraded++; return; } }
    const uint32_t t = cycles(), dt = (t - last_t) / (CLK_HZ / 1000u); last_t = t;
    uint32_t pl = 0, pr = 0;
    if (caps.wave) { const uint32_t pk = REG_RD(R_WAVE_PK); pl = ((pk & 0xFFFFu) * 3u) / 4u; pr = ((pk >> 16) * 3u) / 4u; REG_WR(R_WAVE_CTL, 1u); }      /* peaks since the last tick, then cleared */
    mtr_in_t in = { lvl, frame++, (uint16_t)(dt > 200u ? 200u : dt), (uint16_t)bx, (uint16_t)by, (uint16_t)bw, (uint16_t)bh, th_role[TR_BG_BOTTOM], (uint8_t)force_next, (uint8_t)mp3play_paused_now(), pl, pr, (uint8_t)dg };
    /* ABI 1.1 statistics. Nothing is accumulated while paused (the block is still drained so a resume starts a fresh window). */
    if (caps.stats && stats_take(&in.rms_l, &in.rms_r, &in.corr)) in.stats_ok = 1u; else in.rms_l = in.rms_r = 0u, in.corr = 0;
    if (in.paused) { in.rms_l = in.rms_r = 0u; in.corr = 0; }
    in.centroid = in.paused ? 0u : (uint8_t)as_centroid(lvl);
    as_onset_step(&onset_st, in.paused ? (const uint8_t[MTR_BANDS]){0} : lvl, (uint32_t)(t / (CLK_HZ / 1000u)));
    in.flux = onset_st.flux; in.onset = onset_st.onset;
    { const uint32_t pk = pl > pr ? pl : pr, rm = in.rms_l > in.rms_r ? in.rms_l : in.rms_r; in.crest = (uint8_t)(in.stats_ok ? as_crest_q4(pk, rm) : 0u); }
    force_next = 0;
    const int comp = composed(mdef(cur)) && gfx_compose_begin(bx, by, bw, bh);
    const uint32_t cmd0 = gfx_cmds, scan0 = REG_RD(R_SCAN);
    meter_cost_mark();
    gfx_touch_reset();
#ifdef P2_PACKS
    if (pack_ok(cur)) {
        const uint32_t tp = cycles();
        const int cut = wd_run(wd_limit(cyc_limit_of(cur)), pack_tick, &in);         /* the hardware watchdog cuts an endless or far too long tick off (a bitstream without it just runs the tick) */
        const uint32_t took = cycles() - tp;
        if (cut) pack_struck(cur);                                                    /* struck out at once: Winamp Bars from the next picture, after a full repaint */
        else if (!in.force) {                                                         /* a forced repaint is the costliest picture by design: not judged */
            if (took > cyc_limit_of(cur)) meter_stats.overruns++;
            if (ps_judge(&win_strike, took, cyc_limit_of(cur))) pack_struck(cur);       /* three strikes: out */
        }
    } else
#endif
    mdef(cur)->tick(&in);
    /* Beam exposure (docs/features/meters/METER_FRAMEWORK_FINDINGS.md finding 7): a meter that draws straight onto the shown buffer (not composed, not inside a view's idle-buffer frame) is seen mid-picture when the beam was inside the rows
     * it wrote (helios_core.h, the rule every region uses). Only counted: whether to hold such ticks back is the owner's call once the Pocket shows how often it happens. */
    const uint32_t rows = gfx_touch_rows();
    meter_stats.rows = rows;
    if (rows) {
        if (!comp && !in.force && gfx_cur_buf() == gfx_front_buf()) { meter_stats.direct++; if (!hl_rows_safe((int)((scan0 >> 9) & 1u), scan0 & 0x1FFu, rows >> 16, rows & 0xFFFFu)) meter_stats.exposed++; }
    }
    meter_stats.cmds_last = gfx_cmds - cmd0;                                           /* every engine command the tick issued, built-in or pack */
    if (!in.force) { mb_judge(&budget[cur], meter_stats.cmds_last, cmd_limit_of(cur)); meter_stats.over += (uint32_t)mb_over(meter_stats.cmds_last, cmd_limit_of(cur)); }      /* a forced repaint is the costliest picture by design: not judged */
    if (meter_stats.cmds_last > meter_stats.cmds_max) meter_stats.cmds_max = meter_stats.cmds_last;
    meter_stats.compose_cyc = 0;
    if (!in.force) meter_cost_note(cur, meter_stats.cmds_last);                       /* before the compose copy: the tick's own cost */
    if (comp) { const uint32_t c0 = cycles(); gfx_compose_present(bx, by, bw, bh); meter_stats.compose_cyc = cycles() - c0; }
    const uint32_t spent = cycles() - t;
    meter_stats.ticks++; meter_stats.last_cyc = spent;
    if (spent > meter_stats.max_cyc[cur]) meter_stats.max_cyc[cur] = spent;
}
