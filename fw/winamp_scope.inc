/* Winamp Scope: the meter's drawing code and its state (the smoothed trace). A separate file so that fw/player.c (the built-in meter) and fw/meter_pack_winamp_scope.c
 * (the loadable pack, docs/features/meters/METER_PACKS.md) compile exactly the same source. Needs, from the includer: mtr_in_t, MV_WINAMP_SCOPE(name), fig_rect, ui_bg_restore,
 * ui_bg_blend, SCOPE_NOTE(did_blend, x, y), ui_fullscreen, ui_accent, UI_TRACK, SCOPE_UNIT, wviz_force, WAVE_COLS and meter_core.h. The history of the comments below is B-234..B-450. */
#ifndef WVIZ_SCOPE_Y_N
#define WVIZ_SCOPE_Y_N 256u
#endif
static int16_t  wviz_scope_y[WVIZ_SCOPE_Y_N];          /* smoothed scope trace, signed pixel offset */
static uint8_t  wviz_scope_init;

/* Classic Winamp oscilloscope -- reuses wav_v[]/SCOPE_UNIT and the span-per-
 * column draw exactly as VIZ_WAVE does, plus temporal smoothing. scope_trail
 * is accepted but has no visible effect yet: a real soft trail needs B5
 * alpha blend (shelved). Explicit geometry, same reason as wviz_bars_tick().
 *
 * B-234: `use_gradient` picks how the trace's old position is erased.
 * ui_bg_restore() copies from a gradient strip pre-rendered ONLY for the
 * player screen's own UI_WAVE_Y row range (fw/player.c's UI_BG_X/UI_BG_W
 * comment) -- calling it with a `y` outside that range (as the Configure
 * page's own preview position does) reads whatever garbage happens to sit in
 * that off-screen memory for those rows, which is the reported "Scope
 * preview garbled." The player screen passes use_gradient=1 (its call site's
 * y IS UI_WAVE_Y, so the cache is valid there); Configure passes 0 and a flat
 * panel colour, matching how wviz_bars_tick() already takes an explicit bg
 * for exactly this reason. */
COLD_FN3 static void wviz_scope_tick(const mtr_in_t *in, int use_gradient)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint16_t bg = in->bg;
    const int32_t  ey = (int32_t)(h / 2u) - 1;
    const uint32_t cy = y + h / 2u;

    /* B-234: context just changed -- re-seed the smoothing state cleanly
     * instead of lerping from a stale wviz_scope_y[] left over from a
     * different geometry or a different preset's smoothing amount. */
    if (in->force) { wviz_scope_init = 0u; wviz_force = 0u; }

    /* B-298/B-300/B-301 STOPGAP: the hardware wave path below draws up to 256 columns x up to 3
     * fig_rect() calls each (measured DRAW STALL 34,663 ms cumulative, CPU LOAD 100%, real audible
     * jitter on hardware) -- about 21x wviz_bars_tick()'s own baseline. Forced off here, keeping the
     * 64-column software path (tools/meter_cost_estimate.py: 198 commands, within budget) until the
     * real fix -- batching the hardware path's per-column draws -- is designed and built. Flip this
     * back to `if (wave_hw)` once that lands; do not remove the hardware branch below, it is the
     * higher-resolution path this was always meant to be. */
#ifndef MTR_PACK   /* the pack never draws from the hardware wave block (dead in the firmware too, see above) */
    if (0 && wave_hw) {
        /* B-283: the hardware capture -- 256 columns, each the min..max envelope of the mono mix over 2 samples, started at
         * its own rising zero crossing. Normalised to the PREVIOUS frame's peak (the software path uses the current
         * frame's; the peak of consecutive windows barely moves, and this needs no second pass). The capture is re-armed
         * after every draw and read next time round; while it is still running the last picture stays. */
        static uint32_t sc_pk = 1u;
        static uint8_t  sc_armed;
        if (!sc_armed) { REG(R_WAVE_CTL) = 2u | (1u << 8); sc_armed = 1u; return; }   /* nothing captured yet */
        if (!in->paused && !(REG(R_WAVE_ST) & 2u)) {
            const int32_t smooth = MV_WINAMP_SCOPE(SCOPE_SMOOTH);
            const int percol = ui_fullscreen;               /* fullscreen: erase per column, see the software path */
            if (!percol) {
                if (use_gradient) ui_bg_restore(x0, y, w, h);
                else              fig_rect(x0, y, w, h, bg);
                fig_rect(x0, cy, w, 1, UI_TRACK);
            }
            uint32_t npk = 1u;
            for (uint32_t c = 0; c < WAVE_HW_COLS; c++) {
                REG(R_WAVE_IDX) = c;
                const uint32_t d = REG(R_WAVE_DATA);
                int32_t mn = (int16_t)(d >> 16), mx = (int16_t)(d & 0xFFFFu);
                const uint32_t am = (uint32_t)(mn < 0 ? -mn : mn), ax = (uint32_t)(mx < 0 ? -mx : mx);
                if (am > npk) npk = am;
                if (ax > npk) npk = ax;
                int32_t lo = (mn * ey) / (int32_t)sc_pk, hi = (mx * ey) / (int32_t)sc_pk;
                /* Both bounds, both variables: a DC-biased or unusually polarised capture window can
                 * make `lo` come out positive or `hi` come out negative, and either one being clamped
                 * on only one side let it escape [-ey, ey] -- which is exactly what drew outside the
                 * meter's own box (real corruption seen on hardware, not a timing/RTL issue). */
                if (lo < -ey) lo = -ey; else if (lo > ey) lo = ey;
                if (hi < -ey) hi = -ey; else if (hi > ey) hi = ey;
                if (lo > hi) lo = hi;
                int32_t mid = (lo + hi) / 2, half = (hi - lo) / 2;
                if (!wviz_scope_init) wviz_scope_y[c] = (int16_t)mid;
                wviz_scope_y[c] = (int16_t)(wviz_scope_y[c] + (((mid - wviz_scope_y[c]) * (100 - smooth)) / 100));
                mid = wviz_scope_y[c];
                const uint32_t cx = x0 + (c * w) / WAVE_HW_COLS, cxn = x0 + ((c + 1u) * w) / WAVE_HW_COLS;
                /* Defence in depth: clamp the whole rect to the box in SIGNED space before ever
                 * casting to uint32_t, so a value that still somehow escaped the lo/hi clamp above
                 * clips to the box instead of wrapping to a huge address (a negative int32_t cast to
                 * uint32_t is how a bounds miss here turns into drawing at a garbage location). */
                int32_t s_top = (int32_t)cy - (mid + half), s_bot = s_top + (2 * half) + 2;
                if (s_top < (int32_t)y)     s_top = (int32_t)y;
                if (s_bot > (int32_t)(y + h)) s_bot = (int32_t)(y + h);
                if (s_bot < s_top) s_bot = s_top;
                const uint32_t top = (uint32_t)s_top, rh = (uint32_t)(s_bot - s_top);
                const uint32_t cwid = (cxn > cx) ? (cxn - cx) : 1u;
                if (percol) { fig_rect(cx, y, cwid, h, bg); fig_rect(cx, cy, cwid, 1, UI_TRACK); }
                fig_rect(cx, top, cwid, rh, ui_accent);
            }
            sc_pk = npk;
            wviz_scope_init = 1u;
            REG(R_WAVE_CTL) = 2u | (1u << 8);              /* arm the next capture: 2 samples per column */
        }
        if (in->paused) wviz_scope_init = 0u;
        return;
    }
#endif


    /* Fullscreen: erase COLUMN BY COLUMN, each just before its own trace segment, instead of one clear of the whole box. A
     * whole-box clear is only tear-free if the beam is kept out of the way (which is what starved the fullscreen frame rate), and
     * leaves a blank frame on screen if the beam catches it between the clear and the redraw. Per column, every column is always
     * either the old picture or the new one. The same pixel count; more, smaller commands. */
    const int percol = ui_fullscreen;
    if (!percol) {
        /* B-334: a trail. With the blend bitstream the old trace is faded toward the background instead of erased: `trail` % of it survives
         * each frame (the background strip is blended over the box with the remaining weight). 0, no blend bitstream, or the Configure preview
         * (flat background) erases as before.
         * B-413: neither the ui_bg_ready race (B-411) nor the sticky-base fence audit (B-412) fixed the
         * owner's reported "accumulates after a fullscreen/Configure visit" -- rather than guess a third
         * time, capture WHICH branch actually ran, live, so the next repro tells us directly instead of
         * needing another theory. Counted only when a blend was genuinely attempted (use_gradient && trail
         * && !paused all true), matching did_blend's own short-circuit exactly -- fullscreen/Configure's own
         * legitimate use_gradient=0 skip is not counted as a "failure" here, only a real attempted-and-failed
         * blend is. */
        const uint32_t trail = (uint32_t)MV_WINAMP_SCOPE(SCOPE_TRAIL);
        const int attempt = use_gradient && trail && !in->paused;
        const int did_blend = attempt && ui_bg_blend(x0, y, w, h, (100u - trail) * 256u / 100u);
        if (attempt) SCOPE_NOTE(did_blend, x0 + w / 2u, y + 8u);   /* the Info diagnostics (blend ok/fail, strip, pixel history): the firmware's or, in a pack, the host's (B-413..B-449) */
        if (!did_blend) {
            if (use_gradient) ui_bg_restore(x0, y, w, h);
            else              fig_rect(x0, y, w, h, bg);
        }
        fig_rect(x0, cy, w, 1, UI_TRACK);
    }

    if (!in->paused) {
        int32_t smooth = MV_WINAMP_SCOPE(SCOPE_SMOOTH);      /* 0..90 */
        int32_t prev_y = 0;
        for (uint32_t c = 0; c < WAVE_COLS; c++) {
            uint32_t cx, cw;
            mtr_col_cw(x0, w, WAVE_COLS, c, &cx, &cw);

            int32_t raw = mtr_scale_s(in->wave[c], ey, SCOPE_UNIT);
            if (!wviz_scope_init) wviz_scope_y[c] = (int16_t)raw;
            wviz_scope_y[c] = (int16_t)(wviz_scope_y[c]
                             + (((raw - wviz_scope_y[c]) * (100 - smooth)) / 100));
            int32_t v = wviz_scope_y[c];

            int32_t a  = (c == 0) ? v : prev_y;
            int32_t lo = (a < v) ? a : v;
            int32_t hi = (a < v) ? v : a;
            prev_y = v;

            /* Same defence in depth as the hardware path above: clamp in signed space before the
             * uint32_t cast, so this can never draw outside its own box. */
            int32_t s_top = (int32_t)cy - hi, s_bot = s_top + (hi - lo) + 2;
            if (s_top < (int32_t)y)     s_top = (int32_t)y;
            if (s_bot > (int32_t)(y + h)) s_bot = (int32_t)(y + h);
            if (s_bot < s_top) s_bot = s_top;
            const uint32_t top = (uint32_t)s_top, rh = (uint32_t)(s_bot - s_top);
            if (percol) { fig_rect(cx, y, cw, h, bg); fig_rect(cx, cy, cw, 1, UI_TRACK); }
            fig_rect(cx, top, cw, rh, ui_accent);
        }
        wviz_scope_init = 1u;
    }
}
