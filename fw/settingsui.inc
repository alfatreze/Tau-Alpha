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

/* The home is titled MENU. SET_DIAG is the SETTINGS group (Info, and in the Diagnostic Build a
 * DIAGNOSTICS group = SET_DGN with Tests, Stress and All speeds). The Library on/off toggle and
 * How it works page that used to live here were removed with legacy playlist mode. */
enum { SET_HOME = 0, SET_APPEAR, SET_AUDIO, SET_PLAY, SET_DIAG, SET_TESTS, SET_STRESS, SET_DGN, SET_MENUS };
enum { CH_COLOUR = 0, CH_METER, CH_EQ, CH_REPEAT, CH_BLANK, CH_SPEED, CH_STRESS, CH_SOAK, CH_THEME, CH_MODE, CH_TEMPO, CH_N };
#if TAU_DIAGNOSTIC
static uint8_t info_qr_up;   /* fw/info_export.inc: forward-declared, referenced by set_info_tick() below its own include */
#endif

/* Pages after the menus: one per choice list, then the read-only Info page (diagnostics). */
#define SET_INFO_PG ((uint8_t)(SET_MENUS + CH_N))
#define SET_STAT_PG ((uint8_t)(SET_INFO_PG + 1u))   /* stress status (Diagnostic Build) */
#define SET_WVIZCFG_PG ((uint8_t)(SET_STAT_PG + 1u))   /* Meter > Configure, B-215/B-216 -- always
                                                         * available, not gated on TAU_LIBRARY/TAU_DIAGNOSTIC
                                                         * like the pages below. */
#if TAU_DIAGNOSTIC
#define SET_CHECK_PG ((uint8_t)(SET_WVIZCFG_PG + 1u)) /* the user-facing diagnostics run (fw/suite.inc) */
#if MP3_PROFILE || FLAC_PROFILE
#define SET_SWEEP_PG ((uint8_t)(SET_CHECK_PG + 1u))   /* Decode Profile Sweep, B-090/B-091 (fw/suite.inc) */
#define SET_BLITTEST_PG ((uint8_t)(SET_SWEEP_PG + 1u))   /* Blit Test, B-166 (fw/suite.inc) */
#else
#define SET_BLITTEST_PG ((uint8_t)(SET_CHECK_PG + 1u))   /* Blit Test, B-166 (fw/suite.inc) */
#endif
#define SET_METERSWEEP_PG ((uint8_t)(SET_BLITTEST_PG + 1u))   /* Meter cost sweep, B-301 (fw/suite.inc) */
#define SET_METERTRACE_PG ((uint8_t)(SET_METERSWEEP_PG + 1u)) /* Meter trace recorder, M3 (fw/suite.inc) */
#endif
#define SET_COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
enum { TG_ART = 0, TG_SPEEDS, TG_RATES, TG_CYMO };
enum { RT_GROUP = 0, RT_CHOICE, RT_TOGGLE, RT_VOL, RT_ACTION };
#define SET_PAGE_CH(c) ((uint8_t)(SET_MENUS + (c)))

typedef struct { const char *label; uint8_t type, arg; } set_row_t;
static const set_row_t set_home_rows[] = {
    { "APPEARANCE", RT_GROUP, SET_APPEAR }, { "AUDIO", RT_GROUP, SET_AUDIO },
    { "PLAYBACK", RT_GROUP, SET_PLAY },
    { "SETTINGS", RT_GROUP, SET_DIAG },
};
static const set_row_t set_appear_rows[] = {
    { "COLOUR", RT_CHOICE, CH_COLOUR }, { "THEME", RT_CHOICE, CH_THEME }, { "MODE", RT_CHOICE, CH_MODE },
    { "METER", RT_CHOICE, CH_METER },
    { "METER: CONFIGURE", RT_GROUP, SET_WVIZCFG_PG },
    { "ALBUM ART", RT_TOGGLE, TG_ART }, { "SCREEN BLANK", RT_CHOICE, CH_BLANK } };
static const set_row_t set_audio_rows[] = {
    { "EQUALIZER", RT_CHOICE, CH_EQ }, { "VOLUME", RT_VOL, 0 } };
static const set_row_t set_play_rows[] = {
    { "REPEAT", RT_CHOICE, CH_REPEAT }, { "SPEED", RT_CHOICE, CH_SPEED }
#if TAU_TEMPO
    , { "TEMPO", RT_CHOICE, CH_TEMPO }
#endif
};
static const set_row_t set_diag_rows[] = {
    { "INFO", RT_GROUP, SET_INFO_PG },
#if TAU_DIAGNOSTIC
    { "DIAGNOSTICS", RT_GROUP, SET_DGN },
#endif
};
/* Diagnostic Build only: reached from SETTINGS > DIAGNOSTICS. */
static const set_row_t set_dgn_rows[] = {
#if TAU_DIAGNOSTIC
    { "TESTS", RT_GROUP, SET_TESTS },
    { "STRESS", RT_GROUP, SET_STRESS },
    { "ALL SPEEDS", RT_TOGGLE, TG_SPEEDS },
    { "ACCEPT ALL RATES", RT_TOGGLE, TG_RATES },
    { "CYMO RESAMPLER", RT_TOGGLE, TG_CYMO },
    { "CHECK", RT_GROUP, SET_CHECK_PG },
#if MP3_PROFILE || FLAC_PROFILE
    { "DECODE SWEEP", RT_GROUP, SET_SWEEP_PG },
#endif
    { "BLIT TEST", RT_GROUP, SET_BLITTEST_PG },
    { "METER SWEEP", RT_GROUP, SET_METERSWEEP_PG },
    { "METER TRACE", RT_GROUP, SET_METERTRACE_PG },
#else
    { "INFO", RT_GROUP, SET_INFO_PG },          /* unreachable placeholder: the page exists only in the Diagnostic Build */
#endif
};
/* Diagnostic Build tests: A runs the highlighted one and its result replaces the value.
 * PLAYLIST CHECK (DG_PLC) was removed along with legacy playlist mode. */
enum { DG_WIN = 0, DG_RD, DG_WR, DG_COLD, DG_CLR, DG_N };
static const set_row_t set_tests_rows[] = {
    { "WINDOW TEST", RT_ACTION, DG_WIN }, { "READ CYCLES", RT_ACTION, DG_RD },
    { "WRITE CYCLES", RT_ACTION, DG_WR },
    { "COLD CODE TEST", RT_ACTION, DG_COLD },
    { "CLEAR COUNTERS", RT_ACTION, DG_CLR } };
/* Stress (Diagnostic Build): the pump level and a timed soak, both choice lists, and a live status page. */
static const set_row_t set_stress_rows[] = {
    { "LEVEL", RT_CHOICE, CH_STRESS }, { "SOAK", RT_CHOICE, CH_SOAK },
    { "STATUS", RT_GROUP, SET_STAT_PG } };
static const set_row_t *const set_menu_rows[SET_MENUS] = {
    set_home_rows, set_appear_rows, set_audio_rows, set_play_rows, set_diag_rows,
    set_tests_rows, set_stress_rows, set_dgn_rows };
static const uint8_t set_menu_n[SET_MENUS] = {
    SET_COUNT(set_home_rows), SET_COUNT(set_appear_rows), 2u, 4u, SET_COUNT(set_diag_rows), SET_COUNT(set_tests_rows),
    SET_COUNT(set_stress_rows), SET_COUNT(set_dgn_rows) };
static const char *const set_menu_title[SET_MENUS] = {
    "MENU", "APPEARANCE", "AUDIO", "PLAYBACK", "SETTINGS", "TESTS", "STRESS", "DIAGNOSTICS" };
/* Where B goes from each menu page. */
static const uint8_t set_menu_parent[SET_MENUS] = { SET_HOME, SET_HOME, SET_HOME, SET_HOME, SET_HOME,
    SET_DGN, SET_DGN, SET_DIAG };
static const char *const set_ch_title[CH_N] = { "COLOUR", "METER", "EQUALIZER", "REPEAT",
    "SCREEN BLANK", "SPEED", "STRESS LEVEL", "SOAK", "THEME", "MODE", "TEMPO" };
static const uint8_t set_ch_parent[CH_N] = { SET_APPEAR, SET_APPEAR, SET_AUDIO, SET_PLAY,
    SET_APPEAR, SET_PLAY, SET_STRESS, SET_STRESS, SET_APPEAR, SET_APPEAR, SET_PLAY };
/* The speed list offers every entry up to 2.00x for an MP3 (owner decision 2026-10-03, B-555). The old limit of 1.20x came from A-133/A-135, when 1.25x micro-stuttered at
 * 60 MHz with the software MP3 synthesis window; with the hardware window and the 66.7 MHz clock a stereo 128 kbps MP3 is 83% busy at 1.50x with no FIFO stalls
 * (B-553) and the owner heard clean MP3 at 1.75x. These are VARISPEED speeds: the pitch rises with the speed (real tempo without the pitch change is the Cymo C7
 * work). Above 1.20x is not a promise for every file: a stereo MP3 has about a quarter of the CPU free at 1.50x. 2.50x stays behind Diagnostics > ALL SPEEDS. A FLAC
 * always plays at 1.00x (B-554). The speed table itself lives in player.c. */
#define SET_SPEED_SHOWN 9u
_Static_assert(SET_SPEED_SHOWN <= SPEED_N, "speed list longer than the speed table");
static const char *const set_speed_nm[SPEED_N] = { "0.85X", "0.95X", "1.00X", "1.10X", "1.20X",
    "1.30X", "1.50X", "1.75X", "2.00X", "2.50X" };
/* Diagnostic Build only: Diagnostics > ALL SPEEDS adds the last table entry (2.50x) to the speed list, as a
 * test bed for the audio work. Not persisted; off at every start; turning it off returns 2.50x to 1.00x. */
#if TAU_DIAGNOSTIC
static uint8_t set_speed_all;
#define SET_SPEED_COUNT() ((uint32_t)(set_speed_all ? SPEED_N : SET_SPEED_SHOWN))
#else
#define set_speed_all 0u
#define SET_SPEED_COUNT() ((uint32_t)SET_SPEED_SHOWN)
#endif
static const char *const set_stress_nm[4] = { "OFF", "R1  16K OPS/S PACED", "R2  8 OP BURSTS",
    "R3  32 OP BURSTS" };
static const char *const set_soak_nm[5] = { "OFF", "5 MIN", "15 MIN", "30 MIN", "60 MIN" };
static const uint8_t     set_soak_min[5] = { 0u, 5u, 15u, 30u, 60u };

#include "meter_gen_names.h"
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
_Static_assert(LIB_ROW_H == SET_MENU_ROW_H, "the library rows must match the settings rows");

static uint8_t set_page, set_sel[SET_MENUS], set_ch_cur[CH_N], set_ch_top[CH_N];
/* B-410 (owner-reported: "when the menu is visible and overflows it will overlay the action bar"):
 * set_draw_menu() drew every row in set_menu_rows[p] unconditionally, with no bound against the
 * visible list area -- unlike set_draw_choice()'s own set_ch_top[]/set_ch_follow() scrolling, which
 * this mirrors. Never hit before because no plain menu had grown past SET_MENU_VIS rows until the
 * Diagnostics submenu (set_dgn_rows) did. */
static uint8_t set_top[SET_MENUS];
#define SET_MENU_VIS (SET_LIST_H / SET_MENU_ROW_H)

#include "meter_thumbs.h"
_Static_assert(VIZ_COUNT == 17 && VIZ_BARS == 0 && VIZ_LED == 10 && VIZ_RETIRED_TAPE == 11 &&
               VIZ_WINAMP_BARS == 12 && VIZ_WINAMP_SCOPE == 13 && VIZ_CHLADNI == 14 && VIZ_VU_MASTER == 15 && VIZ_LAYERED_WAVE == 16,
               "meter_thumbs.h is indexed by the VIZ_* enum order (tools/gen_meter_thumbs.py ORDER)");

/* Theme step 0c: the previews are stored as neutral greys (plus an accent-blue placeholder in the newest three), so their palette is
 * re-derived from the theme at draw time instead of drawn as stored. A grey entry becomes a mix from the plate colour (role base) at
 * the darkest stored grey to the primary text colour at the lightest, by its luma; an entry with real chroma becomes the accent.
 * Dark themes therefore look close to the stored greys and Light inverts to dark features on a light plate. Cheap: 8 mixes per draw. */
COLD_FN static void set_thumb_pal(uint32_t viz, uint16_t *out)
{
    const uint16_t *src = meter_thumb_pal[viz];
    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t c = src[i], r = ((c >> 11) & 0x1Fu) * 255u / 31u, g = ((c >> 5) & 0x3Fu) * 255u / 63u, b = (c & 0x1Fu) * 255u / 31u;
        uint32_t hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
        if (hi - lo > 40u) { out[i] = ui_accent; continue; }
        uint32_t l = (2126u * r + 7152u * g + 722u * b) / 10000u;
        l = l <= 16u ? 0u : (l > 255u ? 239u : l - 16u);       /* stored greys run about 16..238; luma 255 would be the primary colour itself */
        out[i] = ui_mix(th_role[TR_BASE], th_role[TR_TEXT_PRIMARY], l, 239u);
    }
}

/* Draw preview `viz` at (x, y) by decoding the RLE in software: fill with the dominant colour,
 * then paint every other run as a one-row rectangle (runs may cross row ends). This is the
 * original path -- dozens of fb_rect draw-engine round-trips per thumbnail -- kept as the B8
 * fail-safe for a bitstream without the blit engine (BLIT_READY() == 0, B-126). */
COLD_FN static void set_draw_thumb_soft(uint32_t x, uint32_t y, uint32_t viz)
{
    uint16_t pal[8];
    set_thumb_pal(viz, pal);
    const uint8_t *p = &meter_thumb_rle[meter_thumb_off[viz]], *e = &meter_thumb_rle[meter_thumb_off[viz + 1u]];
    uint32_t pos = 0;
    fb_rect(x, y, METER_THUMB_W, METER_THUMB_H, pal[0]);
    while (p < e) {
        uint32_t b = *p++, idx = b >> 5, n = (b & 31u) + 1u;
        if (idx) {
            uint32_t at = pos, left = n;
            while (left) {
                uint32_t col = at % METER_THUMB_W, seg = METER_THUMB_W - col;
                if (seg > left) seg = left;
                fb_rect(x + col, y + at / METER_THUMB_W, seg, 1u, pal[idx]);
                at += seg; left -= seg;
            }
        }
        pos += n;
    }
}

/* Phase F B8 firmware integration: VIZ_COUNT flat 56x32 palette-index buffers, one CLUT-blit
 * source per thumbnail, stashed in the same off-screen-row address space ART_STASH_Y already
 * uses (row*FB_STRIDE+col, never displayed past FB_H=360) -- not a new SDRAM region, the same
 * proven convention. Built ONCE, lazily, from the same RLE data set_draw_thumb_soft() decodes.
 *
 * B-192 correction, 2026-09-25: this used to write each pixel via a raw CPU pointer at
 * 0xA0000000+word_addr*2 -- the SAME broken idiom blit_probe.inc's sentinel used (B-191/B-192),
 * hanging forever the first time this ever ran on real hardware (that address is the draw
 * engine's own framebuffer/guard region, below the CPU's mapped SDRAM window, which starts a
 * full 1 MiB later at PL_SDRAM_BASE). Unlike blit_probe()'s single sentinel word, a mailbox
 * round-trip per pixel here (~21,504 individual writes total, VIZ_COUNT x 56 x 32) would cost
 * far more than the original ~16ms one-time budget. Fixed properly instead: one fb_rect() draw-
 * engine command per RLE run-segment (the exact same grouping set_draw_thumb_soft() already uses
 * successfully for its own, proven-working software-fallback path) -- fb_rect() only ever issues
 * MMIO writes (R_FB_ADDR/SIZE/COLOR/GO), never a CPU pointer into this region, so it reaches the
 * stash rows correctly regardless. This also drops run-length from individual-word to
 * one-command-per-run, faster than either the old raw-pointer loop or a mailbox rewrite would be. */
#define THUMB_STASH_Y (ART_STASH_Y + ART_H)
#if TAU_ART_TIMG
_Static_assert(TIMG_PLANE_Y == THUMB_STASH_Y + THUMB_LIVE_SLOTS * METER_THUMB_H && TIMG_PLANE_Y + 128u <= 984u,
               "the cover index plane must sit after the compacted thumbnail stash and end before the Chladni plane (row 984)");
#endif
static uint8_t thumb_flat_ready;

/* B-412 (audit after B-410/B-411): same lazy-cache-marked-ready-regardless-of-success pattern as
 * ui_bg_restore()'s own bug, though its only real caller (set_draw_thumb(), reached from Settings'
 * own choice-list draw or wvcfg_preview_tick(), both of which already set ov_draw=1 around their own
 * call) means FB_HELD() should already be false whenever this genuinely runs -- fixed anyway so it
 * can't become a real bug if a future caller reaches it from a context that doesn't. */
COLD_FN static void set_thumb_flat_build(void)
{
    if (FB_HELD()) return;
    /* B-572: the index plane must live in buffer 0, where set_draw_thumb()'s CLUT blit reads it (blit-mode commands follow the sticky bases,
     * never R_DBUF_CPU). fb_rect() follows R_DBUF_CPU, which Settings leaves on the DISPLAYED buffer (0 or 1 depending on how many chrome
     * redraws the session has had), so build with it forced to 0 and restore it once the commands have really executed. */
    const uint32_t was_cpu = ui_bg_cur_buf();
    if (was_cpu) REG(R_DBUF_CPU) = 0u;
    for (uint32_t viz = 0; viz < VIZ_COUNT; viz++) {
#if TAU_ART_TIMG
        if (viz == VIZ_VU_MASTER || viz == VIZ_LAYERED_WAVE) continue;    /* excluded from the compacted stash -- no row budget for it, see thumb_slot() */
#endif
        const uint8_t *p = &meter_thumb_rle[meter_thumb_off[viz]], *e = &meter_thumb_rle[meter_thumb_off[viz + 1u]];
        uint32_t pos = 0;
        uint32_t base_row = THUMB_STASH_Y + thumb_slot(viz) * METER_THUMB_H;
        while (p < e) {
            uint32_t b = *p++, idx = b >> 5, n = (b & 31u) + 1u;
            uint32_t at = pos, left = n;
            while (left) {
                uint32_t col = at % METER_THUMB_W, row = at / METER_THUMB_W;
                uint32_t seg = METER_THUMB_W - col;
                if (seg > left) seg = left;
                fb_rect(col, base_row + row, seg, 1u, (uint16_t)idx);
                at += seg; left -= seg;
            }
            pos += n;
        }
    }
    fb_wait();
    if (was_cpu) { fb_fence(); REG(R_DBUF_CPU) = was_cpu; }
    thumb_flat_ready = 1u;
}

COLD_FN static void set_draw_thumb(uint32_t x, uint32_t y, uint32_t viz)
{
    if (!cold_ready()) { fb_rect(x, y, METER_THUMB_W, METER_THUMB_H, UI_TRACK); return; }   /* cold image not loaded: a plain box */
#if TAU_ART_TIMG
    if (viz == VIZ_VU_MASTER || viz == VIZ_LAYERED_WAVE) { set_draw_thumb_soft(x, y, viz); return; }                  /* excluded from the compacted stash, see thumb_slot() */
#endif
    blit_probe_ensure();                                                                    /* B-162: first need, not at boot */
    if (!BLIT_READY()) { set_draw_thumb_soft(x, y, viz); return; }                          /* B8 fail-safe: old bitstream */
    if (!thumb_flat_ready) set_thumb_flat_build();
    uint16_t pal[8];
    set_thumb_pal(viz, pal);
    fb_clut_load(pal, 8u);
    /* B-572: source = the plane in buffer 0, destination = the buffer the page is being drawn into (R_DBUF_CPU: the displayed one, or the
     * back buffer during the menu cross-fade render). fb_fence() makes the blit EXECUTE before the bases are restored and before the next
     * preview reloads the (single, shared) CLUT: fb_clut_load() only waits for FIFO space, so without it the previews drew with each
     * other's palettes. */
    fb_set_bases(0u, ui_bg_cur_buf() ? DBUF_BASE1_W : 0u);
    fb_cblit(0u, THUMB_STASH_Y + thumb_slot(viz) * METER_THUMB_H, x, y, METER_THUMB_W, METER_THUMB_H);
    fb_fence();
    fb_set_bases(0u, 0u);
}

#if TAU_DIAGNOSTIC
/* ---- Timed soak (Diagnostic Build): runs the stress pump for N minutes beside normal playback,
 * then stops it and reports PASS (no data mismatch or timeout) or FAIL. Late underruns are counted
 * and shown on the status page but are not part of the verdict (at R3 they can be CPU starvation). ---- */
static uint8_t  dg_soak_idx, dg_soak_res;       /* index in set_soak_min; 0 none, 1 running, 2 pass, 3 fail */
static uint32_t dg_soak_left, dg_soak_at;       /* seconds left; next one-second boundary */
static uint32_t dg_soak_active(void) { return dg_soak_res == 1u ? dg_soak_idx : 0u; }
static void dg_soak_cancel(void) { dg_soak_res = 0u; dg_soak_idx = 0u; }
static void dg_soak_start(uint32_t i)
{
    if (!i) { dg_soak_cancel(); stress_set_level(0u); return; }
    uint8_t lvl = stress_on ? stress_level : 2u;        /* keep the chosen level, else R2 */
    stress_set_level(0u);
    stress_set_level(lvl);                              /* fresh counters */
    if (!stress_on) return;                             /* no window: refused, toast shown */
    dg_soak_idx  = (uint8_t)i;
    dg_soak_left = (uint32_t)set_soak_min[i] * 60u;
    dg_soak_at   = cycles() + CLK_HZ;
    dg_soak_res  = 1u;
}
static void dg_soak_tick(void)
{
    if (dg_soak_res != 1u) return;
    if ((int32_t)(cycles() - dg_soak_at) < 0) return;
    dg_soak_at = cycles() + CLK_HZ;
    if (dg_soak_left) dg_soak_left--;
    if (stress_failures || !stress_on || !dg_soak_left) {
        dg_soak_res = (stress_failures || (!stress_on && dg_soak_left)) ? 3u : 2u;
        stress_set_level(0u);
    }
}
#else
#define dg_soak_active() 0u
#endif

COLD_FN static uint32_t set_ch_count(uint32_t c)
{
    /* VIZ_SEL_COUNT, not VIZ_COUNT: retired meters (cassette, magic eye, L/R levels, mirrored bars)
     * have no row, so the choice list offers fewer rows than the enum has values -- see viz_sel_to_mode()/viz_mode_to_sel() in fw/player.c. */
    return c == CH_COLOUR ? (uint32_t)UI_PALETTE_N : c == CH_METER ? (uint32_t)VIZ_SEL_COUNT
         : c == CH_EQ ? (uint32_t)EQ_COUNT : c == CH_REPEAT ? 3u
         : c == CH_STRESS ? 4u : c == CH_SPEED ? SET_SPEED_COUNT()
         : c == CH_THEME ? TH_COUNT() : c == CH_MODE ? 2u : c == CH_TEMPO ? 6u : 5u;
}
COLD_FN static uint32_t set_blank_idx(void)
{
    uint32_t n = 0;
    while (n < 4u && set_blank[n] != blank_min) n++;
    return n;
}
COLD_FN static uint32_t set_ch_active(uint32_t c)
{
    if (c == CH_STRESS) {
#if TAU_DIAGNOSTIC
        return stress_on ? stress_level : 0u;
#else
        return 0u;
#endif
    }
    if (c == CH_SOAK) return dg_soak_active();
    if (c == CH_SPEED) return speed_eff();      /* a FLAC always plays at 1.00x (B-554) */
    if (c == CH_THEME) return th_theme;
    if (c == CH_MODE) return th_pol;
#if TAU_TEMPO
    if (c == CH_TEMPO) return tempo_idx;
#endif
    return c == CH_COLOUR ? ui_pal_idx : c == CH_METER ? viz_mode_to_sel(viz_mode) : c == CH_EQ ? eq_idx
         : c == CH_REPEAT ? rep_mode : set_blank_idx();
}
COLD_FN static const char *set_ch_name(uint32_t c, uint32_t i)
{
    return c == CH_COLOUR ? ui_palette_name[i] : c == CH_METER ? set_viz[viz_sel_to_mode(i)]
         : c == CH_EQ ? eq_name[i] : c == CH_REPEAT ? set_rep[i]
         : c == CH_STRESS ? set_stress_nm[i] : c == CH_SOAK ? set_soak_nm[i]
         : c == CH_SPEED ? set_speed_nm[i] : c == CH_THEME ? th_get(i)->name
         : c == CH_MODE ? (i ? "LIGHT" : "DARK")
#if TAU_TEMPO
         : c == CH_TEMPO ? tempo_txt[i]
#endif
         : set_blank_nm[i];
}
COLD_FN static uint32_t set_ch_rowh(uint32_t c)
{
    return c == CH_METER ? SET_TH_ROW_H : SET_CH_ROW_H;
}
COLD_FN static uint32_t set_ch_visible(uint32_t c) { return SET_LIST_H / set_ch_rowh(c); }

COLD_FN static void set_ch_apply(uint32_t c, uint32_t i)
{
    if (c == CH_STRESS || c == CH_SOAK) {
#if TAU_DIAGNOSTIC
        if (c == CH_STRESS) { dg_soak_cancel(); stress_set_level((uint8_t)i); }
        else                dg_soak_start(i);
#endif
        return;
    }
    if (c == CH_SPEED) {
        if (track_fmt == FMT_FLAC) { ui_toast_msg("SPEED: MP3 ONLY"); return; }   /* B-554: no speed for FLAC */
#if TAU_TEMPO
        tempo_idx = 0u;                          /* B-558: choosing a varispeed speed turns tempo off */
#endif
        speed_idx = (uint8_t)i;
        pcm_rate_apply(track_hz);
        ui_last_sec = 0xFFFFFFFFu;               /* repaint the speed marker next frame */
        ui_mode_dirty = 1u;
        return;                                  /* not persisted */
    }
#if TAU_TEMPO
    if (c == CH_TEMPO) {
        if (track_fmt == FMT_FLAC) { ui_toast_msg("TEMPO: MP3 ONLY"); return; }
        tempo_idx = (uint8_t)i;
        if (i) speed_idx = SPEED_1X;             /* tempo and varispeed exclude each other */
        tempo_cfg_key = 0u;                      /* the stretcher starts again at the next frame */
        pcm_rate_apply(track_hz);
        ui_last_sec = 0xFFFFFFFFu;               /* repaint the speed marker next frame */
        ui_mode_dirty = 1u;
        return;                                  /* not persisted */
    }
#endif
    if (c == CH_COLOUR) {
        ui_pal_idx = i;
        ui_accent = th_accent_of(i);
        ui_grad_set(ui_accent);
        ui_accent_changed = 1u;
    } else if (c == CH_METER) {
        viz_mode = (uint8_t)viz_sel_to_mode(i);
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
    } else if (c == CH_THEME || c == CH_MODE) {
        /* B-416 (owner-reported: theme and mode fail to persist across a restart, while colour and
         * meter preset both pass): this comment used to say "session-only (step 0b): reapplies every
         * colour, nothing persisted" and returned immediately -- true before persist widening (step
         * 0d) existed, stale ever since. SW_THEME/SW_POL are genuinely saved/loaded via
         * set_wr32()/set_rd32() in settings_save()/settings_load() (fw/settings.inc), but nothing
         * ever triggered that save for THIS specific change: the old early `return` skipped the same
         * settings_mark_dirty() call every other branch here reaches at the bottom. Fixed by moving
         * this into the same if/else-if chain as CH_COLOUR/CH_METER/CH_EQ/CH_REPEAT instead of being
         * a separate block with its own return, so it falls through the identical way they do. */
        if (c == CH_THEME) th_theme = (uint8_t)i; else th_pol = (uint8_t)i;
        th_apply();
        ui_last_sec = 0xFFFFFFFFu;
    } else {
        blank_min = set_blank[i];
        ui_blank_touch();
        return;                                  /* not persisted */
    }
    settings_mark_dirty();
}

COLD_FN static void set_toggle(uint32_t t)
{
    if (t == TG_ART) {
        art_toggle = 1u;                         /* the main loop applies it, as for the hold */
#if TAU_DIAGNOSTIC
    } else if (t == TG_SPEEDS) {
        set_speed_all = (uint8_t)!set_speed_all;
        if (!set_speed_all && speed_idx >= SET_SPEED_SHOWN) {
            speed_idx = SPEED_1X;                /* leave the fast speeds behind */
            pcm_rate_apply(track_hz);
            ui_last_sec = 0xFFFFFFFFu;
            ui_mode_dirty = 1u;
        }
        return;                                  /* not persisted */
    } else if (t == TG_RATES) {
        /* 2026-09-28, B-356: lets a FLAC above FLAC_MAX_RATE (measured, not chosen -- see the comment on
         * that constant) load and run for a real decode-stage Check reading, instead of being refused
         * outright. Off at every start, never persisted, same convention as TG_SPEEDS: this is a test
         * switch, not a listening preference. */
        flac_accept_all_rates = (uint8_t)!flac_accept_all_rates;
        return;                                  /* not persisted */
    } else if (t == TG_CYMO) {
        /* B-471..B-478: first-ever hardware test of the Cymo resampler's live audio path. No-op if the
         * bitstream doesn't have it (the write lands on an unmapped register, same convention as every
         * other inert-when-absent register here). Off at every start, never persisted -- a test switch,
         * not a listening preference, same convention as TG_RATES/TG_SPEEDS. */
        if (CYMO_RESAMP_READY()) {
            cymo_live_toggle = (uint8_t)!cymo_live_toggle;
            cymo_guard_apply(0u);   /* B-530: engages only for a 44.1 kHz file at 1.00x */
        }
        return;                                  /* not persisted */
#endif
    }
    settings_mark_dirty();
}

COLD_FN static void set_volume(int dir)
{
    if (dir > 0) volume = (volume + VOL_STEP > VOL_MAX) ? VOL_MAX : volume + VOL_STEP;
    else         volume = (volume < VOL_STEP) ? 0u : volume - VOL_STEP;
    vol_apply();
    ui_mode_dirty = 1u;                          /* the speaker icon */
    settings_mark_dirty();
}

static char *set_str(char *q, const char *s) { while (*s) *q++ = *s++; return q; }
#if TAU_DIAGNOSTIC
#include "settings_diag.inc"
#else
#define dg_base_under 0u
#define dg_base_stall 0u
#endif

COLD_FN static void set_menu_value(const set_row_t *r, char *o)
{
    const char *s = "";
    char *q = o;
    if (r->type == RT_GROUP) s = "";
#if TAU_DIAGNOSTIC
    else if (r->type == RT_ACTION) s = dg_res[r->arg];
#else
    else if (r->type == RT_ACTION) s = "";
#endif
    else if (r->type == RT_CHOICE) s = set_ch_name(r->arg, set_ch_active(r->arg));
    else if (r->type == RT_VOL) { q = ui_dec(q, volume); s = "%"; }
    else if (r->arg == TG_ART)    s = art_pref ? "ON" : "OFF";
#if TAU_DIAGNOSTIC
    else if (r->arg == TG_RATES)  s = flac_accept_all_rates ? "ON" : "OFF";
    else if (r->arg == TG_CYMO)   s = !cymo_live_toggle ? "OFF" : cymo_live_on ? "ON" : "WAIT";   /* WAIT = on, but this file is not 44.1 kHz at 1.00x (B-530) */
#endif
    else                          s = set_speed_all ? "ON" : "OFF";   /* TG_SPEEDS, the only remaining toggle */
    while (*s) *q++ = *s++;
    *q = 0;
}

/* A filled circle of diameter d centred on (cx, cy), corners cut to bg. */
COLD_FN static void set_disc(uint32_t cx, uint32_t cy, uint32_t d, uint16_t colour, uint16_t bg)
{
    fb_round_rect_on(cx - d / 2u, cy - d / 2u, d, d, d / 2u, colour, bg);
}

COLD_FN static void set_draw_menu(uint32_t p)
{
    const set_row_t *rows = set_menu_rows[p];
    uint32_t n = set_menu_n[p], top = set_top[p];
    ov_frame(set_menu_title[p], "",
             p == SET_HOME ? "A OPEN   B CLOSE"
             : p == SET_DIAG || p == SET_STRESS || p == SET_DGN ? "A OPEN   B BACK" : p == SET_TESTS ? "A RUN   B BACK"
             : "A CHANGE   B BACK");
    if (n > SET_MENU_VIS) {                      /* scroll track, as set_draw_choice()'s own */
        uint32_t tx = PL_UI_X + PL_UI_W - 11u, th = SET_LIST_H * SET_MENU_VIS / n;
        fb_rect(tx, PL_UI_LIST_Y - 2u, 3u, SET_LIST_H, ui_mix(OV_BODY, UI_DIM, 1u, 3u));
        fb_rect(tx, PL_UI_LIST_Y - 2u + (SET_LIST_H - th) * top / (n - SET_MENU_VIS), 3u, th, ui_accent);
    }
    for (uint32_t k = 0; k < SET_MENU_VIS && top + k < n; k++) {
        uint32_t i = top + k;
        uint32_t y = PL_UI_LIST_Y + k * SET_MENU_ROW_H;
        int sel = (i == set_sel[p]);
        uint16_t bg = sel ? ui_accent : OV_BODY;
        if (sel) fb_round_rect_on(PL_UI_X + 4u, y, PL_UI_W - 8u, SET_MENU_ROW_H - 4u, 5u, bg, OV_BODY);
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

COLD_FN static void set_draw_choice(uint32_t c)
{
    uint32_t n = set_ch_count(c), rowh = set_ch_rowh(c), vis = set_ch_visible(c);
    uint32_t top = set_ch_top[c], act = set_ch_active(c);
    ov_frame(set_ch_title[c], "", "A SELECT   B BACK");
    if (n > vis) {                               /* scroll track, as the playlist */
        uint32_t tx = PL_UI_X + PL_UI_W - 11u, th = SET_LIST_H * vis / n;
        fb_rect(tx, PL_UI_LIST_Y - 2u, 3u, SET_LIST_H, ui_mix(OV_BODY, UI_DIM, 1u, 3u));
        fb_rect(tx, PL_UI_LIST_Y - 2u + (SET_LIST_H - th) * top / (n - vis), 3u, th, ui_accent);
    }
    for (uint32_t k = 0; k < vis && top + k < n; k++) {
        uint32_t i = top + k, y = PL_UI_LIST_Y + k * rowh, cy = y + (rowh - 2u) / 2u;
        int sel = (i == set_ch_cur[c]), on = (i == act);
        uint16_t bg = sel ? ui_accent : OV_BODY;
        if (sel) fb_round_rect_on(PL_UI_X + 4u, y, PL_UI_W - 20u, rowh - 2u, 5u, bg, OV_BODY);
        uint32_t mx = PL_UI_TEXT_X + 10u;         /* marker centre */
        if (c == CH_COLOUR) {                     /* swatch: theme colour, white ring if active */
            set_disc(mx, cy, 20u, on ? UI_WHITE : (sel ? UI_PANEL : ui_mix(OV_BODY, UI_DIM, 1u, 3u)), bg);
            set_disc(mx, cy, 14u, ui_palette[i], on ? UI_WHITE : (sel ? UI_PANEL : ui_mix(OV_BODY, UI_DIM, 1u, 3u)));
        } else {                                  /* radio: ring, dot when active */
            uint16_t ring = sel ? UI_PANEL : UI_WHITE;
            set_disc(mx, cy, 18u, ring, bg);
            set_disc(mx, cy, 12u, bg, ring);
            if (on) set_disc(mx, cy, 8u, sel ? UI_PANEL : ui_accent, bg);
        }
        uint32_t tx = mx + 24u;
        if (c == CH_METER) {                      /* preview placeholder */
            set_draw_thumb(tx, y + (rowh - 2u - SET_TH_H) / 2u, viz_sel_to_mode(i));
            tx += SET_TH_W + 14u;
        }
        fb_set_color(sel ? UI_PANEL : UI_WHITE, bg);
        fb_text_clipped(tx, y + (rowh - 2u - 16u) / 2u, set_ch_name(c, i), TS_1X, TS_1X,
                        PL_UI_X + PL_UI_W - 24u - tx);
    }
}

/* ---- Diagnostics / Info: read-only live values, refreshed once a second (rows only). ---- */
extern char _heap_start, _heap_end;
#define SET_INFO_ROWS 34u   /* B-481: +1 for CYMO RESAMP. B-467: +1 for I2S JITTER -- the clk_audio->clk_mclk CDC's real min/max/average
                              * update interval (clk_sys cycles) since reset, measured via a single-bit
                              * toggle synchronised with tau_cdc_sync1 then timed against cycle_ctr entirely
                              * in clk_sys -- no further CDC risk. Part of the Cymo 44.1 kHz investigation
                              * after the MCLK PLL fix (B-457/B-460/B-463) measured no SINAD improvement on
                              * real hardware (B-467): this checks whether the CDC itself, the one remaining
                              * unverified layer, behaves as cleanly on real silicon as it does in
                              * simulation (ideal: min/max within 1-2 cycles of CLK_HZ/44100). 0 on any
                              * bitstream without TAU_I2S_DIAG. See sound_i2s.v/mp3_soc.v.
                              * B-370: +1 for FLAC LPC.
                              * B-073: +1 for COVER (art_bad_code, for the "no
                                              * picture found" report); B-234: +1 for METER YIELD
                                              * (a cheap diagnostic for the suspected meter_afford()
                                              * freeze, see its own comment in fw/player.c); B-238:
                                              * +1 for VBLANK -- proves the
                                              * new Helios/Talos H0 MMIO end to end (edges/sec,
                                              * should read this core's real vertical rate once a
                                              * TAU_VBLANK bitstream is installed; 0 on any other
                                              * bitstream, see vblank_sample()'s own comment).
                                              * The legacy-playlist-only PLAYLIST row was removed
                                              * with legacy playlist mode. B-413: +1 for SCOPE BG
                                              * (last row) -- live blend attempt/fail counters and
                                              * ui_bg_ready, for the still-unexplained Winamp Scope
                                              * trail "accumulation" report; see fw/player.c's
                                              * wviz_scope_tick() for what increments them. B-433:
                                              * +1 for STRIP -- Option A of the same investigation,
                                              * reads back the gradient strip's actual SDRAM content
                                              * against what ui_grad_at() expects right now, to tell
                                              * a corrupted strip apart from a correct strip feeding a
                                              * broken blend datapath; see dbg_strip_check() in
                                              * fw/player.c. B-434: +1 for BASES -- owner confirmed
                                              * STRIP always reads OK, so before JTAG this shows the
                                              * sticky SRC_BASE/DST_BASE fields' shadow (write-only in
                                              * hardware) at the moment of the last Scope trail blend;
                                              * see fb_set_bases()'s own comment in fw/player.c. B-439:
                                              * +1 for DBUF -- STRIP/BASES both read clean during a live
                                              * repro, so this checks the last remaining addressing gap:
                                              * whether R_DBUF_CPU (what RECT-class draws follow) and
                                              * R_DBUF_DISP (what's actually shown) agree at the moment
                                              * of the blend; see dbg_strip_check() in fw/player.c.
                                              * B-440: +1 for ALPHA -- trail 5% and 80% look identical
                                              * on hardware, so this shows fb_blend_on()'s own shadow of
                                              * the last alpha value actually written (write-only in
                                              * hardware); see fw/blit_probe.inc. B-444: +1 for PIXHIST
                                              * -- JTAG polling proved it can't catch a real decay curve
                                              * (a genuinely fading pixel converges too fast for any
                                              * opportunistic poll), so this instead logs one fixed
                                              * on-screen pixel's actual content across the last 4
                                              * blend attempts; see dbg_pixel_log() in fw/player.c. */
static const char *const set_info_label[SET_INFO_ROWS] = {
    "FIRMWARE", "FPGA REV", "SDRAM WINDOW", "WINDOW READ", "FREE RAM",
    "COLD IMAGE",
    "TRACK", "COVER", "UNDERRUNS", "DRAW STALL", "LOAD MS"
    , "LIBRARY"
    , "METER YIELD", "VBLANK", "SPECTRUM", "BEAM", "CHLADNI", "CPU LOAD", "TIM1 COVER", "MP3 WINDOW", "THEME FILE", "METER FILE"
    , "SCOPE BG"
    , "STRIP"
    , "BASES"
    , "DBUF"
    , "ALPHA"
    , "PIXHIST"
    , "FLAC LPC"
    , "I2S JITTER"
    , "CYMO RESAMP"
    , "METER DRAW"
    , "LW COST"
    , "HEADROOM"
    };
static uint32_t set_info_at;


COLD_FN static void set_info_value(uint32_t i, char *o)
{
    char *q = o;
    switch (i) {
    case 0: q = set_str(q, APP_VER); break;
    case 1: {
        uint32_t v = REG(R_VERSION);
        for (int sh = 24; sh >= 0; sh -= 8) q = ui_hex2(q, (uint8_t)(v >> sh));
        break; }
    case 2:
        q = set_str(q, pl_sdram_state == 1u ? "OK" : pl_sdram_state == 2u ? "FAILED" : "UNTESTED");
        break;
    case 3:
        if (pl_win_rd) { q = ui_dec(q, pl_win_rd); q = set_str(q, " CYC"); }
        else q = set_str(q, "-");
        break;
    case 4: q = ui_dec(q, (uint32_t)(&_heap_end - &_heap_start)); q = set_str(q, " B"); break;
    case 5:
        if (cold_state == 1u) { q = ui_dec(q, cold_bytes); q = set_str(q, " B "); q = ui_dec(q, cold_ms); q = set_str(q, " MS");
            if (COLD_READY()) q = set_str(q, " CODE");
            else if (cold_err) { q = set_str(q, " E"); q = ui_dec(q, cold_err); }   /* image loaded but cold code refused */
        }
        else { q = set_str(q, "OFF E"); q = ui_dec(q, cold_err); }
        break;
    case 6:
        if (!track_hz) { q = set_str(q, "NONE"); break; }
        q = set_str(q, track_fmt == FMT_FLAC ? "FLAC " : "MP3 ");
        if (track_kbps) { q = ui_dec(q, track_kbps); q = set_str(q, "K "); }
        q = ui_dec(q, track_hz / 1000u); *q++ = '.'; q = ui_dec(q, (track_hz % 1000u) / 100u);
        *q++ = 'K';
        break;
    case 7:                                          /* B-073: art_bad_code, to tell "no picture in the tag" from "unreadable" */
        if (!art_bad_code) q = set_str(q, art_have ? "OK" : "-");
        else { q = set_str(q, "E"); q = ui_hex2(q, art_bad_code); }
        break;
    case 8: q = ui_dec(q, pcm_under_n - dg_base_under);
#if TAU_DIAGNOSTIC
        q = set_str(q, " ALL "); q = ui_dec(q, ur_all.n);   /* B-546: every stall since boot; the first number is at most one per flush */
#endif
        break;
    case 9: q = ui_dec(q, (REG(R_FB_STALL) - dg_base_stall) / (CLK_HZ / 1000u)); q = set_str(q, " MS"); break;
    case 10:
        q = ui_dec(q, ld_head); *q++ = '/'; q = ui_dec(q, ld_size); *q++ = '/';
        q = ui_dec(q, ld_art);  *q++ = '/'; q = ui_dec(q, ld_total);
        break;
    case 11: q = lib_info_text(q); break;
    case 12:
        /* METER YIELD (B-234) -- see meter_afford()'s own comment */
        if (meter_yield) { q = set_str(q, "YIELD "); q = ui_dec(q, meter_yield_secs); q = set_str(q, "S"); }
        else q = set_str(q, "OK");
        if (meter_yield_worst) { q = set_str(q, " (WORST "); q = ui_dec(q, meter_yield_worst); q = set_str(q, "S)"); }
        break;
    case 13:   /* VBLANK (B-238) -- see vblank_sample()'s own comment */
        q = ui_dec(q, vblank_rate); q = set_str(q, "/S");
        break;
    case 14:   /* SPECTRUM (B-263): which analyser is running, and for the hardware one how many windows it has finished */
        if (spec_hw) { q = set_str(q, "HW W"); q = ui_dec(q, (REG(R_SPEC_ST) >> 16) & 0xFFFFu); }
        else q = set_str(q, "NO HW BANK");
        break;
    case 15:   /* BEAM (B-267): Helios beam-aware drawing -- share of meter updates that had to wait for the beam */
        if (!helios_beam_ok) q = set_str(q, "NONE");
        else {
            uint32_t tot = helios_drawn_n + helios_deferred_n;
            q = set_str(q, "OK "); q = ui_dec(q, tot ? (helios_deferred_n * 100u) / tot : 0u); q = set_str(q, "% WAITED");
        }
        break;
    case 16:   /* CHLADNI (B-276): first-use probe result, mailbox half order, figures drawn and skipped */
        if (chl_ok == 0u) q = set_str(q, "NOT USED");
        else if (chl_ok == 2u) q = set_str(q, "UNAVAILABLE");
        else {
            q = set_str(q, chl_swap ? "OK SWAP " : "OK ");
            q = ui_dec(q, chl_drawn_n); q = set_str(q, " DRAWN ");
            q = ui_dec(q, chl_skipped_n); q = set_str(q, " SKIP");
        }
        break;
    case 17:   /* CPU LOAD: share of the last second the decode loop was not waiting on the audio FIFO */
        q = ui_dec(q, ui_cpu_pct()); q = set_str(q, "%");
        break;
    case 18:   /* TIM1 COVER (B-288): how covers were shown -- files loaded, the last load time, and the last failure code (0 = none) */
#if TAU_ART_TIMG
        q = ui_dec(q, timg_loaded_n); q = set_str(q, " LOADED ");
        q = ui_dec(q, timg_ms); q = set_str(q, " MS E");
        q = ui_dec(q, timg_last_err);
        if (timg_last_err) { q = set_str(q, " ("); q = ui_dec(q, timg_fail_n); q = set_str(q, " FAILED)"); }
        if (timg_last_err == TIMG_E_PROBE) {                    /* what the probe read back, so a mismatch can be diagnosed from a screenshot */
            q = set_str(q, " "); q = ui_hex2(q, (uint8_t)(timg_probe_raw >> 24)); q = ui_hex2(q, (uint8_t)(timg_probe_raw >> 16));
            q = ui_hex2(q, (uint8_t)(timg_probe_raw >> 8)); q = ui_hex2(q, (uint8_t)timg_probe_raw);
        }
#else
        q = set_str(q, "OFF");
#endif
        break;
    case 19:   /* MP3 WINDOW (B-307): the hardware synthesis window -- slots it computed, self-check mismatches, timeouts */
#if TAU_POLY_FW
        if (!hw_poly) { q = set_str(q, "NO UNIT"); break; }
        q = set_str(q, tau_poly_hw_enable ? "HW " : "SW ");
        q = ui_dec(q, tau_poly_stat_slots); q = set_str(q, " SLOTS ");
        q = ui_dec(q, tau_poly_stat_mismatch); q = set_str(q, " BAD ");
        q = ui_dec(q, tau_poly_stat_timeout); q = set_str(q, " TMO");
#else
        q = set_str(q, "OFF");
#endif
        break;
    case 20:   /* THEME FILE (step 0d): extra themes read from tau-assets.bin -- how many, or the failure code, or none */
        if (th_file_err) { q = set_str(q, "E"); q = ui_dec(q, th_file_err); }
        else if (th_file_n) { q = ui_dec(q, th_file_n); q = set_str(q, " LOADED"); }
        else q = set_str(q, "NONE");
        break;
    case 21:   /* METER FILE (M4): meters whose presets were replaced from tau-assets.bin METR, or the failure code */
        if (mf_err) { q = set_str(q, "E"); q = ui_dec(q, mf_err); }
        else if (mf_n) { q = ui_dec(q, mf_n); q = set_str(q, " LOADED"); }
        else q = set_str(q, "NONE");
        break;
    case 22:   /* SCOPE BG (B-413): ready=1/0 (the lazy gradient-strip cache flag), then attempted blends OK/failed
                * (only counted when a blend was genuinely attempted -- use_gradient && trail && !paused all true) */
        q = set_str(q, ui_bg_ready ? "READY " : "STALE ");
        q = ui_dec(q, dbg_scope_blend_ok); q = set_str(q, " OK ");
        q = ui_dec(q, dbg_scope_blend_fail); q = set_str(q, " FAIL");
        break;
    case 23:   /* STRIP (B-433): OK/BAD across the last 3-row sample, then actual vs expected RGB565 for
                * each row (hex), so a mismatch is visible without needing a screenshot decode. */
        q = set_str(q, dbg_strip_bad ? "BAD " : "OK ");
        for (uint32_t k = 0; k < 3u; k++) {
            q = ui_hex2(q, (uint8_t)(dbg_strip_actual[k] >> 8)); q = ui_hex2(q, (uint8_t)dbg_strip_actual[k]);
            q = set_str(q, "/");
            q = ui_hex2(q, (uint8_t)(dbg_strip_expect[k] >> 8)); q = ui_hex2(q, (uint8_t)dbg_strip_expect[k]);
            q = set_str(q, " ");
        }
        break;
    case 24:   /* BASES (B-434): dbg_strip_base_src/dst, the sticky SRC_BASE/DST_BASE fields' last-known
                * value at the moment the last Scope trail blend fired (write-only in hardware, this is
                * the only way to see them). Nonzero here would mean that blend and ui_bg_blend()'s own
                * destination pre-read targeted the wrong SDRAM region -- worth chasing before JTAG. */
        q = set_str(q, "S "); q = ui_hex2(q, (uint8_t)(dbg_strip_base_src >> 16));
        q = ui_hex2(q, (uint8_t)(dbg_strip_base_src >> 8)); q = ui_hex2(q, (uint8_t)dbg_strip_base_src);
        q = set_str(q, " D "); q = ui_hex2(q, (uint8_t)(dbg_strip_base_dst >> 16));
        q = ui_hex2(q, (uint8_t)(dbg_strip_base_dst >> 8)); q = ui_hex2(q, (uint8_t)dbg_strip_base_dst);
        break;
    case 25:   /* DBUF (B-439): R_DBUF_CPU/R_DBUF_DISP's own buffer-select bits at the moment of the last
                * Scope trail blend. RECT-class draws (the trace bars, the strip build) auto-follow
                * R_DBUF_CPU; the blend's own fb_blit() does not -- if CPU != DISP here, the blend is
                * silently updating a buffer nothing is looking at while the trace keeps drawing on the
                * one that IS displayed, which is exactly "new draws pile up, nothing ever fades." */
        q = set_str(q, "CPU "); q = ui_dec(q, dbg_dbuf_cpu);
        q = set_str(q, " DISP "); q = ui_dec(q, dbg_dbuf_disp);
        q = set_str(q, dbg_dbuf_cpu != dbg_dbuf_disp ? " MISMATCH" : " OK");
        break;
    case 26:   /* ALPHA (B-440): dbg_strip_alpha, the last value actually WRITTEN to the sticky BLEND
                * field's alpha bits (write-only in hardware, fb_blend_on()'s own shadow). Owner reports
                * trail 5% and 80% look identical -- if this reads a plausible nonzero value matching
                * the configured trail, firmware's write is correct and hardware isn't applying it; if
                * it reads 0 or something unexpected, the bug is firmware's own computation, not the
                * blend datapath. */
        q = set_str(q, "A "); q = ui_dec(q, dbg_strip_alpha);
        break;
    case 27:   /* PIXHIST (B-444/B-445): the last 3 raw mailbox reads (both 16-bit halves each -- (x,y)
                * stays fixed so whichever half is this pixel's own stays the SAME half every read,
                * telling it apart from its neighbour's unrelated content by which one actually trends)
                * of one fixed on-screen pixel (the Scope box's own top-left corner), oldest first, then
                * W=the row's current expected background. JTAG polling proved unable to catch a real
                * decay curve (a genuinely fading pixel converges within ~4-5 frames, far faster than any
                * opportunistic poll) -- this instead shows the actual frame-by-frame sequence: one half
                * trending down toward W each entry means the blend IS decaying correctly; staying near a
                * stuck bright value means it genuinely isn't. 3 entries, not 4 -- B-445: 4 overflowed
                * set_draw_ro()'s fixed char v[40] row buffer and crashed hardware on real Pocket
                * silicon the instant this row scrolled into view; always check the byte count against
                * that buffer before adding to any row here. See dbg_pixel_log() in fw/player.c. */
        for (uint32_t k = 0; k < DBG_PIXHIST_N; k++) {
            uint32_t idx = (dbg_pixhist_pos + k) % DBG_PIXHIST_N;   /* oldest first */
            uint32_t v = dbg_pixhist[idx];
            q = ui_hex2(q, (uint8_t)(v >> 8)); q = ui_hex2(q, (uint8_t)v); q = set_str(q, "/");
            q = ui_hex2(q, (uint8_t)(v >> 24)); q = ui_hex2(q, (uint8_t)(v >> 16)); q = set_str(q, " ");
        }
        q = set_str(q, "W"); q = ui_hex2(q, (uint8_t)(dbg_pixhist_want >> 8)); q = ui_hex2(q, (uint8_t)dbg_pixhist_want);
        break;
    case 28:   /* FLAC LPC (B-370): the hardware LPC reconstruction unit -- samples reconstructed, timeouts */
#if TAU_LPC_FW
        if (!hw_lpc) { q = set_str(q, "NO UNIT"); break; }
        q = set_str(q, tau_lpc_hw_enable ? "HW " : "SW ");
        q = ui_dec(q, tau_lpc_stat_samples); q = set_str(q, " SAMPLES ");
        q = ui_dec(q, tau_lpc_stat_timeout); q = set_str(q, " TMO");
#else
        q = set_str(q, "OFF");
#endif
        break;
    case 29: {   /* I2S JITTER (B-467): min/max/avg interval, in clk_sys cycles, between real I2S DAC-domain
                  * sample updates since reset -- see this row's own SET_INFO_ROWS comment above. */
        if (!(REG(R_I2S_DIAG_ST) & 1u)) { q = set_str(q, "NO UNIT"); break; }
        uint32_t mm = REG(R_I2S_DIAG_MINMAX), cnt = REG(R_I2S_DIAG_CNT), sum = REG(R_I2S_DIAG_SUM);
        q = ui_dec(q, mm & 0xFFFFu); q = set_str(q, "/");
        q = ui_dec(q, mm >> 16); q = set_str(q, " AVG ");
        q = ui_dec(q, cnt ? sum / cnt : 0u); q = set_str(q, " N");
        q = ui_dec(q, cnt);
        break;
    }
    case 31:   /* METER DRAW (docs/issues/022): redraws of the full-repaint meters (Layered Wave, Chladni), how many were composed off-screen and presented, F = drains
                * that never finished (frame dropped), K = Layered Wave redraws skipped for the audio, last and worst time in microseconds (to engine completion in a Diagnostic Build), and Layered Wave's stride */
        q = set_str(q, "D"); q = ui_dec(q, helios_n_draw);
        q = set_str(q, " O"); q = ui_dec(q, helios_n_comp);
        if (helios_n_fail) { q = set_str(q, " F"); q = ui_dec(q, helios_n_fail); }
        if (hm_n_skip) { q = set_str(q, " K"); q = ui_dec(q, hm_n_skip); }          /* redraws skipped to protect the audio (rate limit / low FIFO) */
        q = set_str(q, " "); q = ui_dec(q, helios_t_last / (CLK_HZ / 1000000u));
        q = set_str(q, "/"); q = ui_dec(q, helios_t_max / (CLK_HZ / 1000000u));
        q = set_str(q, "US S"); q = ui_dec(q, lw_stride);
        break;
    case 33: {   /* HEADROOM (Cymo C0a, B-538/B-541/B-544): `<speed> I<latest>/<worst> O<io> WS<x.x>X`. I = idle share (spare CPU) of the latest / the worst second of plain
                  * playback since the track or speed changed; seconds with a menu or this page up are not counted (redrawing is not decode load). O = the most any such
                  * second spent blocked waiting for file bytes, in percent (FLAC only; a stall there, not CPU, can cause a click). WS = the speed the WORST second projects
                  * with the tempo stretcher's ESTIMATED fixed cost taken off first (fw/headroom.h; an approximation, confirm with a run). */
        if (idle || paused || hr.last > 99u) { q = set_str(q, "PLAY 10 S, NO MENUS"); break; }   /* needs a few seconds of plain playback first */
        uint32_t n, d;
        speed_ratio(&n, &d);
        const uint32_t m1 = hr_max_speed_x100(hr.min_idle, n, d, HR_WSOLA_EST_PCT);
        /* <speed> I<latest idle>/<worst idle> O<worst file-wait> WS<x.x>X -- at most 25 characters: a longer value runs into the label (DEV 74 showed that). */
        { const char *sm = speed_marker(); q = set_str(q, sm ? sm : "1.00x"); } q = set_str(q, " I");
        q = ui_dec(q, hr.last); q = set_str(q, "/"); q = ui_dec(q, hr.min_idle);
        q = set_str(q, " O"); q = ui_dec(q, hr.max_io);
        q = set_str(q, " WS");
        if (m1 > 999u) { q = set_str(q, ">9.9"); } else { q = ui_dec(q, m1 / 100u); q = set_str(q, "."); q = ui_dec(q, (m1 % 100u) / 10u); }
        q = set_str(q, "X");
        break;
    }
    case 32:   /* LW COST (B-519): commands in the last draw, CPU time to issue it, and how much of that was reading the PSRAM history (lw_unwrap) */
        q = set_str(q, "C"); q = ui_dec(q, lw_cmd_last);
        q = set_str(q, " CPU "); q = ui_dec(q, lw_t_cpu / (CLK_HZ / 1000000u));
        q = set_str(q, " UNW "); q = ui_dec(q, lw_t_unw / (CLK_HZ / 1000000u));
        q = set_str(q, " PRES "); q = ui_dec(q, helios_t_pres / (CLK_HZ / 1000000u));
        q = set_str(q, "US");
        break;
    default: {   /* CYMO RESAMP (B-481): whether the probe found the unit, and -- once the Diagnostics >
                  * CYMO RESAMPLER toggle is on -- its live status (busy/done/pop_req), so the owner can
                  * confirm the probe passed before relying on the toggle having any real effect. */
#if TAU_DIAGNOSTIC
        if (!CYMO_RESAMP_READY()) { q = set_str(q, "NO UNIT"); break; }
        uint32_t st = REG(R_CYMO_STATUS), dg = REG(R_CYMO_DIAG);
        q = set_str(q, (st & 0x10u) ? "LIVE " : "READY ");
        q = set_str(q, (st & 0x8u) ? "POP " : "- ");
        /* B-527: S = consumes that found no pushed sample (the resampler repeated one), D = ticks dropped because the hand-off queue was full, Q = the queue
         * level now. Both counters clear when the toggle is switched on and keep their value when it is switched off. With the elastic hand-off S and D should
         * stay at 0 (the old tick-gated hand-off read thousands within a second, B-527); Q should sit at 2-3. */
        q = set_str(q, "S"); q = ui_dec(q, dg & 0xFFFFu);
        q = set_str(q, " D"); q = ui_dec(q, dg >> 16);
        q = set_str(q, " Q"); q = ui_dec(q, (st >> 5) & 7u);
#else
        q = set_str(q, "OFF");
#endif
        break;
    }
    }
    *q = 0;
}

/* Read-only value pages share one painter: labels on the left, values right-aligned. */
/* Read-only value pages (Info, Stress status) show up to SET_RO_VIS rows at once and scroll with Up/Down when they
 * have more (B-257: Info grew to 15 rows and the 15th fell off the bottom of the screen). */
/* 12, not 14: PL_UI_LIST_Y..OV_HINT_Y is 272px at PL_UI_ROW_H=22 -- 14 rows (308px) reached past the
 * action bar (owner: confirmed on a real Info-page screenshot, the last 1-2 rows overlapped it). */
#define SET_RO_VIS 12u
static uint8_t set_ro_top;                       /* first visible row; reset to 0 when the page is left */

COLD_FN static void set_draw_ro(int full, const char *title, const char *const *labels, uint32_t n,
                        void (*value)(uint32_t, char *))
{
    const uint32_t vis = (n < SET_RO_VIS) ? n : SET_RO_VIS;
    uint32_t top = set_ro_top;
    if (top + vis > n) top = n - vis;
    /* Up/Down scrolling is self-evident. The Info page (diagnostic build) says what A does. */
    const char *hint = "B BACK";
#if TAU_DIAGNOSTIC
    if (title[0] == 'I' && title[1] == 'N' && title[2] == 'F' && title[3] == 'O' && !title[4]) hint = "A QR CODE   B BACK";
#endif
    if (full) ov_frame(title, "", hint);
    if (n > vis) {                               /* scroll track, as the lists */
        const uint32_t tx = PL_UI_X + PL_UI_W - 11u, lh = vis * PL_UI_ROW_H, th = lh * vis / n;
        fb_rect(tx, PL_UI_LIST_Y - 2u, 3u, lh, ui_mix(OV_BODY, UI_DIM, 1u, 3u));
        fb_rect(tx, PL_UI_LIST_Y - 2u + (lh - th) * top / (n - vis), 3u, th, ui_accent);
    }
    for (uint32_t k = 0; k < vis; k++) {
        const uint32_t i = top + k, y = PL_UI_LIST_Y + k * PL_UI_ROW_H;
        char v[40];
        value(i, v);
        fb_rect(PL_UI_X + 4u, y - 2u, PL_UI_W - 16u, PL_UI_ROW_H, OV_BODY);
        fb_set_color(UI_DIM, OV_BODY);
        fb_text_clipped(PL_UI_TEXT_X, y, labels[i], TS_1X, TS_1X, 170u);
        uint32_t w = fb_text_width(v, TS_1X);
        fb_set_color(UI_WHITE, OV_BODY);
        fb_text_clipped(PL_UI_X + PL_UI_W - 20u - w, y, v, TS_1X, TS_1X, w + 2u);
    }
    /* Unconditional, not `if (full)`: set_info_tick()'s once-a-second partial refresh ("repaint the
     * rows only, never the panel") calls this with full=0, and used to never touch the hint bar at
     * all -- meaning any row redraw after the first could silently erase it with nothing to put it
     * back (owner: "the action bar in info is not always visible"). Cheap even every second: two
     * fb_rect-scale calls, not a real cost. */
    ov_hint_repaint(hint);
}

#if TAU_DIAGNOSTIC
#define SET_STAT_ROWS 11u
static const char *const set_stat_label[SET_STAT_ROWS] = {
    "LEVEL", "STATE", "PASSES", "OPERATIONS", "FAILURES", "EARLY UNDERRUNS", "LATE UNDERRUNS",
    "WORST ACCESS", "DRAW STALL", "RATE", "SOAK" };

COLD_FN static void set_stat_value(uint32_t i, char *o)
{
    char *q = o;
    switch (i) {
    case 0: q = set_str(q, stress_on ? (stress_level == 1u ? "R1" : stress_level == 2u ? "R2" : "R3") : "OFF"); break;
    case 1: q = set_str(q, stress_fault ? "FAILED" : stress_on ? "RUNNING" : "STOPPED"); break;
    case 2: q = ui_dec(q, stress_passes); break;
    case 3: q = ui_dec(q, stress_words); break;
    case 4: q = ui_dec(q, stress_failures); break;
    case 5: q = ui_dec(q, stress_und_early); break;
    case 6: q = ui_dec(q, stress_und_late); break;
    case 7: q = ui_dec(q, stress_rd_max > stress_wr_max ? stress_rd_max : stress_wr_max);
            q = set_str(q, " CYC"); break;
    case 8: q = ui_dec(q, (REG(R_FB_STALL) - stress_fb0) / (CLK_HZ / 1000u)); q = set_str(q, " MS"); break;
    case 9: q = ui_dec(q, stress_rate / 1000u); *q++ = '.'; q = ui_dec(q, (stress_rate % 1000u) / 100u);
            q = set_str(q, "K OPS/S"); break;
    default:
        if (dg_soak_res == 1u) { q = ui_mmss(q, dg_soak_left); q = set_str(q, " LEFT"); }
        else q = set_str(q, dg_soak_res == 2u ? "PASS" : dg_soak_res == 3u ? "FAIL" : "OFF");
    }
    *q = 0;
}
#endif

/* Once a second while a value page is open: repaint the rows only, never the panel. */
COLD_FN static void set_info_tick(void)
{
    if (!set_open || set_page < SET_INFO_PG) return;
    if ((int32_t)(cycles() - set_info_at) < 0) return;
    set_info_at = cycles() + CLK_HZ;
    uint8_t o = ov_draw; ov_draw = 1u;
    if (set_page == SET_INFO_PG) {
#if TAU_DIAGNOSTIC
        if (!info_qr_up)
#endif
            set_draw_ro(0, "INFO", set_info_label, SET_INFO_ROWS, set_info_value);
    }
#if TAU_DIAGNOSTIC
    else if (set_page == SET_STAT_PG) set_draw_ro(0, "STRESS STATUS", set_stat_label, SET_STAT_ROWS, set_stat_value);
#endif
    ov_draw = o;
}

#if TAU_DIAGNOSTIC
#include "suite.inc"
#endif

/* Settings > Appearance > Meter > Configure (B-215..B-218, B-234; generic since M2, docs/METER_MODULE_SPEC.md section 5): a live preview
 * of the meter being edited -- still reading real playing audio -- pinned to the top, with a scrollable list of that meter's parameters
 * below. Nothing here is specific to a meter: rows come from the generated parameter tables (fw/meters_gen.h) through the generic logic
 * in fw/meter_module.h, so a new meter with parameters gets this page for free. Session-only: every change writes straight into the
 * meter's live values; nothing is persisted (B-216).
 *
 * Rows: PRESET, METER (cycles the meters that have parameters), the meter's parameters that are visible under their `when` clause,
 * EXPORT QR. Placed after suite.inc's own #include because the QR export needs sr_tlv()/qr_encode() (Diagnostic Build only). */
#define WVR_PRESET 0u
#define WVR_METER  1u
#define WVR_P0     2u              /* parameter i is row WVR_P0 + i */
#define WVR_EXPORT 250u
#define WVCFG_ROWS_VISIBLE 8u
#define WVCFG_MAX_ROWS 48u
static uint8_t wvcfg_sel, wvcfg_top;
static uint8_t wvcfg_vis[WVCFG_MAX_ROWS], wvcfg_vis_n;
static const mtr_data_t *wvcfg_md;

static void wvcfg_build_vis(void)
{
    uint32_t n = 0;
    wvcfg_md = mtr_of(viz_mode);
    wvcfg_vis[n++] = (uint8_t)WVR_PRESET;
    wvcfg_vis[n++] = (uint8_t)WVR_METER;
    for (uint32_t i = 0; wvcfg_md && i < wvcfg_md->n && n + 1u < WVCFG_MAX_ROWS; i++)
        if (mtr_visible(wvcfg_md, i)) wvcfg_vis[n++] = (uint8_t)(WVR_P0 + i);
    wvcfg_vis[n++] = (uint8_t)WVR_EXPORT;
    wvcfg_vis_n = (uint8_t)n;
    if (wvcfg_sel >= wvcfg_vis_n) wvcfg_sel = (uint8_t)(wvcfg_vis_n - 1u);
}

static inline uint32_t wvcfg_row(uint32_t k) { return wvcfg_vis[k]; }

static uint8_t wvcfg_direct;                 /* B-524: the page was opened by the Start+Y shortcut: B closes Settings instead of going back to Appearance */
static void wvcfg_open(void)
{
    wvcfg_direct = 0u;
    wvcfg_sel = 0u; wvcfg_top = 0u;
    if (!mtr_of(viz_mode)) viz_mode = mtr_modules[0]->viz;      /* editing a meter without parameters: start with the first that has some */
    wvcfg_build_vis();
    wviz_force = 1u;   /* B-234: entering at a different rect than the player screen's own */
}

/* Start+Y shortcut (fw/player.c, moved from Start+X in B-522 when Start began opening Settings on release): jump straight to Meter > Configure from the player screen, skipping
 * the Home -> Appearance -> Meter navigation chain. Mirrors set_input()'s own Start-opens-at-SET_HOME
 * path (closing any open library browser first, running the one-time hardware-blend probe before
 * FB_HELD() becomes true) but lands directly on the Configure page and calls its own open() the same way
 * normal RT_GROUP menu navigation does, instead of SET_HOME. */
COLD_FN static void set_open_wvizcfg_direct(void)
{
    if (lib_ui_open) lib_ui_close();
    blend_ensure();
    set_open = 1u; set_page = SET_WVIZCFG_PG; set_dirty = 1u;
    wvcfg_open();
    wvcfg_direct = 1u;                  /* after wvcfg_open(), which clears it for the ordinary route */
}

COLD_FN static void wvcfg_follow(void)
{
    if (wvcfg_sel < wvcfg_top) wvcfg_top = wvcfg_sel;
    else if (wvcfg_sel >= wvcfg_top + WVCFG_ROWS_VISIBLE) wvcfg_top = (uint8_t)(wvcfg_sel - WVCFG_ROWS_VISIBLE + 1u);
}

/* B-218/M2: exports the live values of the meter being edited as a QR code (SR_T_METERCFG), reusing the record/text/QR pipeline
 * fw/suite.inc built for Check/Blit Test (this page and those are never open together, so sharing their SDRAM staging is safe).
 * Diagnostic-Build only, like every QR feature here; outside one the row is present but inert. */
#if TAU_DIAGNOSTIC
#include "wvcfg_export.inc"
#include "info_export.inc"
#endif

COLD_FN static void wvcfg_row_name(uint32_t row, char *o)
{
    const char *t = row == WVR_PRESET ? "PRESET" : row == WVR_METER ? "METER" : row == WVR_EXPORT ? "EXPORT QR"
                  : (wvcfg_md && row >= WVR_P0 && row - WVR_P0 < wvcfg_md->n) ? wvcfg_md->p[row - WVR_P0].label : "";
    char *q = set_str(o, t);
    *q = 0;
}

COLD_FN static void wvcfg_value_text(uint32_t row, char *o)
{
    char *q = o;
    if (row == WVR_PRESET) {
        uint8_t pi = wvcfg_md ? *wvcfg_md->pre_idx : MTR_CUSTOM;
        q = set_str(q, (wvcfg_md && pi < wvcfg_md->npre) ? wvcfg_md->pre_names[pi] : "CUSTOM");
    } else if (row == WVR_METER) {
        q = set_str(q, wvcfg_md ? wvcfg_md->name : "");
    } else if (row == WVR_EXPORT) {
#if TAU_DIAGNOSTIC
        q = set_str(q, "PRESS A");
#else
        q = set_str(q, "DIAG BUILD ONLY");
#endif
    } else if (wvcfg_md && row >= WVR_P0 && row - WVR_P0 < wvcfg_md->n) {
        const mtr_param_t *p = &wvcfg_md->p[row - WVR_P0];
        const uint32_t v = wvcfg_md->val[row - WVR_P0];
        if (p->type == MTR_ENUM) q = set_str(q, p->enums[v]);
        else if (p->type == MTR_BOOL) q = set_str(q, v ? "ON" : "OFF");
        else {
            const int32_t sv = (int32_t)v + (int32_t)p->off;
            if (sv < 0) { *q++ = '-'; q = ui_dec(q, (uint32_t)(-sv)); } else if (p->off) { if (sv) *q++ = '+'; q = ui_dec(q, (uint32_t)sv); } else q = ui_dec(q, v);
            q = set_str(q, p->unit);
        }
    }
    *q = 0;
}

COLD_FN static void wvcfg_adjust(uint32_t row, int32_t dir)
{
    if (!wvcfg_md || row == WVR_EXPORT) return;                   /* EXPORT is an A-only action */
    if (row == WVR_PRESET) {
        mtr_preset_step(wvcfg_md, dir);
        wviz_force = 1u;    /* B-234: geometry/values just jumped -- see wviz_force's own comment */
        wvcfg_build_vis();  /* a preset can change which rows are visible (a `when` clause) */
        return;
    }
    if (row == WVR_METER) {                                       /* another meter with parameters; not a value, so no CUSTOM */
        uint32_t k = 0;
        while (k < MTR_MODULE_N && mtr_modules[k] != wvcfg_md) k++;
        k = (dir > 0) ? (k + 1u) % MTR_MODULE_N : (k + MTR_MODULE_N - 1u) % MTR_MODULE_N;
        viz_mode = mtr_modules[k]->viz;
        wvcfg_build_vis();
        wviz_force = 1u;
        return;
    }
    if (row >= WVR_P0 && row - WVR_P0 < wvcfg_md->n) {
        mtr_adjust(wvcfg_md, row - WVR_P0, dir);
        wvcfg_build_vis();  /* e.g. turning the peak cap off hides its dependent rows */
    }
}

COLD_FN static void wvcfg_draw(void)
{
#if TAU_DIAGNOSTIC
    if (wvcfg_qr_up) { wvcfg_draw_qr(); return; }
#endif
    ov_frame("METER CONFIGURE", "", (wvcfg_vis_n > WVCFG_ROWS_VISIBLE) ? "UP DOWN SCROLL   L/R ADJUST   B BACK" : "L/R ADJUST   B BACK");

    /* Live preview, pinned to the top of the page -- still reads real playing
     * audio (spec_lvl[]/wav_v[]), same wviz_bars_tick()/wviz_scope_tick() the
     * normal meter box uses (fw/player.c), just at this page's own position
     * and against the panel's flat background instead of the gradient
     * (B-234: wviz_scope_tick()'s use_gradient=0 path fixes the previously-
     * garbled preview -- see that function's own comment). */
    uint32_t px = PL_UI_TEXT_X, pw = PL_UI_X + PL_UI_W - 8u - px, py = PL_UI_LIST_Y, ph = UI_WAVE_H;
    fb_rect(px - 4u, py - 4u, pw + 8u, ph + 8u, OV_BODY);
    wviz_force = 1u;                      /* the panel was just wiped: the meters' change caches must repaint everything */
    mtr_preview(viz_mode, px, py, pw, ph, UI_PANEL);

    uint32_t list_y = py + ph + 16u;
    if (wvcfg_vis_n > WVCFG_ROWS_VISIBLE) {                      /* scroll track, as the other lists */
        const uint32_t n = wvcfg_vis_n, vis = WVCFG_ROWS_VISIBLE, tx = PL_UI_X + PL_UI_W - 11u, lh = vis * PL_UI_ROW_H, th = lh * vis / n;
        fb_rect(tx, list_y - 2u, 3u, lh, ui_mix(OV_BODY, UI_DIM, 1u, 3u));
        fb_rect(tx, list_y - 2u + (lh - th) * wvcfg_top / (n - vis), 3u, th, ui_accent);
    }
    for (uint32_t k = 0; k < WVCFG_ROWS_VISIBLE && wvcfg_top + k < wvcfg_vis_n; k++) {
        uint32_t vi = wvcfg_top + k, i = wvcfg_row(vi), y = list_y + k * PL_UI_ROW_H;
        int sel = (vi == wvcfg_sel);
        uint16_t bg = sel ? ui_accent : OV_BODY;
        if (sel) fb_round_rect_on(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, PL_UI_ROW_H, 5u, bg, OV_BODY);
        else     fb_rect(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, PL_UI_ROW_H, OV_BODY);
        char v[24];
        char nm[24];
        wvcfg_value_text(i, v);
        wvcfg_row_name(i, nm);
        fb_set_color(sel ? UI_PANEL : UI_DIM, bg);
        fb_text_clipped(PL_UI_TEXT_X, y, nm, TS_1X, TS_1X, 190u);
        uint32_t w = fb_text_width(v, TS_1X);
        fb_set_color(sel ? UI_PANEL : UI_WHITE, bg);
        fb_text_clipped(PL_UI_X + PL_UI_W - 16u - w, y, v, TS_1X, TS_1X, w + 2u);
    }
}

/* Once per display frame while the page is open: ONLY the live preview. The rest of the page is drawn on demand (set_dirty);
 * redrawing it every frame wiped the preview panel under the meters' change caches, which is why bars vanished and the page
 * glitched. */
COLD_FN static void wvcfg_preview_tick(void)
{
#if TAU_DIAGNOSTIC
    if (wvcfg_qr_up) return;
#endif
    /* B-452: meters_publish() (fw/player.c) is normally only reached from ui_draw_dynamic_cold() while
     * !UI_OVERLAY_UP -- correct for the ordinary player screen, but Settings being open here is itself
     * a UI_OVERLAY_UP case, so without this call peak_l/peak_r/spec_lvl[] (everything but Winamp
     * Scope's own self-contained R_WAVE_CTL capture) were frozen at whatever they held the moment
     * Settings opened, reading as "only Scope actually plays" in the preview. meters_feed() (the decode
     * path) keeps accumulating peak_acc* regardless of any overlay -- this is only the missing PUBLISH
     * step, deliberately bypassing the overlay gate for exactly the meter being previewed. */
    meters_publish();
    const uint8_t o = ov_draw; ov_draw = 1u;
    const uint32_t px = PL_UI_TEXT_X, pw = PL_UI_X + PL_UI_W - 8u - px, py = PL_UI_LIST_Y, ph = UI_WAVE_H;
    mtr_preview(viz_mode, px, py, pw, ph, UI_PANEL);
    ov_draw = o;
}

static int wvcfg_input(uint32_t edge)
{
#if TAU_DIAGNOSTIC
    if (wvcfg_qr_up) {                             /* QR screen: anything closes it */
        if (edge & (KEY_A | KEY_B | KEY_START)) { wvcfg_qr_up = 0u; set_dirty = 1u; }
        return 1;
    }
#endif
    /* Start closes Settings from any depth (like every other page); B goes back one level, or closes when the page was opened by the Start+Y shortcut
     * (there is no Appearance page behind it to return to in the user's head). B-524. */
    if (edge & KEY_START) { wvcfg_direct = 0u; wviz_force = 1u; set_close(); return 1; }
    if (edge & KEY_B) {
        if (wvcfg_direct) { wvcfg_direct = 0u; wviz_force = 1u; set_close(); }
        else { set_page = SET_APPEAR; wviz_force = 1u; set_dirty = 1u; }
        return 1;
    }
    if (edge & KEY_UP)    { wvcfg_sel = wvcfg_sel ? (uint8_t)(wvcfg_sel - 1u) : (uint8_t)(wvcfg_vis_n - 1u); wvcfg_follow(); set_dirty = 1u; }
    if (edge & KEY_DOWN)  { wvcfg_sel = (uint8_t)((wvcfg_sel + 1u) % wvcfg_vis_n); wvcfg_follow(); set_dirty = 1u; }
    if (edge & KEY_LEFT)  { wvcfg_adjust(wvcfg_row(wvcfg_sel), -1); set_dirty = 1u; }
    if (edge & KEY_RIGHT) { wvcfg_adjust(wvcfg_row(wvcfg_sel),  1); set_dirty = 1u; }
#if TAU_DIAGNOSTIC
    if ((edge & KEY_A) && wvcfg_row(wvcfg_sel) == WVR_EXPORT) { wvcfg_build_qr(); wvcfg_qr_up = 1u; set_dirty = 1u; }
#endif
    return 1;
}

/* Helios review item 2 (docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md section 3.4/5): one
 * function-pointer table replacing the three hand-maintained if/else-if chains (draw dispatch, input
 * dispatch, open-on-select) that used to grow by one line each time a "rich" page (needing its own
 * full draw pass and multi-key input, not expressible by the plain declarative set_menu_rows table)
 * was added -- Check, Decode Sweep, Blit Test, Meter Sweep, Meter Trace and the Winamp Configurator.
 * Adding one now means adding one row here, not touching set_draw_now()/set_input() at all. `.open` is
 * NULL for the two pages (Meter Sweep, Meter Trace) that never had one -- they reset their own state
 * from inside draw()/input() instead, unchanged from before this table existed. The `#if` guards below
 * exactly mirror the ones on each page's own definition (SET_CHECK_PG etc., lines ~26-40) and on
 * suite.inc's own #include (TAU_DIAGNOSTIC) -- a page that would not compile in this build simply has
 * no row, same as the old chains simply had no branch for it. */
typedef struct { uint8_t page; void (*draw)(void); int (*input)(uint32_t); void (*open)(void); } set_spg_t;
static const set_spg_t set_spg[] = {
    { SET_WVIZCFG_PG, wvcfg_draw, wvcfg_input, wvcfg_open },
#if TAU_DIAGNOSTIC
    { SET_CHECK_PG, chk_draw, chk_input, chk_open },
#if MP3_PROFILE || FLAC_PROFILE
    { SET_SWEEP_PG, sw_draw, sw_input, sw_open },
#endif
    { SET_BLITTEST_PG, bt_draw, bt_input, bt_open },
    { SET_METERSWEEP_PG, mw_draw, mw_input, 0 },
    { SET_METERTRACE_PG, mt_draw, mt_input, 0 },
#endif
};
#define SET_SPG_N SET_COUNT(set_spg)
static const set_spg_t *set_spg_find(uint8_t page)
{
    for (uint32_t i = 0; i < SET_SPG_N; i++) if (set_spg[i].page == page) return &set_spg[i];
    return 0;
}

/* ---- B-334: menu page fade-in with easing --------------------------------------------------------------------------------------------------------
 * Opening Settings and moving between its pages fades the new page in from the page body colour over about 100 ms. There is no blend in
 * this: the page is simply redrawn a few times with every role colour (and the accent) mixed toward the body colour by an eased factor, then
 * once more at full strength. Text is drawn with a foreground and a background from the same roles, so its anti-aliasing stays consistent.
 * The role table is faded in place for the duration of one draw and restored straight after, so nothing else ever sees a faded colour.
 * Pages that draw live content, a QR code or thumbnails that are cached from the roles (the meter list) are not faded. */
#ifndef TAU_MENU_FADE
#define TAU_MENU_FADE 1
#endif
#define SET_FADE_STEPS 7u
static const uint16_t set_fade_k[SET_FADE_STEPS] = { 0u, 108u, 180u, 224u, 246u, 255u, 256u };   /* ease-out cubic, 256*(1-(1-t)^3), t = 0..6/6 */
static uint16_t set_fade_last = 0xFFFFu;         /* page drawn last; a different page starts a fade */
static uint8_t  set_fade_step = SET_FADE_STEPS;  /* == SET_FADE_STEPS: not fading */
static uint32_t set_fade_at;

/* ---- real hardware crossfade, upgrading the colour-interpolation fade above where it can run -----
 * B-334's own comment says "there is no blend in this" -- true then (TAU_BLIT_BLEND was shelved for
 * a timing failure, and H2 double buffering didn't exist yet), not true now: T2-00 (B-388/398) closed
 * blend's own old timing problem as a side effect of fixing glyphbuf, and H2 (B-396) gives a second,
 * already-idle buffer (Settings holds FB_HELD(), so dbuf_redraw_begin() itself refuses it, B-402) to
 * use as scratch render space. Render the NEW page ONCE, normally, into that buffer; then blend it
 * onto the buffer actually being shown, at increasing alpha over the SAME eased curve/cadence
 * set_fade_k[]/SET_FADE_STEPS already defined -- no recolouring, so unlike the fallback below this
 * does not need set_fade_page_ok()'s exclusions (the meter list's cached thumbnails, a live QR) at
 * all; kept scoped to the SAME eligible pages for this pass regardless, to change one thing at a
 * time. Falls back to the colour-interpolation fade whenever SET_XFADE_READY() is false (any
 * bitstream without both TAU_BLIT_BLEND and TAU_DBUF) -- old behaviour, unchanged.
 *
 * B-408: BLEND_READY() only reports the answer to the one-time blend_probe() (fw/blit_probe.inc);
 * nothing ran that probe on its own, and the ONLY caller anywhere in this codebase used to be
 * ui_bg_blend() (Winamp Scope's own trail effect) -- so this crossfade was silently DISABLED (plain
 * colour fade) for the whole boot until the user happened to view the Scope trail at least once, an
 * incidental, confusing trigger for a feature with no relation to Scope. Tried calling blend_ensure()
 * from HERE first -- wrong: blend_probe() itself refuses while FB_HELD() (fw/blit_probe.inc), and
 * SET_XFADE_READY() is only ever evaluated from inside set_draw(), which only runs while Settings'
 * OWN overlay already holds FB_HELD() true -- the probe would refuse every single time from this
 * call site. Fixed at the actual point FB_HELD() is still false: set_input()'s own Start-opens-
 * Settings handler, right before `set_open` flips to 1 (see there). */
#define SET_XFADE_READY() (BLEND_READY() && DBUF_READY())
/* The crossfade below claims the H2 back buffer through helios_back_busy (fw/helios.inc, set/cleared in place of the old set_xfade_active), so no meter composes into it meanwhile. */
static uint32_t set_xfade_src_base, set_xfade_dst_base;

COLD_FN static void set_draw_now(void);

/* Renders the new page into whichever buffer is NOT currently displayed, then restores R_DBUF_CPU to
 * the displayed buffer immediately -- every OTHER draw call in this codebase assumes CPU writes land
 * in the displayed buffer, so this window must be as short as this one render pass, nothing else.
 *
 * B-410 (owner-reported on hardware, real corruption on every "rich" diagnostic subpage -- Check,
 * Blit Test, etc.): this used to restore R_DBUF_CPU to `front` IMMEDIATELY after set_draw_now()
 * RETURNS, but set_draw_now() only QUEUES its draw commands -- it does not wait for the hardware to
 * actually execute them. R_DBUF_CPU is a sticky field, consulted at command EXECUTION time (same
 * class as every other sticky field this codebase has repeatedly hit this exact race with: blend-on,
 * the CLUT reload, B-349's corner-cut LUT race). Any command still queued when R_DBUF_CPU flipped
 * back landed in `front` -- the LIVE DISPLAYED buffer -- instead of `back`, corrupting the real
 * screen with fragments of the new page while leaving `back` incomplete. Rich pages queue far more
 * commands than a plain menu list, so the race window was wide enough to hit reliably there while
 * staying invisible on the simpler pages this feature was first tested against. Fixed the same way
 * set_xfade_step_draw() already handles its own fb_fence() call: ov_draw must be set here too, or
 * fb_fence() (like every draw primitive) silently no-ops under FB_HELD() and waits for nothing. */
COLD_FN static void set_xfade_render(void)
{
    const uint32_t front = REG(R_DBUF_DISP) & 1u;
    const uint32_t back  = front ^ 1u;
    set_xfade_dst_base = front ? DBUF_BASE1_W : 0u;
    set_xfade_src_base = back  ? DBUF_BASE1_W : 0u;
    const uint8_t o = ov_draw; ov_draw = 1u;
    REG(R_DBUF_CPU) = back;
    set_draw_now();
    fb_fence();
    REG(R_DBUF_CPU) = front;
    ov_draw = o;
    fb_set_bases(set_xfade_src_base, set_xfade_dst_base);
}

/* One animation step: composite the rendered page onto the displayed buffer at this step's alpha.
 * fb_blit()'s underlying glyphbuf is 128 words wide (the same FB_COPY_MAX limit fb_copy() itself
 * splits around), so the full FB_W=400 columns need several calls -- each still covers all FB_H=360
 * rows in one command, only the column count is chunked. k==256 (the last step) is drawn as a plain
 * copy, not a 255/256 blend, so the final frame is pixel-exact against what was actually rendered,
 * not a near-miss with an imperceptible but real trace of the old buffer left in it; this is also
 * the one point that restores the sticky bases to (0, 0) so no later, unrelated caller inherits
 * them. ov_draw must be set for the SAME reason set_draw_now() itself sets it: FB_HELD() would
 * otherwise silently no-op every fb_blit() call here, since Settings holds UI_OVERLAY_UP the entire
 * time and this runs outside set_draw_now()'s own ov_draw bracket. */
COLD_FN static void set_xfade_step_draw(uint32_t k)
{
    const uint8_t o = ov_draw; ov_draw = 1u;
    const int last = (k >= 256u);
    if (!last) fb_blend_on(k);
    uint32_t x = 0u, w = FB_W;
    while (w) {
        uint32_t n = (w < 100u) ? w : 100u;
        fb_blit(x, 0u, x, 0u, n, FB_H);
        x += n; w -= n;
    }
    fb_fence();
    if (!last) fb_blend_off();
    else       fb_set_bases(0u, 0u);
    ov_draw = o;
}

COLD_FN static int set_fade_page_ok(void)
{
    if (set_page < SET_MENUS) return 1;
    if (set_page == SET_INFO_PG)
#if TAU_DIAGNOSTIC
        return info_qr_up ? 0 : 1;
#else
        return 1;
#endif
    if (set_page >= SET_MENUS && set_page < SET_MENUS + CH_N) return (set_page - SET_MENUS) != CH_METER;   /* the meter list caches thumbnails from the roles */
    return 0;
}

COLD_FN static void set_draw(void)
{
#if TAU_MENU_FADE
    const int fade_ok = set_fade_page_ok();
    if (set_page != set_fade_last) {
        set_fade_last = set_page;
        /* An xfade abandoned mid-flight (a key pressed before the last step, changing page again)
         * must not leave the sticky bases nonzero for whatever unrelated blit-mode caller runs
         * next -- the normal "reset on the last step" path (set_xfade_step_draw()) never got there.
         * B-412 (found auditing every sticky-field redirect after B-410/B-411): a PRIOR tick's
         * set_xfade_step_draw() may have just queued its fb_blit() composite using the OLD non-zero
         * bases -- resetting them here without fb_fence() first (and ov_draw, since this runs before
         * set_draw_now()/set_xfade_render() ever set it, matching those functions' own precedent) risks
         * that still-queued blit executing AFTER the reset and reading (0,0) instead of the bases it
         * was actually issued under, same race class as B-410. */
        if (helios_back_busy) {
            const uint8_t o = ov_draw; ov_draw = 1u;
            fb_fence();
            fb_set_bases(0u, 0u);
            ov_draw = o;
        }
        helios_back_busy = 0u;
        if (fade_ok && SET_XFADE_READY()) {
            set_xfade_render();
            helios_back_busy = 1u;
            set_fade_step = 0u;
            set_fade_at = cycles();
            set_dirty = 1u;
            return;
        }
        set_fade_step = fade_ok ? 0u : SET_FADE_STEPS;
        set_fade_at = cycles();
    }
    if (helios_back_busy) {
        if ((int32_t)(cycles() - set_fade_at) < 0) { set_dirty = 1u; return; }
        set_xfade_step_draw(set_fade_k[set_fade_step]);
        set_fade_step++;
        set_fade_at = cycles() + CLK_HZ / 60u;
        if (set_fade_step < SET_FADE_STEPS) { set_dirty = 1u; return; }
        helios_back_busy = 0u;
        return;         /* the displayed buffer already shows the exact final frame -- nothing left to draw */
    }
    if (!fade_ok) set_fade_step = SET_FADE_STEPS;
    if (set_fade_step < SET_FADE_STEPS) {
        if ((int32_t)(cycles() - set_fade_at) < 0) { set_dirty = 1u; return; }      /* not yet time for the next step: stay dirty, try again */
        const uint32_t k = set_fade_k[set_fade_step];
        uint16_t save[TR_COUNT];
        const uint16_t save_acc = ui_accent, body = OV_BODY;
        for (uint32_t i = 0; i < TR_COUNT; i++) { save[i] = th_role[i]; th_role[i] = ui_mix(body, save[i], k, 256u); }
        ui_accent = ui_mix(body, save_acc, k, 256u);
        set_draw_now();
        for (uint32_t i = 0; i < TR_COUNT; i++) th_role[i] = save[i];
        ui_accent = save_acc;
        set_fade_step++;
        set_fade_at = cycles() + CLK_HZ / 60u;
        if (set_fade_step < SET_FADE_STEPS) set_dirty = 1u;
        return;
    }
#endif
    set_draw_now();
}

COLD_FN static void set_draw_now(void)
{
    uint8_t o = ov_draw; ov_draw = 1u;
    const set_spg_t *spg = set_spg_find(set_page);
    if (set_page < SET_MENUS)                set_draw_menu(set_page);
    else if (spg)                            spg->draw();
    else if (set_page == SET_INFO_PG) {
#if TAU_DIAGNOSTIC
        if (info_qr_up) { info_draw_qr(); ov_draw = o; return; }
#endif
        set_draw_ro(1, "INFO", set_info_label, SET_INFO_ROWS, set_info_value);
    }
#if TAU_DIAGNOSTIC
    else if (set_page == SET_STAT_PG)
        set_draw_ro(1, "STRESS STATUS", set_stat_label, SET_STAT_ROWS, set_stat_value);
#endif
    else                                     set_draw_choice(set_page - SET_MENUS);
    ov_draw = o;
}

/* Helios view registry (item 4): no view-specific enter/exit of its own today -- HELIOS_INV_ALL
 * already covers everything closing Settings needs, same as before this existed. */
static const helios_view_t settings_view = { 0, 0, HELIOS_INV_ALL };

COLD_FN static void set_close(void)
{
    set_open = 0u; set_page = SET_HOME;
    set_fade_last = 0xFFFFu;                   /* B-334: the next open fades in again */
    if (helios_back_busy) {   /* B-405/B-412: same reasoning as set_draw()'s own abandon case -- fence before resetting bases */
        const uint8_t o = ov_draw; ov_draw = 1u;
        fb_fence();
        fb_set_bases(0u, 0u);
        ov_draw = o;
        helios_back_busy = 0u;
    }
    /* Missing since the legacy playlist overlay was removed (45630c3): that overlay's own close site
     * was apparently the only one still setting the shared repaint flag (fw/player.c's own comment on
     * pl_ui_restore claims settings does this too -- it never did after that removal, only
     * lib_ui_close() did). Without it the main loop never calls ui_chrome_paint()/ui_art_draw(), never
     * clears ui_bg_ready, and never forces the meter/wave caches stale on return from Settings --
     * exactly the "closing the menu leaves visual leftovers" symptom (B-349). Now routed through the
     * shared helios_view_switch() (B-350/item 4) rather than a bare flag write, so this can't be
     * silently dropped again the way the bare write was. */
    helios_view_switch(&settings_view, &helios_player_view);
}

COLD_FN static void set_ch_follow(uint32_t c)
{
    uint32_t vis = set_ch_visible(c), cur = set_ch_cur[c];
    if (cur < set_ch_top[c]) set_ch_top[c] = (uint8_t)cur;
    else if (cur >= set_ch_top[c] + vis) set_ch_top[c] = (uint8_t)(cur - vis + 1u);
}

/* B-410: the plain-menu equivalent of set_ch_follow() above, same logic. */
COLD_FN static void set_menu_follow(uint32_t p)
{
    uint32_t cur = set_sel[p];
    if (cur < set_top[p]) set_top[p] = (uint8_t)cur;
    else if (cur >= set_top[p] + SET_MENU_VIS) set_top[p] = (uint8_t)(cur - SET_MENU_VIS + 1u);
}

/* Returns 1 when the input was consumed. Start opens (tap) and closes. */
COLD_SR static int set_input(uint32_t edge, uint32_t keys)     /* B-333: cold code in the 192 KB link; poll_input() only calls it when settings are open or Start went down */
{
    if (!set_open) {
        if ((edge & KEY_START) && !(keys & KEY_SELECT)) {
#if TAU_G4
            if (!COLD_READY()) { ui_toast_msg("MENU OFF: NO COLD IMAGE"); return 0; }   /* the menus are cold code; say why nothing opened */
#endif
            /* One overlay at a time: an open library browser would keep repainting its
             * marquee row over the settings (its rows draw through the ov_draw gate), so
             * it is closed here. Closing settings returns to the player, not to the browser.
             * B-406: this used to be a bare `lib_ui_open = 0u;`, bypassing lib_ui_close()'s own
             * helios_view_switch() call entirely -- no invalidation was queued, so nothing repainted
             * the player screen Library had been covering before Settings' own hardware crossfade
             * (set_xfade_render()) captured "whatever is currently displayed" as its source frame.
             * Combined with the helios_pending_mask reorder above this file's own #include point
             * (fw/player.c, same B-406), the repaint now lands in the SAME tick, before set_draw()
             * runs below. */
            if (lib_ui_open) lib_ui_close();
            /* B-408: run the one-time hardware-blend probe HERE, the last point before FB_HELD()
             * becomes true (set_open below flips it) -- blend_probe() (fw/blit_probe.inc) refuses
             * outright while FB_HELD(), so this is the ONLY place in Settings' own open path where it
             * can ever succeed. Without this, SET_XFADE_READY() depends entirely on whether Winamp
             * Scope's ui_bg_blend() happened to run first (the previously ONLY caller of
             * blend_ensure() anywhere in this codebase) -- an incidental trigger with no relation to
             * Settings, and the reason the crossfade (and whatever it does right or wrong) looked
             * like it was "caused by" viewing Scope. A no-op once already probed (blend_ensure()'s
             * own guard). */
            blend_ensure();
            set_open = 1u; set_page = SET_HOME; set_dirty = 1u;
            return 1;
        }
        return 0;
    }
    /* Checked before the generic Start-closes-the-menu handling below: every one of these pages owns
     * its own Start key (Check's own result pages must not be closed by it, same reason the old chain
     * ran this first) -- the table lookup preserves that by construction, still ahead of KEY_START. */
    { const set_spg_t *spg = set_spg_find(set_page); if (spg) return spg->input(edge); }
    if (edge & KEY_START) { set_close(); return 1; }
    /* B-073/B-446: Up/Down auto-repeat while held, the same shape as the playlist and library lists (a
     * step on the edge, a hold delay, then steady) -- a menu used to need one tap per row, which read as
     * inconsistent next to the lists. Computed here, before EVERY page's own Up/Down handling (including
     * the read-only Info/Stress-Status scroll below, which used to `return` before this ever ran --
     * owner-reported: "let me know if you want continuous scroll while holding" -- so it never got the
     * repeat treatment B-073 gave every other list). */
    static kr_t set_kr;
    uint32_t rep;
    edge = kr_step(&set_kr, edge, keys, KEY_UP | KEY_DOWN, cycles(), CLK_HZ / 1000u * PL_HOLD_MS, CLK_HZ / 16u, &rep);   /* B-534: the shared primitive */

    if (set_page >= SET_INFO_PG) {                /* read-only value page: Up/Down scroll, B (back) */
#if TAU_DIAGNOSTIC
        if (set_page == SET_INFO_PG && info_qr_up) {     /* QR screen: anything closes it */
            if (edge & (KEY_A | KEY_B | KEY_START)) { info_qr_up = 0u; set_dirty = 1u; }
            return 1;
        }
#endif
        uint32_t n = SET_INFO_ROWS;
#if TAU_DIAGNOSTIC
        if (set_page == SET_STAT_PG) n = SET_STAT_ROWS;
#endif
        const uint32_t maxtop = (n > SET_RO_VIS) ? n - SET_RO_VIS : 0u;
#if TAU_DIAGNOSTIC
        if (set_page == SET_INFO_PG && (edge & KEY_A) && !rep) { info_build_qr(); info_qr_up = 1u; set_dirty = 1u; return 1; }
#endif
        /* Wraps like every other list here (helios_wrap_index(), fw/helios.inc): past the last visible
         * window, Down jumps back to the top; Up from the top jumps to the last window. `maxtop + 1` is
         * the count of valid `set_ro_top` positions (0..maxtop inclusive). */
        if (edge & KEY_UP)   { set_ro_top = helios_wrap_index(set_ro_top, maxtop + 1u, 1); set_dirty = 1u; }
        if (edge & KEY_DOWN) { set_ro_top = helios_wrap_index(set_ro_top, maxtop + 1u, 0); set_dirty = 1u; }
        if ((edge & (KEY_B | KEY_LEFT)) && !rep) { set_ro_top = 0u; set_page = (set_page == SET_STAT_PG) ? SET_STRESS : SET_DIAG; set_dirty = 1u; }   /* Info -> SETTINGS */
        return 1;
    }

    if (set_page >= SET_MENUS) {                  /* a choice list */
        uint32_t c = set_page - SET_MENUS, n = set_ch_count(c);
        if (edge & KEY_UP)   { set_ch_cur[c] = (uint8_t)helios_wrap_index(set_ch_cur[c], n, 1); set_ch_follow(c); set_dirty = 1u; }
        if (edge & KEY_DOWN) { set_ch_cur[c] = (uint8_t)helios_wrap_index(set_ch_cur[c], n, 0); set_ch_follow(c); set_dirty = 1u; }
        if ((edge & (KEY_A | KEY_RIGHT)) && !rep) { set_ch_apply(c, set_ch_cur[c]); set_dirty = 1u; }     /* Right = forward = select */
        if ((edge & (KEY_B | KEY_LEFT)) && !rep)  { set_page = set_ch_parent[c]; set_dirty = 1u; }         /* Left = back */
        return 1;
    }
    uint8_t *sel = &set_sel[set_page];
    if (edge & KEY_UP)   { *sel = (uint8_t)helios_wrap_index(*sel, set_menu_n[set_page], 1); set_menu_follow(set_page); set_dirty = 1u; }
    if (edge & KEY_DOWN) { *sel = (uint8_t)helios_wrap_index(*sel, set_menu_n[set_page], 0); set_menu_follow(set_page); set_dirty = 1u; }
    if (rep) return 1;      /* a repeat only ever moves the cursor: A/B/Left/Right below react to a real press */
    const set_row_t *r = &set_menu_rows[set_page][*sel];
    if (edge & (KEY_A | KEY_RIGHT)) {
        if (r->type == RT_GROUP) {
            set_page = r->arg; set_dirty = 1u;
            { const set_spg_t *spg = set_spg_find(set_page); if (spg && spg->open) spg->open(); }
        }
        else if (r->type == RT_CHOICE) {
            set_page = SET_PAGE_CH(r->arg);
            set_ch_cur[r->arg] = (uint8_t)set_ch_active(r->arg);
            set_ch_follow(r->arg);
            set_dirty = 1u;
        } else if (r->type == RT_TOGGLE) { set_toggle(r->arg); set_dirty = 1u; }
#if TAU_DIAGNOSTIC
        else if (r->type == RT_ACTION && (edge & KEY_A)) { dg_run(r->arg); set_dirty = 1u; }
#endif
        else if (r->type == RT_VOL && (edge & KEY_RIGHT)) { set_volume(1); set_dirty = 1u; }
    }
    if (edge & KEY_LEFT) {
        if (r->type == RT_TOGGLE) { set_toggle(r->arg); set_dirty = 1u; }
        else if (r->type == RT_VOL) { set_volume(-1); set_dirty = 1u; }
    }
    /* Left is Back on rows that open something; on a toggle or the volume it still changes the value. */
    if ((edge & KEY_B) || ((edge & KEY_LEFT) && r->type != RT_TOGGLE && r->type != RT_VOL)) {
        if (set_page == SET_HOME) set_close();
        else { set_page = set_menu_parent[set_page]; set_dirty = 1u; }
    }
    return 1;
}
