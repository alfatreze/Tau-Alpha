#pragma GCC optimize ("Os")
#include "meter.h"
#include "theme.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../generated/meter_presets.h"
#include "../cores/ramp_core.h"
#define CHL_DATA
#include "../../fw/chladni_core.h"

/* Chladni meter (docs/CHLADNI_METER_SPEC.md): nodal-line figures of a vibrating plate driven by the spectrum. The maths is the old player's portable, host-tested core (fw/chladni_core.h). How a figure reaches the
 * screen, using only primitives proven on the Pocket: (1) chl_render streams one tile a row of cell levels at a time, packed two pixels to a word and written through the SDRAM mailbox into off-screen rows
 * CHL_PLANE_Y..; (2) ONE scaled blit puts that plane 3x (or 2x) into the box's top-left tile; (3) plain blits double the tile across the box (at most 120 words a copy; every mode of a figure is from one parity
 * class, so the field repeats exactly across a tile edge). Self-test: the first figure runs a known-answer copy that tells which half of a mailbox word is the lower address; if the path does not behave as assumed the
 * meter says so on its own face instead of drawing garbage. Player 2 draws with the default blit bases (no double buffer), so the bases are never touched. */
#define CHL_PLANE_Y    984u             /* off-screen rows 984..1023: the tile plane (Ry <= 37) and the probe word */
#define CHL_PROBE_ROW  (CHL_PLANE_Y + 39u)
#define CHL_PROBE_DX   496u
#define CHL_PROBE_DY   2u
#define CHL_PERIOD_MS  66u              /* about 15 figures a second */

static chl_state_t         st;
static chl_cfg_t           cfg;
static const chl_preset_t *pre;
static uint8_t layout_cur = 0xFFu, layout_want, paint_want;
static int16_t ring[3u * CHL_MAX_RX], half[CHL_HALF_N], cxm[CHL_MAX_K * CHL_MAX_RX], cxn[CHL_MAX_K * CHL_MAX_RX];
static uint8_t lvl[CHL_MAX_RX];
static uint8_t ok;                      /* 0 = not probed yet, 1 = ready, 2 = unavailable on this hardware */
static uint8_t swap;                    /* 1: the mailbox's high half is the LOWER SDRAM address */
static uint8_t shed;
static uint32_t now_ms, last_draw, drawn_n, skipped_n;
uint32_t chladni_drawn(void) { return drawn_n; }
uint32_t chladni_unavailable(void) { return ok == 2u; }
uint16_t meter_chladni_mv[MPP_CHL_N];                           /* the live parameters, in the manifest's order (the Configure page edits these) */
const char *meter_chladni_preset_name(uint32_t i) { return mpp_chl_name[i < MPP_CHL_PRESETS ? i : 0u]; }
void meter_chladni_apply(void)
{
    const uint16_t *v = meter_chladni_mv;
    layout_want = (uint8_t)(v[MPP_CHL_LAYOUT] < CHL_PRESET_N ? v[MPP_CHL_LAYOUT] : 0u); paint_want = (uint8_t)(v[MPP_CHL_PAINT] < 3u ? v[MPP_CHL_PAINT] : 0u);   /* a new layout restarts the figure at the next tick */
}
void meter_chladni_preset(uint32_t i)
{
    for (uint32_t k = 0; k < MPP_CHL_N; k++) meter_chladni_mv[k] = mpp_chl[i < MPP_CHL_PRESETS ? i : 0u][k];
    meter_chladni_apply();
}

/* Known-answer probe: two DIFFERENT pixels (0x07E0 low half, 0xF800 high half) at the probe word, the engine copies that word elsewhere, the mailbox reads it back. A low half of 0x07E0 means the mailbox's low
 * half is the lower address; 0xF800 in the high half means the halves are swapped; anything else means the path does not behave as assumed. */
static void probe(void)
{
    const uint32_t src = CHL_PROBE_ROW * GFX_STRIDE, dst = CHL_PROBE_DY * GFX_STRIDE + CHL_PROBE_DX;
    uint32_t r = 0;
    ok = 2u;
    if (gfx_mb_write(src, 0xF80007E0u)) return;
    gfx_blit_abs(0u, CHL_PROBE_ROW, CHL_PROBE_DX, CHL_PROBE_DY, 1u, 1u);
    gfx_fence();
    if (gfx_mb_read(dst, &r)) return;
    if ((r & 0xFFFFu) == 0x07E0u)  { swap = 0u; ok = 1u; }
    else if ((r >> 16) == 0xF800u) { swap = 1u; ok = 1u; }
}

struct ctx { uint16_t pal[4]; uint32_t rx; int ok; };
static void row(uint32_t j, const uint8_t *l, void *ud)           /* chl_render's row callback: row j of cell colours into the plane */
{
    struct ctx *c = (struct ctx *)ud;
    if (!c->ok) return;
#ifdef CHL_TRACE
    CHL_TRACE(j, l, c->rx);
#endif
    const uint32_t base = (CHL_PLANE_Y + j) * GFX_STRIDE;
    for (uint32_t i = 0; i < c->rx; i += 2u) {
        uint32_t p0 = c->pal[l[i] & 3u], p1 = (i + 1u < c->rx) ? c->pal[l[i + 1u] & 3u] : c->pal[0];
        uint32_t d = swap ? ((p0 << 16) | p1) : ((p1 << 16) | p0);
        if (gfx_mb_write(base + i, d)) { c->ok = 0; return; }
    }
}

/* Double the tile across the box: rightwards over the tile's own rows, then downwards over the full width. Copies are at most 120 words wide; source and destination never overlap (n <= have). */
static void replicate(uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th, uint32_t w, uint32_t h)
{
    const uint32_t rows = th < h ? th : h;
    uint32_t have = tw;
    while (have < w) {
        uint32_t n = have < w - have ? have : w - have, done = 0u;
        while (done < n) { uint32_t c = n - done; if (c > 120u) c = 120u; gfx_blit(x0 + done, y0, x0 + have + done, y0, c, rows); done += c; }
        have += n;
    }
    have = rows;
    while (have < h) {
        uint32_t n = have < h - have ? have : h - have;
        for (uint32_t cx = 0u; cx < w; cx += 120u) { uint32_t c = w - cx < 120u ? w - cx : 120u; gfx_blit(x0 + cx, y0, x0 + cx, y0 + have, c, n); }
        have += n;
    }
}

static void tunables(void)
{
    const uint16_t *pv = meter_chladni_mv;
    cfg.eps0 = pv[MPP_CHL_LINE_WIDTH]; cfg.topk = (uint8_t)pv[MPP_CHL_MODES]; cfg.up_q12_s = pv[MPP_CHL_RISE]; cfg.dn_q12_s = pv[MPP_CHL_FALL];
    cfg.morph_base = pv[MPP_CHL_MORPH]; cfg.morph_gain = pv[MPP_CHL_MORPH_GAIN]; cfg.sens_q4 = (uint8_t)pv[MPP_CHL_TRIGGER]; cfg.refr_ms = pv[MPP_CHL_REFRACTORY]; cfg.tonal = (uint8_t)pv[MPP_CHL_TONAL];
}
static void sync(void)
{
    if (layout_want != layout_cur || !pre) {                       /* a new layout is a new tile size, so a new plane: restart the state */
        pre = &chl_presets[layout_want];
        layout_cur = layout_want;
        cfg = pre->c;                                              /* the geometry-bound fields (mode limit, fold) */
        tunables();
        chl_init(&st, &cfg);
        last_draw = now_ms;
    } else tunables();                                             /* an edit shows at once */
}

void meter_chladni_tick(const mtr_in_t *in)
{
    now_ms += in->dt_ms;
    if (in->force) { gfx_rect(in->x, in->y, in->w, in->h, in->bg); last_draw = now_ms - CHL_PERIOD_MS; }
    sync();
    if (ok == 2u) { if (in->force) gfx_text(in->x + 8u, in->y + in->h / 2u - 8u, "CHLADNI UNAVAILABLE", UI_RED, in->bg); return; }
    if (!in->force && (uint32_t)(now_ms - last_draw) < CHL_PERIOD_MS) return;
    if (ok == 0u) { probe(); if (ok != 1u) { gfx_text(in->x + 8u, in->y + in->h / 2u - 8u, "CHLADNI UNAVAILABLE", UI_RED, in->bg); return; } }
    const uint32_t dt = in->force ? CHL_PERIOD_MS : (uint32_t)(now_ms - last_draw);
    last_draw = now_ms;
    const uint32_t t0 = cycles();
    chl_detect(&st, &cfg, in->spec, now_ms);
    chl_update(&st, &cfg, in->spec, dt);
    chl_cfg_t sc = cfg;                                            /* load shedding: fewer blended modes when a figure takes long, the frame rate stays */
    sc.topk = (uint8_t)(cfg.topk > shed + 1u ? cfg.topk - shed : 1u);
    chl_mode_t md[CHL_MAX_K];
    const uint32_t K = chl_select(&st, &sc, md), rx = pre->Rx, ry = pre->Ry, cell = pre->cell;
    struct ctx ctx; ctx.rx = rx; ctx.ok = 1;
    if (paint_want == 1u)      { ctx.pal[0] = 0x3800u; ctx.pal[1] = 0xC2C1u; ctx.pal[2] = 0xFD42u; ctx.pal[3] = 0xFF55u; }   /* EMBER */
    else if (paint_want == 2u) { ctx.pal[0] = 0x0806u; ctx.pal[1] = 0x1237u; ctx.pal[2] = 0x25F7u; ctx.pal[3] = 0xBF7Eu; }   /* OCEAN */
    else { ctx.pal[0] = in->bg; ctx.pal[1] = rc_ramp(in->bg, UI_ACCENT, 1u, 3u); ctx.pal[2] = rc_ramp(in->bg, UI_ACCENT, 2u, 3u); ctx.pal[3] = UI_ACCENT; }   /* LINE */
    chl_render(md, K, rx, ry, chl_hw_q8(&st, &cfg, rx), ring, cfg.fold ? half : (int16_t *)0, cxm, cxn, lvl, row, &ctx);
    if (!ctx.ok) { ok = 2u; gfx_text(in->x + 8u, in->y + in->h / 2u - 8u, "CHLADNI UNAVAILABLE", UI_RED, in->bg); return; }
    /* the scaled blit's w/h are SOURCE cells (the engine scales them, clamped at 127): the plane's own size, cut so the scaled tile never leaves the box */
    const uint32_t sw = rx * cell > 127u ? 127u / cell : rx, sh = ry * cell > in->h ? in->h / cell : ry, tw = sw * cell, th = sh * cell;
    gfx_sblit(0u, CHL_PLANE_Y, in->x, in->y, sw, sh, cell);
    replicate(in->x, in->y, tw, th, in->w, in->h);
    gfx_fence();                                                   /* the next figure rewrites the plane: every blit of this one must have read it first */
    const uint32_t ms = (cycles() - t0) / (CLK_HZ / 1000u);
    shed = ms > 14u ? 2u : ms > 9u ? (shed < 1u ? 1u : shed) : ms < 5u ? 0u : shed;
    drawn_n++;
}
