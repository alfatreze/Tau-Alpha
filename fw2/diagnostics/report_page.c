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

/* Where the report record lives. The counter table grew past 4 KB (B-867: "REPORT TOO BIG FOR A PIXEL GRID" on DEV 187 although the lossless grid holds 259 KB), so the record is built in a PSRAM scratch of
 * 32 KB (offset 0x530000, a gap of the PSRAM map that no other user touches: the report is built and read inside one page draw, never while a library, art or assets load runs); the 4 KB tag landing zone
 * (idle after the boot load, so no RAM of its own) is only the fall-back for a bitstream without the PSRAM window. The grid then picks its density: robust when the record fits (6,059 bytes), lossless
 * when it does not (259,184), so a bigger table makes the report denser, never refuses it. sim/test_report_capacity.py keeps the worst-case record under the scratch size. */
extern char _tag_start;
#define PS_SCRATCH 0xA4530000u
#define PS_SCRATCH_CAP 0x8000u
#define TAG_CAP 4096u
static uint8_t *rec; static uint32_t REC_CAP;
static int scratch_ok(void)                                           /* proven before the first store, like every PSRAM user (assets.c assets_psram_ok) */
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG_RD(0x80000088u) != 0x50535231u) return 0;
    if (!((REG_RD(0x800000A0u) >> 16) & 1u)) return 0;
    volatile uint32_t *w0 = (volatile uint32_t *)(uintptr_t)PS_SCRATCH, *w1 = (volatile uint32_t *)(uintptr_t)(PS_SCRATCH + PS_SCRATCH_CAP - 4u);
    for (uint32_t k = 0; k < 2u; k++) { *w0 = pat[k]; *w1 = ~pat[k]; if (*w0 != pat[k] || *w1 != ~pat[k]) return 0; }
    return 1;
}
uint32_t report_rows, report_len;                                     /* the last report: counters in it and its bytes (Info REPORT ROWS / REPORT BYTES) */

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
    static uint8_t ps_state;                                            /* 0 untried, 1 the PSRAM scratch works, 2 it does not */
    if (!ps_state) ps_state = scratch_ok() ? 1u : 2u;
    if (ps_state == 1u) { rec = (uint8_t *)(uintptr_t)PS_SCRATCH; REC_CAP = PS_SCRATCH_CAP; } else { rec = (uint8_t *)&_tag_start; REC_CAP = TAG_CAP; }
    if (cfg) len = report_record_build_meter(rec, REC_CAP, cfg_buf, cfg);
    else { counters_refresh(); len = report_record_build(rec, REC_CAP, counters_table, counters_count); }
    gfx_rect(0, 0, GFX_W, GFX_H, BG);
    tpg_t t;
    uint32_t mode = TPG_MODE_R;
    int ok = len && tpg_init(&t, rec, len, mode);
    if (!ok && len) { mode = TPG_MODE_L; ok = tpg_init(&t, rec, len, mode); }
    report_rows = cfg ? 0u : counters_count; report_len = len;
    if (!ok) {                                                          /* never a blank screen: say why and how much */
        char m[40]; uint32_t k = 0; const char *a = len ? "REPORT TOO BIG: " : "REPORT RECORD FULL: ";
        for (; *a; a++) m[k++] = *a;
        uint32_t v = report_rows, d = 1000u; int st = 0;
        for (; d; d /= 10u) { const uint32_t q = v / d; v %= d; if (q || st || d == 1u) { m[k++] = (char)('0' + q); st = 1; } }
        for (const char *b = " ROWS"; *b; b++) m[k++] = *b;
        m[k] = 0;
        gfx_text(10, 10, m, ACC, BG); gfx_text(10, 30, ps_state == 1u ? "PSRAM SCRATCH 32 KB" : "NO PSRAM: 4 KB FALLBACK", DIM, BG); return;
    }
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
