/* ==================================================================== CHLADNI meter (VIZ_CHLADNI)
 * Nodal-line figures of a vibrating plate, driven by the spectrum: the strongest bands pick the plate's modes, the
 * lines are the places where the plate does not move. Design, constants and measurements: docs/CHLADNI_METER_SPEC.md and
 * tools/lab/chladni_lab.html; the maths is fw/chladni_core.h (portable, tested natively, no floating point).
 *
 * HOW ONE FIGURE REACHES THE SCREEN (chosen to use only primitives already proven on the Pocket):
 *   1. chl_render() streams the tile one row of cell levels (0..3) at a time; each row is packed two pixels to a
 *      32-bit word and written through the SDRAM MAILBOX into off-screen framebuffer rows CHL_PLANE_Y.. (below the art and
 *      thumbnail stashes). The mailbox is the path blit_probe() already uses; the CPU window cannot reach these rows.
 *   2. ONE OP_SBLIT scales that plane 3x into the top-left tile of the meter box (the same op the Blit Test exercises).
 *   3. OP_BLIT copies replicate the tile across the box (99 -> 198 -> 360 px, at most 127 words per copy). Every mode of a
 *      figure is from one parity class (chl_select guarantees it), so the field repeats exactly across a tile edge.
 * Nothing here writes the sticky blit fields (R_BLT_IDX/R_BLT_DATA): those were implicated in two earlier hangs and have
 * never been proven on hardware, so this module stays on default addressing.
 *
 * SAFETY: it runs only while VIZ_CHLADNI is selected. The first use runs chl_probe() (never at boot): it proves the mailbox
 * half order against the engine's own addressing with a known-answer copy, and if that does not come out right the meter
 * disables itself and says so instead of drawing garbage. Updates are skipped while the audio FIFO is low (meter_afford)
 * and gated by the beam (the meter block's helios_rows_safe check), 15 figures a second. B-276. */
#include "chladni_core.h"

/* Everything except the two tiny mailbox helpers is COLD code (PSRAM, through the I-cache): the kernel is about 4.6 KB and runs about a
 * millisecond per figure. `flatten` folds every callee (the whole of chladni_core.h) into the cold entry point, so no separate hot copy
 * of any core function survives. Callers are all cold (ui_draw_dynamic_cold, fullscreen.inc) or gated on COLD_READY(). */
#define CHL_COLD COLD_FN3 __attribute__((flatten))

#define CHL_PLANE_Y    984u             /* stash rows 984..1023: the tile plane (Ry <= 37) and the probe word; the thumbnail stash ends at row 984 */
#define CHL_PROBE_ROW  (CHL_PLANE_Y + 39u)
#define CHL_PROBE_DX   496u             /* probe destination: off-screen columns, row 2 (clear of the bg-strip cache rows) */
#define CHL_PROBE_DY   2u
#define CHL_PERIOD_MS  66u              /* about 15 figures a second */

static int meter_afford(void);          /* defined later in this file */

static chl_state_t         chl_st;
static chl_cfg_t           chl_cfg;
static const chl_preset_t *chl_pre;
static uint8_t chl_pre_idx;
static const char *const chl_pre_toast[CHL_PRESET_N] = { "PRESET: LATTICE", "PRESET: SHIMMER" };
static int16_t chl_ring[3u * CHL_MAX_RX], chl_half[CHL_HALF_N], chl_cxm[CHL_MAX_K * CHL_MAX_RX], chl_cxn[CHL_MAX_K * CHL_MAX_RX];
static uint8_t chl_lvl[CHL_MAX_RX];
static uint8_t  chl_ok;                 /* 0 = not probed yet, 1 = ready, 2 = unavailable on this bitstream/hardware */
static uint8_t  chl_swap;               /* 1: the mailbox's high half is the LOWER SDRAM address */
static uint32_t chl_last_draw, chl_ms_acc, chl_ms_base;
static uint32_t chl_drawn_n, chl_skipped_n;

/* Milliseconds since first call, from the 60 MHz cycle counter; accumulated in whole milliseconds so the 71.6 s wrap of
 * cycles() never matters (same technique as ui_blank_touch()). */
static uint32_t chl_now_ms(void)
{
    uint32_t now = cycles(), d = now - chl_ms_base, ms = d / (CLK_HZ / 1000u);
    chl_ms_acc += ms;
    chl_ms_base += ms * (CLK_HZ / 1000u);
    return chl_ms_acc;
}

/* One 32-bit mailbox transaction = two adjacent 16-bit SDRAM words. Returns 0 on success, -1 on timeout. */
static int chl_mb_write(uint32_t word_addr, uint32_t data)
{
    REG(R_SDR_ADDR) = word_addr; REG(R_SDR_DATA) = data;
    REG(R_SDR_CTRL) = 0x3Fu;                                   /* start, write, all four lanes: the pattern blit_probe() proved */
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    return 0;
}

static int chl_mb_read(uint32_t word_addr, uint32_t *out)
{
    REG(R_SDR_ADDR) = word_addr; REG(R_SDR_CTRL) = 0x3Du;      /* start, read, all lanes */
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    *out = REG(R_SDR_RDATA);
    return 0;
}

/* Known-answer probe. Write two DIFFERENT pixels (0x07E0 low half, 0xF800 high half) at the probe word, have the draw engine
 * copy one word from there to a second place, and read that place back through the mailbox. If the mailbox's low half is the
 * lower address the copy holds 0x07E0 and comes back in the low half; if the halves are swapped it holds 0xF800 and comes back
 * in the high half. Anything else means the path does not behave as assumed and the meter stays off. */
CHL_COLD static void chl_probe(void)
{
    const uint32_t src = CHL_PROBE_ROW * FB_STRIDE, dst = CHL_PROBE_DY * FB_STRIDE + CHL_PROBE_DX;
    uint32_t r = 0;
    chl_ok = 2u;
    if (chl_mb_write(src, 0xF80007E0u)) return;
    fb_blit(0u, CHL_PROBE_ROW, CHL_PROBE_DX, CHL_PROBE_DY, 1u, 1u);
    fb_wait();
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 512u) { }                /* let the engine's write land, as blit_probe() does */
    if (chl_mb_read(dst, &r)) return;
    if ((r & 0xFFFFu) == 0x07E0u)      { chl_swap = 0u; chl_ok = 1u; }
    else if ((r >> 16) == 0xF800u)     { chl_swap = 1u; chl_ok = 1u; }
}

struct chl_ctx { uint16_t pal[4]; uint32_t rx; int ok; };

/* chl_render's row callback: pack row j of cell colours into the plane, two pixels per mailbox word. */
CHL_COLD static void chl_row(uint32_t j, const uint8_t *lvl, void *ud)
{
    struct chl_ctx *c = (struct chl_ctx *)ud;
    if (!c->ok) return;
    const uint32_t base = (CHL_PLANE_Y + j) * FB_STRIDE;
    for (uint32_t i = 0; i < c->rx; i += 2u) {
        uint32_t p0 = c->pal[lvl[i] & 3u], p1 = (i + 1u < c->rx) ? c->pal[lvl[i + 1u] & 3u] : c->pal[0];
        uint32_t d  = chl_swap ? ((p0 << 16) | p1) : ((p1 << 16) | p0);
        if (chl_mb_write(base + i, d)) { c->ok = 0; return; }
    }
}

/* Replicate the drawn tile across the w x h box, doubling: first rightwards over the tile's own rows, then downwards over the
 * full width. Every copy is at most 120 words wide, and source and destination never overlap (n <= have). */
CHL_COLD static void chl_replicate(uint32_t x0, uint32_t y0, uint32_t tw, uint32_t th, uint32_t w, uint32_t h)
{
    const uint32_t rows = (th < h) ? th : h;
    uint32_t have = tw;
    while (have < w) {
        uint32_t n = (have < w - have) ? have : (w - have), done = 0u;
        while (done < n) {
            uint32_t c = n - done; if (c > 120u) c = 120u;
            fb_blit(x0 + done, y0, x0 + have + done, y0, c, rows);
            done += c;
        }
        have += n;
    }
    have = rows;
    while (have < h) {
        uint32_t n = (have < h - have) ? have : (h - have);
        for (uint32_t cx = 0u; cx < w; cx += 120u) {
            uint32_t c = (w - cx < 120u) ? w - cx : 120u;
            fb_blit(x0 + cx, y0, x0 + cx, y0 + have, c, n);
        }
        have += n;
    }
}

CHL_COLD static void chladni_init(void)
{
    chl_pre = &chl_presets[chl_pre_idx];                       /* 0 Lattice (4 tiles across, calm), 1 Shimmer (8 across, follows melody) */
    chl_cfg = chl_pre->c;
    chl_init(&chl_st, &chl_cfg);
    chl_last_draw = 0u;
    chl_ms_base = cycles(); chl_ms_acc = 0u;
}

/* Draw one figure into the w x h box at (x0, y0). `ground` is the colour of the plate between the lines. Returns 1 if a figure
 * was drawn, 0 if it was skipped (rate limit, audio FIFO low) or the meter is unavailable. `force` redraws immediately. Callers
 * decide WHEN it is safe to draw (the beam check); this decides whether it is worth it. */
CHL_COLD static int chladni_tick_box(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, uint16_t ground, uint32_t force)
{
    if (chl_ok == 2u || UI_OVERLAY_UP) return 0;
    if (!chl_pre) chladni_init();
    const uint32_t now = chl_now_ms();
    if (!force && (uint32_t)(now - chl_last_draw) < CHL_PERIOD_MS) return 0;
    if (!meter_afford()) { chl_skipped_n++; return 0; }        /* the audio FIFO is low: no figure this time */
    if (chl_ok == 0u) {
        chl_probe();
        if (chl_ok != 1u) { ui_toast_msg("CHLADNI UNAVAILABLE"); return 0; }
    }
    const uint32_t dt = force ? CHL_PERIOD_MS : (uint32_t)(now - chl_last_draw);
    chl_last_draw = now;

    chl_detect(&chl_st, &chl_cfg, spec_lvl, now);
    chl_update(&chl_st, &chl_cfg, spec_lvl, dt);
    chl_mode_t md[CHL_MAX_K];
    /* Load shedding: the frame rate stays, the cost per figure drops. Every active mode costs one multiply-add per cell, so under
     * load fewer modes are blended (the figure gets a little plainer, not slower). 1 step above 90 percent CPU, 2 above 96, back
     * when under 75 (hysteresis, or it would flap: the figure itself is most of that load). ui_cpu_pct() is a one-second average. */
    static uint8_t shed;
    { const uint32_t cpu = ui_cpu_pct();
      if (cpu >= 96u) shed = 2u; else if (cpu >= 90u && shed < 1u) shed = 1u; else if (cpu <= 75u) shed = 0u; }
    chl_cfg_t sel_cfg = chl_cfg;
    sel_cfg.topk = (uint8_t)((chl_cfg.topk > shed + 1u) ? chl_cfg.topk - shed : 1u);
    const uint32_t K = chl_select(&chl_st, &sel_cfg, md);
    const uint32_t rx = chl_pre->Rx, ry = chl_pre->Ry, cell = chl_pre->cell;
    struct chl_ctx ctx;
    ctx.rx = rx; ctx.ok = 1;
    ctx.pal[0] = ground;
    ctx.pal[1] = ui_mix(ground, ui_accent, 1u, 3u);
    ctx.pal[2] = ui_mix(ground, ui_accent, 2u, 3u);
    ctx.pal[3] = ui_accent;
    chl_render(md, K, rx, ry, chl_hw_q8(&chl_st, &chl_cfg, rx), chl_ring, chl_cfg.fold ? chl_half : (int16_t *)0,
               chl_cxm, chl_cxn, chl_lvl, chl_row, &ctx);
    if (!ctx.ok) { chl_ok = 2u; ui_toast_msg("CHLADNI UNAVAILABLE"); return 0; }

    /* fb_sblit's w/h are SOURCE cells (the engine scales them to the output, clamped at 127): pass the plane's own size, cut so the
     * scaled tile never leaves the box. It used to pass the OUTPUT size, so the engine read past the plane (stale rows) and drew
     * ~16 rows of noise under the meter. */
    const uint32_t sw = (rx * cell > 127u) ? 127u / cell : rx, sh = (ry * cell > h) ? h / cell : ry;
    const uint32_t tw = sw * cell, th = sh * cell;
    fb_sblit(0u, CHL_PLANE_Y, x0, y0, sw, sh, cell == 3u ? 3u : cell == 2u ? 2u : 0u, cell == 3u ? 3u : cell == 2u ? 2u : 0u);
    chl_replicate(x0, y0, tw, th, w, h);
    chl_drawn_n++;
    return 1;
}

/* The normal meter box; called from the meter block of ui_draw_dynamic_cold() (already beam-gated). */
CHL_COLD static void chladni_tick(uint32_t x0, uint32_t y0, uint32_t ww, uint32_t force)
{
    (void)chladni_tick_box(x0, y0, ww, UI_WAVE_H, ui_grad_at(UI_WAVE_Y + UI_WAVE_H / 2u), force);
}

/* Select+X: the next preset. Returns the toast text. */
CHL_COLD static const char *chladni_next_preset(void)
{
    chl_pre_idx = (uint8_t)((chl_pre_idx + 1u) % CHL_PRESET_N);
    chl_pre = &chl_presets[chl_pre_idx];
    chl_cfg = chl_pre->c;
    chl_init(&chl_st, &chl_cfg);
    chl_last_draw = 0u;
    return chl_pre_toast[chl_pre_idx];
}
