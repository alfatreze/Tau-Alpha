#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "nowplaying.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../transport/mp3play.h"
#include "../cores/nowplaying_core.h"
#include "meter.h"
#include "../art/art.h"
#include "helios.h"
#include "theme.h"
#include "../aout/halcyon.h"
#include "regions.h"
#include "../transport/mp3play.h"

#define BG   UI_BG_SCREEN
#define FG   UI_WHITE
#define DIM  UI_DIM
#define ACC  UI_ACCENT
#define TRK  UI_TRACK
#define X0   20u
#define BAR_W 360u
#define BAR_Y 178u
#define TX   160u                   /* text column beside the cover */
#define TW   220u
#define COV_X 20u
#define COV_Y 40u

typedef struct { char s[40]; } field_t;
uint8_t np_art_on = 1;                       /* Settings > Appearance > Album art (not persisted) */
static field_t cache[10]; static uint32_t bar_px = 0xFFFFFFFFu, last_gen = 0xFFFFFFFFu, art_gen = 0xFFFFFFFFu;

/* Draw `s` on the row at y if it differs from what the row holds (the row is cleared first; cells draw their own background). `max_w` trims a long string to the pixel width. */
static uint32_t put_x = X0;
static void put(uint32_t slot, uint32_t y, const char *s, uint16_t fg, uint32_t max_w, int right)
{
    char buf[40]; uint32_t n = 0;
    for (; s[n] && n + 1u < sizeof buf; n++) buf[n] = s[n];
    buf[n] = 0;
    while (n && gfx_text_width(buf) > max_w) buf[--n] = 0;               /* cut to fit; a proportional font, so by measuring */
    uint32_t i = 0; while (buf[i] && buf[i] == cache[slot].s[i]) i++;
    if (!buf[i] && !cache[slot].s[i]) return;                           /* unchanged */
    for (i = 0; i < sizeof cache[slot].s; i++) cache[slot].s[i] = i < n + 1u ? buf[i] : 0;
    gfx_rect(right ? GFX_W - X0 - max_w : put_x, y, max_w, GFX_CELL_H, BG);
    gfx_text(right ? GFX_W - X0 - gfx_text_width(buf) : put_x, y, buf, fg, BG);
}

void nowplaying_enter(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    for (uint32_t i = 0; i < 10u; i++) cache[i].s[0] = 0;
    bar_px = 0xFFFFFFFFu; last_gen = 0xFFFFFFFFu; art_gen = 0xFFFFFFFFu;
    gfx_text(X0, 6, "NOW PLAYING", ACC, BG);
    meters_enter(NP_METER_X, NP_METER_Y0, NP_METER_W, NP_METER_Y1 - NP_METER_Y0);
    gfx_text(X0, 340, "A PLAY B RESTART  HOLD L/R SEEK  START SET", DIM, BG);
}

static void line(char *o, uint32_t *n, const char *s) { while (*s && *n < 46u) o[(*n)++] = *s++; o[*n] = 0; }
static void num(char *o, uint32_t *n, uint32_t v)
{
    char t[10]; uint32_t k = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 9u);
    while (k && *n < 46u) o[(*n)++] = t[--k]; o[*n] = 0;
}

void nowplaying_draw_head(void)
{
    const np_info_t *p = mp3play_np();
    put(0, 6, p->playing ? (p->held ? "PAUSED" : "PLAYING") : "STOPPED", p->playing && !p->held ? ACC : DIM, 120, 1);
    if (p->valid) {
        if (p->gen != art_gen && p->gov < 1u) {                    /* a new track: its cover (the governor must be NORMAL: the load blocks for about 150 ms) */
            art_gen = p->gen; gfx_rect(COV_X, COV_Y, 128, 128, TRK);
            if (p->cover[0] && np_art_on) { mp3play_io_quiesce(); if (art_load(p->cover) == TIMG_OK) art_draw(COV_X, COV_Y); else gfx_text(COV_X + 20, COV_Y + 56, "NO COVER", DIM, TRK); }
            else gfx_text(COV_X + 20, COV_Y + 56, "NO COVER", DIM, TRK);
        }
        put_x = TX;
        put(1, 46, p->title, FG, TW, 0);
        put(2, 72, p->artist, DIM, TW, 0);
        put(3, 96, p->album, DIM, TW, 0);
        put(4, 130, p->quality, DIM, TW, 0);
        put(9, 152, p->fault ? "MP3 UNIT FAILED" : "", ACC, TW, 0);
        put_x = X0;
        char t[16];
        np_time(t, p->pos, p->rate); put(5, BAR_Y + 14u, t, FG, 100, 0);
        if (p->total) np_time(t, p->total, p->rate); else { t[0] = '-'; t[1] = '-'; t[2] = 0; }
        put(6, BAR_Y + 14u, t, DIM, 100, 1);
        uint32_t px = np_bar(p->pos, p->total, BAR_W);
        if (px != bar_px || p->gen != last_gen) {
            if (p->gen != last_gen || px < bar_px) gfx_rect(X0, BAR_Y, BAR_W, 8, TRK);
            gfx_rect(X0, BAR_Y, px, 8, ACC);
            bar_px = px; last_gen = p->gen;
        }
    }
}

void nowplaying_draw_foot(void)
{
    const np_info_t *p = mp3play_np();
    char s[48]; uint32_t n = 0; s[0] = 0;
    line(s, &n, "VOL "); num(s, &n, p->volume); line(s, &n, "   RG "); line(s, &n, p->rg_mode == 0 ? "OFF" : p->rg_mode == 1 ? "TRACK" : "ALBUM");
    put(7, 298, s, FG, BAR_W, 0);
    n = 0; s[0] = 0; char sp[8]; np_speed(sp, p->tempo_q8);
    line(s, &n, p->varispeed ? "SPEED " : "TEMPO "); line(s, &n, sp); line(s, &n, p->repeat == 0 ? "  REP OFF" : p->repeat == 1 ? "  REP ALL" : "  REP ONE"); line(s, &n, p->shuffle ? "  SHUF ON" : "  SHUF OFF");
    put(8, 318, s, FG, BAR_W, 0);
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
