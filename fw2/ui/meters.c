#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../cores/spectrum_core.h"
#include "../generated/meter_presets.h"
#include "../generated/lw_presets.h"
#include "../cores/metr_core.h"

#define R_SPEC_IDX  0x800000DCu
#define R_SPEC_DATA 0x800000E0u
_Static_assert(MTR_BANDS == SPEC_BANDS, "the meter inputs carry one value per hardware band");

extern void meter_winamp_bars_tick(const mtr_in_t *in);
extern void meter_winamp_scope_tick(const mtr_in_t *in);
extern void meter_vu_master_tick(const mtr_in_t *in);
extern void meter_chladni_tick(const mtr_in_t *in);
extern void meter_layered_wave_tick(const mtr_in_t *in);
#define PRESET_API(m) extern void meter_##m##_preset(uint32_t); extern const char *meter_##m##_preset_name(uint32_t); extern uint16_t meter_##m##_mv[]; extern void meter_##m##_apply(void)
PRESET_API(winamp_bars); PRESET_API(winamp_scope); PRESET_API(chladni); PRESET_API(layered_wave); PRESET_API(vu_master);
#define P(m, n, d, np) n, meter_##m##_preset, meter_##m##_preset_name, meter_##m##_mv, d, np, meter_##m##_apply
const meter_def_t meter_table[] = {
    { "WINAMP BARS", meter_winamp_bars_tick, P(winamp_bars, 5, mpd_bars, MPD_BARS_N) },
    { "WINAMP SCOPE", meter_winamp_scope_tick, P(winamp_scope, 5, mpd_scope, MPD_SCOPE_N) },
    { "CHLADNI", meter_chladni_tick, P(chladni, 4, mpd_chl, MPD_CHL_N) },
    { "LAYERED WAVE", meter_layered_wave_tick, P(layered_wave, 8, mpd_lw, MPD_LW_N) },
    { "VU MASTER", meter_vu_master_tick, P(vu_master, 3, mpd_vu, MPD_VU_N) },
};
const uint32_t meter_count = sizeof meter_table / sizeof meter_table[0];

static uint32_t cur, bx, by, bw, bh, win_seen = 0xFFFFFFFFu, frame, last_t, force_next;
static uint8_t lvl[MTR_BANDS];
extern uint32_t mp3play_paused_now(void);          /* mp3play.c: 1 when nothing is being heard */

static uint8_t pre[8], custom[8];                                  /* the selected preset of each meter */
/* User presets (the METR section of tau-assets.bin) replace a meter's built-in ones. The section is copied to PSRAM at boot (the tag buffer it arrives in is reused); a preset is read back a word at a time
 * (the window takes word accesses only) into a small buffer and applied through the same metr_core.h that the host test checks against the Python writer. */
#ifndef METR_PSRAM
#define METR_PSRAM ((volatile uint32_t *)(uintptr_t)0xA4550000u)
#endif
#define METR_MAX_BYTES 8192u
static metr_e_t ue[8]; static uint8_t user_n[8];
static metr_p_t mp[8][40];                                         /* the parameter shapes of each meter, from the descriptors */
static uint8_t is_wide(const mpd_t *p) { return p->type == 0u && p->max > 255u; }
static void shapes(uint32_t m) { const meter_def_t *d = &meter_table[m]; for (uint32_t i = 0; i < d->np && i < 40u; i++) { mp[m][i].min = d->pd[i].min; mp[m][i].max = d->pd[i].max; mp[m][i].kind = d->pd[i].type; mp[m][i].enum_n = d->pd[i].nval; mp[m][i].wide = is_wide(&d->pd[i]); } }
uint32_t meters_npre(uint32_t m) { return m >= meter_count ? 1u : user_n[m] ? user_n[m] : meter_table[m].npre; }
static void user_preset(uint32_t m, uint32_t p, uint16_t *vals, char *name)       /* reads preset p of meter m from the PSRAM copy */
{
    uint8_t buf[96]; metr_e_t e = ue[m]; const uint32_t at = e.off + p * e.psize, w0 = at & ~3u, w1 = (at + e.psize + 3u) & ~3u;
    for (uint32_t w = w0; w < w1; w += 4u) { const uint32_t v = METR_PSRAM[w >> 2]; for (uint32_t k = 0; k < 4u && w - w0 + k < sizeof buf; k++) buf[w - w0 + k] = (uint8_t)(v >> (8u * k)); }
    e.off = at - w0; e.psize = 0u;
    if (name) metr_name(buf, &e, 0u, name);
    if (vals) metr_values(buf, &e, 0u, mp[m], meter_table[m].np, vals);
}
void meters_init(void) { cur = 0; for (uint32_t i = 0; i < meter_count; i++) { pre[i] = 0; custom[i] = 0; user_n[i] = 0; shapes(i); meter_table[i].set_pre(0u); } }
uint32_t meters_metr_load(const uint8_t *src, uint32_t n)       /* returns the number of meters whose presets were replaced; the section is validated first */
{
    static const uint8_t ids[8] = { 12, 13, 14, 16, 15, 0, 0, 0 };       /* the manifest's meter id of each table entry, in the table's order */
    if (n > METR_MAX_BYTES || metr_check(src, n)) return 0;
    uint32_t done = 0;
    for (uint32_t m = 0; m < meter_count; m++) {
        metr_e_t e;
        if (!metr_find(src, n, ids[m], mp[m], meter_table[m].np, &e)) continue;
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
    pre[m] = (uint8_t)p; custom[m] = 0u;
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
void meters_params_changed(uint32_t m) { if (m >= meter_count) return; custom[m] = 1u; meter_table[m].apply(); if (m == cur) force_next = 1; }
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

void meters_tick(int allowed)
{
    if (!caps.spec || !allowed || !bw) return;
    uint32_t m[MTR_BANDS];
    if (!fetch(m)) return;
    for (uint32_t b = 0; b < MTR_BANDS; b++) lvl[b] = (uint8_t)spec_ballistics(lvl[b], spec_level(m[b], b));
    const uint32_t t = cycles(), dt = (t - last_t) / (CLK_HZ / 1000u); last_t = t;
    uint32_t pl = 0, pr = 0;
    if (caps.wave) { const uint32_t pk = REG_RD(R_WAVE_PK); pl = ((pk & 0xFFFFu) * 3u) / 4u; pr = ((pk >> 16) * 3u) / 4u; REG_WR(R_WAVE_CTL, 1u); }      /* peaks since the last tick, then cleared */
    mtr_in_t in = { lvl, frame++, (uint16_t)(dt > 200u ? 200u : dt), (uint16_t)bx, (uint16_t)by, (uint16_t)bw, (uint16_t)bh, th_role[TR_BG_BOTTOM], (uint8_t)force_next, (uint8_t)mp3play_paused_now(), pl, pr };
    force_next = 0;
    meter_table[cur].tick(&in);
}
