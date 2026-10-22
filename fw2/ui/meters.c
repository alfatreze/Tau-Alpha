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
#include "../cores/meter_budget_core.h"
#include "../cores/pack_strike_core.h"
#include "../cores/helios_core.h"

#define R_SCAN      0x800000E8u            /* B-267 beam position: bit 9 = present, bits 8:0 = the video line counter (helios.c) */
#define R_SPEC_IDX  0x800000DCu
#define R_SPEC_DATA 0x800000E0u
_Static_assert(MTR_BANDS == SPEC_BANDS, "the meter inputs carry one value per hardware band");

/* The module list is generated from the manifests (meter_registry.h, tools/player2/gen_meters_p2.py): a meter is its manifest plus one meter_<key>.c that provides these five functions and the live parameters. */
#define DECL(key, nm, id, lc, UC, np, cmp) extern void meter_##key##_tick(const mtr_in_t *in); extern void meter_##key##_preset(uint32_t); extern const char *meter_##key##_preset_name(uint32_t); extern uint16_t meter_##key##_mv[]; extern void meter_##key##_apply(void);
METER_LIST(DECL)
#define ROW(key, nm, id, lc, UC, np, cmp) { nm, meter_##key##_tick, np, meter_##key##_preset, meter_##key##_preset_name, meter_##key##_mv, mpd_##lc, MPD_##UC##_N, meter_##key##_apply, cmp },
const meter_def_t meter_table[] = { METER_LIST(ROW) };
uint32_t meter_count = METER_N;                                     /* the rows in use: the built-in ones, and the highest pack-only one that came up (rows in between may be absent: meters_present) */
/* Rows METER_N.. belong to pack_only meters (manifest p2.pack_only): they exist only when their pack is installed AND its descriptor (tools/player2/meter_desc.py, the last section of the pack image) checks out. The
 * descriptor stays in the PSRAM slot; only the ONE meter being edited has its Configure descriptors copied to hot RAM (dyn_pd). The dynamic code is compiled only into a PACKS build. */
#define DYN_N (METER_SLOTS - METER_N)
#define DYN_ROWS (DYN_N ? DYN_N : 1u)
static const meter_def_t *mdef(uint32_t m);                         /* the row of meter m (a pack-only row builds its Configure descriptors on first use) */
static uint32_t mid_of(uint32_t m);                                  /* the meter's manifest id (tau-assets.bin METR, the reports) */
static uint32_t cmd_limit_of(uint32_t m), cyc_limit_of(uint32_t m);
static uint32_t def_npre(uint32_t m);
static void def_set_pre(uint32_t m, uint32_t p);
static const char *def_pre_name(uint32_t m, uint32_t p);
static void def_apply(uint32_t m);

#ifdef P2_PACKS
#include "meter_pack.h"
static ps_t pack_strike[MTR_PACK_SLOTS];                            /* each pack's overruns of its cycle limit (cores/pack_strike_core.h) */
static mpkb_slot_t pack_slot[MTR_PACK_SLOTS];                       /* per meter slot: the pack's state (see the packs block below) */
static void dyn_register(uint32_t m);                               /* a pack-only meter's row, from its pack's descriptor (below) */
static volatile uint8_t *pack_slot_data(uint32_t i);
#endif
static uint32_t cur, bx, by, bw, bh, win_seen = 0xFFFFFFFFu, frame, last_t, force_next;
static uint8_t lvl[MTR_BANDS];
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
static uint32_t tune_cache[TUNE_WORDS], tune_meter = 0xFFu;
static void tune_clear(void) { for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[k] = 0; tune_meter = 0xFFu; }
uint32_t meters_npre(uint32_t m) { return m >= meter_count || !meters_present(m) ? 1u : user_n[m] ? user_n[m] : def_npre(m); }
static void user_preset(uint32_t m, uint32_t p, uint16_t *vals, char *name)       /* reads preset p of meter m from the PSRAM copy */
{
    uint8_t buf[96]; metr_e_t e = ue[m]; const uint32_t at = e.off + p * e.psize, w0 = at & ~3u, w1 = (at + e.psize + 3u) & ~3u;
    for (uint32_t w = w0; w < w1; w += 4u) { const uint32_t v = METR_PSRAM[w >> 2]; for (uint32_t k = 0; k < 4u && w - w0 + k < sizeof buf; k++) buf[w - w0 + k] = (uint8_t)(v >> (8u * k)); }
    e.off = at - w0; e.psize = 0u;
    if (name) metr_name(buf, &e, 0u, name);
    if (vals) metr_values(buf, &e, 0u, shapes(m), mdef(m)->np, vals);
}
void meters_init(void) {
#ifdef P2_PACKS
    for (uint32_t i = 0; i < MTR_PACK_SLOTS; i++) { pack_slot[i].status = 0xFFu; ps_reset(&pack_strike[i]); }          /* no pack until a bundle has installed one (status 0 would mean a running pack) */
#endif
    cur = 0; meter_count = METER_N;
    for (uint32_t i = 0; i < METER_SLOTS; i++) { mb_reset(&budget[i]); pre[i] = 0; custom[i] = 0; user_n[i] = 0; if (i < METER_N) meter_table[i].set_pre(0u); } }
uint32_t meters_metr_load(const uint8_t *src, uint32_t n)       /* returns the number of meters whose presets were replaced; the section is validated first */
{
    if (n > METR_MAX_BYTES || metr_check(src, n)) return 0;
    uint32_t done = 0;
    for (uint32_t m = 0; m < meter_count; m++) {
        metr_e_t e;
        if (!meters_present(m) || !metr_find(src, n, mid_of(m), shapes(m), mdef(m)->np, &e)) continue;
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
    pre[m] = (uint8_t)p; custom[m] = 0u; if (tune_meter == m) tune_clear();
    if (user_n[m]) { uint16_t v[40]; user_preset(m, p, v, 0); const meter_def_t *d = mdef(m); for (uint32_t i = 0; i < d->np; i++) d->mv[i] = v[i]; def_apply(m); }
    else def_set_pre(m, p);
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
/* ---- saving a tuning (cores/tune_core.h): the parameters of ONE meter that differ from its preset, in four persisted words ---- */
static uint16_t tm_max[40];
static const uint16_t *tune_max_of(uint32_t m, uint32_t *np)
{
    if (m >= meter_count || !meters_present(m)) return 0;
    const meter_def_t *d = mdef(m); *np = d->np;
    for (uint32_t i = 0; i < d->np && i < 40u; i++) tm_max[i] = d->pd[i].type == 3u ? 65535u : d->pd[i].type == 2u ? 1u : d->pd[i].type == 1u ? (uint16_t)(d->pd[i].nval ? d->pd[i].nval - 1u : 0u) : d->pd[i].max;
    return tm_max;
}
void meters_tune_save(uint32_t m)            /* the parameters of meter m were edited (or reset): store what differs from its preset, replacing any tuning of another meter */
{
    if (m >= meter_count || !meters_present(m)) return;
    const meter_def_t *d = mdef(m);
    if (!custom[m]) { if (tune_meter == m) tune_clear(); return; }
    uint16_t keep[40], base[40];
    for (uint32_t i = 0; i < d->np && i < 40u; i++) keep[i] = d->mv[i];
    if (user_n[m]) user_preset(m, pre[m], base, 0); else { def_set_pre(m, pre[m]); for (uint32_t i = 0; i < d->np && i < 40u; i++) base[i] = d->mv[i]; }
    for (uint32_t i = 0; i < d->np && i < 40u; i++) d->mv[i] = keep[i];
    def_apply(m);
    uint32_t np = 0; const uint16_t *mx = tune_max_of(m, &np);
    uint32_t w[TUNE_WORDS];
    if (tune_pack(m, pre[m], keep, base, mx, np, w)) { for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[k] = w[k]; tune_meter = m; }
    else if (tune_meter == m) tune_clear();
}
uint32_t meters_payload(uint32_t m, uint8_t *out)                  /* the SR_T_METERCFG entry: manifest meter id, schema 1, preset (0xFF = tuned), nparams, then the values (two bytes for a wide one) */
{
    if (m >= meter_count || !meters_present(m)) return 0;
    const meter_def_t *d = mdef(m); uint32_t n = 0;
    out[n++] = (uint8_t)mid_of(m); out[n++] = 1u; out[n++] = custom[m] ? 0xFFu : pre[m]; out[n++] = d->np;
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
    if (!n || mt >= meter_count || !meters_present(mt) || pr != pre[mt]) return;
    const meter_def_t *d = mdef(mt);
    for (uint32_t k = 0; k < n; k++) { uint32_t v = vl[k]; const mpd_t *p = &d->pd[ix[k]]; if (p->type == 0u && v < p->min) v = p->min; d->mv[ix[k]] = (uint16_t)v; }
    custom[mt] = 1u; def_apply(mt); if (mt == cur) force_next = 1;
    for (uint32_t k = 0; k < TUNE_WORDS; k++) tune_cache[k] = w[k];
    tune_meter = mt;
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
void meters_select(uint32_t idx) { cur = idx < meter_count && meters_present(idx) ? idx : 0u; force_next = 1; }
void meters_next(int dir)
{
    for (uint32_t k = 0; k < METER_SLOTS; k++) { cur = dir > 0 ? (cur + 1u) % meter_count : (cur + meter_count - 1u) % meter_count; if (meters_present(cur)) break; }      /* skipping a row whose pack is not there */
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
static mtr_host_api_t pack_api = { MTR_PACK_ABI, gfx_rect, th_role, 0, pack_stats, gfx_text, gfx_rect_stash, gfx_blend_ready, gfx_blend_on, gfx_blend_blit, gfx_blend_off, gfx_mb_write, gfx_mb_read, gfx_fence, gfx_blit, gfx_blit_abs, gfx_sblit, gfx_bar, gfx_scroll_down, gfx_clut_load, gfx_cblit };
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
    for (uint32_t m = METER_N; m < METER_SLOTS; m++) if (pack_slot[m].status == MPK_OK) dyn_register(m);        /* the pack-only meters: listed only when their descriptor is sound */
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
    pack_api.params = mdef(cur)->mv;
    PACK_CALL(pack_slot[cur].info.entry, in, &pack_api);
}
#endif

/* ---- the pack-only rows (descriptors from the packs; B-812) and the accessors every other part of the host uses ---------------------------------------------------------------------------------------- */
#ifdef P2_PACKS
#include "../cores/meter_desc_core.h"
#define DYN_MAX_P 12u                                          /* parameters a pack-only meter may have (its Configure descriptors are copied to hot RAM while it is edited) */
#define DYN_POOL 320u                                          /* the label and choice strings of those descriptors */
#define DYN_MAX_V 16u
typedef struct { uint32_t off, len, cmds, cyc, cmp; uint16_t mid, np, npre; uint8_t ok, code; char name[MD_MAX_STR + 1u]; } dyn_t;
static dyn_t dyn[DYN_ROWS];
static meter_def_t dyn_def[DYN_ROWS];
static uint16_t dyn_mv[DYN_ROWS][DYN_MAX_P];
static mpd_t dyn_pd[DYN_MAX_P]; static char dyn_pool[DYN_POOL]; static const char *dyn_vp[DYN_MAX_V]; static uint32_t dyn_cached = 0xFFu;
static void dyn_stub_tick(const mtr_in_t *in) { if (in->force) gfx_text(in->x + 8u, in->y + in->h / 2u - 8u, "PACK NOT RUNNING", UI_RED, in->bg); }     /* the pack was struck out or refused after it was listed */
static int desc_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)       /* the descriptor, straight out of the PSRAM slot, a word at a time */
{
    const dyn_t *d = (const dyn_t *)ctx; const uint32_t k = (uint32_t)(d - dyn);
    if (off > d->len || n > d->len - off) return 0;
    volatile uint8_t *base = pack_slot_data(METER_N + k);
    for (uint32_t i = 0; i < n; i++) { const uint32_t a = d->off + off + i; dst[i] = (uint8_t)(*(volatile uint32_t *)(base + (a & ~3u)) >> (8u * (a & 3u))); }
    return 1;
}
static void dyn_cache_build(uint32_t k)                           /* copy the Configure descriptors of dynamic meter k (labels and choice names into a pool) */
{
    md_hdr_t h; uint32_t pool = 0, vp = 0;
    if (!md_header(desc_rd, &dyn[k], dyn[k].len, &h)) return;
    for (uint32_t i = 0; i < h.np && i < DYN_MAX_P; i++) {
        md_param_t p; md_param(desc_rd, &dyn[k], &h, i, &p);
        mpd_t *o = &dyn_pd[i]; o->label = &dyn_pool[pool]; int n = md_string(desc_rd, &dyn[k], &h, p.label, &dyn_pool[pool], MD_MAX_STR + 1u); pool += (uint32_t)(n < 0 ? 0 : n) + 1u;
        o->type = p.type; o->hidden = p.hidden; o->min = p.min; o->max = p.max; o->step = p.step; o->nval = p.nval; o->when_idx = p.when_idx; o->when_val = p.when_val; o->vals = 0;
        if (p.nval) {
            o->vals = &dyn_vp[vp];
            for (uint32_t j = 0; j < p.nval; j++) { uint32_t so = 0; md_val_off(desc_rd, &dyn[k], &p, j, &so); dyn_vp[vp++] = &dyn_pool[pool]; n = md_string(desc_rd, &dyn[k], &h, so, &dyn_pool[pool], MD_MAX_STR + 1u); pool += (uint32_t)(n < 0 ? 0 : n) + 1u; }
        }
    }
    dyn_cached = k;
}
/* A pack that is installed (status OK) becomes a row when its descriptor is sound and fits the caches; otherwise its status says why (41 descriptor missing or damaged, 42 too big for the host's caches). */
static void dyn_register(uint32_t m)
{
    const uint32_t k = m - METER_N; dyn_t *d = &dyn[k]; const uint32_t size = pack_slot[m].info.size;
    d->ok = 0;
    if (size < 16u || (size & 3u)) { pack_slot[m].status = MPK_E_DESC; return; }
    volatile uint32_t *t = (volatile uint32_t *)(pack_slot_data(m) + size - 8u); const uint32_t len = t[0], mg = t[1];
    if (mg != MD_TRAILER || len < MD_HDR + 8u || len > size || len > MD_MAX_LEN || (len & 3u)) { pack_slot[m].status = MPK_E_DESC; return; }
    d->off = size - len; d->len = len;
    md_hdr_t h;
    if (md_check(desc_rd, d, len, TR_COUNT, &h) || h.slot != m) { pack_slot[m].status = MPK_E_DESC; return; }
    uint32_t pool = 0, nv = 0;                                     /* will the Configure descriptors fit the caches? */
    char sbuf[MD_MAX_STR + 2u];
    for (uint32_t i = 0; i < h.np; i++) {
        md_param_t p; md_param(desc_rd, d, &h, i, &p); pool += (uint32_t)md_string(desc_rd, d, &h, p.label, sbuf, sizeof sbuf) + 1u; nv += p.nval;
        for (uint32_t j = 0; j < p.nval; j++) { uint32_t so = 0; md_val_off(desc_rd, d, &p, j, &so); pool += (uint32_t)md_string(desc_rd, d, &h, so, sbuf, sizeof sbuf) + 1u; }
    }
    if (h.np > DYN_MAX_P || pool > DYN_POOL || nv > DYN_MAX_V) { pack_slot[m].status = MPK_E_DESC_BIG; return; }
    d->cmds = h.cmds; d->cyc = h.max_cycles; d->cmp = h.cmp; d->mid = h.mid; d->np = h.np; d->npre = h.npre;
    md_string(desc_rd, d, &h, h.name, d->name, sizeof d->name);
    meter_def_t *r = &dyn_def[k];
    r->name = d->name; r->tick = dyn_stub_tick; r->npre = h.npre; r->set_pre = 0; r->pre_name = 0; r->mv = dyn_mv[k]; r->pd = dyn_pd; r->np = (uint8_t)h.np; r->apply = 0; r->cmp = h.cmp;
    for (uint32_t i = 0; i < DYN_MAX_P; i++) dyn_mv[k][i] = 0;
    d->ok = 1; if (m + 1u > meter_count) meter_count = m + 1u;
    if (dyn_cached == k) dyn_cached = 0xFFu;
    def_set_pre(m, 0u);
}
static void dyn_set_pre(uint32_t k, uint32_t p)
{
    md_hdr_t h; uint32_t no; uint16_t v[MD_MAX_PARAMS];
    if (!dyn[k].ok || !md_header(desc_rd, &dyn[k], dyn[k].len, &h) || !md_preset(desc_rd, &dyn[k], &h, p < h.npre ? p : 0u, &no, v)) return;
    for (uint32_t i = 0; i < h.np && i < DYN_MAX_P; i++) dyn_mv[k][i] = v[i];
}
static const char *dyn_pre_name(uint32_t k, uint32_t p)
{
    static char nm[MD_MAX_STR + 1u]; md_hdr_t h; uint32_t no; uint16_t v[MD_MAX_PARAMS];
    nm[0] = 0;
    if (dyn[k].ok && md_header(desc_rd, &dyn[k], dyn[k].len, &h) && md_preset(desc_rd, &dyn[k], &h, p < h.npre ? p : 0u, &no, v)) md_string(desc_rd, &dyn[k], &h, no, nm, sizeof nm);
    return nm;
}
int meters_present(uint32_t m) { return m < METER_N || (m < METER_SLOTS && dyn[m - METER_N].ok && pack_slot[m].status != 0xFFu); }
static const meter_def_t *mdef(uint32_t m)
{
    if (m < METER_N) return &meter_table[m];
    if (m >= METER_SLOTS) return &meter_table[0];
    const uint32_t k = m - METER_N;
    if (dyn_cached != k && dyn[k].ok) dyn_cache_build(k);
    return &dyn_def[k];
}
static uint32_t mid_of(uint32_t m) { return m < METER_N ? meter_manifest_id[m] : dyn[m - METER_N].mid; }
static uint32_t cmd_limit_of(uint32_t m) { return m < METER_N ? meter_cmd_budget[m] : dyn[m - METER_N].cmds; }
static uint32_t cyc_limit_of(uint32_t m) { return m < METER_N ? meter_cyc_limit[m] : dyn[m - METER_N].cyc; }
static uint32_t def_npre(uint32_t m) { return m < METER_N ? meter_table[m].npre : dyn[m - METER_N].npre; }
static void def_set_pre(uint32_t m, uint32_t p) { if (m < METER_N) meter_table[m].set_pre(p); else dyn_set_pre(m - METER_N, p); }
static const char *def_pre_name(uint32_t m, uint32_t p) { return m < METER_N ? meter_table[m].pre_name(p) : dyn_pre_name(m - METER_N, p); }
static void def_apply(uint32_t m) { if (m < METER_N) meter_table[m].apply(); }              /* a pack reads its parameters from the host's table at every tick */
uint32_t meters_thumb_len(uint32_t m) { md_hdr_t h; return m >= METER_N && m < METER_SLOTS && dyn[m - METER_N].ok && md_header(desc_rd, &dyn[m - METER_N], dyn[m - METER_N].len, &h) ? h.rle_len : 0u; }
int meters_thumb_rle(uint32_t m, uint32_t i, uint8_t *v)                                       /* the preview of a pack-only meter: byte i of its run-length stream */
{
    md_hdr_t h; if (m < METER_N || m >= METER_SLOTS || !dyn[m - METER_N].ok || !md_header(desc_rd, &dyn[m - METER_N], dyn[m - METER_N].len, &h)) return 0;
    return md_rle_byte(desc_rd, &dyn[m - METER_N], &h, i, v);
}
int meters_thumb_col(uint32_t m, uint32_t c, uint16_t *fixed, uint8_t *a, uint8_t *b, uint8_t *t, uint8_t *n)       /* ... and palette entry c (the layout of generated/meter_thumbs.h mth_col_t) */
{
    md_hdr_t h; md_col_t e; if (m < METER_N || m >= METER_SLOTS || !dyn[m - METER_N].ok || !md_header(desc_rd, &dyn[m - METER_N], dyn[m - METER_N].len, &h) || !md_col(desc_rd, &dyn[m - METER_N], &h, c, &e)) return 0;
    *fixed = e.fixed; *a = e.a; *b = e.b; *t = e.t; *n = e.n; return 1;
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
uint32_t meters_list_count(void) { uint32_t n = 0; for (uint32_t m = 0; m < METER_SLOTS; m++) n += (uint32_t)meters_present(m); return n; }
uint32_t meters_list_row(uint32_t i) { for (uint32_t m = 0; m < METER_SLOTS; m++) if (meters_present(m) && i-- == 0u) return m; return 0u; }       /* the i-th meter that is there -> its row (the saved meter index) */
uint32_t meters_list_index(uint32_t row) { uint32_t n = 0; for (uint32_t m = 0; m < row && m < METER_SLOTS; m++) n += (uint32_t)meters_present(m); return n; }       /* the other way */
meter_stats_t meter_stats;
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
    force_next = 0;
    const int comp = composed(mdef(cur)) && gfx_compose_begin(bx, by, bw, bh);
    const uint32_t cmd0 = gfx_cmds, scan0 = REG_RD(R_SCAN);
    gfx_touch_reset();
#ifdef P2_PACKS
    if (pack_ok(cur)) {
        const uint32_t tp = cycles(); pack_tick(&in); const uint32_t took = cycles() - tp;
        if (!in.force) {                                                              /* a forced repaint is the costliest picture by design: not judged */
            if (took > cyc_limit_of(cur)) meter_stats.overruns++;
            if (ps_judge(&pack_strike[cur], took, cyc_limit_of(cur))) { pack_slot[cur].status = MPK_E_STRIKE; force_next = 1; meter_stats.struck++; }      /* struck out: the built-in code (or nothing) from the next picture */
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
    if (comp) { const uint32_t c0 = cycles(); gfx_compose_present(bx, by, bw, bh); meter_stats.compose_cyc = cycles() - c0; }
    const uint32_t spent = cycles() - t;
    meter_stats.ticks++; meter_stats.last_cyc = spent;
    if (spent > meter_stats.max_cyc[cur]) meter_stats.max_cyc[cur] = spent;
}
