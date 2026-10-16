#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "nowplaying.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../transport/mp3play.h"
#include "../cores/nowplaying_core.h"
#include "meter.h"
#include "../art/art.h"

#define BG   RGB565(0, 0, 0)
#define FG   RGB565(255, 255, 255)
#define DIM  RGB565(140, 146, 160)
#define ACC  RGB565(255, 160, 40)
#define TRK  RGB565(50, 54, 64)
#define X0   20u
#define BAR_W 360u
#define BAR_Y 178u
#define TX   160u                   /* text column beside the cover */
#define TW   220u
#define COV_X 20u
#define COV_Y 40u

typedef struct { char s[40]; } field_t;
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
    meter_enter(212, 80);
    gfx_text(X0, 340, "A PLAY B PAUSE Y SEEK  SEL+A LIB  SEL+UP SET", DIM, BG);
}

static void line(char *o, uint32_t *n, const char *s) { while (*s && *n < 46u) o[(*n)++] = *s++; o[*n] = 0; }
static void num(char *o, uint32_t *n, uint32_t v)
{
    char t[10]; uint32_t k = 0; do { t[k++] = (char)('0' + v % 10u); v /= 10u; } while (v && k < 9u);
    while (k && *n < 46u) o[(*n)++] = t[--k]; o[*n] = 0;
}

void nowplaying_draw(void)
{
    const np_info_t *p = mp3play_np();
    put(0, 6, p->playing ? (p->held ? "PAUSED" : "PLAYING") : "STOPPED", p->playing && !p->held ? ACC : DIM, 120, 1);
    if (p->valid) {
        if (p->gen != art_gen && p->gov < 1u) {                    /* a new track: its cover (the governor must be NORMAL: the load blocks for about 150 ms) */
            art_gen = p->gen; gfx_rect(COV_X, COV_Y, 128, 128, TRK);
            if (p->cover[0]) { mp3play_io_quiesce(); if (art_load(p->cover) == TIMG_OK) art_draw(COV_X, COV_Y); else gfx_text(COV_X + 20, COV_Y + 56, "NO COVER", DIM, TRK); }
            else gfx_text(COV_X + 20, COV_Y + 56, "NO COVER", DIM, TRK);
        }
        put_x = TX;
        put(1, 46, p->title, FG, TW, 0);
        put(2, 72, p->artist, DIM, TW, 0);
        put(3, 96, p->album, DIM, TW, 0);
        put(4, 130, p->quality, DIM, TW, 0);
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
    char s[48]; uint32_t n = 0; s[0] = 0;
    line(s, &n, "VOL "); num(s, &n, p->volume); line(s, &n, "   RG "); line(s, &n, p->rg_mode == 0 ? "OFF" : p->rg_mode == 1 ? "TRACK" : "ALBUM");
    put(7, 298, s, FG, BAR_W, 0);
    n = 0; s[0] = 0; char sp[8]; np_speed(sp, p->tempo_q8);
    line(s, &n, "TEMPO "); line(s, &n, sp); line(s, &n, p->repeat == 0 ? "  REP OFF" : p->repeat == 1 ? "  REP ALL" : "  REP ONE"); line(s, &n, p->shuffle ? "  SHUF ON" : "  SHUF OFF");
    put(8, 318, s, FG, BAR_W, 0);
    meter_draw(p->gov < 1u);                                    /* the governor sheds the UI first: no bars while the ring is below half */
}
