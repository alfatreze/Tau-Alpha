#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../cores/spectrum_core.h"
#include "../generated/meter_presets.h"
#include "../generated/lw_presets.h"
#include "../generated/meter_registry.h"
#include "../cores/metr_core.h"
#include "../cores/tune_core.h"

#define R_SPEC_IDX  0x800000DCu
#define R_SPEC_DATA 0x800000E0u
_Static_assert(MTR_BANDS == SPEC_BANDS, "the meter inputs carry one value per hardware band");

/* The module list is generated from the manifests (meter_registry.h, tools/player2/gen_meters_p2.py): a meter is its manifest plus one meter_<key>.c that provides these five functions and the live parameters. */
#define DECL(key, nm, id, lc, UC, np, cmp) extern void meter_##key##_tick(const mtr_in_t *in); extern void meter_##key##_preset(uint32_t); extern const char *meter_##key##_preset_name(uint32_t); extern uint16_t meter_##key##_mv[]; extern void meter_##key##_apply(void);
METER_LIST(DECL)
#define ROW(key, nm, id, lc, UC, np, cmp) { nm, meter_##key##_tick, np, meter_##key##_preset, meter_##key##_preset_name, meter_##key##_mv, mpd_##lc, MPD_##UC##_N, meter_##key##_apply, cmp },
const meter_def_t meter_table[] = { METER_LIST(ROW) };
const uint32_t meter_count = sizeof meter_table / sizeof meter_table[0];

#ifdef P2_PACKS
#include "meter_pack.h"
static mpkb_slot_t pack_slot[MTR_PACK_SLOTS];                       /* per meter slot: the pack's state (see the packs block below) */
#endif
static uint32_t cur, bx, by, bw, bh, win_seen = 0xFFFFFFFFu, frame, last_t, force_next;
static uint8_t lvl[MTR_BANDS];
extern uint32_t mp3play_paused_now(void);          /* mp3play.c: 1 when nothing is being heard */
extern uint32_t mp3play_degrade_now(void);         /* mp3play.c: the meter degrade level 0..3 (governor_core.h gov_degrade) */

static uint8_t pre[METER_N], custom[METER_N];                                  /* the selected preset of each meter */
/* User presets (the METR section of tau-assets.bin) replace a meter's built-in ones. The section is copied to PSRAM at boot (the tag buffer it arrives in is reused); a preset is read back a word at a time
 * (the window takes word accesses only) into a small buffer and applied through the same metr_core.h that the host test checks against the Python writer. */
#ifndef METR_PSRAM
#define METR_PSRAM ((volatile uint32_t *)(uintptr_t)0xA4550000u)
#endif
#define METR_MAX_BYTES 0xF000u                                      /* the PSRAM region 0xA4550000..0xA4560000 less a margin (the Layered Wave ring starts at 0xA4560000) */
static metr_e_t ue[METER_N]; static uint8_t user_n[METER_N];
static metr_p_t sh[40];                                            /* the parameter shapes of ONE meter, from its descriptors, rebuilt when needed (all of them at once cost 2,560 B of hot RAM) */
static uint8_t is_wide(const mpd_t *p) { return p->type == 3u || (p->type == 0u && p->max > 255u); }
static const metr_p_t *shapes(uint32_t m) { const meter_def_t *d = &meter_table[m]; for (uint32_t i = 0; i < d->np && i < 40u; i++) { sh[i].min = d->pd[i].min; sh[i].max = d->pd[i].max; sh[i].kind = d->pd[i].type; sh[i].enum_n = d->pd[i].nval; sh[i].wide = is_wide(&d->pd[i]); } return sh; }
static uint32_t tune_cache[TUNE_WORDS], tune_meter = 0xFFu;
static void tune_clear(void) { for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[k] = 0; tune_meter = 0xFFu; }
uint32_t meters_npre(uint32_t m) { return m >= meter_count ? 1u : user_n[m] ? user_n[m] : meter_table[m].npre; }
static void user_preset(uint32_t m, uint32_t p, uint16_t *vals, char *name)       /* reads preset p of meter m from the PSRAM copy */
{
    uint8_t buf[96]; metr_e_t e = ue[m]; const uint32_t at = e.off + p * e.psize, w0 = at & ~3u, w1 = (at + e.psize + 3u) & ~3u;
    for (uint32_t w = w0; w < w1; w += 4u) { const uint32_t v = METR_PSRAM[w >> 2]; for (uint32_t k = 0; k < 4u && w - w0 + k < sizeof buf; k++) buf[w - w0 + k] = (uint8_t)(v >> (8u * k)); }
    e.off = at - w0; e.psize = 0u;
    if (name) metr_name(buf, &e, 0u, name);
    if (vals) metr_values(buf, &e, 0u, shapes(m), meter_table[m].np, vals);
}
void meters_init(void) {
#ifdef P2_PACKS
    for (uint32_t i = 0; i < MTR_PACK_SLOTS; i++) pack_slot[i].status = 0xFFu;          /* no pack until a bundle has installed one (status 0 would mean a running pack) */
#endif
    cur = 0; for (uint32_t i = 0; i < meter_count; i++) { pre[i] = 0; custom[i] = 0; user_n[i] = 0; meter_table[i].set_pre(0u); } }
uint32_t meters_metr_load(const uint8_t *src, uint32_t n)       /* returns the number of meters whose presets were replaced; the section is validated first */
{
    if (n > METR_MAX_BYTES || metr_check(src, n)) return 0;
    uint32_t done = 0;
    for (uint32_t m = 0; m < meter_count; m++) {
        metr_e_t e;
        if (!metr_find(src, n, meter_manifest_id[m], shapes(m), meter_table[m].np, &e)) continue;
        ue[m] = e; user_n[m] = e.npre; done++;
    }
    if (!done) return 0;
    for (uint32_t w = 0; w < (n + 3u) / 4u; w++) METR_PSRAM[w] = (uint32_t)src[4u * w] | ((w * 4u + 1u < n ? (uint32_t)src[4u * w + 1u] : 0u) << 8) | ((w * 4u + 2u < n ? (uint32_t)src[4u * w + 2u] : 0u) << 16) | ((w * 4u + 3u < n ? (uint32_t)src[4u * w + 3u] : 0u) << 24);
    for (uint32_t m = 0; m < meter_count; m++) if (user_n[m]) meters_preset_select(m, ue[m].def);
    return done;
}
uint32_t meters_preset_of(uint32_t m) { return m < meter_count ? pre[m] : 0u; }
void meters_preset_select(uint32_t m, uint32_t p)
{
    if (m >= meter_count) return;
    if (p >= meters_npre(m)) p = 0u;
    pre[m] = (uint8_t)p; custom[m] = 0u; if (tune_meter == m) tune_clear();
    if (user_n[m]) { uint16_t v[40]; user_preset(m, p, v, 0); const meter_def_t *d = &meter_table[m]; for (uint32_t i = 0; i < d->np; i++) d->mv[i] = v[i]; d->apply(); }
    else meter_table[m].set_pre(p);
    if (m == cur) force_next = 1;
}
void meters_preset_next(void) { meters_preset_select(cur, (pre[cur] + 1u) % meters_npre(cur)); }
const char *meters_preset_name(void)
{
    static char nm[METR_NAME];
    if (custom[cur]) return "CUSTOM";
    if (user_n[cur]) { user_preset(cur, pre[cur], 0, nm); return nm; }
    return meter_table[cur].pre_name(pre[cur]);
}
/* ---- saving a tuning (cores/tune_core.h): the parameters of ONE meter that differ from its preset, in four persisted words ---- */
static uint16_t tm_max[40];
static const uint16_t *tune_max_of(uint32_t m, uint32_t *np)
{
    if (m >= meter_count) return 0;
    const meter_def_t *d = &meter_table[m]; *np = d->np;
    for (uint32_t i = 0; i < d->np && i < 40u; i++) tm_max[i] = d->pd[i].type == 3u ? 65535u : d->pd[i].type == 2u ? 1u : d->pd[i].type == 1u ? (uint16_t)(d->pd[i].nval ? d->pd[i].nval - 1u : 0u) : d->pd[i].max;
    return tm_max;
}
void meters_tune_save(uint32_t m)            /* the parameters of meter m were edited (or reset): store what differs from its preset, replacing any tuning of another meter */
{
    if (m >= meter_count) return;
    const meter_def_t *d = &meter_table[m];
    if (!custom[m]) { if (tune_meter == m) tune_clear(); return; }
    uint16_t keep[40], base[40];
    for (uint32_t i = 0; i < d->np && i < 40u; i++) keep[i] = d->mv[i];
    if (user_n[m]) user_preset(m, pre[m], base, 0); else { d->set_pre(pre[m]); for (uint32_t i = 0; i < d->np && i < 40u; i++) base[i] = d->mv[i]; }
    for (uint32_t i = 0; i < d->np && i < 40u; i++) d->mv[i] = keep[i];
    d->apply();
    uint32_t np = 0; const uint16_t *mx = tune_max_of(m, &np);
    uint32_t w[TUNE_WORDS];
    if (tune_pack(m, pre[m], keep, base, mx, np, w)) { for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[k] = w[k]; tune_meter = m; }
    else if (tune_meter == m) tune_clear();
}
uint32_t meters_payload(uint32_t m, uint8_t *out)                  /* the SR_T_METERCFG entry: manifest meter id, schema 1, preset (0xFF = tuned), nparams, then the values (two bytes for a wide one) */
{
    if (m >= meter_count) return 0;
    const meter_def_t *d = &meter_table[m]; uint32_t n = 0;
    out[n++] = meter_manifest_id[m]; out[n++] = 1u; out[n++] = custom[m] ? 0xFFu : pre[m]; out[n++] = d->np;
    for (uint32_t i = 0; i < d->np; i++) { out[n++] = (uint8_t)d->mv[i]; if (is_wide(&d->pd[i])) out[n++] = (uint8_t)(d->mv[i] >> 8); }
    return n;
}
void meters_tune_words(uint32_t *w) { for (uint32_t k = 0; k < TUNE_WORDS; k++) w[k] = tune_cache[k]; }
void meters_tune_apply(const uint32_t *w)    /* the persisted words changed (boot, or Core Settings): apply the tuning when it belongs to the preset now selected */
{
    int same = 1; for (uint32_t k = 0; k < TUNE_WORDS; k++) if (w[k] != tune_cache[k]) same = 0;
    if (same) return;
    uint32_t mt = 0, pr = 0; uint8_t ix[TUNE_MAX]; uint16_t vl[TUNE_MAX];
    const uint32_t n = tune_unpack(w, tune_max_of, &mt, &pr, ix, vl);
    if (!n || mt >= meter_count || pr != pre[mt]) return;
    const meter_def_t *d = &meter_table[mt];
    for (uint32_t k = 0; k < n; k++) { uint32_t v = vl[k]; const mpd_t *p = &d->pd[ix[k]]; if (p->type == 0u && v < p->min) v = p->min; d->mv[ix[k]] = (uint16_t)v; }
    custom[mt] = 1u; d->apply(); if (mt == cur) force_next = 1;
    for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[k] = w[k];
    tune_meter = mt;
}
void meters_params_changed(uint32_t m) { if (m >= meter_count) return; custom[m] = 1u; meter_table[m].apply(); if (m == cur) force_next = 1; meters_tune_save(m); }
uint32_t meters_custom(uint32_t m) { return m < meter_count ? custom[m] : 0u; }
uint32_t meters_cur(void) { return cur; }
const char *meters_name(void) { return meter_table[cur].name; }

void meters_enter(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    bx = x; by = y; bw = w; bh = h; frame = 0; force_next = 1;
    win_seen = caps.spec ? (REG_RD(R_SPEC_ST) >> 16) & 0xFFFFu : 0xFFFFFFFFu;      /* the window in progress is skipped: its mean mixes in what played before the meter was shown */
    for (uint32_t b = 0; b < MTR_BANDS; b++) lvl[b] = 0;
    gfx_rect(x, y, w, h, th_role[TR_BG_BOTTOM]);
    last_t = cycles();
}
void meters_select(uint32_t idx) { cur = idx < meter_count ? idx : 0u; force_next = 1; }
void meters_next(int dir)
{
    cur = dir > 0 ? (cur + 1u) % meter_count : (cur + meter_count - 1u) % meter_count;
    force_next = 1;                                  /* the new meter repaints everything, the old picture is erased by its forced clear */
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
/* Loadable meters (docs/features/meters/METER_PACKS.md): a pack in the MPKB section of tau-assets.bin is verified and copied into its PSRAM slot once at boot; while it is the selected meter its entry point draws instead of the
 * built-in one (which stays as the per-meter fallback: a refused or missing pack just leaves the built-in meter in use). Only the active pack's working state is resident, in the 4 KB scratch under the tag zone. */
static uint32_t pack_act = 0xFFu, pack_seen, pack_err = 0xFFu;         /* the meter whose pack state is in the scratch; packs in the file; the bundle's own result (0xFF: no bundle) */
static uint32_t pack_stats[MTR_PACK_STATS];
static mtr_host_api_t pack_api = { MTR_PACK_ABI, gfx_rect, th_role, 0, pack_stats, gfx_text, gfx_rect_stash, gfx_blend_ready, gfx_blend_on, gfx_blend_blit, gfx_blend_off, gfx_mb_write, gfx_mb_read, gfx_fence, gfx_blit, gfx_blit_abs, gfx_sblit, gfx_bar, gfx_scroll_down };
/* The three places that touch the Pocket's address space can be replaced by a host test (sim/test_p2_meter_pack_dispatch.py): the slot (written through the data alias, executed at the instruction alias), the scratch and the call. */
#ifndef PACK_SLOT_DATA
#define PACK_SLOT_DATA(i) ((volatile uint8_t *)(uintptr_t)((MTR_PACK_SLOT_BASE + (i) * MTR_PACK_SLOT_SIZE) | 0x80000000u))
#endif
#ifndef PACK_SCRATCH
#define PACK_SCRATCH ((uint8_t *)(uintptr_t)MTR_PACK_SCRATCH_ORG)
#endif
#ifndef PACK_CALL
#define PACK_CALL(e, in, api) ((mtr_pack_entry_fn)(uintptr_t)(e))(in, api)
#endif
static volatile uint8_t *pack_slot_data(uint32_t i) { return PACK_SLOT_DATA(i); }
static int pack_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len) { const volatile uint8_t *s = (const volatile uint8_t *)ctx; for (uint32_t i = 0; i < len; i++) dst[i] = s[off + i]; return 1; }
void meters_packs_load(const uint8_t *p, uint32_t n)                    /* the MPKB section: the caller has proven the PSRAM window (the slots live in it) */
{
    uint16_t ids[MTR_PACK_SLOTS];                                        /* slot i belongs to meter i: the manifests' p2.slot, which is the registry order */
    for (uint32_t i = 0; i < MTR_PACK_SLOTS; i++) ids[i] = (uint16_t)i;
    pack_act = 0xFFu;
    pack_err = (uint32_t)mpkb_install_all(pack_rd, (void *)p, PACK_SCRATCH, n, pack_slot_data(0), ids, MTR_PACK_SCRATCH_ORG, MTR_PACK_SCRATCH_SIZE, pack_slot, &pack_seen);
}
uint32_t meters_pack_state(uint32_t m) { return m < MTR_PACK_SLOTS ? pack_slot[m].status : 0xFFu; }     /* 0 = the pack runs, 0xFF = none, else the refusal code */
void meters_pack_info(uint32_t *running, uint32_t *first_err, uint32_t *file_err, uint32_t *seen)     /* for Info: bit m of `running` = meter m runs as a pack; the first refusal code (0 none); the bundle's own result (0xFF no bundle); packs in the file */
{
    uint32_t r = 0, e = 0;
    for (uint32_t m = 0; m < MTR_PACK_SLOTS; m++) { if (pack_slot[m].status == MPK_OK) r |= 1u << m; else if (pack_slot[m].status != 0xFFu && !e) e = pack_slot[m].status; }
    *running = r; *first_err = e; *file_err = pack_err; *seen = pack_seen;
}
static int pack_ok(uint32_t m) { return m < MTR_PACK_SLOTS && pack_slot[m].status == MPK_OK; }
static void pack_tick(const mtr_in_t *in)
{
    if (pack_act != cur) { mpk_activate(&pack_slot[cur].info, pack_slot_data(cur), (volatile uint8_t *)PACK_SCRATCH); pack_act = cur; }
    pack_api.params = meter_table[cur].mv;
    PACK_CALL(pack_slot[cur].info.entry, in, &pack_api);
}
#endif
meter_stats_t meter_stats;
static uint32_t shed_win = 0xFFFFFFFFu, degrade_win;                  /* the last window counted as shed */
_Static_assert(METER_N <= 8u, "meter_stats_t.max_cyc holds eight meters: widen it with the registry");
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
    const uint32_t dg = mp3play_degrade_now();
    if (dg && dg < 3u && !force_next) { degrade_win++; if (degrade_win & ((1u << dg) - 1u)) { meter_stats.degraded++; return; } }
    const uint32_t t = cycles(), dt = (t - last_t) / (CLK_HZ / 1000u); last_t = t;
    uint32_t pl = 0, pr = 0;
    if (caps.wave) { const uint32_t pk = REG_RD(R_WAVE_PK); pl = ((pk & 0xFFFFu) * 3u) / 4u; pr = ((pk >> 16) * 3u) / 4u; REG_WR(R_WAVE_CTL, 1u); }      /* peaks since the last tick, then cleared */
    mtr_in_t in = { lvl, frame++, (uint16_t)(dt > 200u ? 200u : dt), (uint16_t)bx, (uint16_t)by, (uint16_t)bw, (uint16_t)bh, th_role[TR_BG_BOTTOM], (uint8_t)force_next, (uint8_t)mp3play_paused_now(), pl, pr, (uint8_t)dg };
    force_next = 0;
    const int comp = meter_table[cur].compose && gfx_compose_begin(bx, by, bw, bh);
#ifdef P2_PACKS
    if (pack_ok(cur)) pack_tick(&in); else
#endif
    meter_table[cur].tick(&in);
    meter_stats.compose_cyc = 0;
    if (comp) { const uint32_t c0 = cycles(); gfx_compose_present(bx, by, bw, bh); meter_stats.compose_cyc = cycles() - c0; }
    const uint32_t spent = cycles() - t;
    meter_stats.ticks++; meter_stats.last_cyc = spent;
    if (spent > meter_stats.max_cyc[cur]) meter_stats.max_cyc[cur] = spent;
}
