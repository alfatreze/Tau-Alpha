#if TAU_SETTINGS_UI
/* In-app settings (A-115/A-116): a full-screen overlay sharing the playlist's frame
 * (ov_frame, PL_UI_* geometry). A home lists Appearance / Audio / Playback; each page lists
 * settings with their current value. Two kinds of value:
 *   - choices with more than two options (Colour, Meter, Equalizer, Repeat, Screen blank)
 *     open a list page: a circle per option, the active one marked, A selects the highlighted
 *     option (it applies immediately and the mark moves; B goes back). This is the default
 *     behaviour for any multi-option setting.
 *   - two-state settings and Volume stay inline: A or Left/Right changes them.
 * Colour rows are swatches (a circle in each theme colour); Meter rows carry a preview
 * thumbnail (a grey placeholder for now); the others use a plain radio circle.
 * Start opens (tap) and closes; while it is up poll_input() consumes d-pad, A, B and Start
 * (set_input()), and every player drawing primitive is held by FB_HELD().
 * Every change goes through the same variables and side effects as the direct shortcuts
 * (which stay); nothing new is persisted; Screen blank is not persisted (as Select+Down). */

enum { SET_HOME = 0, SET_APPEAR, SET_AUDIO, SET_PLAY, SET_DIAG, SET_MENUS };
enum { CH_COLOUR = 0, CH_METER, CH_EQ, CH_REPEAT, CH_BLANK, CH_N };
/* Pages after the menus: one per choice list, then the read-only Info page (diagnostics). */
#define SET_INFO_PG ((uint8_t)(SET_MENUS + CH_N))
#define SET_COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
enum { TG_ART = 0, TG_SHUF, TG_RESUME, TG_SPEED };
enum { RT_GROUP = 0, RT_CHOICE, RT_TOGGLE, RT_VOL };
#define SET_PAGE_CH(c) ((uint8_t)(SET_MENUS + (c)))

typedef struct { const char *label; uint8_t type, arg; } set_row_t;
static const set_row_t set_home_rows[] = {
    { "APPEARANCE", RT_GROUP, SET_APPEAR }, { "AUDIO", RT_GROUP, SET_AUDIO },
    { "PLAYBACK", RT_GROUP, SET_PLAY },
#if TAU_DIAG_INFO
    { "DIAGNOSTICS", RT_GROUP, SET_DIAG },
#endif
};
static const set_row_t set_appear_rows[] = {
    { "COLOUR", RT_CHOICE, CH_COLOUR }, { "METER", RT_CHOICE, CH_METER },
    { "ALBUM ART", RT_TOGGLE, TG_ART }, { "SCREEN BLANK", RT_CHOICE, CH_BLANK } };
static const set_row_t set_audio_rows[] = {
    { "EQUALIZER", RT_CHOICE, CH_EQ }, { "VOLUME", RT_VOL, 0 } };
static const set_row_t set_play_rows[] = {
    { "REPEAT", RT_CHOICE, CH_REPEAT }, { "SHUFFLE", RT_TOGGLE, TG_SHUF },
    { "RESUME", RT_TOGGLE, TG_RESUME }, { "SPEED", RT_TOGGLE, TG_SPEED } };
static const set_row_t set_diag_rows[] = {
    { "INFO", RT_GROUP, SET_INFO_PG } };
static const set_row_t *const set_menu_rows[SET_MENUS] = {
    set_home_rows, set_appear_rows, set_audio_rows, set_play_rows, set_diag_rows };
static const uint8_t set_menu_n[SET_MENUS] = {
    SET_COUNT(set_home_rows), 4u, 2u, 4u, SET_COUNT(set_diag_rows) };
static const char *const set_menu_title[SET_MENUS] = {
    "SETTINGS", "APPEARANCE", "AUDIO", "PLAYBACK", "DIAGNOSTICS" };
static const char *const set_ch_title[CH_N] = { "COLOUR", "METER", "EQUALIZER", "REPEAT", "SCREEN BLANK" };
static const uint8_t set_ch_parent[CH_N] = { SET_APPEAR, SET_APPEAR, SET_AUDIO, SET_PLAY, SET_APPEAR };

/* Same order as the VIZ_* enum; the label VIZ_LED shares with its toast is SPECTRUM. */
static const char *const set_viz[] = { "BARS", "WATERFALL", "L/R LEVELS", "PHASE SCOPE",
    "OSCILLOSCOPE", "VU", "WAVEFORM", "MIRRORED BARS", "PEAK DOTS", "MAGIC EYE", "SPECTRUM" };
_Static_assert(sizeof(set_viz) / sizeof(set_viz[0]) == VIZ_COUNT,
               "set_viz[] must name every visualizer");
static const char *const set_rep[3]      = { "OFF", "ALL", "ONE" };
static const char *const set_blank_nm[5] = { "NEVER", "1 MIN", "5 MIN", "10 MIN", "30 MIN" };
static const uint8_t     set_blank[5]    = { 0u, 1u, 5u, 10u, 30u };

/* Geometry (read by the snapshot fixtures). Rows in a choice list are SET_CH_ROW_H tall, or
 * SET_TH_ROW_H when they carry a preview thumbnail; menu rows are SET_MENU_ROW_H. */
#define SET_MENU_ROW_H 36u
#define SET_CH_ROW_H   26u
#define SET_TH_ROW_H   40u
#define SET_TH_W       56u
#define SET_TH_H       32u
#define SET_LIST_H     (PL_UI_ROWS * PL_UI_ROW_H)     /* 264 px of list area */

static uint8_t set_page, set_sel[SET_MENUS], set_ch_cur[CH_N], set_ch_top[CH_N];

static uint32_t set_ch_count(uint32_t c)
{
    return c == CH_COLOUR ? (uint32_t)UI_PALETTE_N : c == CH_METER ? (uint32_t)VIZ_COUNT
         : c == CH_EQ ? (uint32_t)EQ_COUNT : c == CH_REPEAT ? 3u : 5u;
}
static uint32_t set_blank_idx(void)
{
    uint32_t n = 0;
    while (n < 4u && set_blank[n] != blank_min) n++;
    return n;
}
static uint32_t set_ch_active(uint32_t c)
{
    return c == CH_COLOUR ? ui_pal_idx : c == CH_METER ? viz_mode : c == CH_EQ ? eq_idx
         : c == CH_REPEAT ? rep_mode : set_blank_idx();
}
static const char *set_ch_name(uint32_t c, uint32_t i)
{
    return c == CH_COLOUR ? ui_palette_name[i] : c == CH_METER ? set_viz[i]
         : c == CH_EQ ? eq_name[i] : c == CH_REPEAT ? set_rep[i] : set_blank_nm[i];
}
static uint32_t set_ch_rowh(uint32_t c)
{
    return c == CH_METER ? SET_TH_ROW_H : SET_CH_ROW_H;
}
static uint32_t set_ch_visible(uint32_t c) { return SET_LIST_H / set_ch_rowh(c); }

static void set_ch_apply(uint32_t c, uint32_t i)
{
    if (c == CH_COLOUR) {
        ui_pal_idx = i;
        ui_accent = ui_palette[i];
        ui_grad_set(ui_accent);
        ui_accent_changed = 1u;
    } else if (c == CH_METER) {
        viz_mode = (uint8_t)i;
        ui_wave_clear();
        ui_wave_force = 1u;
        for (uint32_t k = 0; k < UI_WAVE_N; k++) {
            wave_drawn[k] = 0xFFu; wave_pk_drawn[k] = 0xFFu;
            for (uint32_t z = 0; z < SPEC_BANDS; z++) spec_drawn[z] = 0xFFu;
        }
    } else if (c == CH_EQ) {
        eq_idx = (uint8_t)i;
        eq_apply = 1u;
    } else if (c == CH_REPEAT) {
        rep_mode = (uint8_t)i;
        ui_mode_dirty = 1u;
    } else {
        blank_min = set_blank[i];
        ui_blank_touch();
        return;                                  /* not persisted */
    }
    settings_mark_dirty();
}

static void set_toggle(uint32_t t)
{
    if (t == TG_ART) {
        art_toggle = 1u;                         /* the main loop applies it, as for the hold */
    } else if (t == TG_SHUF) {
        shuffle_on = (uint8_t)!shuffle_on;
        if (shuffle_on) pl_rng = cycles() | 1u;
        if (pl_count) {
            uint16_t cur = pl_order[pl_pos];
            pl_reorder();
            pl_resync(cur);                      /* keep playing what is playing */
        }
        ui_mode_dirty = 1u;
    } else if (t == TG_RESUME) {
        resume_on = (uint8_t)!resume_on;
    } else {
        speed_fast ^= 1u;
        pcm_rate_apply(track_hz);
        ui_mode_dirty = 1u;
    }
    settings_mark_dirty();
}

static void set_volume(int dir)
{
    if (dir > 0) volume = (volume + VOL_STEP > VOL_MAX) ? VOL_MAX : volume + VOL_STEP;
    else         volume = (volume < VOL_STEP) ? 0u : volume - VOL_STEP;
    vol_apply();
    ui_mode_dirty = 1u;                          /* the speaker icon */
    settings_mark_dirty();
}

static void set_menu_value(const set_row_t *r, char *o)
{
    const char *s = "";
    char *q = o;
    if (r->type == RT_GROUP) s = "";
    else if (r->type == RT_CHOICE) s = set_ch_name(r->arg, set_ch_active(r->arg));
    else if (r->type == RT_VOL) { q = ui_dec(q, volume); s = "%"; }
    else if (r->arg == TG_ART)    s = art_pref ? "ON" : "OFF";
    else if (r->arg == TG_SHUF)   s = shuffle_on ? "ON" : "OFF";
    else if (r->arg == TG_RESUME) s = resume_on ? "ON" : "OFF";
    else                          s = speed_fast ? "1.2X" : "NORMAL";
    while (*s) *q++ = *s++;
    *q = 0;
}

/* A filled circle of diameter d centred on (cx, cy), corners cut to bg. */
static void set_disc(uint32_t cx, uint32_t cy, uint32_t d, uint16_t colour, uint16_t bg)
{
    fb_round_rect_on(cx - d / 2u, cy - d / 2u, d, d, d / 2u, colour, bg);
}

static void set_draw_menu(uint32_t p)
{
    const set_row_t *rows = set_menu_rows[p];
    ov_frame(set_menu_title[p], "",
             p == SET_HOME ? "A OPEN   B CLOSE" : p == SET_DIAG ? "A OPEN   B BACK" : "A CHANGE   B BACK");
    for (uint32_t i = 0; i < set_menu_n[p]; i++) {
        uint32_t y = PL_UI_LIST_Y + i * SET_MENU_ROW_H;
        int sel = (i == set_sel[p]);
        uint16_t bg = sel ? ui_accent : UI_PANEL;
        if (sel) fb_round_rect_on(PL_UI_X + 4u, y, PL_UI_W - 8u, SET_MENU_ROW_H - 4u, 5u, bg, UI_PANEL);
        uint32_t ty = y + 8u;
        fb_set_color(sel ? UI_PANEL : UI_WHITE, bg);
        fb_text_clipped(PL_UI_TEXT_X, ty, rows[i].label, TS_1X, TS_1X, 200u);
        char v[24];
        set_menu_value(&rows[i], v);
        uint32_t right = PL_UI_X + PL_UI_W - 16u;
        if (rows[i].type == RT_GROUP || rows[i].type == RT_CHOICE) {
            fb_set_color(sel ? UI_PANEL : UI_DIM, bg);
            right -= 12u;
            fb_text_clipped(right, ty, ">", TS_1X, TS_1X, 12u);
            right -= 8u;
        }
        uint32_t w = fb_text_width(v, TS_1X);
        if (w) {
            fb_set_color(sel ? UI_PANEL : UI_DIM, bg);
            fb_text_clipped(right - w, ty, v, TS_1X, TS_1X, w + 2u);
        }
    }
}

static void set_draw_choice(uint32_t c)
{
    uint32_t n = set_ch_count(c), rowh = set_ch_rowh(c), vis = set_ch_visible(c);
    uint32_t top = set_ch_top[c], act = set_ch_active(c);
    ov_frame(set_ch_title[c], "", "A SELECT   B BACK");
    if (n > vis) {                               /* scroll track, as the playlist */
        uint32_t tx = PL_UI_X + PL_UI_W - 11u, th = SET_LIST_H * vis / n;
        fb_rect(tx, PL_UI_LIST_Y - 2u, 3u, SET_LIST_H, ui_mix(UI_PANEL, UI_DIM, 1u, 3u));
        fb_rect(tx, PL_UI_LIST_Y - 2u + (SET_LIST_H - th) * top / (n - vis), 3u, th, ui_accent);
    }
    for (uint32_t k = 0; k < vis && top + k < n; k++) {
        uint32_t i = top + k, y = PL_UI_LIST_Y + k * rowh, cy = y + (rowh - 2u) / 2u;
        int sel = (i == set_ch_cur[c]), on = (i == act);
        uint16_t bg = sel ? ui_accent : UI_PANEL;
        if (sel) fb_round_rect_on(PL_UI_X + 4u, y, PL_UI_W - 20u, rowh - 2u, 5u, bg, UI_PANEL);
        uint32_t mx = PL_UI_TEXT_X + 10u;         /* marker centre */
        if (c == CH_COLOUR) {                     /* swatch: theme colour, white ring if active */
            set_disc(mx, cy, 20u, on ? UI_WHITE : (sel ? UI_PANEL : ui_mix(UI_PANEL, UI_DIM, 1u, 3u)), bg);
            set_disc(mx, cy, 14u, ui_palette[i], on ? UI_WHITE : (sel ? UI_PANEL : ui_mix(UI_PANEL, UI_DIM, 1u, 3u)));
        } else {                                  /* radio: ring, dot when active */
            uint16_t ring = sel ? UI_PANEL : UI_WHITE;
            set_disc(mx, cy, 18u, ring, bg);
            set_disc(mx, cy, 12u, bg, ring);
            if (on) set_disc(mx, cy, 8u, sel ? UI_PANEL : ui_accent, bg);
        }
        uint32_t tx = mx + 24u;
        if (c == CH_METER) {                      /* preview placeholder */
            fb_rect(tx, y + (rowh - 2u - SET_TH_H) / 2u, SET_TH_W, SET_TH_H, UI_DIM);
            tx += SET_TH_W + 14u;
        }
        fb_set_color(sel ? UI_PANEL : UI_WHITE, bg);
        fb_text_clipped(tx, y + (rowh - 2u - 16u) / 2u, set_ch_name(c, i), TS_1X, TS_1X,
                        PL_UI_X + PL_UI_W - 24u - tx);
    }
}

#if TAU_DIAG_INFO
/* ---- Diagnostics / Info: read-only live values, refreshed once a second (rows only). ---- */
extern char _heap_start, _heap_end;
#define SET_INFO_ROWS 11u
static const char *const set_info_label[SET_INFO_ROWS] = {
    "FIRMWARE", "FPGA REV", "SDRAM WINDOW", "WINDOW READ", "FREE RAM", "PLAYLIST",
    "LIST CLIPPED", "TRACK", "UNDERRUNS", "DRAW STALL", "LOAD MS" };
static uint32_t set_info_at;

static char *set_str(char *q, const char *s) { while (*s) *q++ = *s++; return q; }

static void set_info_value(uint32_t i, char *o)
{
    char *q = o;
    switch (i) {
    case 0: q = set_str(q, APP_VER); break;
    case 1: {
        uint32_t v = REG(R_VERSION);
        for (int sh = 24; sh >= 0; sh -= 8) q = ui_hex2(q, (uint8_t)(v >> sh));
        break; }
    case 2:
#if TAU_PL_SDRAM
        q = set_str(q, pl_sdram_state == 1u ? "OK" : pl_sdram_state == 2u ? "FAILED" : "UNTESTED");
#else
        q = set_str(q, "OFF");
#endif
        break;
    case 3:
#if TAU_PL_SDRAM
        if (pl_win_rd) { q = ui_dec(q, pl_win_rd); q = set_str(q, " CYC"); }
        else q = set_str(q, "-");
#else
        q = set_str(q, "-");
#endif
        break;
    case 4: q = ui_dec(q, (uint32_t)(&_heap_end - &_heap_start)); q = set_str(q, " B"); break;
    case 5:
        if (pl_count) { q = ui_dec(q, pl_count); q = set_str(q, " TRACKS"); }
        else q = set_str(q, "NONE");
        break;
    case 6: q = set_str(q, pl_truncated ? "YES" : "NO"); break;
    case 7:
        if (!track_hz) { q = set_str(q, "NONE"); break; }
        q = set_str(q, track_fmt == FMT_FLAC ? "FLAC " : "MP3 ");
        if (track_kbps) { q = ui_dec(q, track_kbps); q = set_str(q, "K "); }
        q = ui_dec(q, track_hz / 1000u); *q++ = '.'; q = ui_dec(q, (track_hz % 1000u) / 100u);
        *q++ = 'K';
        break;
    case 8: q = ui_dec(q, pcm_under_n); break;
    case 9: q = ui_dec(q, REG(R_FB_STALL) / (CLK_HZ / 1000u)); q = set_str(q, " MS"); break;
    default:
        q = ui_dec(q, ld_head); *q++ = '/'; q = ui_dec(q, ld_size); *q++ = '/';
        q = ui_dec(q, ld_art);  *q++ = '/'; q = ui_dec(q, ld_total);
    }
    *q = 0;
}

static void set_draw_info(int full)
{
    if (full) ov_frame("INFO", "", "B BACK");
    for (uint32_t i = 0; i < SET_INFO_ROWS; i++) {
        uint32_t y = PL_UI_LIST_Y + i * PL_UI_ROW_H;
        char v[40];
        set_info_value(i, v);
        fb_rect(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, PL_UI_ROW_H, UI_PANEL);
        fb_set_color(UI_DIM, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, y, set_info_label[i], TS_1X, TS_1X, 170u);
        uint32_t w = fb_text_width(v, TS_1X);
        fb_set_color(UI_WHITE, UI_PANEL);
        fb_text_clipped(PL_UI_X + PL_UI_W - 16u - w, y, v, TS_1X, TS_1X, w + 2u);
    }
}

/* Once a second while the page is open: repaint the rows only, never the panel. */
static void set_info_tick(void)
{
    if (!set_open || set_page != SET_INFO_PG) return;
    if ((int32_t)(cycles() - set_info_at) < 0) return;
    set_info_at = cycles() + CLK_HZ;
    uint8_t o = ov_draw; ov_draw = 1u;
    set_draw_info(0);
    ov_draw = o;
}
#endif

static void set_draw(void)
{
    uint8_t o = ov_draw; ov_draw = 1u;
    if (set_page < SET_MENUS)                set_draw_menu(set_page);
#if TAU_DIAG_INFO
    else if (set_page == SET_INFO_PG)        set_draw_info(1);
#endif
    else                                     set_draw_choice(set_page - SET_MENUS);
    ov_draw = o;
}

static void set_close(void) { set_open = 0u; set_page = SET_HOME; pl_ui_restore = 1u; }

static void set_ch_follow(uint32_t c)
{
    uint32_t vis = set_ch_visible(c), cur = set_ch_cur[c];
    if (cur < set_ch_top[c]) set_ch_top[c] = (uint8_t)cur;
    else if (cur >= set_ch_top[c] + vis) set_ch_top[c] = (uint8_t)(cur - vis + 1u);
}

/* Returns 1 when the input was consumed. Start opens (tap) and closes. */
static int set_input(uint32_t edge, uint32_t keys)
{
    if (!set_open) {
        if ((edge & KEY_START) && !(keys & KEY_SELECT)) {
            /* One overlay at a time: an open playlist would keep repainting its marquee row
             * over the settings (its rows draw through the ov_draw gate), so it is closed
             * here. Closing settings returns to the player, not to the list. */
            pl_ui_open = 0u;
            set_open = 1u; set_page = SET_HOME; set_dirty = 1u;
            return 1;
        }
        return 0;
    }
    if (edge & KEY_START) { set_close(); return 1; }
    if (set_page == SET_INFO_PG) {                /* read-only page: only B (back) */
        if (edge & KEY_B) { set_page = SET_DIAG; set_dirty = 1u; }
        return 1;
    }
    if (set_page >= SET_MENUS) {                  /* a choice list */
        uint32_t c = set_page - SET_MENUS, n = set_ch_count(c);
        if (edge & KEY_UP)   { set_ch_cur[c] = (uint8_t)(set_ch_cur[c] ? set_ch_cur[c] - 1u : n - 1u); set_ch_follow(c); set_dirty = 1u; }
        if (edge & KEY_DOWN) { set_ch_cur[c] = (uint8_t)((set_ch_cur[c] + 1u) % n); set_ch_follow(c); set_dirty = 1u; }
        if (edge & KEY_A)    { set_ch_apply(c, set_ch_cur[c]); set_dirty = 1u; }
        if (edge & KEY_B)    { set_page = set_ch_parent[c]; set_dirty = 1u; }
        return 1;
    }
    uint8_t *sel = &set_sel[set_page];
    if (edge & KEY_UP)   { *sel = *sel ? (uint8_t)(*sel - 1u) : (uint8_t)(set_menu_n[set_page] - 1u); set_dirty = 1u; }
    if (edge & KEY_DOWN) { *sel = (uint8_t)((*sel + 1u) % set_menu_n[set_page]); set_dirty = 1u; }
    const set_row_t *r = &set_menu_rows[set_page][*sel];
    if (edge & (KEY_A | KEY_RIGHT)) {
        if (r->type == RT_GROUP) { set_page = r->arg; set_dirty = 1u; }
        else if (r->type == RT_CHOICE) {
            set_page = SET_PAGE_CH(r->arg);
            set_ch_cur[r->arg] = (uint8_t)set_ch_active(r->arg);
            set_ch_follow(r->arg);
            set_dirty = 1u;
        } else if (r->type == RT_TOGGLE) { set_toggle(r->arg); set_dirty = 1u; }
        else if (r->type == RT_VOL && (edge & KEY_RIGHT)) { set_volume(1); set_dirty = 1u; }
    }
    if (edge & KEY_LEFT) {
        if (r->type == RT_TOGGLE) { set_toggle(r->arg); set_dirty = 1u; }
        else if (r->type == RT_VOL) { set_volume(-1); set_dirty = 1u; }
    }
    if (edge & KEY_B) {
        if (set_page == SET_HOME) set_close();
        else { set_page = SET_HOME; set_dirty = 1u; }
    }
    return 1;
}
#endif
