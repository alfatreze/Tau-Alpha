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

/* The home is titled MENU. SET_DIAG is the SETTINGS group (Info, How it works, and in the Diagnostic Build a
 * DIAGNOSTICS group = SET_DGN with Tests, Stress and All speeds). */
enum { SET_HOME = 0, SET_APPEAR, SET_AUDIO, SET_PLAY, SET_DIAG, SET_TESTS, SET_STRESS, SET_DGN, SET_MENUS };
enum { CH_COLOUR = 0, CH_METER, CH_EQ, CH_REPEAT, CH_BLANK, CH_SPEED, CH_STRESS, CH_SOAK, CH_N };
/* Pages after the menus: one per choice list, then the read-only Info page (diagnostics). */
#define SET_INFO_PG ((uint8_t)(SET_MENUS + CH_N))
#define SET_STAT_PG ((uint8_t)(SET_INFO_PG + 1u))   /* stress status (Diagnostic Build) */
#define SET_WVIZCFG_PG ((uint8_t)(SET_STAT_PG + 1u))   /* Meter > Configure, B-215/B-216 -- always
                                                         * available, not gated on TAU_LIBRARY/TAU_DIAGNOSTIC
                                                         * like the pages below. */
#define SET_HELP_PG ((uint8_t)(SET_WVIZCFG_PG + 1u))   /* how files, playlists and the library are loaded */
#define SET_LIBCFG_PG ((uint8_t)(SET_HELP_PG + 1u)) /* library on/off with a confirmation */
#if TAU_DIAGNOSTIC
#define SET_CHECK_PG ((uint8_t)(SET_LIBCFG_PG + 1u)) /* the user-facing diagnostics run (fw/suite.inc) */
#if MP3_PROFILE || FLAC_PROFILE
#define SET_SWEEP_PG ((uint8_t)(SET_CHECK_PG + 1u))   /* Decode Profile Sweep, B-090/B-091 (fw/suite.inc) */
#define SET_BLITTEST_PG ((uint8_t)(SET_SWEEP_PG + 1u))   /* Blit Test, B-166 (fw/suite.inc) */
#else
#define SET_BLITTEST_PG ((uint8_t)(SET_CHECK_PG + 1u))   /* Blit Test, B-166 (fw/suite.inc) */
#endif
#endif
#define SET_COUNT(a) ((uint8_t)(sizeof(a) / sizeof((a)[0])))
enum { TG_ART = 0, TG_SHUF, TG_RESUME, TG_SPEEDS };
enum { RT_GROUP = 0, RT_CHOICE, RT_TOGGLE, RT_VOL, RT_ACTION };
#define SET_PAGE_CH(c) ((uint8_t)(SET_MENUS + (c)))

typedef struct { const char *label; uint8_t type, arg; } set_row_t;
static const set_row_t set_home_rows[] = {
    { "APPEARANCE", RT_GROUP, SET_APPEAR }, { "AUDIO", RT_GROUP, SET_AUDIO },
    { "PLAYBACK", RT_GROUP, SET_PLAY },
    { "SETTINGS", RT_GROUP, SET_DIAG },
};
static const set_row_t set_appear_rows[] = {
    { "COLOUR", RT_CHOICE, CH_COLOUR }, { "METER", RT_CHOICE, CH_METER },
    { "METER: CONFIGURE", RT_GROUP, SET_WVIZCFG_PG },
    { "ALBUM ART", RT_TOGGLE, TG_ART }, { "SCREEN BLANK", RT_CHOICE, CH_BLANK } };
static const set_row_t set_audio_rows[] = {
    { "EQUALIZER", RT_CHOICE, CH_EQ }, { "VOLUME", RT_VOL, 0 } };
static const set_row_t set_play_rows[] = {
    { "REPEAT", RT_CHOICE, CH_REPEAT }, { "SHUFFLE", RT_TOGGLE, TG_SHUF },
    { "RESUME", RT_TOGGLE, TG_RESUME }, { "SPEED", RT_CHOICE, CH_SPEED } };
static const set_row_t set_diag_rows[] = {
    { "INFO", RT_GROUP, SET_INFO_PG },
    { "LIBRARY", RT_GROUP, SET_LIBCFG_PG },
    { "HOW IT WORKS", RT_GROUP, SET_HELP_PG },
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
    { "CHECK", RT_GROUP, SET_CHECK_PG },
#if MP3_PROFILE || FLAC_PROFILE
    { "DECODE SWEEP", RT_GROUP, SET_SWEEP_PG },
#endif
    { "BLIT TEST", RT_GROUP, SET_BLITTEST_PG },
#else
    { "INFO", RT_GROUP, SET_INFO_PG },          /* unreachable placeholder: the page exists only in the Diagnostic Build */
#endif
};
/* Diagnostic Build tests: A runs the highlighted one and its result replaces the value. */
enum { DG_WIN = 0, DG_RD, DG_WR, DG_PLC, DG_COLD, DG_CLR, DG_N };
static const set_row_t set_tests_rows[] = {
    { "WINDOW TEST", RT_ACTION, DG_WIN }, { "READ CYCLES", RT_ACTION, DG_RD },
    { "WRITE CYCLES", RT_ACTION, DG_WR }, { "PLAYLIST CHECK", RT_ACTION, DG_PLC },
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
    SET_COUNT(set_home_rows), 4u, 2u, 4u, SET_COUNT(set_diag_rows), SET_COUNT(set_tests_rows),
    SET_COUNT(set_stress_rows), SET_COUNT(set_dgn_rows) };
static const char *const set_menu_title[SET_MENUS] = {
    "MENU", "APPEARANCE", "AUDIO", "PLAYBACK", "SETTINGS", "TESTS", "STRESS", "DIAGNOSTICS" };
/* Where B goes from each menu page. */
static const uint8_t set_menu_parent[SET_MENUS] = { SET_HOME, SET_HOME, SET_HOME, SET_HOME, SET_HOME,
    SET_DGN, SET_DGN, SET_DIAG };
static const char *const set_ch_title[CH_N] = { "COLOUR", "METER", "EQUALIZER", "REPEAT",
    "SCREEN BLANK", "SPEED", "STRESS LEVEL", "SOAK" };
static const uint8_t set_ch_parent[CH_N] = { SET_APPEAR, SET_APPEAR, SET_AUDIO, SET_PLAY,
    SET_APPEAR, SET_PLAY, SET_STRESS, SET_STRESS };
/* Only the speeds that play cleanly are offered (A-133/A-135: 1.30x and above degrade and 1.25x micro-stuttered on some
 * material, the decoder budget at 60 MHz allows about 1.20x). The higher entries stay in speed_num/speed_den in
 * player.c for the resampler / decoder work and return here by raising SET_SPEED_SHOWN. */
#define SET_SPEED_SHOWN 5u
_Static_assert(SET_SPEED_SHOWN <= SPEED_N, "speed list longer than the speed table");
#if TAU_DIAGNOSTIC
static const char *const set_speed_nm[SPEED_N] = { "0.85X", "0.95X", "1.00X", "1.10X", "1.20X",
    "1.30X", "1.50X", "1.75X", "2.00X", "2.50X" };
#else
static const char *const set_speed_nm[SET_SPEED_SHOWN] = { "0.85X", "0.95X", "1.00X", "1.10X", "1.20X" };
#endif
/* Diagnostic Build only: Diagnostics > ALL SPEEDS offers the whole speed table (up to 2.50x) in the speed list, as a
 * test bed for the audio work. Not persisted; off at every start; turning it off returns a fast speed to 1.00x. */
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

/* Same order as the VIZ_* enum; the label VIZ_LED shares with its toast is SPECTRUM. */
static const char *const set_viz[] = { "BARS", "WATERFALL", "-", "PHASE SCOPE",
    "OSCILLOSCOPE", "VU", "WAVEFORM", "-", "PEAK DOTS", "-", "SPECTRUM",
    "-", "WINAMP BARS", "WINAMP SCOPE", "CHLADNI" };
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
_Static_assert(LIB_ROW_H == SET_MENU_ROW_H, "the library rows must match the settings rows");
_Static_assert(PLIST_ROW_H == SET_MENU_ROW_H && PLIST_ROWS == LIB_ROWS, "the playlist rows must match the settings/library rows (B-073)");

static uint8_t set_page, set_sel[SET_MENUS], set_ch_cur[CH_N], set_ch_top[CH_N];
static uint8_t set_lib_confirm;               /* the Library page is waiting for a second A */

#include "meter_thumbs.h"
_Static_assert(VIZ_COUNT == 15 && VIZ_BARS == 0 && VIZ_LED == 10 && VIZ_RETIRED_TAPE == 11 &&
               VIZ_WINAMP_BARS == 12 && VIZ_WINAMP_SCOPE == 13 && VIZ_CHLADNI == 14,
               "meter_thumbs.h is indexed by the VIZ_* enum order (tools/gen_meter_thumbs.py ORDER)");

/* Draw preview `viz` at (x, y) by decoding the RLE in software: fill with the dominant colour,
 * then paint every other run as a one-row rectangle (runs may cross row ends). This is the
 * original path -- dozens of fb_rect draw-engine round-trips per thumbnail -- kept as the B8
 * fail-safe for a bitstream without the blit engine (BLIT_READY() == 0, B-126). */
COLD_FN static void set_draw_thumb_soft(uint32_t x, uint32_t y, uint32_t viz)
{
    const uint16_t *pal = meter_thumb_pal[viz];
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

COLD_FN static void set_thumb_flat_build(void)
{
    for (uint32_t viz = 0; viz < VIZ_COUNT; viz++) {
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
    thumb_flat_ready = 1u;
}

COLD_FN static void set_draw_thumb(uint32_t x, uint32_t y, uint32_t viz)
{
    if (!cold_ready()) { fb_rect(x, y, METER_THUMB_W, METER_THUMB_H, UI_TRACK); return; }   /* cold image not loaded: a plain box */
    blit_probe_ensure();                                                                    /* B-162: first need, not at boot */
    if (!BLIT_READY()) { set_draw_thumb_soft(x, y, viz); return; }                          /* B8 fail-safe: old bitstream */
    if (!thumb_flat_ready) set_thumb_flat_build();
    fb_clut_load(meter_thumb_pal[viz], 8u);
    fb_cblit(0u, THUMB_STASH_Y + thumb_slot(viz) * METER_THUMB_H, x, y, METER_THUMB_W, METER_THUMB_H);
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
         : c == CH_STRESS ? 4u : c == CH_SPEED ? SET_SPEED_COUNT() : 5u;
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
    if (c == CH_SPEED) return speed_idx;
    return c == CH_COLOUR ? ui_pal_idx : c == CH_METER ? viz_mode_to_sel(viz_mode) : c == CH_EQ ? eq_idx
         : c == CH_REPEAT ? rep_mode : set_blank_idx();
}
COLD_FN static const char *set_ch_name(uint32_t c, uint32_t i)
{
    return c == CH_COLOUR ? ui_palette_name[i] : c == CH_METER ? set_viz[viz_sel_to_mode(i)]
         : c == CH_EQ ? eq_name[i] : c == CH_REPEAT ? set_rep[i]
         : c == CH_STRESS ? set_stress_nm[i] : c == CH_SOAK ? set_soak_nm[i]
         : c == CH_SPEED ? set_speed_nm[i] : set_blank_nm[i];
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
        speed_idx = (uint8_t)i;
        pcm_rate_apply(track_hz);
        ui_last_sec = 0xFFFFFFFFu;               /* repaint the speed marker next frame */
        ui_mode_dirty = 1u;
        return;                                  /* not persisted */
    }
    if (c == CH_COLOUR) {
        ui_pal_idx = i;
        ui_accent = ui_palette[i];
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
    } else if (t == TG_SHUF) {
        shuffle_on = (uint8_t)!shuffle_on;
        if (shuffle_on) pl_rng = cycles() | 1u;
        if (pl_count) {
            uint16_t cur = pl_order[pl_pos];
            pl_reorder();
            pl_resync(cur);                      /* keep playing what is playing */
        }
        ui_mode_dirty = 1u;
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
#endif
    } else {
        resume_on = (uint8_t)!resume_on;
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
    else if (r->arg == TG_SHUF)   s = shuffle_on ? "ON" : "OFF";
    else if (r->arg == TG_SPEEDS) s = set_speed_all ? "ON" : "OFF";
    else                          s = resume_on ? "ON" : "OFF";
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
    ov_frame(set_menu_title[p], "",
             p == SET_HOME ? "A OPEN   B CLOSE"
             : p == SET_DIAG || p == SET_STRESS || p == SET_DGN ? "A OPEN   B BACK" : p == SET_TESTS ? "A RUN   B BACK"
             : "A CHANGE   B BACK");
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

COLD_FN static void set_draw_choice(uint32_t c)
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
#define SET_INFO_ROWS 21u   /* B-073: +1 for COVER (art_bad_code, for the "no
                                              * picture found" report); B-234: +1 for METER YIELD
                                              * (a cheap diagnostic for the suspected meter_afford()
                                              * freeze, see its own comment in fw/player.c); B-238:
                                              * +1 for VBLANK (last row either way) -- proves the
                                              * new Helios/Talos H0 MMIO end to end (edges/sec,
                                              * should read this core's real vertical rate once a
                                              * TAU_VBLANK bitstream is installed; 0 on any other
                                              * bitstream, see vblank_sample()'s own comment). */
static const char *const set_info_label[SET_INFO_ROWS] = {
    "FIRMWARE", "FPGA REV", "SDRAM WINDOW", "WINDOW READ", "FREE RAM", "PLAYLIST",
    "COLD IMAGE",
    "TRACK", "COVER", "UNDERRUNS", "DRAW STALL", "LOAD MS"
    , "LIBRARY"
    , "METER YIELD", "VBLANK", "SPECTRUM", "BEAM", "CHLADNI", "CPU LOAD", "TIM1 COVER"
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
        if (pl_count) { q = ui_dec(q, pl_count); q = set_str(q, " TRACKS"); }
        else q = set_str(q, "NONE");
        break;
    case 6:
        if (cold_state == 1u) { q = ui_dec(q, cold_bytes); q = set_str(q, " B "); q = ui_dec(q, cold_ms); q = set_str(q, " MS");
            if (COLD_READY()) q = set_str(q, " CODE");
            else if (cold_err) { q = set_str(q, " E"); q = ui_dec(q, cold_err); }   /* image loaded but cold code refused */
        }
        else { q = set_str(q, "OFF E"); q = ui_dec(q, cold_err); }
        break;
    case 7:
        if (!track_hz) { q = set_str(q, "NONE"); break; }
        q = set_str(q, track_fmt == FMT_FLAC ? "FLAC " : "MP3 ");
        if (track_kbps) { q = ui_dec(q, track_kbps); q = set_str(q, "K "); }
        q = ui_dec(q, track_hz / 1000u); *q++ = '.'; q = ui_dec(q, (track_hz % 1000u) / 100u);
        *q++ = 'K';
        break;
    case 8:                                          /* B-073: art_bad_code, to tell "no picture in the tag" from "unreadable" */
        if (!art_bad_code) q = set_str(q, art_have ? "OK" : "-");
        else { q = set_str(q, "E"); q = ui_hex2(q, art_bad_code); }
        break;
    case 9: q = ui_dec(q, pcm_under_n - dg_base_under); break;
    case 10: q = ui_dec(q, (REG(R_FB_STALL) - dg_base_stall) / (CLK_HZ / 1000u)); q = set_str(q, " MS"); break;
    case 11:
        q = ui_dec(q, ld_head); *q++ = '/'; q = ui_dec(q, ld_size); *q++ = '/';
        q = ui_dec(q, ld_art);  *q++ = '/'; q = ui_dec(q, ld_total);
        break;
    case 12: q = lib_info_text(q); break;
    case 13:
        /* METER YIELD (B-234) -- see meter_afford()'s own comment */
        if (meter_yield) { q = set_str(q, "YIELD "); q = ui_dec(q, meter_yield_secs); q = set_str(q, "S"); }
        else q = set_str(q, "OK");
        if (meter_yield_worst) { q = set_str(q, " (WORST "); q = ui_dec(q, meter_yield_worst); q = set_str(q, "S)"); }
        break;
    case 14:   /* VBLANK (B-238) -- see vblank_sample()'s own comment */
        q = ui_dec(q, vblank_rate); q = set_str(q, "/S");
        break;
    case 15:   /* SPECTRUM (B-263): which analyser is running, and for the hardware one how many windows it has finished */
        if (spec_hw) { q = set_str(q, "HW W"); q = ui_dec(q, (REG(R_SPEC_ST) >> 16) & 0xFFFFu); }
        else q = set_str(q, "NO HW BANK");
        break;
    case 16:   /* BEAM (B-267): Helios beam-aware drawing -- share of meter updates that had to wait for the beam */
        if (!helios_beam_ok) q = set_str(q, "NONE");
        else {
            uint32_t tot = helios_drawn_n + helios_deferred_n;
            q = set_str(q, "OK "); q = ui_dec(q, tot ? (helios_deferred_n * 100u) / tot : 0u); q = set_str(q, "% WAITED");
        }
        break;
    case 17:   /* CHLADNI (B-276): first-use probe result, mailbox half order, figures drawn and skipped */
        if (chl_ok == 0u) q = set_str(q, "NOT USED");
        else if (chl_ok == 2u) q = set_str(q, "UNAVAILABLE");
        else {
            q = set_str(q, chl_swap ? "OK SWAP " : "OK ");
            q = ui_dec(q, chl_drawn_n); q = set_str(q, " DRAWN ");
            q = ui_dec(q, chl_skipped_n); q = set_str(q, " SKIP");
        }
        break;
    case 18:   /* CPU LOAD: share of the last second the decode loop was not waiting on the audio FIFO */
        q = ui_dec(q, ui_cpu_pct()); q = set_str(q, "%");
        break;
    case 19:   /* TIM1 COVER (B-288): how covers were shown -- files loaded, the last load time, and the last failure code (0 = none) */
#if TAU_ART_TIMG
        q = ui_dec(q, timg_loaded_n); q = set_str(q, " LOADED ");
        q = ui_dec(q, timg_ms); q = set_str(q, " MS E");
        q = ui_dec(q, timg_last_err);
        if (timg_last_err) { q = set_str(q, " ("); q = ui_dec(q, timg_fail_n); q = set_str(q, " FAILED)"); }
#else
        q = set_str(q, "OFF");
#endif
        break;
    default:   /* MP3 WINDOW (B-307): the hardware synthesis window -- slots it computed, self-check mismatches, timeouts */
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
    }
    *q = 0;
}

/* Read-only value pages share one painter: labels on the left, values right-aligned. */
/* Read-only value pages (Info, Stress status) show up to SET_RO_VIS rows at once and scroll with Up/Down when they
 * have more (B-257: Info grew to 15 rows and the 15th fell off the bottom of the screen). */
#define SET_RO_VIS 14u
static uint8_t set_ro_top;                       /* first visible row; reset to 0 when the page is left */

COLD_FN static void set_draw_ro(int full, const char *title, const char *const *labels, uint32_t n,
                        void (*value)(uint32_t, char *))
{
    const uint32_t vis = (n < SET_RO_VIS) ? n : SET_RO_VIS;
    uint32_t top = set_ro_top;
    if (top + vis > n) top = n - vis;
    if (full) ov_frame(title, "", (n > vis) ? "UP DOWN SCROLL   B BACK" : "B BACK");
    if (n > vis) {                               /* scroll track, as the lists */
        const uint32_t tx = PL_UI_X + PL_UI_W - 11u, lh = vis * PL_UI_ROW_H, th = lh * vis / n;
        fb_rect(tx, PL_UI_LIST_Y - 2u, 3u, lh, ui_mix(UI_PANEL, UI_DIM, 1u, 3u));
        fb_rect(tx, PL_UI_LIST_Y - 2u + (lh - th) * top / (n - vis), 3u, th, ui_accent);
    }
    for (uint32_t k = 0; k < vis; k++) {
        const uint32_t i = top + k, y = PL_UI_LIST_Y + k * PL_UI_ROW_H;
        char v[40];
        value(i, v);
        fb_rect(PL_UI_X + 4u, y - 2u, PL_UI_W - 16u, PL_UI_ROW_H, UI_PANEL);
        fb_set_color(UI_DIM, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, y, labels[i], TS_1X, TS_1X, 170u);
        uint32_t w = fb_text_width(v, TS_1X);
        fb_set_color(UI_WHITE, UI_PANEL);
        fb_text_clipped(PL_UI_X + PL_UI_W - 20u - w, y, v, TS_1X, TS_1X, w + 2u);
    }
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
    if (set_page == SET_INFO_PG) set_draw_ro(0, "INFO", set_info_label, SET_INFO_ROWS, set_info_value);
#if TAU_DIAGNOSTIC
    else if (set_page == SET_STAT_PG) set_draw_ro(0, "STRESS STATUS", set_stat_label, SET_STAT_ROWS, set_stat_value);
#endif
    ov_draw = o;
}

/* How music gets into the player. Two texts: with a library loaded, and without one (legacy playlist mode). */
/* The text is one blob per mode, lines separated by NUL, so it can live in the cold image (PSRAM). */
#define SET_HELP_LINES 12u
static const char set_help_lib[] COLD_DATA =
    "LIBRARY MODE\0" "Select opens your library.\0" "\0"
    "Playlists are inside it: the\0" "sync tool imports your .m3u\0" "files. Core menu playlist picks\0"
    "are ignored.\0" "\0" "A file picked in the core menu\0" "still plays once.\0" "\0"
    "New music: run the sync tool.";
static const char set_help_legacy[] COLD_DATA =
    "LEGACY PLAYLIST MODE\0" "No library file was found.\0" "\0"
    "Play one file:\0" "  Core menu > Load MP3\0" "Play a list:\0" "  Core menu > Load Playlist\0"
    "  (Select opens the list)\0" "\0" "For a library, run the sync tool\0" "on your music: it writes\0"
    "tau-library.tdb. Then restart.";

COLD_FN static void set_draw_help(void)
{
    int lib_on = (lib_state == LIB_ST_OK);
    const char *p = lib_on ? set_help_lib : set_help_legacy;
    ov_frame("HOW IT WORKS", "", "B BACK");
    if (!cold_ready()) {                                   /* cold image not loaded: no text to show */
        fb_set_color(UI_DIM, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y, "Help text is not loaded.", TS_1X, TS_1X, PL_UI_W - 32u);
        return;
    }
    for (uint32_t i = 0; i < SET_HELP_LINES && i < PL_UI_ROWS; i++) {
        fb_set_color(i ? UI_WHITE : ui_accent, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + i * PL_UI_ROW_H, p, TS_1X, TS_1X, PL_UI_W - 32u);
        while (*p) p++;
        p++;
    }
}

/* Library on/off. Off = the player behaves as before the library existed (legacy playlist mode); the index stays on the card. */
COLD_FN static void set_draw_libcfg(void)
{
    static const char *const on_txt[] = { "Turning it off makes the player", "work as a plain playlist player:", "Load MP3 / Load Playlist in the", "core menu, Select opens the list.",
        "The index file stays on the card." };
    static const char *const off_txt[] = { "Turning it on loads your library", "at start: browse and play from", "Select. Your history is kept." };
    ov_frame("LIBRARY", "", set_lib_confirm ? "A CONFIRM   B CANCEL" : "A CHANGE   B BACK");
    uint32_t y = PL_UI_LIST_Y;
    fb_set_color(ui_accent, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, y, lib_disabled ? "Library is OFF" : "Library is ON", TS_1X, TS_1X, PL_UI_W - 32u);
    y += 2u * PL_UI_ROW_H;
    const char *const *t = lib_disabled ? off_txt : on_txt;
    uint32_t n = lib_disabled ? SET_COUNT(off_txt) : SET_COUNT(on_txt);
    fb_set_color(UI_WHITE, UI_PANEL);
    for (uint32_t i = 0; i < n; i++, y += PL_UI_ROW_H) fb_text_clipped(PL_UI_TEXT_X, y, t[i], TS_1X, TS_1X, PL_UI_W - 32u);
    y += PL_UI_ROW_H;
    fb_set_color(UI_DIM, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, y, "Applies after a restart.", TS_1X, TS_1X, PL_UI_W - 32u);
    if (set_lib_confirm) {
        fb_set_color(ui_accent, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, y + PL_UI_ROW_H + 8u, lib_disabled ? "Press A to turn it on." : "Press A to turn it off.", TS_1X, TS_1X, PL_UI_W - 32u);
    }
}

#if TAU_DIAGNOSTIC
#include "suite.inc"
#endif

/* Settings > Appearance > Meter > Configure (B-215/B-216/B-217/B-218, row
 * filtering + independent per-mode presets B-234): a live preview of
 * whichever Winamp meter is selected -- still reading real playing audio --
 * pinned to the top of the page, with a scrollable list of its parameters
 * below. Session-only: every change here writes straight into the live
 * wviz_cfg_bars/wviz_cfg_scope (fw/player.c); nothing is persisted (see
 * B-216 for why not). Placed after suite.inc's own #include (not where the
 * rest of this file's Settings pages live) because the QR export row below
 * needs sr_tlv()/qr_encode() from suite_core.h/qrcode.h, which only exist
 * under TAU_DIAGNOSTIC.
 *
 * B-234: rows are now filtered by mode -- Bars shows only its own fields
 * (bands, easing, attack/release, peak cap/fall/hold), Scope shows only its
 * own (scope smooth/trail); PRESET, MODE and EXPORT QR are common to both.
 * `wvcfg_vis[]`/`wvcfg_vis_n` hold the current mode's visible row list;
 * `wvcfg_sel` indexes INTO that list (a "visible index"), not the WVR_* enum
 * directly -- wvcfg_row(k) maps one to the other. Rebuilt on open and on
 * every MODE toggle (wvcfg_build_vis()). */
enum { WVR_PRESET = 0, WVR_MODE, WVR_BANDS, WVR_EASE, WVR_ATTACK, WVR_RELEASE,
       WVR_PEAK_ON, WVR_PEAK_GRAV, WVR_PEAK_HOLD, WVR_PEAK_FALL,
       WVR_SCOPE_SMOOTH, WVR_SCOPE_TRAIL, WVR_EXPORT, WVR_N };
static const char *const wvcfg_row_nm[WVR_N] = {
    "PRESET", "MODE", "BANDS", "EASING", "ATTACK", "RELEASE",
    "PEAK CAP", "PEAK FALL", "PEAK HOLD MS", "PEAK FALL SPEED",
    "SCOPE SMOOTH", "SCOPE TRAIL", "EXPORT QR" };
static const char *const wvcfg_ease_nm[4] = { "INSTANT", "LINEAR", "EXPONENTIAL", "SPRING" };
static const char *const wvcfg_grav_nm[2] = { "LINEAR", "GRAVITY" };
#define WVCFG_ROWS_VISIBLE 8u
static uint8_t wvcfg_sel, wvcfg_top;
static uint8_t wvcfg_vis[WVR_N], wvcfg_vis_n;

static void wvcfg_build_vis(void)
{
    uint8_t n = 0;
    wvcfg_vis[n++] = WVR_PRESET;
    if (viz_mode == VIZ_WINAMP_BARS) {
        wvcfg_vis[n++] = WVR_BANDS;
        wvcfg_vis[n++] = WVR_EASE;
        wvcfg_vis[n++] = WVR_ATTACK;
        wvcfg_vis[n++] = WVR_RELEASE;
        wvcfg_vis[n++] = WVR_PEAK_ON;
        wvcfg_vis[n++] = WVR_PEAK_GRAV;
        wvcfg_vis[n++] = WVR_PEAK_HOLD;
        wvcfg_vis[n++] = WVR_PEAK_FALL;
    } else {
        wvcfg_vis[n++] = WVR_SCOPE_SMOOTH;
        wvcfg_vis[n++] = WVR_SCOPE_TRAIL;
    }
    wvcfg_vis[n++] = WVR_EXPORT;
    wvcfg_vis_n = n;
    if (wvcfg_sel >= wvcfg_vis_n) wvcfg_sel = (uint8_t)(wvcfg_vis_n - 1u);
}

static inline uint32_t wvcfg_row(uint32_t k) { return wvcfg_vis[k]; }

static void wvcfg_open(void)
{
    wvcfg_sel = 0u; wvcfg_top = 0u;
    if (viz_mode != VIZ_WINAMP_BARS && viz_mode != VIZ_WINAMP_SCOPE) viz_mode = VIZ_WINAMP_BARS;
    wvcfg_build_vis();
    wviz_force = 1u;   /* B-234: entering at a different rect than the player screen's own */
}

COLD_FN static void wvcfg_follow(void)
{
    if (wvcfg_sel < wvcfg_top) wvcfg_top = wvcfg_sel;
    else if (wvcfg_sel >= wvcfg_top + WVCFG_ROWS_VISIBLE) wvcfg_top = (uint8_t)(wvcfg_sel - WVCFG_ROWS_VISIBLE + 1u);
}

/* B-218: exports the live wviz_cfg_bars/wviz_cfg_scope as a QR code, reusing the exact record/text/
 * QR pipeline fw/suite.inc already built for Check/Blit Test (sr_init/sr_tlv/
 * sr_finish/sr_text/qr_encode, the CHK_REC/CHK_TXT/CHK_QR SDRAM staging
 * buffers) rather than inventing a second channel -- this page and Check/Blit
 * Test are never open at the same time, so sharing those buffers is safe.
 * Diagnostic-Build only (TAU_DIAGNOSTIC), same as every other QR feature here;
 * outside a Diagnostic Build the row is present but inert (see below). */
#if TAU_DIAGNOSTIC
#include "wvcfg_export.inc"
#endif

COLD_FN static void wvcfg_value_text(uint32_t row, char *o)
{
    char *q = o;
    int scope = (viz_mode == VIZ_WINAMP_SCOPE);
    uint8_t pidx = scope ? wvcfg_preset_idx_scope : wvcfg_preset_idx_bars;
    switch (row) {
    case WVR_PRESET:        q = set_str(q, (pidx < WVIZ_PRESET_N) ? wviz_preset_nm[pidx] : "CUSTOM"); break;
    case WVR_MODE:          q = set_str(q, scope ? "SCOPE" : "BARS"); break;
    case WVR_BANDS:         q = ui_dec(q, wviz_cfg_bars.bands); break;
    case WVR_EASE:          q = set_str(q, wvcfg_ease_nm[wviz_cfg_bars.ease_mode & 3u]); break;
    case WVR_ATTACK:        q = ui_dec(q, wviz_cfg_bars.attack); break;
    case WVR_RELEASE:       q = ui_dec(q, wviz_cfg_bars.release); break;
    case WVR_PEAK_ON:       q = set_str(q, wviz_cfg_bars.peak_on ? "ON" : "OFF"); break;
    case WVR_PEAK_GRAV:     q = set_str(q, wvcfg_grav_nm[wviz_cfg_bars.peak_gravity & 1u]); break;
    case WVR_PEAK_HOLD:     q = ui_dec(q, wviz_cfg_bars.peak_hold_ms); q = set_str(q, " MS"); break;
    case WVR_PEAK_FALL:     q = ui_dec(q, wviz_cfg_bars.peak_fall); break;
    case WVR_SCOPE_SMOOTH:  q = ui_dec(q, wviz_cfg_scope.scope_smooth); q = set_str(q, "%"); break;
    case WVR_SCOPE_TRAIL:   q = ui_dec(q, wviz_cfg_scope.scope_trail); q = set_str(q, "%"); break;
    default:                /* WVR_EXPORT */
#if TAU_DIAGNOSTIC
        q = set_str(q, "PRESS A");
#else
        q = set_str(q, "DIAG BUILD ONLY");
#endif
        break;
    }
    *q = 0;
}

COLD_FN static void wvcfg_adjust(uint32_t row, int32_t dir)
{
    int scope = (viz_mode == VIZ_WINAMP_SCOPE);
    if (row == WVR_PRESET) {
        uint8_t *pidx = scope ? &wvcfg_preset_idx_scope : &wvcfg_preset_idx_bars;
        int32_t p = (*pidx < WVIZ_PRESET_N) ? (int32_t)*pidx : 0;
        p += dir;
        if (p < 0) p = (int32_t)WVIZ_PRESET_N - 1; else if (p >= (int32_t)WVIZ_PRESET_N) p = 0;
        *pidx = (uint8_t)p;
        if (scope) wviz_cfg_scope = wviz_scope_presets[*pidx];
        else       wviz_cfg_bars  = wviz_bars_presets[*pidx];
        wviz_force = 1u;    /* B-234: geometry/values just jumped -- see wviz_force's own comment */
        return;
    }
    if (row == WVR_MODE) {                        /* mode isn't a wviz_cfg_* field -- doesn't mark CUSTOM */
        viz_mode = scope ? VIZ_WINAMP_BARS : VIZ_WINAMP_SCOPE;
        wvcfg_build_vis();
        wviz_force = 1u;
        return;
    }
    if (row == WVR_EXPORT) return;                 /* A-only action, not Left/Right-adjustable */
    if (scope) wvcfg_preset_idx_scope = 0xFFu; else wvcfg_preset_idx_bars = 0xFFu;   /* any other field -> CUSTOM */
    int32_t v;
    switch (row) {
    case WVR_BANDS:
        v = (int32_t)wviz_cfg_bars.bands + dir;
        if (v < (int32_t)WVIZ_BANDS_MIN) v = WVIZ_BANDS_MIN; else if (v > (int32_t)WVIZ_BANDS_MAX) v = WVIZ_BANDS_MAX;
        wviz_cfg_bars.bands = (uint8_t)v; break;
    case WVR_EASE:
        v = (int32_t)wviz_cfg_bars.ease_mode + dir;
        if (v < 0) v = 3; else if (v > 3) v = 0;
        wviz_cfg_bars.ease_mode = (uint8_t)v; break;
    case WVR_ATTACK:
        v = (int32_t)wviz_cfg_bars.attack + dir * 5; if (v < 1) v = 1; else if (v > 100) v = 100;
        wviz_cfg_bars.attack = (uint8_t)v; break;
    case WVR_RELEASE:
        v = (int32_t)wviz_cfg_bars.release + dir * 5; if (v < 1) v = 1; else if (v > 100) v = 100;
        wviz_cfg_bars.release = (uint8_t)v; break;
    case WVR_PEAK_ON:     wviz_cfg_bars.peak_on = wviz_cfg_bars.peak_on ? 0u : 1u; break;
    case WVR_PEAK_GRAV:   wviz_cfg_bars.peak_gravity = wviz_cfg_bars.peak_gravity ? 0u : 1u; break;
    case WVR_PEAK_HOLD:
        v = (int32_t)wviz_cfg_bars.peak_hold_ms + dir * 20; if (v < 0) v = 0; else if (v > 800) v = 800;
        wviz_cfg_bars.peak_hold_ms = (uint16_t)v; break;
    case WVR_PEAK_FALL:
        v = (int32_t)wviz_cfg_bars.peak_fall + dir * 5; if (v < 1) v = 1; else if (v > 100) v = 100;
        wviz_cfg_bars.peak_fall = (uint8_t)v; break;
    case WVR_SCOPE_SMOOTH:
        v = (int32_t)wviz_cfg_scope.scope_smooth + dir * 5; if (v < 0) v = 0; else if (v > 90) v = 90;
        wviz_cfg_scope.scope_smooth = (uint8_t)v; break;
    default:
        v = (int32_t)wviz_cfg_scope.scope_trail + dir * 5; if (v < 0) v = 0; else if (v > 80) v = 80;
        wviz_cfg_scope.scope_trail = (uint8_t)v; break;
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
    fb_rect(px - 4u, py - 4u, pw + 8u, ph + 8u, UI_PANEL);
    wviz_force = 1u;                      /* the panel was just wiped: the meters' change caches must repaint everything */
    if (viz_mode == VIZ_WINAMP_SCOPE) wviz_scope_tick(px, py, pw, ph, 0, UI_PANEL);
    else                              wviz_bars_tick(px, py, pw, ph, UI_PANEL);

    uint32_t list_y = py + ph + 16u;
    if (wvcfg_vis_n > WVCFG_ROWS_VISIBLE) {                      /* scroll track, as the other lists */
        const uint32_t n = wvcfg_vis_n, vis = WVCFG_ROWS_VISIBLE, tx = PL_UI_X + PL_UI_W - 11u, lh = vis * PL_UI_ROW_H, th = lh * vis / n;
        fb_rect(tx, list_y - 2u, 3u, lh, ui_mix(UI_PANEL, UI_DIM, 1u, 3u));
        fb_rect(tx, list_y - 2u + (lh - th) * wvcfg_top / (n - vis), 3u, th, ui_accent);
    }
    for (uint32_t k = 0; k < WVCFG_ROWS_VISIBLE && wvcfg_top + k < wvcfg_vis_n; k++) {
        uint32_t vi = wvcfg_top + k, i = wvcfg_row(vi), y = list_y + k * PL_UI_ROW_H;
        int sel = (vi == wvcfg_sel);
        uint16_t bg = sel ? ui_accent : UI_PANEL;
        if (sel) fb_round_rect_on(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, PL_UI_ROW_H, 5u, bg, UI_PANEL);
        else     fb_rect(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, PL_UI_ROW_H, UI_PANEL);
        char v[24];
        wvcfg_value_text(i, v);
        fb_set_color(sel ? UI_PANEL : UI_DIM, bg);
        fb_text_clipped(PL_UI_TEXT_X, y, wvcfg_row_nm[i], TS_1X, TS_1X, 190u);
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
    const uint8_t o = ov_draw; ov_draw = 1u;
    const uint32_t px = PL_UI_TEXT_X, pw = PL_UI_X + PL_UI_W - 8u - px, py = PL_UI_LIST_Y, ph = UI_WAVE_H;
    if (viz_mode == VIZ_WINAMP_SCOPE) wviz_scope_tick(px, py, pw, ph, 0, UI_PANEL);
    else                              wviz_bars_tick(px, py, pw, ph, UI_PANEL);
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
    if (edge & (KEY_B | KEY_START)) { set_page = SET_APPEAR; wviz_force = 1u; set_dirty = 1u; return 1; }
    if (edge & KEY_UP)    { wvcfg_sel = wvcfg_sel ? (uint8_t)(wvcfg_sel - 1u) : (uint8_t)(wvcfg_vis_n - 1u); wvcfg_follow(); set_dirty = 1u; }
    if (edge & KEY_DOWN)  { wvcfg_sel = (uint8_t)((wvcfg_sel + 1u) % wvcfg_vis_n); wvcfg_follow(); set_dirty = 1u; }
    if (edge & KEY_LEFT)  { wvcfg_adjust(wvcfg_row(wvcfg_sel), -1); set_dirty = 1u; }
    if (edge & KEY_RIGHT) { wvcfg_adjust(wvcfg_row(wvcfg_sel),  1); set_dirty = 1u; }
#if TAU_DIAGNOSTIC
    if ((edge & KEY_A) && wvcfg_row(wvcfg_sel) == WVR_EXPORT) { wvcfg_build_qr(); wvcfg_qr_up = 1u; set_dirty = 1u; }
#endif
    return 1;
}

COLD_FN static void set_draw(void)
{
    uint8_t o = ov_draw; ov_draw = 1u;
    if (set_page < SET_MENUS)                set_draw_menu(set_page);
    else if (set_page == SET_WVIZCFG_PG)     wvcfg_draw();
    else if (set_page == SET_LIBCFG_PG)      set_draw_libcfg();
    else if (set_page == SET_HELP_PG)        set_draw_help();
#if TAU_DIAGNOSTIC
    else if (set_page == SET_CHECK_PG)       chk_draw();
#if MP3_PROFILE || FLAC_PROFILE
    else if (set_page == SET_SWEEP_PG)       sw_draw();
#endif
    else if (set_page == SET_BLITTEST_PG)    bt_draw();
#endif
    else if (set_page == SET_INFO_PG)
        set_draw_ro(1, "INFO", set_info_label, SET_INFO_ROWS, set_info_value);
#if TAU_DIAGNOSTIC
    else if (set_page == SET_STAT_PG)
        set_draw_ro(1, "STRESS STATUS", set_stat_label, SET_STAT_ROWS, set_stat_value);
#endif
    else                                     set_draw_choice(set_page - SET_MENUS);
    ov_draw = o;
}

COLD_FN static void set_close(void)
{
    set_open = 0u; set_page = SET_HOME; pl_ui_restore = 1u;
    set_lib_confirm = 0u;
}

COLD_FN static void set_ch_follow(uint32_t c)
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
#if TAU_G4
            if (!COLD_READY()) { ui_toast_msg("MENU OFF: NO COLD IMAGE"); return 0; }   /* the menus are cold code; say why nothing opened */
#endif
            /* One overlay at a time: an open playlist would keep repainting its marquee row
             * over the settings (its rows draw through the ov_draw gate), so it is closed
             * here. Closing settings returns to the player, not to the list. */
            pl_ui_open = 0u;
            lib_ui_open = 0u;
            set_open = 1u; set_page = SET_HOME; set_dirty = 1u;
            return 1;
        }
        return 0;
    }
#if TAU_DIAGNOSTIC
    if (set_page == SET_CHECK_PG) return chk_input(edge);      /* first: Start is handled there (it must not close the result pages) */
#if MP3_PROFILE || FLAC_PROFILE
    if (set_page == SET_SWEEP_PG) return sw_input(edge);
#endif
    if (set_page == SET_BLITTEST_PG) return bt_input(edge);
#endif
    if (set_page == SET_WVIZCFG_PG) return wvcfg_input(edge);
    if (edge & KEY_START) { set_close(); return 1; }
    if (set_page == SET_LIBCFG_PG) {              /* library on/off: A asks, a second A confirms, B cancels or goes back */
        if ((edge & KEY_A) || ((edge & KEY_RIGHT) && !set_lib_confirm)) {     /* Right may ask, only A confirms */
            if (!set_lib_confirm) set_lib_confirm = 1u;
            else { lib_disabled = (uint8_t)!lib_disabled; set_lib_confirm = 0u; settings_mark_dirty(); }
            set_dirty = 1u;
        }
        if (edge & (KEY_B | KEY_LEFT)) {
            if (set_lib_confirm) set_lib_confirm = 0u; else set_page = SET_DIAG;
            set_dirty = 1u;
        }
        return 1;
    }
    if (set_page >= SET_INFO_PG) {                /* read-only value page: Up/Down scroll, B (back) */
        uint32_t n = SET_INFO_ROWS;
#if TAU_DIAGNOSTIC
        if (set_page == SET_STAT_PG) n = SET_STAT_ROWS;
#endif
        const uint32_t maxtop = (n > SET_RO_VIS) ? n - SET_RO_VIS : 0u;
        if ((edge & KEY_UP)   && set_ro_top > 0u)      { set_ro_top--; set_dirty = 1u; }
        if ((edge & KEY_DOWN) && set_ro_top < maxtop)  { set_ro_top++; set_dirty = 1u; }
        if (edge & (KEY_B | KEY_LEFT)) { set_ro_top = 0u; set_page = (set_page == SET_STAT_PG) ? SET_STRESS : SET_DIAG; set_dirty = 1u; }   /* Info, How it works -> SETTINGS */
        return 1;
    }
    /* B-073: Up/Down auto-repeat while held, the same shape as the playlist and library lists (a step on the edge, a
     * hold delay, then steady) -- a menu used to need one tap per row, which read as inconsistent next to the lists. */
    static uint32_t set_rep_at;
    if (edge & (KEY_UP | KEY_DOWN)) set_rep_at = cycles() + CLK_HZ / 1000u * PL_HOLD_MS;
    uint32_t rep = (keys & (KEY_UP | KEY_DOWN)) && (int32_t)(cycles() - set_rep_at) >= 0;
    if (rep) { set_rep_at = cycles() + CLK_HZ / 16u; edge |= keys & (KEY_UP | KEY_DOWN); }

    if (set_page >= SET_MENUS) {                  /* a choice list */
        uint32_t c = set_page - SET_MENUS, n = set_ch_count(c);
        if (edge & KEY_UP)   { set_ch_cur[c] = (uint8_t)(set_ch_cur[c] ? set_ch_cur[c] - 1u : n - 1u); set_ch_follow(c); set_dirty = 1u; }
        if (edge & KEY_DOWN) { set_ch_cur[c] = (uint8_t)((set_ch_cur[c] + 1u) % n); set_ch_follow(c); set_dirty = 1u; }
        if ((edge & (KEY_A | KEY_RIGHT)) && !rep) { set_ch_apply(c, set_ch_cur[c]); set_dirty = 1u; }     /* Right = forward = select */
        if ((edge & (KEY_B | KEY_LEFT)) && !rep)  { set_page = set_ch_parent[c]; set_dirty = 1u; }         /* Left = back */
        return 1;
    }
    uint8_t *sel = &set_sel[set_page];
    if (edge & KEY_UP)   { *sel = *sel ? (uint8_t)(*sel - 1u) : (uint8_t)(set_menu_n[set_page] - 1u); set_dirty = 1u; }
    if (edge & KEY_DOWN) { *sel = (uint8_t)((*sel + 1u) % set_menu_n[set_page]); set_dirty = 1u; }
    if (rep) return 1;      /* a repeat only ever moves the cursor: A/B/Left/Right below react to a real press */
    const set_row_t *r = &set_menu_rows[set_page][*sel];
    if (edge & (KEY_A | KEY_RIGHT)) {
        if (r->type == RT_GROUP) {
            set_page = r->arg; set_dirty = 1u;
            if (set_page == SET_WVIZCFG_PG) wvcfg_open();
#if TAU_DIAGNOSTIC
            if (set_page == SET_CHECK_PG) chk_open();
#if MP3_PROFILE || FLAC_PROFILE
            if (set_page == SET_SWEEP_PG) sw_open();
#endif
            if (set_page == SET_BLITTEST_PG) bt_open();
#endif
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
