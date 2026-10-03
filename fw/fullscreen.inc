/* ==================================================================== FULLSCREEN visualiser (Select+Y), B-277
 * Design (Figma "Tau" node 190-779 and the playing variant supplied by the owner): the meter fills the whole screen, a slim
 * playing bar sits at the bottom, and a small label in the top-right corner shows the toggle hint and the processor load.
 *
 *     y 0..322    the figure, full width (Chladni today; the other meters are drawn for the normal box only)
 *     y 323..359  black bar: "TITLE . ARTIST . ALBUM [YEAR]" centred (white / accent / grey), and a 2 px progress line
 *                 under it: accent while playing, red while stopped or paused, dark track, a small tick at the position
 *     top right   "SELECT+Y  NN%" on black: hint and CPU load (ui_cpu_pct), redrawn with every figure
 *
 * HOW IT COEXISTS WITH THE NORMAL UI: while ui_fullscreen is set FB_HELD() is true, exactly as for the settings and playlist
 * overlays, so every ordinary draw call (chrome, progress, clock, transport, toasts, the other meters) is a silent no-op while
 * their state machines keep running -- elapsed time, resume position and audio are untouched. This module draws with ov_draw
 * set, the same bypass the overlays use. Leaving fullscreen repaints the normal screen through helios_view_switch().
 *
 * All of this is COLD code. Hot entries: ui_draw_chrome() -> ui_fs_frame() only while ui_fullscreen is set (which only the cold-gated toggle can
 * do), ui_draw_dynamic() -> ui_fs_dynamic() after its COLD_READY() check, and poll_input() -> the two Select combos, gated on COLD_READY().
 *
 * Chladni and the two Winamp meters are fullscreen-capable for now: the other meters use fixed constants for the normal box in hundreds of places.
 * Choosing another meter while fullscreen leaves fullscreen. */

#define FS_W        400u
#define FS_FIG_H    323u
#define FS_BAR_Y    323u
#define FS_TXT_Y    328u
#define FS_PROG_X   8u
#define FS_PROG_W   384u
#define FS_PROG_Y   350u

static uint32_t fs_last_sec = 0xFFFFFFFFu;
static uint8_t  fs_last_state = 0xFFu, fs_force;

/* Helios view registry (item 4): no view-specific enter/exit of its own today -- HELIOS_INV_ALL
 * already covers everything toggling fullscreen needs, same as before this existed. */
static const helios_view_t fullscreen_view = { 0, 0, HELIOS_INV_ALL };

static void fs_upper(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0;
    for (; s[i] && i + 1u < n; i++) d[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    d[i] = 0;
}

/* Title . Artist . Album [Year], centred in FS_PROG_W; each part is clipped if the line would not fit. */
COLD_FN3 static void fs_draw_text(void)
{
    char t[48], a[48], l[64];
    fs_upper(t, track_title, sizeof t);
    fs_upper(a, track_artist, sizeof a);
    fs_upper(l, track_album, sizeof l);
    if (track_year[0]) {
        uint32_t n = 0; while (l[n]) n++;
        if (n + 8u < sizeof l) { l[n++] = ' '; l[n++] = '['; for (uint32_t i = 0; track_year[i] && i < 4u; i++) l[n++] = track_year[i]; l[n++] = ']'; l[n] = 0; }
    }
    const uint32_t GAP = 12u, DOT = 3u, SEP = GAP + DOT + GAP;
    uint32_t wt = t[0] ? fb_text_width(t, TS_1X) : 0u, wa = a[0] ? fb_text_width(a, TS_1X) : 0u, wl = l[0] ? fb_text_width(l, TS_1X) : 0u;
    const uint32_t parts = (t[0] ? 1u : 0u) + (a[0] ? 1u : 0u) + (l[0] ? 1u : 0u);
    const uint32_t avail = FS_PROG_W - (parts > 1u ? (parts - 1u) * SEP : 0u);
    if (wt + wa + wl > avail) {                                   /* share what there is: 40 / 30 / 30 */
        uint32_t ct = avail * 4u / 10u, ca = avail * 3u / 10u, cl = avail - ct - ca;
        if (wt < ct) { cl += ct - wt; ct = wt; } if (wa < ca) { cl += ca - wa; ca = wa; }
        if (wl < cl) { ca += cl - wl; cl = wl; }
        if (wt > ct) wt = ct; if (wa > ca) wa = ca; if (wl > cl) wl = cl;
    }
    uint32_t total = wt + wa + wl + (parts > 1u ? (parts - 1u) * SEP : 0u);
    uint32_t x = FS_PROG_X + (FS_PROG_W - total) / 2u;
    fb_rect(0, FS_BAR_Y, FS_W, 26u, 0u);                          /* clear the text rows */
    uint32_t drawn = 0;
    if (t[0]) { fb_set_color(UI_WHITE, 0u);   fb_text_clipped(x, FS_TXT_Y, t, TS_1X, TS_1X, wt); x += wt; drawn++; }
    if (a[0]) {
        if (drawn) { fb_rect(x + GAP, FS_TXT_Y + 7u, DOT, DOT, UI_FAINT); x += SEP; }
        fb_set_color(ui_accent, 0u); fb_text_clipped(x, FS_TXT_Y, a, TS_1X, TS_1X, wa); x += wa; drawn++;
    }
    if (l[0]) {
        if (drawn) { fb_rect(x + GAP, FS_TXT_Y + 7u, DOT, DOT, UI_FAINT); x += SEP; }
        fb_set_color(UI_DIM, 0u); fb_text_clipped(x, FS_TXT_Y, l, TS_1X, TS_1X, wl);
    }
}

/* The progress line and its tick: accent while playing, red otherwise. */
COLD_FN3 static void fs_draw_progress(void)
{
    const uint16_t col = (!paused && !stopped) ? ui_accent : FS_RED;
    uint32_t done = track_secs ? (ui_sec * FS_PROG_W) / track_secs : 0u;
    if (done > FS_PROG_W) done = FS_PROG_W;
    fb_rect(FS_PROG_X - 2u, FS_PROG_Y - 4u, FS_PROG_W + 4u, 10u, 0u);            /* clear line and old tick */
    fb_rect(FS_PROG_X, FS_PROG_Y, FS_PROG_W, 2u, FS_TRACK);
    if (done) fb_rect(FS_PROG_X, FS_PROG_Y, done, 2u, col);
    fb_rect(FS_PROG_X + done - (done ? 1u : 0u), FS_PROG_Y - 3u, 2u, 8u, col);
    fs_last_sec = ui_sec; fs_last_state = (uint8_t)(paused ? 1u : stopped ? 2u : 0u);
}

#define FS_LABEL_EXCL 0u   /* helios_excl[] slot reserved for this corner */

/* The top-right corner: hint and CPU load on a black plate. Three rules keep it from flickering:
 *   1. The figure never draws into it. Its rectangle is a helios_excl[] slot, and while a Winamp/Layered Wave figure is drawn fig_clip_on makes
 *      fb_rect()/fb_bar() split around it (helios_clip_rect/helios_clip_bar), so the plate is not overwritten and need not be repainted after every figure.
 *   2. The label is repainted only when its text changes (CPU% updates once a second) or the screen was wiped, never per figure.
 *   3. Chladni fills by replicating a computed tile with block copies, which cannot be clipped (the vertical copies would carry the hole's pixels down), so
 *      its figure and the label repaint run only while the beam is well clear of the label rows (fs_corner_ok()).
 * fs_label_prepare() runs BEFORE the figure so the exclusion rect always matches the width the label will have. A width change forces one full figure
 * repaint (its outer clear honours the new rect) so no sliver of the old, wider plate is left behind. */
static char     fs_lbl_txt[8];
static uint32_t fs_lbl_x, fs_lbl_w;
static uint8_t  fs_lbl_dirty = 1u;

/* The beam is inside the active picture and well below the label rows (22) and well above the bottom, so a draw of a few ms finishes before it wraps. */
COLD_FN3 static int fs_corner_ok(void)
{
    if (!helios_beam_ok) return 1;
    const uint32_t vc = REG(R_SCAN) & 0x1FFu;
    if (vc < HELIOS_VOFF || vc >= HELIOS_VOFF + HELIOS_VACT) return 0;
    const uint32_t by = vc - HELIOS_VOFF;
    return by >= 26u && by <= 290u;
}

COLD_FN3 static void fs_label_prepare(void)
{
    char b[8], *q = b;
    q = ui_dec(q, ui_cpu_pct()); *q++ = '%'; *q = 0;
    const uint32_t wh = fb_text_width("SELECT+Y", TS_1X), wp = fb_text_width("100%", TS_1X);   /* fixed width: a 99/100% reading must not change the plate and force a full figure repaint */
    const uint32_t w = wh + 12u + wp + 14u;
    helios_exclude_set(FS_LABEL_EXCL, (int32_t)(FS_W - w), 0, (int32_t)w, 22);
    uint32_t same = (fs_lbl_w == w);
    for (uint32_t i = 0; i < sizeof fs_lbl_txt; i++) { if (fs_lbl_txt[i] != b[i]) same = 0u; if (!b[i]) break; }
    if (same) return;
    for (uint32_t i = 0; i < sizeof fs_lbl_txt; i++) { fs_lbl_txt[i] = b[i]; if (!b[i]) break; }
    if (fs_lbl_w && fs_lbl_w != w) fs_force = 1u;
    fs_lbl_w = w; fs_lbl_x = FS_W - w; fs_lbl_dirty = 1u;
}

/* Paint the plate and its two texts. `now` skips the beam check (Chladni, which only calls this inside its own beam-checked figure). */
COLD_FN3 static void fs_label_paint(int now)
{
    if (!fs_lbl_dirty || (!now && !fs_corner_ok())) return;
    const uint32_t x = fs_lbl_x, wh = fb_text_width("SELECT+Y", TS_1X), wp = fb_text_width(fs_lbl_txt, TS_1X);
    fb_rect(x, 0u, fs_lbl_w, 22u, 0u);
    fb_set_color(UI_DIM, 0u);   fb_text_clipped(x + 8u, 3u, "SELECT+Y", TS_1X, TS_1X, wh);
    fb_set_color(UI_WHITE, 0u); fb_text_clipped(x + 8u + wh + 12u, 3u, fs_lbl_txt, TS_1X, TS_1X, wp);
    fs_lbl_dirty = 0u;
}

/* Called by ui_draw_chrome() in place of the normal screen: a black frame, the bar, and a request for an immediate figure. */
COLD_FN3 static void ui_fs_frame(void)
{
    const uint8_t o = ov_draw; ov_draw = 1u;
    fb_rect(0, 0, FS_W, FB_H, 0u);
    fs_draw_text();
    fs_draw_progress();
    ov_draw = o;
    fs_force = 1u; fs_lbl_dirty = 1u;        /* the wipe above took the label with it */
}

/* Called every display frame while fullscreen (after the normal, now silent, dynamic pass). */
static inline int fs_capable(void) { return viz_mode == VIZ_CHLADNI || viz_mode == VIZ_WINAMP_BARS || viz_mode == VIZ_WINAMP_SCOPE || viz_mode == VIZ_LAYERED_WAVE; }

COLD_FN3 static void ui_fs_dynamic(void)
{
    if (UI_OVERLAY_UP) return;
    if (!fs_capable()) { ui_fullscreen = 0u; helios_view_switch(&fullscreen_view, &helios_player_view); ui_wave_force = 1u; return; }   /* B-350/item 4 */
    const uint8_t o = ov_draw; ov_draw = 1u;
    /* NO beam gate here (B-288). It used to require the whole 323-row figure to be clear of the beam, which is true only in vertical
     * blanking and while the beam is in the last 40 rows: about 19 percent of the time. ui_draw_dynamic() is called once per decoded frame
     * (26 ms), not polled, so a closed gate meant no figure at all until the NEXT frame: about 38 x 0.19 = 7 figures a second, which is
     * the "very slow" fullscreen. The figures are all drawn so that a partly finished frame is still a coherent picture (Winamp Bars
     * redraw only changed bars, the scope erases and redraws column by column, Chladni copies tile rows). */
    fs_label_prepare();
    if (viz_mode != VIZ_CHLADNI) {                                          /* Winamp Bars / Scope / Layered Wave: explicit geometry, black ground */
        /* helios_fill_excl() skips the label's corner for the outer clear; helios_meter(HM_CLIP) keeps the figure's own fills out of it. */
        if (fs_force) { wviz_force = 1u; helios_fill_excl(0, 0, FS_W, FS_FIG_H, 0u); }
        const mtr_in_t in = mtr_build(0u, 0u, FS_W, FS_FIG_H, 0u, wviz_force);
        (void)helios_meter(viz_mode, &in, HM_CLIP);
        fs_force = 0u; fs_label_paint(0);
    } else {
        /* Chladni composes off-screen and is presented around the label rect by helios_meter(HM_CLIP), so the label is untouched and only repainted when its
         * text changes. Without the H2 back buffer the tile copies still overwrite the corner: then the label is repainted after the figure, and the figure
         * only runs while the beam is clear of the label rows. */
        if (helios_offscreen_ok() || fs_corner_ok()) {
            const mtr_in_t in = mtr_build(0u, 0u, FS_W, FS_FIG_H, 0u, fs_force);
            const uint32_t r = helios_meter(VIZ_CHLADNI, &in, HM_CLIP);
            if (r & HM_DREW) {
                fs_force = 0u;
                if (!(r & HM_COMPOSED)) { fs_lbl_dirty = 1u; fs_label_paint(1); }   /* the tile copies overwrote the corner */
                else fs_label_paint(0);
            }
        }
    }
    if (ui_sec != fs_last_sec || (uint8_t)(paused ? 1u : stopped ? 2u : 0u) != fs_last_state) fs_draw_progress();
    ov_draw = o;
}

/* Select+Y */
COLD_FN3 static void ui_fs_toggle(void)
{
    if (idle) return;
    if (!fs_capable()) { ui_toast_msg("FULLSCREEN: CHLADNI AND WINAMP ONLY"); return; }
    ui_fullscreen = (uint8_t)!ui_fullscreen;
    /* B-350/item 4: repaint -- the fullscreen frame, or the normal screen again. Direction is picked
     * from the NEW value just set above, not tracked global state (see fw/helios.inc's own comment). */
    if (ui_fullscreen) helios_view_switch(&helios_player_view, &fullscreen_view);
    else               helios_view_switch(&fullscreen_view, &helios_player_view);
    ui_wave_force = 1u;
    fs_last_sec = 0xFFFFFFFFu; fs_last_state = 0xFFu;
}

/* Select+X: the next preset of the current meter, if it has any. */
COLD_FN3 static void meter_preset_next(void)
{
    if (viz_mode == VIZ_CHLADNI) {
        const mtr_data_t *md = mtr_of(VIZ_CHLADNI);
        const uint32_t p = (*md->pre_idx < md->npre) ? ((uint32_t)*md->pre_idx + 1u) % md->npre : 0u;   /* from CUSTOM: the first preset */
        mtr_apply_preset(md, p);                                 /* chl_sync() picks the new layout and values up on the next figure */
        if (ui_fullscreen) helios_view_changed(); else { ui_wave_clear(); ui_wave_force = 1u; }   /* B-350 */
        ui_toast_set("PRESET: ", 0xFFFFFFFFu, md->pre_names[p]);
    } else if (viz_mode == VIZ_BARS) {
        bars_layout = (uint8_t)!bars_layout;                     /* up <-> mirrored */
        ui_wave_clear(); ui_wave_force = 1u;
        settings_mark_dirty();
        ui_toast_msg(bars_layout ? "PRESET: MIRRORED" : "PRESET: BARS");
    } else if (viz_mode == VIZ_WINAMP_BARS || viz_mode == VIZ_WINAMP_SCOPE || viz_mode == VIZ_VU_MASTER || viz_mode == VIZ_LAYERED_WAVE) {
        const mtr_data_t *md = mtr_of(viz_mode);
        const uint32_t p = (*md->pre_idx < md->npre) ? ((uint32_t)*md->pre_idx + 1u) % md->npre : 0u;   /* from CUSTOM: the first preset */
        mtr_apply_preset(md, p);
        wviz_force = 1u;                       /* VU Master reads this too, same as Winamp Bars/Scope (fw/vu_master.inc's vum_tick()) */
        ui_toast_set("PRESET: ", 0xFFFFFFFFu, md->pre_names[p]);
    } else {
        ui_toast_msg("NO PRESETS FOR THIS METER");
    }
}
