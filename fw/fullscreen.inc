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
 * set, the same bypass the overlays use. Leaving fullscreen repaints the normal screen through pl_ui_restore.
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
#define FS_RED      0xF249u          /* #F04848: the stopped/paused progress colour */
#define FS_TRACK    0x1905u          /* #1A2028: the unplayed part of the line */

static uint32_t fs_last_sec = 0xFFFFFFFFu;
static uint8_t  fs_last_state = 0xFFu, fs_force;

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

/* The top-right corner: hint and CPU load on a black plate, redrawn after every figure (the figure covers it). */
COLD_FN3 static void fs_draw_label(void)
{
    char b[8], *q = b;
    q = ui_dec(q, ui_cpu_pct()); *q++ = '%'; *q = 0;
    const uint32_t wh = fb_text_width("SELECT+Y", TS_1X), wp = fb_text_width(b, TS_1X);
    const uint32_t w = wh + 12u + wp + 14u, x = FS_W - w;
    fb_rect(x, 0u, w, 22u, 0u);
    fb_set_color(UI_DIM, 0u);   fb_text_clipped(x + 8u, 3u, "SELECT+Y", TS_1X, TS_1X, wh);
    fb_set_color(UI_WHITE, 0u); fb_text_clipped(x + 8u + wh + 12u, 3u, b, TS_1X, TS_1X, wp);
}

/* Called by ui_draw_chrome() in place of the normal screen: a black frame, the bar, and a request for an immediate figure. */
COLD_FN3 static void ui_fs_frame(void)
{
    const uint8_t o = ov_draw; ov_draw = 1u;
    fb_rect(0, 0, FS_W, FB_H, 0u);
    fs_draw_text();
    fs_draw_progress();
    ov_draw = o;
    fs_force = 1u;
}

/* Called every display frame while fullscreen (after the normal, now silent, dynamic pass). */
static inline int fs_capable(void) { return viz_mode == VIZ_CHLADNI || viz_mode == VIZ_WINAMP_BARS || viz_mode == VIZ_WINAMP_SCOPE; }

COLD_FN3 static void ui_fs_dynamic(void)
{
    if (UI_OVERLAY_UP) return;
    if (!fs_capable()) { ui_fullscreen = 0u; pl_ui_restore = 1u; ui_wave_force = 1u; return; }
    const uint8_t o = ov_draw; ov_draw = 1u;
    if (viz_mode != VIZ_CHLADNI) {                                          /* Winamp Bars / Scope: explicit geometry, black ground */
        if (fs_force) { wviz_force = 1u; fb_rect(0, 0, FS_W, FS_FIG_H, 0u); }
        if (fs_force || helios_rows_safe_counted(0u, FS_FIG_H - 1u)) {
            if (viz_mode == VIZ_WINAMP_BARS) wviz_bars_tick(0u, 0u, FS_W, FS_FIG_H, 0u);
            else                             wviz_scope_tick(0u, 0u, FS_W, FS_FIG_H, 0, 0u);
            fs_force = 0u; fs_draw_label();
        }
    } else
    if (fs_force || helios_rows_safe_counted(0u, FS_FIG_H - 1u)) {           /* whole figure: only with the beam out of the way */
        if (chladni_tick_box(0u, 0u, FS_W, FS_FIG_H, 0u, fs_force)) { fs_force = 0u; fs_draw_label(); }
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
    pl_ui_restore = 1u;                     /* repaint: the fullscreen frame, or the normal screen again */
    ui_wave_force = 1u;
    fs_last_sec = 0xFFFFFFFFu; fs_last_state = 0xFFu;
}

/* Select+X: the next preset of the current meter, if it has any. */
COLD_FN3 static void meter_preset_next(void)
{
    static const char *const wnm[5] = { "PRESET: FLUID", "PRESET: CLASSIC", "PRESET: BOUNCY", "PRESET: SLOW FADE", "PRESET: SNAPPY" };
    if (viz_mode == VIZ_CHLADNI) {
        const char *t = chladni_next_preset();
        if (ui_fullscreen) pl_ui_restore = 1u; else { ui_wave_clear(); ui_wave_force = 1u; }
        ui_toast_msg(t);
    } else if (viz_mode == VIZ_BARS) {
        bars_layout = (uint8_t)!bars_layout;                     /* up <-> mirrored */
        ui_wave_clear(); ui_wave_force = 1u;
        settings_mark_dirty();
        ui_toast_msg(bars_layout ? "PRESET: MIRRORED" : "PRESET: BARS");
    } else if (viz_mode == VIZ_WINAMP_BARS || viz_mode == VIZ_WINAMP_SCOPE) {
        const int scope = (viz_mode == VIZ_WINAMP_SCOPE);
        uint8_t *pidx = scope ? &wvcfg_preset_idx_scope : &wvcfg_preset_idx_bars;
        const uint32_t p = (*pidx < WVIZ_PRESET_N) ? ((uint32_t)*pidx + 1u) % WVIZ_PRESET_N : 0u;
        *pidx = (uint8_t)p;
        if (scope) wviz_cfg_scope = wviz_scope_presets[p]; else wviz_cfg_bars = wviz_bars_presets[p];
        wviz_force = 1u;
        ui_toast_msg(wnm[p]);
    } else {
        ui_toast_msg("NO PRESETS FOR THIS METER");
    }
}
