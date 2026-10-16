#include "nowplaying.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#include "../transport/mp3play.h"
#include "../cores/nowplaying_core.h"

#define BG   RGB565(0, 0, 0)
#define FG   RGB565(255, 255, 255)
#define DIM  RGB565(140, 146, 160)
#define ACC  RGB565(255, 160, 40)
#define TRK  RGB565(50, 54, 64)
#define X0   20u
#define BAR_W 360u
#define BAR_Y 170u

typedef struct { char s[48]; } field_t;
static field_t cache[10]; static uint32_t bar_px = 0xFFFFFFFFu, last_gen = 0xFFFFFFFFu;

/* Draw `s` on the row at y if it differs from what the row holds (the row is cleared first; cells draw their own background). `max_w` trims a long string to the pixel width. */
static void put(uint32_t slot, uint32_t y, const char *s, uint16_t fg, uint32_t max_w, int right)
{
    char buf[48]; uint32_t n = 0;
    for (; s[n] && n + 1u < sizeof buf; n++) buf[n] = s[n];
    buf[n] = 0;
    while (n && gfx_text_width(buf) > max_w) buf[--n] = 0;               /* cut to fit; a proportional font, so by measuring */
    uint32_t i = 0; while (buf[i] && buf[i] == cache[slot].s[i]) i++;
    if (!buf[i] && !cache[slot].s[i]) return;                           /* unchanged */
    for (i = 0; i < sizeof cache[slot].s; i++) cache[slot].s[i] = i < n + 1u ? buf[i] : 0;
    gfx_rect(right ? GFX_W - X0 - max_w : X0, y, max_w, GFX_CELL_H, BG);
    gfx_text(right ? GFX_W - X0 - gfx_text_width(buf) : X0, y, buf, fg, BG);
}

void nowplaying_enter(void)
{
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    for (uint32_t i = 0; i < 10u; i++) cache[i].s[0] = 0;
    bar_px = 0xFFFFFFFFu; last_gen = 0xFFFFFFFFu;
    gfx_text(X0, 6, "NOW PLAYING", ACC, BG);
    gfx_text(X0, 304, "A PLAY  B PAUSE  Y +30S  X SHUFFLE", DIM, BG);
    gfx_text(X0, 322, "L1 R1 VOL  SEL+L1 R1 TEMPO", DIM, BG);
    gfx_text(X0, 340, "SEL+B INFO  START REPORT", DIM, BG);
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
        put(1, 50, p->title, FG, BAR_W, 0);
        put(2, 74, p->artist, DIM, BAR_W, 0);
        put(3, 96, p->album, DIM, BAR_W, 0);
        put(4, 130, p->quality, DIM, BAR_W, 0);
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
    put(7, 230, s, FG, BAR_W, 0);
    n = 0; s[0] = 0; char sp[8]; np_speed(sp, p->tempo_q8);
    line(s, &n, "TEMPO "); line(s, &n, sp); line(s, &n, p->repeat == 0 ? "   REPEAT OFF" : p->repeat == 1 ? "   REPEAT ALL" : "   REPEAT ONE");
    put(8, 252, s, FG, BAR_W, 0);
    put(9, 274, p->shuffle ? "SHUFFLE ON" : "SHUFFLE OFF", DIM, BAR_W, 0);
}
