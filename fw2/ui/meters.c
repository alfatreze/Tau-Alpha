#pragma GCC optimize ("Os")
#include "meter.h"
#include "meters_toc.h"
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
const meter_def_t meter_table[] = { METER_LIST(ROW) };              /* the built-in rows: Winamp Bars alone (D-M22); every other meter is a row of the TOC and a pack */
uint32_t meter_count = METER_N;                                     /* the rows in use: the TOC rows (at most 64), or the built-in one with no bin */
_Static_assert(METER_N == 1u, "the built-in fallback meter is Winamp Bars alone: the window loader treats row fb_row as built-in and every other row as a pack");
#define MTR_NO_MID 0xFFFFu                                          /* a meter that is not one of the shipped seven has no manifest id: no METR section and no decimal preset folder, only the folder named by its id */
#define MTR_ROWS 64u                                                /* TOC rows a bin may carry (meterbin_core.h MB_MAX_METERS) */
#define MTR_COMMIT_SECS 5u                                          /* the saved selection follows a newly chosen meter only after it has run cleanly for this long (the commit rule, safe boot). 5 s: three overruns (the strike limit) or a hang (cut by the watchdog within one tick) show within a second or two of drawing, a meter that fails on its first onset or first forced repaint shows within a few seconds more, and a person who picks a meter and quits at once is still unlikely to be inside the window */
#define MTR_NAME_LEN 24u                                            /* a TOC name field (1..23 characters and the NUL) */

/* ---- ONE active-meter record (external meters P2.4): the only per-meter hot state is that of the meter on screen. Everything else about a row is read from the TOC copy in PSRAM when it is needed; what is remembered per row
 * (the last refusal, which rows are selectable) lives in PSRAM, never in hot RAM. ---- */
typedef struct {
    uint32_t hash, cmds, cyc, off, len;                             /* the meter's id_hash, its draw-command ceiling and cycle limit (TOC row), where its descriptor sits in the window (a pack) */
    uint16_t mid, np, rev;                                          /* manifest id (METR, the old decimal preset folder; MTR_NO_MID), parameter count, params_rev (the tuning key) */
    uint8_t pre, custom, user_n, want_id, slot, ext;                /* the selected preset; edited since it was chosen; presets from METR or files (0: the meter's own); the preset FILE chosen (id + 1); the shipped slot 0..6 (7: another meter: the statistics row); 1 while its preset files are in use */
    mb_t budget;                                                    /* the meter's own degrade level, from the draw commands it issues (cores/meter_budget_core.h) */
    char name[MTR_NAME_LEN];                                        /* a pack meter's name (from its TOC row); the built-in one reads meter_table */
} act_t;
static act_t act;
static uint32_t slot_of_hash(uint32_t h) { for (uint32_t i = 0; i < SHIPPED_METERS_N; i++) if (shipped_meter_hash[i] == h) return i; return 7u; }
static uint32_t mid_of_slot(uint32_t s) { return s < SHIPPED_METERS_N ? shipped_meter_mid[s] : (uint32_t)MTR_NO_MID; }
/* The commit rule and the saved selection (PSRAM state, word accesses): see meters_select_row. */
MTR_STATE uint32_t msel_saved;
MTR_STATE static uint32_t keep_pos, keep_id, pend_on, pend_t0;      /* the 31-bit hash the register file holds; its preset while another meter is shown; a selection waiting for its clean run, and when it started */
MTR_STATE uint32_t msel_commits, msel_aborts, msel_clean_s, msel_pend_hash, msel_skipped, msel_notune, msel_cur_notune, msel_rows;    /* Info rows MSEL: commits, selections cut before they committed (hang, strike, refusal), seconds the selection has run cleanly, the hash waiting, rows skipped as unavailable, rows without tuning (key collision), whether the meter on screen has none, rows X can reach */
#ifdef P2_PACKS
static void packs_reset(void);
#else
static void packs_reset(void) { }
#endif
static const meter_def_t *mdef(uint32_t m);                         /* the meter of row m: the one on screen is complete; another row answers its name and parameter count only */
static uint32_t mid_of(uint32_t m);                                 /* the meter's manifest id (tau-assets.bin METR, the reports) */
static uint32_t cmd_limit_of(uint32_t m), cyc_limit_of(uint32_t m);
static uint32_t def_npre(void);
static void def_set_pre(uint32_t p);
static const char *def_pre_name(uint32_t p);
static void def_apply(void);
static uint32_t row_key(uint32_t m);
static int row_tunable(uint32_t m);
static void act_builtin(void);
static void ext_enter_cur(void);
static void metr_attach(void);
#ifdef P2_PACKS
static int row_hash_of(uint32_t m, uint32_t *h);
#endif

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
MTR_STATE uint32_t mwin_readfail, mwin_readfail_cyc, mwin_q_last, mwin_q_max, mwin_last_row;       /* card reads that failed (the load stops at the first), the cycles that failed read cost, the quiesce wait (last, worst), the last row refused */
extern void mp3play_io_quiesce(void);                                       /* fw2/transport/mp3play.c: lets the music stream's read in flight finish; EVERY blocking card read while music plays starts with it (cover, preset files): the slot driver and the 4 KB tag zone are shared */
#if defined(P2_HOST) && !defined(MTR_OWN_QUIESCE)
__attribute__((weak)) void mp3play_io_quiesce(void) { }                    /* a host test with no music stream (the window harness has its own) */
#endif
static void quiesce(void)
{
    const uint32_t t0 = cycles(); mp3play_io_quiesce(); const uint32_t d = cycles() - t0;
    mwin_q_last = d; if (d > mwin_q_max) mwin_q_max = d;
}
MTR_STATE static uint32_t toc_pv_off, toc_packs_off, toc_total, toc_coll[2], toc_sel[2], toc_una[2], row_code[MTR_ROWS];     /* the TOC copy's regions; the rows whose tuning key another row shares / that X and the list can reach / that this firmware cannot run; the last refusal of each row (0 none) */
static uint32_t pack_act = 0xFFu;                                         /* the row whose .data and .bss are in the scratch */
#define DYN_MAX_P 40u                                          /* parameters a pack meter may have (the numeric part of its Configure descriptors is copied to hot RAM while it is loaded; the label and choice strings are read from the window when a row is drawn: meters_param_label / meters_param_choice) */
static meter_def_t act_def, idle_def;                          /* the loaded row's definition; and what any other row answers (its name and parameter count: the rest is not loaded) */
static char idle_name[MTR_NAME_LEN];
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
extern uint32_t mp3play_paused_now(void);          /* mp3play.c: 1 when nothing is being heard */
extern uint32_t mp3play_degrade_now(void);         /* mp3play.c: the meter degrade level 0..3 (governor_core.h gov_degrade) */

/* User presets (the METR section of tau-assets.bin) replace a meter's built-in ones. The section is copied to PSRAM at boot (the tag buffer it arrives in is reused); when a meter is selected (the built-in one or a pack, right after its
 * pack is loaded and its descriptor registered) its entry is looked up by the meter's manifest id and a preset is read back a word at a time (the window takes word accesses only) into a small buffer, through the same metr_core.h
 * that the host test checks against the Python writer. */
#ifndef METR_PSRAM
#define METR_PSRAM ((volatile uint32_t *)(uintptr_t)0xA4550000u)
#endif
#define METR_MAX_BYTES 0xF000u                                      /* the PSRAM region 0xA4550000..0xA4560000 less a margin (the Layered Wave ring starts at 0xA4560000) */
MTR_STATE static uint32_t metr_n;                                   /* the bytes of the METR section in PSRAM (0 none) */
static metr_e_t ue;                                                 /* the selected meter's entry in it (valid when act.user_n and not act.ext) */
static metr_p_t sh[40];                                            /* the parameter shapes of the selected meter, from its descriptors, rebuilt when needed (all of them at once cost 2,560 B of hot RAM) */
static uint8_t is_wide(const mpd_t *p) { return p->type == 3u || (p->type == 0u && p->max > 255u); }
static const metr_p_t *shapes(uint32_t m) { const meter_def_t *d = mdef(m); for (uint32_t i = 0; i < d->np && i < 40u; i++) { sh[i].min = d->pd[i].min; sh[i].max = d->pd[i].max; sh[i].kind = d->pd[i].type; sh[i].enum_n = d->pd[i].nval; sh[i].wide = is_wide(&d->pd[i]); } return sh; }
#define TUNE_SLOTS 2u                                   /* the tuning pool: two slots of TUNE_WORDS words, one tuned meter each; the one saved least recently is evicted */
static uint32_t tune_cache[TUNE_SLOTS][TUNE_WORDS];
static uint32_t tune_k[TUNE_SLOTS] = { TUNE_KEY_NONE, TUNE_KEY_NONE };           /* the key of the tuning in each slot (tune_key: the low 16 bits of the meter's id hash and the low 4 bits of its params_rev): a tuning follows its meter, not a table position */
static uint8_t tune_newer;
static void tune_clear_slot(uint32_t s) { for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[s][k] = 0; tune_k[s] = TUNE_KEY_NONE; }
static void tune_drop(uint32_t m) { const uint32_t key = row_key(m); for (uint32_t s = 0; s < TUNE_SLOTS; s++) if (tune_k[s] == key) tune_clear_slot(s); }       /* any tuning of meter m */
uint32_t meters_npre(uint32_t m) { return m != cur ? 1u : act.user_n ? act.user_n : def_npre(); }
#ifndef P2_PACKS
#define ext_id1(p) 0u
#endif
static uint32_t anchor_of(void) { return act.ext ? (uint32_t)act.want_id : (uint32_t)act.pre; }      /* what a saved tuning is anchored to: the file's id + 1 (0 = the defaults) while the meter runs on files, else the position */
#ifdef P2_PACKS
#define ext_id1(p) mph_id1(p)
static int ext_get(uint32_t p, uint16_t *vals, char *name);
#endif
static uint32_t metr_b(uint32_t off) { return (METR_PSRAM[off >> 2] >> (8u * (off & 3u))) & 0xFFu; }
static int user_preset(uint32_t p, uint16_t *vals, char *name)       /* reads preset p of the selected meter from its preset files (or the PSRAM copy of METR); 0 when it cannot be read now */
{
#ifdef P2_PACKS
    if (act.ext) return ext_get(p, vals, name);
#endif
    uint8_t buf[96]; metr_e_t e = ue; const uint32_t at = e.off + p * e.psize, w0 = at & ~3u, w1 = (at + e.psize + 3u) & ~3u;
    for (uint32_t w = w0; w < w1; w += 4u) { const uint32_t v = METR_PSRAM[w >> 2]; for (uint32_t k = 0; k < 4u && w - w0 + k < sizeof buf; k++) buf[w - w0 + k] = (uint8_t)(v >> (8u * k)); }
    e.off = at - w0; e.psize = 0u;
    if (name) metr_name(buf, &e, 0u, name);
    if (vals) metr_values(buf, &e, 0u, shapes(cur), mdef(cur)->np, vals);
    return 1;
}
static void metr_attach(void)                                      /* the METR entry of the meter just selected (its descriptors are registered): its presets replace the meter's own, its boot preset is the first one */
{
    act.user_n = 0;
    if (!metr_n || act.mid == MTR_NO_MID) return;
    metr_e_t e;
    if (!metr_find_rd(metr_b, metr_n, act.mid, shapes(cur), mdef(cur)->np, &e)) return;
    ue = e; act.user_n = e.npre; act.pre = e.def;
}
void meters_init(void)
{
    packs_reset();
    tune_clear_slot(0u); tune_clear_slot(1u); tune_newer = 0u;
    msel_saved = shipped_meter_hash[0] & 0x7FFFFFFFu; keep_pos = keep_id = 0u; pend_on = pend_t0 = 0u; metr_n = 0u;
    msel_commits = msel_aborts = msel_clean_s = msel_pend_hash = msel_skipped = msel_notune = msel_cur_notune = msel_rows = 0u;
    cur = 0; meter_count = METER_N;
    act_builtin(); meter_table[0].set_pre(0u);
}
uint32_t meters_metr_load(const uint8_t *src, uint32_t n)       /* the METR section of tau-assets.bin: validated, copied to PSRAM (a pack is not loaded at boot: each meter looks its entry up when it is selected); returns how many entries belong to a meter of the TOC (by manifest id) */
{
    if (n > METR_MAX_BYTES || metr_check(src, n)) return 0;
    for (uint32_t w = 0; w < (n + 3u) / 4u; w++) METR_PSRAM[w] = (uint32_t)src[4u * w] | ((w * 4u + 1u < n ? (uint32_t)src[4u * w + 1u] : 0u) << 8) | ((w * 4u + 2u < n ? (uint32_t)src[4u * w + 2u] : 0u) << 16) | ((w * 4u + 3u < n ? (uint32_t)src[4u * w + 3u] : 0u) << 24);
    metr_n = n;
    uint32_t done = 0, pos = 14u; const uint32_t count = (uint32_t)src[12] | ((uint32_t)src[13] << 8);
    for (uint32_t k = 0; k < count && pos + 2u <= n; k++) {
        const uint32_t len = (uint32_t)src[pos] | ((uint32_t)src[pos + 1u] << 8), at = pos + 2u;
        if (len < 7u || at + len > n) break;
        pos = at + len;
#ifdef P2_PACKS
        for (uint32_t r = 0; r < meter_count; r++) { uint32_t h; if (row_hash_of(r, &h) && mid_of_slot(slot_of_hash(h)) == src[at]) { done++; break; } }
#else
        if (src[at] == meter_manifest_id[0]) done++;
#endif
    }
    if (meters_present(cur)) { metr_attach(); if (act.user_n) meters_preset_select(cur, act.pre); }        /* the meter already on screen (a test, or a core that loads METR late) */
    return done;
}
uint32_t meters_preset_of(uint32_t m) { return m == cur ? act.pre : 0u; }
void meters_preset_select(uint32_t m, uint32_t p)
{
    if (m != cur || !meters_present(m)) return;                                       /* only the meter on screen is loaded: another meter starts from its first preset when it is selected */
    if (p >= meters_npre(m)) p = 0u;
    if (act.user_n) { uint16_t v[40]; if (!user_preset(p, v, 0)) return; const meter_def_t *d = mdef(m); for (uint32_t i = 0; i < d->np; i++) d->mv[i] = v[i]; def_apply(); }      /* a preset file that cannot be read now (busy, damaged): the meter keeps what it has */
    else def_set_pre(p);
    act.pre = (uint8_t)p; act.custom = 0u; tune_drop(m);
    act.want_id = act.ext ? (uint8_t)ext_id1(p) : 0u;                                 /* a preset file is remembered by its id, a pack's own preset by its position */
    force_next = 1;
}
void meters_preset_next(void) { meters_preset_select(cur, (act.pre + 1u) % meters_npre(cur)); }
const char *meters_preset_name(void)
{
    static char nm[METR_NAME];
    if (act.custom) return "CUSTOM";
    if (act.user_n) { user_preset(act.pre, 0, nm); return nm; }
    return def_pre_name(act.pre);
}
/* ---- saving a tuning (cores/tune_core.h): the parameters of ONE meter that differ from its preset, in four persisted words; two slots, keyed by the meter's id hash (low 16 bits) and params_rev (low 4 bits) ---- */
static uint16_t tm_max[40];
static const uint16_t *tune_max_of(uint32_t key, uint32_t *np)         /* the largest value of each parameter of the meter on screen when `key` names it; 0 for any other key: its tuning cannot be read now */
{
    if (!meters_present(cur) || row_key(cur) != key || !row_tunable(cur)) return 0;
    const meter_def_t *d = mdef(cur); *np = d->np;
    for (uint32_t i = 0; i < d->np && i < 40u; i++) tm_max[i] = d->pd[i].type == 3u ? 65535u : d->pd[i].type == 2u ? 1u : d->pd[i].type == 1u ? (uint16_t)(d->pd[i].nval ? d->pd[i].nval - 1u : 0u) : d->pd[i].max;
    return tm_max;
}
const uint16_t *meters_shipped_max_of(uint32_t idx, uint32_t *np)       /* the parameter maxima of a shipped meter by its layout-1 table index: lets the one-time migration of a layout-1 card keep its tunings (settings_init_t); 0 for any other index */
{
    if (idx >= SHIPPED_TMAX_N) return 0;
    *np = shipped_tmax_np[idx]; return &shipped_tmax[shipped_tmax_off[idx]];
}
void meters_tune_save(uint32_t m)            /* the parameters of the meter on screen were edited (or reset): store what differs from its preset, replacing any tuning of another meter */
{
    if (m != cur || !meters_present(m)) return;
    const meter_def_t *d = mdef(m);
    if (!act.custom || !row_tunable(m)) { tune_drop(m); return; }
    uint16_t keep[40], base[40];
    for (uint32_t i = 0; i < d->np && i < 40u; i++) keep[i] = d->mv[i];
    if (act.user_n) { if (!user_preset(act.pre, base, 0)) { tune_drop(m); return; } } else { def_set_pre(act.pre); for (uint32_t i = 0; i < d->np && i < 40u; i++) base[i] = d->mv[i]; }
    for (uint32_t i = 0; i < d->np && i < 40u; i++) d->mv[i] = keep[i];
    def_apply();
    const uint32_t key = row_key(m); uint32_t np = 0; const uint16_t *mx = tune_max_of(key, &np);
    uint32_t w[TUNE_WORDS];
    if (mx && tune_pack(key, anchor_of(), keep, base, mx, np, w)) {
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
    if (m != cur || !meters_present(m)) return 0;
    const meter_def_t *d = mdef(m); uint32_t n = 0;
    out[n++] = (uint8_t)mid_of(m); out[n++] = 1u; out[n++] = act.custom ? 0xFFu : act.pre; out[n++] = d->np;
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
static uint32_t tune_words_anchor(const uint32_t *w)                 /* the preset a persisted tuning started from (the file's id + 1, else the position) */
{
    uint8_t b[16]; uint32_t pos = 28u;
    tune_from_words(w, b); return tune_get(b, &pos, 7u);
}
static void tune_reapply(void)                                        /* the meter on screen was just loaded (or the saved words changed): apply the tuning stored for it, when it was made from the preset now selected */
{
    if (!meters_present(cur) || !row_tunable(cur)) return;
    const uint32_t key = row_key(cur);
    for (uint32_t s = 0; s < TUNE_SLOTS; s++) {
        if (tune_k[s] != key) continue;
        uint32_t mt = 0, pr = 0; uint8_t ix[TUNE_MAX]; uint16_t vl[TUNE_MAX];
        const uint32_t n = tune_unpack(tune_cache[s], tune_max_of, &mt, &pr, ix, vl);
        if (!n || mt != key || pr != anchor_of()) continue;
        const meter_def_t *d = mdef(cur);
        for (uint32_t k = 0; k < n; k++) { uint32_t v = vl[k]; const mpd_t *p = &d->pd[ix[k]]; if (p->type == 0u && v < p->min) v = p->min; d->mv[ix[k]] = (uint16_t)v; }
        act.custom = 1u; def_apply(); force_next = 1;
    }
}
void meters_tune_apply(const uint32_t *wp, uint32_t newer)    /* the persisted words changed (boot): keep them in the pool; the tuning of the meter on screen is applied when it belongs to the preset now selected, the other slot's meter gets its tuning when it is selected */
{
    tune_newer = (uint8_t)(newer & 1u);
    for (uint32_t s = 0; s < TUNE_SLOTS; s++) {
        const uint32_t *w = &wp[s * TUNE_WORDS];
        int same = 1; for (uint32_t k = 0; k < TUNE_WORDS; k++) if (w[k] != tune_cache[s][k]) same = 0;
        if (same) continue;
        for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[s][k] = w[k];
        tune_k[s] = tune_words_key(w);
    }
    tune_reapply();
}
/* ---- what is remembered of a preset (B-968): a preset FILE by its id (7 bits, SF_METER_PRE_ID; the list can be reordered or replaced and the choice follows the id), a preset of the pack's own list by its position (SF_METER_PRE_POS) ---- */
uint32_t meters_preset_pos_saved(uint32_t m) { return m == cur && !act.want_id ? act.pre : 0u; }
uint32_t meters_preset_id_saved(uint32_t m) { return m == cur ? act.want_id : 0u; }
static void apply_id(void)                                          /* the meter runs on files: its remembered id becomes the selected preset; an id that is no longer in the list is forgotten and the meter goes to its defaults */
{
#ifdef P2_PACKS
    int pos = act.want_id ? mph_find(act.want_id) : 0;
    if (pos < 0) { act.want_id = 0; pos = 0; }
    meters_preset_select(cur, (uint32_t)pos);
#endif
}
void meters_preset_restore(uint32_t m, uint32_t id1, uint32_t pos)
{
    if (m != cur) return;
    if (id1) { act.want_id = (uint8_t)id1; if (act.ext) apply_id(); return; }             /* the files are in use already (they were entered at selection): the id is applied; else the pack's own first preset runs */
    act.want_id = 0;
    if (pos != act.pre) meters_preset_select(m, pos);
}
void meters_params_changed(uint32_t m) { if (m != cur || !meters_present(m)) return; act.custom = 1u; def_apply(); force_next = 1; meters_tune_save(m); }
uint32_t meters_custom(uint32_t m) { return m == cur ? act.custom : 0u; }
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
/* ---- the saved selection and the commit rule (safe boot, spec section 2 item 4) ----
 * The register file word the Pocket saves on Quit holds the id_hash of the COMMITTED meter. A meter the user selects is committed only after it has run cleanly for MTR_COMMIT_SECS (a drawn tick at least that long after the
 * selection, with no strike, refusal or watchdog cut in between); the built-in Winamp Bars cannot hang, so choosing it commits at once. A meter cut by the watchdog, struck out or refused inside the window never reaches
 * the saved word: the next boot starts on the last clean meter. Selecting the meter that is already committed changes nothing. A newly installed meter (a row never selected) is never selected by itself: the saved
 * selection is a hash, not a position. What this does NOT cover: a hang that resets the core (nothing survives a reset, and the Pocket saves persist values on Quit only, so such a hang plus a power cycle never persists). */
static uint32_t hash31_of_row_hash(uint32_t h) { return h & 0x7FFFFFFFu; }
static uint32_t cur_hash31(void) { return hash31_of_row_hash(act.hash); }
static int shown_is_saved(void) { return cur_hash31() == msel_saved; }
static void commit_now(void) { msel_saved = cur_hash31(); if (pend_on) msel_commits++; pend_on = 0u; msel_pend_hash = 0u; }
static void pend_start(void) { pend_on = 1u; pend_t0 = cycles(); msel_pend_hash = cur_hash31(); msel_clean_s = 0u; }
static void pend_clear(void) { pend_on = 0u; msel_pend_hash = 0u; }                  /* the selection waiting for its clean run is replaced by another choice */
static void pend_drop(void) { if (pend_on) msel_aborts++; pend_clear(); }              /* ... or cut: a strike or a hang */
uint32_t meters_saved_hash(void) { return msel_saved; }                       /* what the register file holds: the committed meter, or the saved one that could not be honoured at boot (kept until the user picks another) */
uint32_t meters_hash_is_shown(void) { return !pend_on && shown_is_saved(); }   /* 1 when the saved meter is the one on screen: its preset is live; else the kept snapshot is saved */
uint32_t meters_saved_pos(void) { return meters_hash_is_shown() ? meters_preset_pos_saved(cur) : keep_pos; }
uint32_t meters_saved_id(void) { return meters_hash_is_shown() ? meters_preset_id_saved(cur) : keep_id; }
void meters_keep_preset(uint32_t pos, uint32_t id1) { keep_pos = pos; keep_id = id1; }      /* the boot: the saved preset of a saved meter that is not on screen (removed, refused) is written back unchanged */
static void keep_capture(void) { if (shown_is_saved()) { keep_pos = act.want_id ? 0u : act.pre; keep_id = act.want_id; } }      /* the committed meter is about to leave the screen: its preset stays what is saved until another meter commits */

#ifdef P2_PACKS
/* The meter bin and the window (docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2 and 3, plan P2.2 and P2.4). At boot the v2 bin (the user's tau-meters.bin, data slot 10, else the core's tau-packs.bin, slot 9) is read ONCE:
 * the header, the TOC and the previews go to PSRAM (cores/meterbin_core.h checks toc_crc and every row from that copy); no pack is read. A row is read from that copy (256 bytes) whenever its name, flags or limits are needed.
 * A pack is read from the bin only when its meter is selected, into the ONE window (slot 7, 64 KiB incl. pstate: the path P0 proved on a Pocket): header against its TOC row, image through the uncached alias with the CRC
 * summed, fence.i, pstate zeroed, run under the watchdog. A refusal or a strike falls back to the built-in Winamp Bars. */
#ifndef MTR_TAU_MM                                                     /* this firmware as major << 8 | minor, what a row's tau_min is compared with: generated by fw2/build.sh from dist/Cores/alfatreze.TAU/core.json (tools/tau_mm.py), never edited by hand */
#ifdef P2_HOST
#define MTR_TAU_MM 0x0008u
#else
#error "MTR_TAU_MM is passed by fw2/build.sh (tools/tau_mm.py)"
#endif
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
static mb_info_t toc_info;                                                 /* the accepted TOC (hot: 24 bytes) */
static int toc_row(uint32_t m, mb_row_t *r) { return mtoc_state == 1u && mb_row_fast(toc_rd, 0, &toc_info, m, r) == MB_OK; }       /* row m of the accepted TOC (one 256-byte read of the PSRAM copy, no validation: the TOC was checked at boot) */
static int row_hash_of(uint32_t m, uint32_t *h)                            /* the id_hash of row m (four bytes) */
{
    uint8_t b[4];
    if (mtoc_state != 1u || m >= toc_info.count || !toc_rd(0, MB_HDR + MB_TOC * m, b, 4u)) return 0;
    *h = mb_ld32(b); return 1;
}
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
    for (uint32_t off = 0; off < packs_off; ) {                           /* one pass, 4 KB at a time (the slot driver's landing zone): the pieces never straddle pv_off in a word */
        uint32_t n = packs_off - off; if (n > 4096u) n = 4096u;
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
/* The registry of the accepted TOC: which rows X and the list can reach (visible and runnable; the fallback row always), which this firmware cannot run (another ABI, a newer TAU: skipped and marked), which share their tuning key with another row. Bits in PSRAM, nothing per row in hot RAM. */
static void toc_index(const mb_info_t *in)
{
    uint32_t h[MTR_ROWS]; uint8_t fl[MTR_ROWS]; mb_row_t r;
    toc_info = *in; mtoc_count = in->count; mtoc_state = 1u; mtoc_err = 0u;
    toc_sel[0] = toc_sel[1] = toc_una[0] = toc_una[1] = 0u; msel_skipped = 0u; fb_row = 0xFFu;
    for (uint32_t i = 0; i < in->count; i++) {
        h[i] = 0u; row_code[i] = 0u;
        if (mb_row_fast(toc_rd, 0, in, i, &r) != MB_OK) continue;
        h[i] = r.id_hash;
        const int av = mb_avail(&r, MTR_PACK_ABI, MTR_TAU_MM) == MB_U_OK, fb = (r.flags & MB_F_FALLBACK) != 0u, vis = (r.flags & MB_F_VISIBLE) != 0u;
        if (fb && fb_row == 0xFFu) fb_row = (uint8_t)i;
        if (fb || (av && vis)) toc_sel[i >> 5] |= 1u << (i & 31u);
        if (!av && !fb) { toc_una[i >> 5] |= 1u << (i & 31u); if (vis) msel_skipped++; }
    }
    if (fb_row == 0xFFu) fb_row = 0u;
    mb_tune_collisions(h, in->count, fl);
    toc_coll[0] = toc_coll[1] = 0u; msel_notune = 0u; msel_rows = 0u;
    for (uint32_t i = 0; i < in->count; i++) { if (fl[i]) { toc_coll[i >> 5] |= 1u << (i & 31u); msel_notune++; } if (toc_sel[i >> 5] >> (i & 31u) & 1u) msel_rows++; }
    meter_count = in->count;
}
static void toc_install(const mb_info_t *in, lib_read_fn rd, void *ctx) { bin_rd = rd; bin_ctx = ctx; toc_index(in); }
int meters_packs_load_file(lib_read_fn rd, void *ctx)                   /* a data-slot bin (the caller tries the user's first, ctx 1, then the core's, ctx 2 or 0): 1 when it was sound and is now the registry, 0 when not (the error is in MTOC) */
{
    mb_info_t in; const uint32_t t0 = cycles();
    in.bad_row = -1;
    quiesce();                                                             /* no stream runs at boot, and this is cheap when it is idle; the same rule for every card read */
    const int e = toc_open(rd, ctx, &in);
    if (e < 0) return 0;
    mtoc_cyc = cycles() - t0;
    if (e) { mtoc_err = (uint32_t)e; mtoc_bad_row = (uint32_t)(int32_t)in.bad_row; mtoc_state = 2u; return 0; }
    mtoc_src = ctx ? (uint32_t)(uintptr_t)ctx : 2u;
    toc_install(&in, rd, ctx);
    return 1;
}
uint32_t meters_pack_source(void) { return mtoc_state == 1u ? mtoc_src : 0u; }
uint32_t meters_pack_state(uint32_t m) { return m < meter_count && m != fb_row ? (win_row == m ? 0u : row_code[m] ? row_code[m] : 0xFFu) : 0xFFu; }     /* 0 = meter m is the pack in the window, 0xFF = none (or built-in), else its last refusal code */
void meters_pack_info(uint32_t *running, uint32_t *first_err, uint32_t *file_err, uint32_t *seen)     /* for Info: bit m of `running` = meter m is the pack in the window (rows 0..31); the last load error (0 none); the bin's own result (0xFF: no bin); rows in the TOC */
{
    *running = win_row < 32u ? 1u << win_row : 0u; *first_err = mwin_err; *file_err = mtoc_state == 1u ? 0u : mtoc_state == 2u ? mtoc_err : 0xFFu; *seen = mtoc_count;
}
#define MWIN_E_READ 43u                                                    /* the card read failed (a timeout of the slot driver): the load stopped at the first failed chunk */
static int cnt_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)       /* the bin reader with the failures counted: the first failed read ends the load (mpk_install and mb_check_pack return at once), so timeouts never chain */
{
    (void)ctx; const uint32_t t0 = cycles();
    if (bin_rd(bin_ctx, off, dst, n)) return 1;
    mwin_readfail++; mwin_readfail_cyc = cycles() - t0;
    return 0;
}
/* Loads the pack of row m into the window. 0 = it runs; else the refusal code (and the window holds nothing that may run). The audio stream is untouched; this is called at selection only, never inside a tick. */
#ifndef MTR_TEST_WINDOW_LOAD                                              /* a host test that has no bin of packs provides its own (sim/p2_dyn_register.h) */
static int window_load(uint32_t m)
{
    mb_row_t r; int e = MB_E_COUNT;
    quiesce();                                                             /* BEFORE the first card read: the music stream's command in flight finishes first (the P0 probe did the same; without it a read collides with the stream: corrupt header, dropouts, a freeze) */
    win_row = 0xFFu; pack_act = 0xFFu;
    if (m >= toc_info.count || (e = mb_row_fast(toc_rd, 0, &toc_info, m, &r)) != MB_OK) return e;
    if (mb_avail(&r, MTR_PACK_ABI, MTR_TAU_MM) != MB_U_OK) return MPK_E_ABI;
    const uint32_t t0 = cycles();
    mpkb_rd_t src = { cnt_rd, 0, r.pack_off };
    const uint32_t rf0 = mwin_readfail;
    e = mb_check_pack(cnt_rd, 0, &r);
    if (mwin_readfail != rf0) return MWIN_E_READ;
    if (e) return e;
    e = mpk_install(mpkb_rd, &src, PACK_SCRATCH, pack_window_data(), MB_WINDOW, MTR_PACK_SLOT_SIZE, MTR_PACK_SCRATCH_ORG, MTR_PACK_SCRATCH_SIZE, r.id_hash & 0xFFFFu, &win_info);
    if (mwin_readfail != rf0) return MWIN_E_READ;                          /* a failed card read stopped the install at that chunk: it is a read failure, not a damaged pack */
    if (e) return e;
    MTR_FLUSH();                                                           /* BEFORE anything runs: the instruction cache may hold the previous meter's code at these addresses */
    mpk_activate(&win_info, pack_window_data(), (volatile uint8_t *)PACK_SCRATCH);
    win_row = m; pack_act = m;
    mwin_loads++; mwin_last_hash = r.id_hash; mwin_last_cyc = cycles() - t0; if (mwin_last_cyc > mwin_max_cyc) mwin_max_cyc = mwin_last_cyc;
    return MPK_OK;
}
#endif
void meters_pack_probe_done(void) { pack_act = 0xFFu; force_next = 1; }          /* the P0 probe (if linked) used the scratch: the live pack's state is gone, activate it again */
static uint32_t pv_row = 0xFFFFFFFFu, pv_off_c, pv_len_c;                      /* the preview block of the row the list drew last (its place in the TOC copy) */
static void packs_reset(void)
{
    mtoc_state = mtoc_count = mtoc_err = mtoc_bad_row = mtoc_src = mtoc_cyc = 0u; mwin_loads = mwin_last_cyc = mwin_max_cyc = mwin_last_hash = mwin_err = mwin_strikes = mwin_refused = 0u; mwin_readfail = mwin_readfail_cyc = mwin_q_last = mwin_q_max = mwin_last_row = 0u;
    toc_pv_off = toc_packs_off = toc_total = toc_coll[0] = toc_coll[1] = toc_una[0] = toc_una[1] = 0u; toc_sel[0] = 1u; toc_sel[1] = 0u;      /* no bin: row 0, Winamp Bars, is the one meter */
    win_row = 0xFFu; pack_act = 0xFFu; fb_row = 0u; ps_reset(&win_strike); toc_info.count = 0u; pv_row = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < MTR_ROWS; i++) row_code[i] = 0u;
}
static int pack_ok(uint32_t m) { return m == win_row; }
static void fallback_builtin(void)                                       /* the meter on screen failed (struck out): the built-in meter takes over on its own first preset; no card read here (this runs inside a tick): its preset files come back at the next selection */
{
    cur = fb_row; act_builtin(); metr_attach(); force_next = 1;
    if (act.user_n) { uint16_t v[40]; if (user_preset(act.pre, v, 0)) { const meter_def_t *d = mdef(cur); for (uint32_t i = 0; i < d->np; i++) d->mv[i] = v[i]; def_apply(); } } else def_set_pre(act.pre);
}
static void pack_struck(uint32_t m)                                      /* the pack in the window hung or kept overrunning: it is out, the built-in meter takes over */
{
    row_code[m] = MPK_E_STRIKE; mwin_strikes++; mwin_err = MPK_E_STRIKE; win_row = 0xFFu; pack_act = 0xFFu; meter_stats.struck++;
    pend_drop();                                                         /* a selection still waiting for its clean run never reaches the saved word */
    fallback_builtin();
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
static void dyn_stub_tick(const mtr_in_t *in) { if (in->force) gfx_text(in->x + 8u, in->y + in->h / 2u - 8u, "PACK NOT RUNNING", UI_RED, in->bg); }
static int desc_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)       /* the loaded pack's descriptor, straight out of the window, a word at a time */
{
    const act_t *d = (const act_t *)ctx;
    if (off > d->len || n > d->len - off) return 0;
    volatile uint8_t *base = pack_window_data();
    for (uint32_t i = 0; i < n; i++) { const uint32_t a = d->off + off + i; dst[i] = (uint8_t)(*(volatile uint32_t *)(base + (a & ~3u)) >> (8u * (a & 3u))); }
    return 1;
}
static void dyn_cache_build(void)                                 /* copy the numeric Configure descriptors of the loaded meter (the strings stay in the window: dyn_str) */
{
    md_hdr_t h;
    if (!md_header(desc_rd, &act, act.len, &h)) return;
    for (uint32_t i = 0; i < h.np && i < DYN_MAX_P; i++) {
        md_param_t p; md_param(desc_rd, &act, &h, i, &p);
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
    if (m != cur || m != win_row || !md_header(desc_rd, &act, act.len, &h) || id >= h.np) return buf;
    md_param(desc_rd, &act, &h, id, &p);
    so = p.label;
    if (choice >= 0 && !md_val_off(desc_rd, &act, &p, (uint32_t)choice, &so)) return buf;
    if (md_string(desc_rd, &act, &h, so, buf, n) < 0) buf[0] = 0;
    return buf;
}
static void act_from_row(const mb_row_t *r)                       /* the pack meter of this TOC row is about to become the meter on screen: its name, limits, parameter count and identity from the TOC alone (no pack is read) */
{
    act.hash = r->id_hash; act.cmds = r->cmds; act.cyc = r->max_cycles; act.off = act.len = 0u; act.np = r->nparams; act.rev = r->params_rev;
    act.slot = (uint8_t)slot_of_hash(r->id_hash); act.mid = (uint16_t)mid_of_slot(act.slot);
    act.pre = act.custom = act.user_n = act.want_id = act.ext = 0u; mb_reset(&act.budget);
    for (uint32_t i = 0; i < sizeof act.name; i++) act.name[i] = i < sizeof r->name ? r->name[i] : 0;
    act.name[MD_MAX_STR] = 0;
}
/* The pack in the window is the meter of act: its descriptor must be sound and say what the TOC row says. Fills the loaded row's definition. 0 = ok, else MPK_E_DESC / MPK_E_DESC_BIG. */
static int dyn_register(uint32_t m)
{
    (void)m; const uint32_t size = win_info.size;
    if (size < 16u || (size & 3u)) return MPK_E_DESC;
    volatile uint32_t *t = (volatile uint32_t *)(pack_window_data() + size - 8u); const uint32_t len = t[0], mg = t[1];
    if (mg != MD_TRAILER || len < MD_HDR + 8u || len > size || len > MD_MAX_LEN || (len & 3u)) return MPK_E_DESC;
    act.off = size - len; act.len = len;
    md_hdr_t h;
    if (md_check(desc_rd, &act, len, TR_COUNT, &h) || h.np != act.np) return MPK_E_DESC;
    if (h.np > DYN_MAX_P) return MPK_E_DESC_BIG;
    meter_def_t *r = &act_def;
    r->name = act.name; r->tick = dyn_stub_tick; r->npre = 1u; r->set_pre = 0; r->pre_name = 0; r->mv = act_mv; r->pd = dyn_pd; r->np = (uint8_t)h.np; r->apply = 0; r->cmp = h.cmp;
    for (uint32_t i = 0; i < DYN_MAX_P; i++) act_mv[i] = 0;
    dyn_cache_build();
    return 0;
}
static void dyn_set_pre(uint32_t p)                                  /* a pack meter has one preset in itself, DEFAULT: the default of every parameter (the descriptor carries no preset table, B-970) */
{
    (void)p; md_hdr_t h; md_param_t q;
    if (!md_header(desc_rd, &act, act.len, &h)) return;
    for (uint32_t i = 0; i < h.np && i < DYN_MAX_P; i++) if (md_param(desc_rd, &act, &h, i, &q)) act_mv[i] = q.def;
}
static const char *dyn_pre_name(uint32_t p) { (void)p; return "DEFAULT"; }
/* ---- the preset files (B-967; fw2/art/mpreset_host.c, formats in fw2/cores/mpreset_core.h): the folder is named by the meter's id (first 8 hex digits), the decimal manifest-index folder of older cards is the fallback for the shipped seven ------------------------ */
#ifndef MPH_REAL                                                      /* a host test that does not run the preset files (the real ones are in fw2/art/mpreset_host.c): nothing found */
__attribute__((weak)) uint32_t mph_enter(uint32_t hash, uint32_t mid) { (void)hash; (void)mid; return 0u; }
__attribute__((weak)) void mph_name(uint32_t pos, char *out) { (void)pos; out[0] = 0; }
__attribute__((weak)) uint32_t mph_id1(uint32_t pos) { (void)pos; return 0u; }
__attribute__((weak)) int mph_find(uint32_t id1) { (void)id1; return -1; }
__attribute__((weak)) int mph_values(uint32_t hash, uint32_t pos, const mp_param_t *ps, uint32_t np, uint16_t *vals) { (void)hash; (void)pos; (void)ps; (void)np; (void)vals; return 0; }
#endif
#define KD_X(key, nm, id, lc, UC, np, cmp) mpk_##lc,
static const mpkd_t *const meter_keydef[METER_N] = { METER_LIST(KD_X) };      /* the key hashes and defaults of the built-in meters, generated with their descriptors */
static uint32_t ext_params(mp_param_t *ps)               /* the meter on screen's parameters as the preset reader needs them (key hash, range, default): from the descriptor of the pack in the window, from the generated tables for the built-in meter */
{
    if (cur == fb_row) { const meter_def_t *d = mdef(cur); uint32_t n = 0; for (; n < d->np && n < 40u; n++) { ps[n].key = meter_keydef[0][n].key; ps[n].min = d->pd[n].min; ps[n].max = d->pd[n].max; ps[n].def = meter_keydef[0][n].def; } return n; }
    md_hdr_t h; uint32_t n = 0;
    if (cur != win_row || !md_header(desc_rd, &act, act.len, &h)) return 0u;
    for (uint32_t i = 0; i < h.np && i < 40u; i++) { md_param_t p; if (!md_param(desc_rd, &act, &h, i, &p)) break; ps[n].key = p.key; ps[n].min = p.min; ps[n].max = p.max; ps[n].def = p.def; n++; }
    return n;
}
static int ext_get(uint32_t p, uint16_t *vals, char *name)
{
    if (name) mph_name(p, name);
    if (!vals) return 1;
    mp_param_t ps[40]; const uint32_t np = ext_params(ps);
    return np && mph_values(act.hash, p, ps, np, vals);
}
static void ext_enter_cur(void)                                     /* the meter on screen has just been selected: its preset files (when it has a folder) become its list: [DEFAULT] + the index */
{
    act.ext = 0u;
    const uint32_t n = mph_enter(act.hash, act.mid);
    if (!n) return;                                                  /* no files, damaged, or the governor is busy: the meter's one preset, DEFAULT (or its METR presets), stays */
    act.ext = 1u; act.user_n = (uint8_t)(n + 1u);
}
void meters_ext_enter(uint32_t m) { if (m == cur && meters_present(m)) { ext_enter_cur(); } }
int meters_present(uint32_t m) { return m < meter_count && (m == fb_row || (toc_sel[m >> 5] >> (m & 31u) & 1u)); }
static const meter_def_t *mdef(uint32_t m)
{
    if (m == cur) return m == fb_row ? &meter_table[0] : &act_def;
    if (m == fb_row || m >= meter_count) return &meter_table[0];
    mb_row_t r; idle_def.name = idle_name; idle_def.np = 0u;
    for (uint32_t i = 0; i < sizeof idle_name; i++) idle_name[i] = 0;
    if (toc_row(m, &r)) { for (uint32_t i = 0; i < MD_MAX_STR; i++) idle_name[i] = i < sizeof r.name ? r.name[i] : 0; idle_def.np = (uint8_t)r.nparams; }
    idle_def.tick = dyn_stub_tick; idle_def.npre = 1u; idle_def.set_pre = 0; idle_def.pre_name = 0; idle_def.mv = act_mv; idle_def.pd = dyn_pd; idle_def.apply = 0; idle_def.cmp = 0u;
    return &idle_def;                                                /* a row that is not on screen: its name and parameter count (from the TOC); nothing else is loaded */
}
static uint32_t mid_of(uint32_t m) { (void)m; return act.mid; }
static uint32_t cmd_limit_of(uint32_t m) { (void)m; return act.cmds; }
static uint32_t cyc_limit_of(uint32_t m) { (void)m; return act.cyc; }
static uint32_t def_npre(void) { return cur == fb_row ? meter_table[0].npre : 1u; }
static void def_set_pre(uint32_t p) { if (cur == fb_row) meter_table[0].set_pre(p); else dyn_set_pre(p); }
static const char *def_pre_name(uint32_t p) { return cur == fb_row ? meter_table[0].pre_name(p) : dyn_pre_name(p); }
static void def_apply(void) { if (cur == fb_row) meter_table[0].apply(); }              /* a pack reads its parameters from the host's table at every tick */
static void act_builtin(void)                                     /* the built-in meter is the one on screen: its identity from the TOC row of the fallback (the generated tables when there is no bin) */
{
    mb_row_t r; act.hash = shipped_meter_hash[0]; act.rev = shipped_meter_rev[0];
    if (toc_row(fb_row, &r)) { act.hash = r.id_hash; act.rev = r.params_rev; }
    act.cmds = meter_cmd_budget[0]; act.cyc = meter_cyc_limit[0]; act.off = act.len = 0u; act.mid = meter_manifest_id[0]; act.np = meter_table[0].np;
    act.slot = (uint8_t)slot_of_hash(act.hash);
    act.pre = act.custom = act.user_n = act.want_id = act.ext = 0u; mb_reset(&act.budget); act.name[0] = 0;
}
static int meter_row_hash31(uint32_t m, uint32_t *h31) { uint32_t h; if (!row_hash_of(m, &h)) return 0; *h31 = hash31_of_row_hash(h); return 1; }
static uint32_t row_key(uint32_t m)                                 /* the tuning key of row m: the low 16 bits of its id_hash and the low 4 bits of its params_rev */
{
    if (m == cur) return tune_key(act.hash, act.rev);
    uint8_t b[2]; uint32_t h;
    if (!row_hash_of(m, &h) || !toc_rd(0, MB_HDR + MB_TOC * m + 80u, b, 2u)) return TUNE_KEY_NONE;
    return tune_key(h, mb_ld16(b));
}
static int row_tunable(uint32_t m) { return mtoc_state != 1u || !(toc_coll[m >> 5] >> (m & 31u) & 1u); }       /* two rows sharing the low 16 bits of their hash would share a tuning key: such a meter runs from its defaults and saves no tuning (spec section 3) */
uint32_t meters_thumb_len(uint32_t m)                                /* a pack meter's preview, from the TOC's preview block (palette 64 B, then the run-length stream) */
{
    if (m >= meter_count || m == fb_row || !meters_present(m)) return 0u;
    if (m != pv_row) { mb_row_t r; if (!toc_row(m, &r)) return 0u; pv_row = m; pv_off_c = r.preview_off; pv_len_c = r.preview_len; }
    return pv_len_c > MB_PAL ? pv_len_c - MB_PAL : 0u;
}
int meters_thumb_rle(uint32_t m, uint32_t i, uint8_t *v) { return i < meters_thumb_len(m) && toc_rd(0, pv_off_c + MB_PAL + i, v, 1u); }
int meters_thumb_col(uint32_t m, uint32_t c, uint16_t *fixed, uint8_t *a, uint8_t *b, uint8_t *t, uint8_t *n)       /* ... and palette entry c (the layout of generated/meter_thumbs.h mth_col_t) */
{
    uint8_t e[8]; if (!meters_thumb_len(m) || c >= 8u || !toc_rd(0, pv_off_c + 8u * c, e, 8u)) return 0;
    *fixed = (uint16_t)((uint32_t)e[0] | ((uint32_t)e[1] << 8)); *a = e[2]; *b = e[3]; *t = e[4]; *n = e[5]; return 1;
}
int meters_is_builtin(uint32_t m) { return m == fb_row && m < meter_count; }
uint32_t meters_row_flags(uint32_t m)                                /* what the list needs to know of a row (bits: meters_toc.h) */
{
    uint32_t f = 0u;
    if (m >= meter_count) return 0u;
    mb_row_t r; const int have = toc_row(m, &r);
    if (m == fb_row || (have && (r.flags & MB_F_VISIBLE))) f |= MROW_VISIBLE;
    if (toc_una[m >> 5] >> (m & 31u) & 1u) f |= MROW_UNAVAILABLE;
    if (row_code[m]) f |= MROW_REFUSED;
    if (!row_tunable(m)) f |= MROW_NO_TUNING;
    if (meters_present(m)) f |= MROW_SELECTABLE;
    if (have && (r.flags & MB_F_BINARY_ONLY)) f |= MROW_BINARY_ONLY;
    return f;
}
const char *meters_row_name(uint32_t m, char *buf, uint32_t n)       /* the name of row m into buf (always terminated): the built-in meter's own name, a pack meter's from its TOC row */
{
    uint32_t i = 0; mb_row_t r;
    if (n == 0u) return buf;
    const char *s = m == fb_row || !toc_row(m, &r) ? meter_table[0].name : r.name;
    for (; i + 1u < n && i < MTR_NAME_LEN && s[i]; i++) buf[i] = s[i];
    buf[i] = 0; return buf;
}
#else
uint32_t meters_thumb_len(uint32_t m) { (void)m; return 0u; }
int meters_thumb_rle(uint32_t m, uint32_t i, uint8_t *v) { (void)m; (void)i; (void)v; return 0; }
int meters_thumb_col(uint32_t m, uint32_t c, uint16_t *fixed, uint8_t *a, uint8_t *b, uint8_t *t, uint8_t *n) { (void)m; (void)c; (void)fixed; (void)a; (void)b; (void)t; (void)n; return 0; }
int meters_present(uint32_t m) { return m < METER_N; }
static const meter_def_t *mdef(uint32_t m) { return &meter_table[m < METER_N ? m : 0u]; }
static uint32_t mid_of(uint32_t m) { (void)m; return act.mid; }
static uint32_t cmd_limit_of(uint32_t m) { (void)m; return act.cmds; }
static uint32_t cyc_limit_of(uint32_t m) { (void)m; return act.cyc; }
static uint32_t def_npre(void) { return meter_table[0].npre; }
static void def_set_pre(uint32_t p) { meter_table[0].set_pre(p); }
static const char *def_pre_name(uint32_t p) { return meter_table[0].pre_name(p); }
static void def_apply(void) { meter_table[0].apply(); }
static void act_builtin(void)
{
    act.hash = shipped_meter_hash[0]; act.rev = shipped_meter_rev[0]; act.cmds = meter_cmd_budget[0]; act.cyc = meter_cyc_limit[0]; act.off = act.len = 0u; act.mid = meter_manifest_id[0]; act.np = meter_table[0].np;
    act.slot = 0u; act.pre = act.custom = act.user_n = act.want_id = act.ext = 0u; mb_reset(&act.budget); act.name[0] = 0;
}
static uint32_t row_key(uint32_t m) { (void)m; return tune_key(act.hash, act.rev); }
static int row_tunable(uint32_t m) { (void)m; return 1; }
static void ext_enter_cur(void) { }
void meters_ext_enter(uint32_t m) { (void)m; }                      /* no loadable meters in this build: nothing to switch over */
int meters_is_builtin(uint32_t m) { return m == 0u; }
uint32_t meters_row_flags(uint32_t m) { return m == 0u ? (MROW_VISIBLE | MROW_SELECTABLE) : 0u; }
const char *meters_row_name(uint32_t m, char *buf, uint32_t n) { (void)m; uint32_t i = 0; if (n == 0u) return buf; for (; i + 1u < n && meter_table[0].name[i]; i++) buf[i] = meter_table[0].name[i]; buf[i] = 0; return buf; }
#endif
const meter_def_t *meters_def(uint32_t m) { return mdef(m); }
/* The strings of a Configure row (fw2/ui/configure_view.c): a built-in meter's are static; the loaded pack meter's are read from its descriptor in the window into `buf` (see dyn_str). */
const char *meters_param_label(uint32_t m, uint32_t id, char *buf, uint32_t n)
{
#ifdef P2_PACKS
    if (m != fb_row) return dyn_str(m, id, -1, buf, n);
#endif
    (void)buf; (void)n; const meter_def_t *d = mdef(m); return id < d->np ? d->pd[id].label : "";
}
const char *meters_param_choice(uint32_t m, uint32_t id, uint32_t val, char *buf, uint32_t n)
{
#ifdef P2_PACKS
    if (m != fb_row) return dyn_str(m, id, (int32_t)val, buf, n);
#endif
    (void)buf; (void)n; const meter_def_t *d = mdef(m); return id < d->np && d->pd[id].vals && val < d->pd[id].nval ? d->pd[id].vals[val] : "";
}
/* The METER list and X: the rows a person can reach, in TOC order. A row is listed when it is visible and this firmware can run it (the fallback always); the others are skipped and marked (meters_row_flags). */
uint32_t meters_list_count(void) { uint32_t n = 0; for (uint32_t m = 0; m < meter_count; m++) n += (uint32_t)meters_present(m); return n; }
uint32_t meters_list_row(uint32_t i) { for (uint32_t m = 0; m < meter_count; m++) if (meters_present(m) && i-- == 0u) return m; return 0u; }       /* the i-th meter that is there -> its row */
uint32_t meters_list_index(uint32_t row) { uint32_t n = 0; for (uint32_t m = 0; m < row && m < meter_count; m++) n += (uint32_t)meters_present(m); return n; }       /* the other way */

static void activate_params(void)                                    /* the meter on screen was just selected: its preset (the one a saved tuning started from, else the first), then its tuning */
{
    uint32_t p = act.pre;                                            /* the boot preset of its METR entry, else the first */
    if (row_tunable(cur)) for (uint32_t s = 0; s < TUNE_SLOTS; s++) if (tune_k[s] == row_key(cur)) {
        const uint32_t pr = tune_words_anchor(tune_cache[s]);        /* a tuned meter comes back on the preset it was tuned from, so its tuning applies */
#ifdef P2_PACKS
        if (act.ext) { const int pos = pr ? mph_find(pr) : 0; if (pos >= 0) { p = (uint32_t)pos; act.want_id = (uint8_t)pr; } }
        else
#endif
        if (pr < meters_npre(cur)) p = pr;
    }
    if (p >= meters_npre(cur)) p = 0u;
    if (act.user_n) { uint16_t v[40]; if (user_preset(p, v, 0)) { const meter_def_t *d = mdef(cur); for (uint32_t i = 0; i < d->np && i < 40u; i++) d->mv[i] = v[i]; def_apply(); } }
    else { def_set_pre(p); def_apply(); }
    act.custom = 0u; act.pre = (uint8_t)p;
    tune_reapply();
}
#ifdef P2_PACKS
/* Selection is the ONLY place a pack is loaded (X, the Settings list, the boot restore): never inside a tick, between pictures, with one forced repaint; the audio stream is untouched. A refusal falls back to the built-in meter. */
static void select_row(uint32_t r, uint32_t boot)
{
    int refused = 0;
    if (r >= meter_count || !meters_present(r)) r = fb_row;
    keep_capture();                                                  /* before anything of the meter that is leaving changes */
    pend_clear();                                                    /* a selection that was still waiting for its clean run is replaced by this one */
    cur = r; force_next = 1;
    if (r != fb_row) {
        mb_row_t row; int e = r < toc_info.count && toc_row(r, &row) ? 0 : (int)MB_E_COUNT;
        if (!e && r != win_row) e = window_load(r);
        if (!e) { act_from_row(&row); e = dyn_register(r); if (e) { win_row = 0xFFu; pack_act = 0xFFu; } }
        if (e) { row_code[r] = (uint32_t)e; mwin_err = (uint32_t)e; mwin_refused++; mwin_last_row = r; cur = fb_row; refused = 1; }          /* refused: the built-in Winamp Bars, the row marked with the reason */
        else { row_code[r] = 0u; ps_reset(&win_strike); }
    }
    if (cur == fb_row) act_builtin();
    metr_attach();
    ext_enter_cur();
    activate_params();
    msel_cur_notune = !row_tunable(cur);
    if (boot) return;                                                /* the boot restore: the saved selection is what was committed */
    if (refused) { msel_aborts++; return; }                          /* a refusal never reaches the saved word */
    if (cur_hash31() == msel_saved || cur == fb_row) commit_now();   /* the committed meter again, or the built-in one (it cannot hang) */
    else pend_start();
}
#else
static void select_row(uint32_t r, uint32_t boot)
{
    (void)r; cur = 0; force_next = 1; act_builtin(); metr_attach(); activate_params(); if (!boot) commit_now();
}
#endif
void meters_select(uint32_t idx) { select_row(idx, 0u); }
void meters_next(int dir)
{
    uint32_t t = cur;
    for (uint32_t k = 0; k < meter_count; k++) { t = dir > 0 ? (t + 1u) % meter_count : (t + meter_count - 1u) % meter_count; if (meters_present(t)) break; }      /* skipping a row that is not listed */
    select_row(t, 0u);
}
/* The saved selection is the meter's id hash (31 bits, cores/settings_core.h SF_METER_HASH), not a row: installing, removing or reordering meters never changes which meter a card remembers. */
int meters_select_hash(uint32_t h31)                                  /* the boot restore: 1 when the meter is there and runs, else Winamp Bars runs and the saved hash is kept to be written back */
{
    uint32_t fbh = shipped_meter_hash[0] & 0x7FFFFFFFu;
#ifdef P2_PACKS
    mb_row_t r; if (toc_row(fb_row, &r)) fbh = hash31_of_row_hash(r.id_hash);
#endif
    msel_saved = h31 ? h31 : fbh;                                     /* what a card remembers stays until the user picks another meter (a meter that is gone, unavailable or refused is kept) */
#ifdef P2_PACKS
    if (h31) for (uint32_t m = 0; m < meter_count; m++) { uint32_t h; if (meters_present(m) && meter_row_hash31(m, &h) && h == h31) { select_row(m, 1u); return cur == m; } }
#endif
    select_row(fb_row, 1u);
    return h31 == 0u || fbh == h31;
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

meter_stats_t meter_stats;
/* The per-meter cost measurement (Talos 2 T4 gate, cores/meter_cost_core.h) lives in the Diagnostic Build's fw2/diagnostics/info_counters.c, so this module's hot RAM ceiling (tools/player2/meter_budget.py) does not
 * carry it; these two hooks are no-ops (weak) wherever that file is not linked (the normal build, the host tests). */
__attribute__((weak)) void meter_cost_mark(void) { }
__attribute__((weak)) void meter_cost_note(uint32_t meter, uint32_t cmds) { (void)meter; (void)cmds; }
/* cmp: the compose code of the registry (tools/player2/gen_meters_p2.py cmp_code). A gate is bit 15, the gating parameter in bits 14..10 and a mask of the values that enable it in bits 9..6. */
static int cmp_gate(const uint16_t *mv, uint32_t c) { if (!(c & 0x8000u)) return 1; const uint32_t v = mv[(c >> 10) & 31u]; return v < 4u && ((c >> (6u + v)) & 1u); }
static int composed(const meter_def_t *d) { const uint32_t c = d->cmp & 0xFFFFu; return c == 1u || (c > 1u && cmp_gate(d->mv, c)); }
static uint32_t shed_win = 0xFFFFFFFFu, degrade_win;                  /* the last window counted as shed */
uint32_t meters_self_degrade(uint32_t m) { return m == cur ? act.budget.level : 0u; }
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
    const uint32_t dg = mb_effective(mp3play_degrade_now(), act.budget.level);      /* the governor's level or the meter's own (it drew more commands than its ceiling), whichever is worse */
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
    if (!in.force) { mb_judge(&act.budget, meter_stats.cmds_last, cmd_limit_of(cur)); meter_stats.over += (uint32_t)mb_over(meter_stats.cmds_last, cmd_limit_of(cur)); }      /* a forced repaint is the costliest picture by design: not judged */
    if (meter_stats.cmds_last > meter_stats.cmds_max) meter_stats.cmds_max = meter_stats.cmds_last;
    meter_stats.compose_cyc = 0;
    if (!in.force) meter_cost_note(act.slot, meter_stats.cmds_last);                       /* before the compose copy: the tick's own cost */
    if (comp) { const uint32_t c0 = cycles(); gfx_compose_present(bx, by, bw, bh); meter_stats.compose_cyc = cycles() - c0; }
    const uint32_t spent = cycles() - t;
    meter_stats.ticks++; meter_stats.last_cyc = spent;
    if (spent > meter_stats.max_cyc[act.slot]) meter_stats.max_cyc[act.slot] = spent;                         /* the statistics row of a shipped meter (slot 0..6), 7 for any other meter */
    if (pend_on) { const uint32_t el = (cycles() - pend_t0) / CLK_HZ; msel_clean_s = el; if (el >= MTR_COMMIT_SECS) commit_now(); }          /* a drawn tick that ends MTR_COMMIT_SECS after the selection, with nothing cut in between: the meter is clean, the saved selection follows it */
}
