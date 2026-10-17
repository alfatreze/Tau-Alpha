#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "nowplaying.h"
#include "../gfx/gfx.h"
#include "../gfx/icon.h"
#include "../platform/mmio.h"
#include "../transport/mp3play.h"
#include "../cores/nowplaying_core.h"
#include "meter.h"
#include "../art/art.h"
#include "helios.h"
#include "theme.h"
#include "../aout/halcyon.h"
#include "../aout/aout.h"
#include "regions.h"
#include "../cores/controls_core.h"

/* The layout of Figma "now-playing" (file rT7ux8u0R7SS3D34J4Idsq, nodes 384:563 playing and 384:1315 no track), 400 x 360: the cover top left, the pills + title + artist + album beside it, the meter full width, the
 * time row (state, elapsed, track n/m, total) under a progress bar, and a bottom bar with the two button hints on the left and repeat / shuffle / volume on the right. One font for all text (the font chosen in Appearance). */
#define BG   UI_BG_SCREEN
#define FG   UI_WHITE
#define DIM  UI_DIM
#define ACC  UI_ACCENT
#define TRK  UI_TRACK
#define PAN  UI_PANEL
#define COV_X 8u
#define COV_Y 8u
#define COV_S 128u
#define MX   152u                   /* the meta block */
#define MW   240u
#define PILL_Y 22u
#define PILL_H 18u
#define TITLE_Y 58u                 /* the 24 px title row of the design, the 16 px text centred in it */
#define ARTIST_Y 84u
#define ALBUM_Y 106u
#define PB_X 18u                    /* the progress bar and the time row */
#define PB_W 364u
#define PB_Y 297u
#define PB_H 6u
#define TIME_Y 307u
#define BAR_Y0 332u                 /* the bottom bar */
#define BAR_H 28u

typedef struct { char s[40]; } field_t;
uint8_t np_art_on = 1;                       /* Settings > Appearance > Album art (not persisted) */
static field_t cache[12]; static uint32_t bar_px = 0xFFFFFFFFu, last_gen = 0xFFFFFFFFu, art_gen = 0xFFFFFFFFu, art_state = 0xFFu, pill_sig = 0xFFFFFFFFu, bar_sig = 0xFFFFFFFFu, st_sig = 0xFFFFFFFFu;

/* Draw `s` on the row at y if it differs from what the row holds (the row is cleared first; cells draw their own background). `max_w` trims a long string to the pixel width. mode: 0 left at x, 1 right-aligned
 * to x, 2 centred on x. */
static void put(uint32_t slot, uint32_t x, uint32_t y, const char *s, uint16_t fg, uint32_t max_w, int mode, uint16_t bg)
{
    char buf[40]; uint32_t n = 0;
    for (; s[n] && n + 1u < sizeof buf; n++) buf[n] = s[n];
    buf[n] = 0;
    while (n && gfx_text_width(buf) > max_w) buf[--n] = 0;               /* cut to fit; a proportional font, so by measuring */
    uint32_t i = 0; while (buf[i] && buf[i] == cache[slot].s[i]) i++;
    if (!buf[i] && !cache[slot].s[i]) return;                           /* unchanged */
    for (i = 0; i < sizeof cache[slot].s; i++) cache[slot].s[i] = i < n + 1u ? buf[i] : 0;
    const uint32_t w = gfx_text_width(buf), rx = mode == 1 ? x - max_w : mode == 2 ? x - max_w / 2u : x;
    gfx_rect(rx, y, max_w, GFX_CELL_H, bg);
    gfx_text(mode == 1 ? x - w : mode == 2 ? x - w / 2u : x, y, buf, fg, bg);
}
static uint32_t pill(uint32_t x, const char *s)             /* a rounded-ish tag: surface background, accent text; returns its width */
{
    const uint32_t w = gfx_text_width(s) + 16u;
    gfx_rect(x, PILL_Y, w, PILL_H, PAN);
    gfx_text(x + 8u, PILL_Y + 1u, s, ACC, PAN);
    return w;
}
static void cover_placeholder(void)                          /* no cover, or no track: the surface square with the note icon */
{
    gfx_rect(COV_X, COV_Y, COV_S, COV_S, TRK);
    icon_draw(ICON_NOTE, COV_X + (COV_S - icon_width(ICON_NOTE)) / 2u, COV_Y + (COV_S - ICON_H) / 2u, DIM, TRK);
}
static void state_glyph(uint32_t playing)                     /* the 14 px play / pause icon before the elapsed time */
{
    gfx_rect(PB_X, TIME_Y + 1u, 14, 14, BG);
    const uint32_t id = playing ? ICON_PLAY_S : ICON_PAUSE_S;
    icon_draw(id, PB_X + (14u - icon_width(id)) / 2u, TIME_Y + 1u, ACC, BG);
}
static void bottom_bar_static(void)                          /* the icons are 28 px tall, the height of the bar */
{
    gfx_rect(0, BAR_Y0, GFX_W, BAR_H, PAN);
    uint32_t x = 12u;
    icon_draw(ICON_SELECT, x, BAR_Y0, FG, PAN); x += icon_width(ICON_SELECT) + 4u;
    gfx_text(x, BAR_Y0 + 6u, "PLAYLIST", FG, PAN); x += gfx_text_width("PLAYLIST") + 14u;
    icon_draw(ICON_START, x, BAR_Y0, FG, PAN); x += icon_width(ICON_START) + 4u;
    gfx_text(x, BAR_Y0 + 6u, "MENU", FG, PAN);
}

void nowplaying_enter(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    for (uint32_t i = 0; i < 12u; i++) cache[i].s[0] = 0;
    bar_px = 0xFFFFFFFFu; last_gen = 0xFFFFFFFFu; art_gen = 0xFFFFFFFFu; art_state = 0xFFu; pill_sig = 0xFFFFFFFFu; bar_sig = 0xFFFFFFFFu; st_sig = 0xFFFFFFFFu;
    meters_enter(NP_METER_X, NP_METER_Y0, NP_METER_W, NP_METER_Y1 - NP_METER_Y0);
    bottom_bar_static();
}

static void num(char *o, uint32_t *n, uint32_t v)
{
    char t[10]; uint32_t k = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 9u);
    while (k && *n < 30u) o[(*n)++] = t[--k]; o[*n] = 0;
}

void nowplaying_draw_head(void)
{
    const np_info_t *p = mp3play_np();
    const uint32_t cymo = aout_cymo_user_is_off() ? 0u : 1u, fmt = p->valid ? (p->quality[0] == 'F' ? 2u : 1u) : 0u;
    if (cymo * 4u + fmt != pill_sig) {                              /* the tags: SMOOTH while the resampler is on, the format of the track */
        pill_sig = cymo * 4u + fmt;
        gfx_rect(MX, PILL_Y, MW, PILL_H, BG);
        uint32_t x = MX;
        if (cymo) x += pill(x, "SMOOTH") + 4u;
        if (fmt) pill(x, fmt == 2u ? "FLAC" : "MP3");
    }
    if (!p->valid) {
        if (art_state != 1u) { art_state = 1u; art_gen = 0xFFFFFFFFu; cover_placeholder(); for (uint32_t i = 1; i < 4u; i++) cache[i].s[0] = 0; }
        put(1, MX, TITLE_Y, "SELECT A TRACK", FG, MW, 0, BG);
        put(2, MX, ARTIST_Y, "PRESS SELECT", ACC, MW, 0, BG);
        put(3, MX, ALBUM_Y, "", DIM, MW, 0, BG);
        return;
    }
    if (p->gen != art_gen && p->gov < 1u) {                        /* a new track: its cover (the governor must be NORMAL: the load blocks for about 150 ms) */
        art_gen = p->gen; art_state = 2u; gfx_rect(COV_X, COV_Y, COV_S, COV_S, TRK);
        if (p->cover[0] && np_art_on) { mp3play_io_quiesce(); if (art_load(p->cover) == TIMG_OK) art_draw(COV_X, COV_Y); else cover_placeholder(); }
        else cover_placeholder();
    }
    put(1, MX, TITLE_Y, p->title, FG, MW, 0, BG);
    put(2, MX, ARTIST_Y, p->artist, ACC, MW, 0, BG);
    put(3, MX, ALBUM_Y, p->album, DIM, MW, 0, BG);
    if (p->fault) put(9, MX, 130, "MP3 UNIT FAILED", ACC, MW, 0, BG); else put(9, MX, 130, "", ACC, MW, 0, BG);
}

void nowplaying_draw_foot(void)
{
    const np_info_t *p = mp3play_np();
    if (st_sig != (p->playing && !p->held ? 1u : 0u)) { st_sig = p->playing && !p->held ? 1u : 0u; state_glyph(st_sig); }
    char t[16]; uint32_t n;
    np_time(t, p->valid ? p->pos : 0u, p->rate ? p->rate : 44100u); put(5, PB_X + 18u, TIME_Y, t, ACC, 100, 0, BG);
    if (p->valid && p->total) np_time(t, p->total, p->rate); else { t[0] = '-'; t[1] = '-'; t[2] = 0; }
    put(6, PB_X + PB_W, TIME_Y, t, FG, 100, 1, BG);
    n = 0; t[0] = 0; if (p->valid && lp.qn) { num(t, &n, lp.cur_pos + 1u); t[n++] = '/'; t[n] = 0; num(t, &n, lp.qn); }
    put(10, 200, TIME_Y, t, FG, 100, 2, BG);
    const uint32_t px = p->valid ? np_bar(p->pos, p->total, PB_W) : 0u;
    if (px != bar_px || p->gen != last_gen) {
        if (p->gen != last_gen || px < bar_px) gfx_rect(PB_X, PB_Y, PB_W, PB_H, PAN);
        gfx_rect(PB_X, PB_Y, px, PB_H, ACC);
        bar_px = px; last_gen = p->gen;
    }
    const uint32_t sig = p->repeat * 4u + (p->shuffle ? 1u : 0u) + p->volume * 16u;      /* the bottom bar's right side: repeat, shuffle, volume */
    if (sig != bar_sig) {
        bar_sig = sig;
        const uint32_t ri = p->repeat == 0 ? ICON_PLAY_BOX_S : p->repeat == 1 ? ICON_REPEAT_S : ICON_REPEAT_ONE_S, si = p->shuffle ? ICON_SHUFFLE_S : ICON_SHUFFLE_OFF_S, vi = ctl_vol_icon(p->volume) + 16u;
        gfx_rect(236, BAR_Y0, 164, BAR_H, PAN);
        const uint32_t iy = BAR_Y0 + 7u;                       /* the 14 px icons centred in the bar */
        uint32_t x = 270u;
        icon_draw(ri, x, iy, p->repeat ? ACC : DIM, PAN); x += icon_width(ri) + 12u;
        icon_draw(si, x, iy, p->shuffle ? ACC : DIM, PAN); x += icon_width(si) + 12u;
        icon_draw(vi, x, iy, FG, PAN); x += icon_width(vi) + 8u;
        gfx_rect(x, BAR_Y0 + 13u, 50, 2, TRK);
        gfx_rect(x, BAR_Y0 + 13u, (p->volume > 100u ? 100u : p->volume) / 2u, 2, ACC);
    }
}

void nowplaying_draw_meter(void)
{
    const np_info_t *p = mp3play_np();
    meters_tick(p->gov < 1u);                                   /* the governor sheds the UI first: no bars while the ring is below half */
}

/* The keys of Now Playing that are plain edges (the timed ones -- Left/Right tap and hold, the Select combinations -- are gestures, app/gestures in main_player.c). */
int nowplaying_input(uint32_t edge)
{
    if (edge & KEY_A) mp3play_act(MPA_PLAYPAUSE, 0);
    if (edge & KEY_B) mp3play_act(MPA_RESTART, 0);
    if (edge & KEY_UP) mp3play_act(MPA_VOL, 3);
    if (edge & KEY_DOWN) mp3play_act(MPA_VOL, -3);
    if (edge & KEY_X) meters_next(1);                           /* the next meter (shuffle is in Settings > Playback) */
    if (edge & KEY_Y) halcyon_cycle();                          /* the Halcyon EQ preset, as before */
    if (edge & KEY_L1) theme_pick_step(-1);                     /* the accent colour, as before (the tempo is in Settings > Playback) */
    if (edge & KEY_R1) theme_pick_step(1);
    return -1;                                                   /* VW_STAY */
}
/* Marks the three regions for this pass: each redraw is a cheap compare against what the screen holds, and the flush waits for the beam to leave the rows. */
void nowplaying_mark(void) { helios_dirty_mask(NP_REGIONS); }
