#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "report_page.h"
#include "report_record.h"
#include "info_counters.h"
#include "../gfx/gfx.h"
#include "../platform/mmio.h"
#define TPG_FN static
#include "../../fw/tpg.h"            /* the TPG2 pixel-grid encoder core (pure, tested against tools/tpg.py): where the block is and which value is pixel (x, y) */

#define BG   RGB565(0, 0, 0)
#define DIM  RGB565(140, 146, 160)
#define ACC  RGB565(255, 160, 40)
#define FRAME RGB565(90, 96, 110)

extern char _tag_start;                      /* the 4 KB tag landing zone: used only by the library load at boot, idle afterwards, so the report record lives there instead of in its own 4 KB of RAM */
#define rec ((uint8_t *)&_tag_start)
#define REC_CAP 4096u

static void caption(uint32_t y, uint32_t y0, uint32_t px, uint16_t fg, const char *s)
{
    if (!((int32_t)y + 14 <= (int32_t)y0 - 4 || (int32_t)y >= (int32_t)(y0 + px) + 4)) return;     /* a line that would touch the block is left out */
    gfx_text(10, y, s, fg, BG);
}

static uint8_t cfg_buf[96]; static uint32_t cfg_len;                  /* a meter config waiting to be shown (the Configure page export), shown once */
void report_page_set_payload(const uint8_t *p, uint32_t n) { if (n > sizeof cfg_buf) n = sizeof cfg_buf; for (uint32_t i = 0; i < n; i++) cfg_buf[i] = p[i]; cfg_len = n; }
void report_page_draw(void)
{
    const uint32_t cfg = cfg_len; cfg_len = 0;
    uint32_t len;
    if (cfg) len = report_record_build_meter(rec, REC_CAP, cfg_buf, cfg);
    else { counters_refresh(); len = report_record_build(rec, REC_CAP, counters_table, counters_count); }
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    tpg_t t;
    uint32_t mode = TPG_MODE_R;
    int ok = len && tpg_init(&t, rec, len, mode);
    if (!ok && len) { mode = TPG_MODE_L; ok = tpg_init(&t, rec, len, mode); }
    if (!ok) { gfx_text(10, 10, "REPORT TOO BIG FOR A PIXEL GRID", ACC, BG); return; }
    const uint32_t x0 = t.x0, y0 = t.y0, px = t.px;
    if (x0 >= 8u && y0 >= 8u) {                                         /* a 2 px frame, 4 px outside the block */
        gfx_rect(x0 - 4u, y0 - 4u, px + 8u, 2u, FRAME); gfx_rect(x0 - 4u, y0 + px + 2u, px + 8u, 2u, FRAME);
        gfx_rect(x0 - 4u, y0 - 4u, 2u, px + 8u, FRAME); gfx_rect(x0 + px + 2u, y0 - 4u, 2u, px + 8u, FRAME);
    }
    uint32_t wait0 = cycles();
    while ((uint32_t)(cycles() - wait0) < CLK_HZ / 30u) { }              /* let every queued draw command finish before the mailbox writes land */
    for (uint32_t y = y0; y < y0 + px; y++)
        for (uint32_t x = x0; x < x0 + px; x += 2u)
            if (gfx_mb_write_buf(y * GFX_STRIDE + x, ((uint32_t)tpg_pixel(&t, x + 1u, y) << 16) | tpg_pixel(&t, x, y))) return;
    caption(8, y0, px, ACC, cfg ? "METER CONFIG" : "PLAYER 2 REPORT");
    caption(22, y0, px, DIM, mode == TPG_MODE_R ? "ROBUST GRID" : "LOSSLESS GRID");
    caption(GFX_H - 36, y0, px, DIM, "SELECT+START REFRESH   B BACK");
    caption(GFX_H - 22, y0, px, DIM, "MENU+START FOR A SCREENSHOT");
}
