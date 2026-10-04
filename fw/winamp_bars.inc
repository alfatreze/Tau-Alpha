/* Winamp Bars: the meter's drawing code and its state (the per-band ballistics and what is on screen). A separate file so that fw/player.c (the built-in meter) and
 * fw/meter_pack_winamp_bars.c (the loadable pack, docs/features/meters/METER_PACKS.md) compile exactly the same source. Needs, from the includer: mtr_in_t, the
 * parameter accessor MV_WINAMP_BARS(name), fig_rect/fig_bar, blit_probe_ensure()/BLIT_READY(), ui_accent, UI_WHITE, wviz_force, WVIZ_BANDS_MIN/MAX, SPEC_BANDS and
 * meter_core.h. */
static uint8_t  wviz_disp[WVIZ_BANDS_MAX];        /* displayed height, 0..255 */
static int16_t  wviz_vel[WVIZ_BANDS_MAX];         /* spring mode velocity only */
static mtr_peak_t wviz_pk[WVIZ_BANDS_MAX];       /* peak cap state per band: level, gravity fall speed, ms of hold left */
static uint8_t  wviz_drawn[WVIZ_BANDS_MAX], wviz_peak_drawn[WVIZ_BANDS_MAX];
static uint16_t wviz_bh_d[WVIZ_BANDS_MAX], wviz_ph_d[WVIZ_BANDS_MAX];   /* what is on screen per band, in pixels: bar height and peak-marker height (0 = none): the redraw paints only the difference */
static uint32_t wviz_geo[7];                                           /* x, y, w, h, bands, accent, bg the pixels were drawn for: any change repaints everything */

COLD_FN3 static void wviz_bars_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint16_t bg = in->bg;
    const uint32_t force = in->force;
    uint32_t bands = MV_WINAMP_BARS(BANDS);
    if (bands < WVIZ_BANDS_MIN) bands = WVIZ_BANDS_MIN;
    if (bands > WVIZ_BANDS_MAX) bands = WVIZ_BANDS_MAX;
    uint32_t gap  = 2u;
    uint32_t colw = (w > gap * (bands - 1u)) ? (w - gap * (bands - 1u)) / bands : 1u;

    /* B-234: context just changed (preset applied, mode switched, page opened/
     * closed) -- wipe the whole preview rect once so no leftover pixels from a
     * DIFFERENT geometry or a different mode's draw survive, then force every
     * band to redraw below regardless of the change cache. */
    /* Delta repaint (METER_07): a band whose height changed paints only the rows between its old and new height (one rectangle) instead of the whole column, so
     * the draw engine writes a few hundred pixels per band per frame instead of 22 x 323 in fullscreen. Anything that makes the pixels on screen unreliable
     * (a forced repaint, a new geometry, accent or ground colour) clears the box and repaints every band in full, as before. */
    const uint32_t geo[7] = { x0, y, w, h, bands, ui_accent, bg };
    uint32_t full = force;
    for (uint32_t i = 0; i < 7u; i++) if (wviz_geo[i] != geo[i]) { full = 1u; wviz_geo[i] = geo[i]; }
    if (full) fig_rect(x0, y, w, h, bg);
    blit_probe_ensure();
    const int hw_bar = BLIT_READY();

    for (uint32_t b = 0; b < bands; b++) {
        uint32_t target = mtr_band_target(in->spec, SPEC_BANDS, bands, b);
        if (in->paused) target = 0u;

        uint32_t rate = (target >= wviz_disp[b]) ? MV_WINAMP_BARS(ATTACK) : MV_WINAMP_BARS(RELEASE);
        wviz_disp[b] = mtr_ease(wviz_disp[b], (uint8_t)target, MV_WINAMP_BARS(EASE), rate, &wviz_vel[b]);

        const mtr_peak_cfg_t pcfg = { MV_WINAMP_BARS(PEAK_ON), MV_WINAMP_BARS(PEAK_GRAVITY), MV_WINAMP_BARS(PEAK_HOLD_MS), MV_WINAMP_BARS(PEAK_FALL) };
        mtr_peak_step(&wviz_pk[b], wviz_disp[b], &pcfg, in->dt_ms);

        if (!mtr_delta(&wviz_drawn[b], &wviz_peak_drawn[b], wviz_disp[b], wviz_pk[b].peak, full)) continue;

        const uint32_t x = x0 + b * (colw + gap);
        uint32_t bh = (wviz_disp[b] * h) / 255u;
        if (bh < 2u) bh = 2u;
        uint32_t pnew = 0u;
        if (MV_WINAMP_BARS(PEAK_ON)) {
            const uint32_t ph = (wviz_pk[b].peak * h) / 255u;
            if (ph > bh + 1u && ph < h) pnew = ph;
        }
        const uint32_t old = wviz_bh_d[b], pold = wviz_ph_d[b];

        if (full || !old) {                                   /* nothing reliable on screen for this band: the whole column */
            if (hw_bar) {
                fig_bar(x, y, colw, h, bh, ui_accent, bg);
            } else {
                fig_rect(x, y + h - bh, colw, bh, ui_accent);
                if (h > bh) fig_rect(x, y, colw, h - bh, bg);
            }
            if (pnew) fig_rect(x, y + h - pnew, colw, 1u, UI_WHITE);
        } else {
            if (bh > old)      fig_rect(x, y + h - bh, colw, bh - old, ui_accent);        /* grew: the new rows */
            else if (bh < old) fig_rect(x, y + h - old, colw, old - bh, bg);              /* shrank: the rows it gave up */
            if (pold != pnew) {
                if (pold > bh) fig_rect(x, y + h - pold, colw, 1u, bg);                   /* old marker above the bar (inside it, the growth already covered it) */
                if (pnew) fig_rect(x, y + h - pnew, colw, 1u, UI_WHITE);
            }
        }
        wviz_bh_d[b] = (uint16_t)bh; wviz_ph_d[b] = (uint16_t)pnew;
    }
    wviz_force = 0u;
}
