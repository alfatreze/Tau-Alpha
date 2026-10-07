/* ================================================================== REPORT PLUMBING (shared by the Check page and every report export)
 * Moved out of fw/suite.inc (2026-10-07) so the normal core can export the Info page as a report code too (TAU_INFO_EXPORT): the record builder
 * (suite_core.h), the QR encoder, the report buffers in PSRAM, the pixel-grid report view (tpg.h) and the one drawing path every report page shares.
 * All of it is cold code (COLD_TEXT, run from PSRAM). Included from fw/settingsui.inc when TAU_DIAGNOSTIC or TAU_INFO_EXPORT is set. */
#define SR_FN COLD_TEXT static
#include "suite_core.h"
#define QR_FN COLD_TEXT static
#define QR_TBL COLD_DATA
#include "qrcode.h"

#define CHK_QR      ((uint8_t *)(uintptr_t)0xA4003000u)     /* QR working buffer, QR_BUF_LEN = 13,206 B (to 0x6398) */
#define CHK_REC     ((uint8_t *)(uintptr_t)0xA4007000u)     /* record, 2,048 B */
#define CHK_TXT     ((char *)(uintptr_t)0xA4007800u)        /* QR text, 2,800 B (version 38 at level L holds 2,699 B) */
#define CHK_REC_CAP 2048u
#define CHK_TXT_CAP 2800u
#define CHK_QR_MAXV TAU_QR_MAXV

#ifndef TAU_TPG
#define TAU_TPG 0
#endif
#ifndef TAU_QR_MAXV
#define TAU_QR_MAXV 38u   /* largest QR version shown; 14 keeps every code phone-scannable (bigger reports then show the grid only) */
#endif
/* Report view shared by every page that shows a report code (Check, Decode Sweep, Meter Sweep, Meter Trace, Blit Test, Info and Meter Config exports):
 * rep_len = byte length of the record in CHK_REC, rep_view = 0 robust pixel grid (the default; the lossless grid takes over by itself when the report is too
 * big for it), 1 lossless pixel grid, 2 QR code. X cycles (the QR step is skipped when no QR code could be made).
 * With TAU_TPG=0 every macro collapses to the old QR-only behaviour. A report too big for the QR code is still available as a grid. */
#if TAU_TPG
static uint32_t rep_len;
static uint8_t rep_view;
#define REP_SET(l) (rep_len = (l))
#define REP_AVAIL(qrlen) ((qrlen) || rep_len)
#define REP_GRID(qrlen) (rep_view != 2u || !(qrlen))
#define REP_KEY(edge, qrlen) (((edge) & KEY_X) ? (rep_view = (uint8_t)((rep_view + 1u) % ((qrlen) ? 3u : 2u)), set_dirty = 1u, 1) : 0)
#else
#define REP_SET(l) ((void)(l))
#define REP_AVAIL(qrlen) (qrlen)
#define REP_GRID(qrlen) 0
#define REP_KEY(edge, qrlen) 0
#endif

#if TAU_TPG
/* What every report carries so a bare screenshot says what it was taken on and while playing what (the Info page's identity rows, as the page
 * shows them, plus the now-playing entry). `info_rows` = 0 for the Info export, which already dumps every row. About 150-280 bytes. */
COLD_TEXT static void rep_context(sr_t *s, int info_rows)
{
    static const char *const want[] = { "FIRMWARE", "FPGA REV", "COLD IMAGE", "TRACK", "LIBRARY", "UNDERRUNS" };
    for (uint32_t w = 0; info_rows && w < sizeof want / sizeof want[0]; w++)
        for (uint32_t i = 0; i < SET_INFO_ROWS; i++) {
            const char *l = set_info_label[i], *x = want[w];
            while (*l && *l == *x) { l++; x++; }
            if (*l || *x) continue;                                  /* not this row */
            uint8_t e[72]; char v[40]; uint32_t n = 0;
            set_info_value(i, v);
            e[n++] = (uint8_t)i;
            for (const char *p = set_info_label[i]; *p && n < 28u; p++) e[n++] = (uint8_t)*p;
            e[n++] = 0u;
            for (const char *p = v; *p && n < sizeof e; p++) e[n++] = (uint8_t)*p;
            sr_tlv(s, (uint8_t)SR_T_INFOTEXT, e, n);
            break;
        }
    uint8_t e[128]; uint32_t n = 0;
    const uint32_t have = track_title[0] || track_file[0];
    const uint32_t qp = (lib_src && lib_qn) ? (uint32_t)lib_qpos + 1u : 0u, ql = lib_src ? lib_qn : 0u;
    e[n++] = (uint8_t)(!have ? 0u : stopped ? 1u : paused ? 2u : 3u);
    e[n++] = (uint8_t)qp; e[n++] = (uint8_t)(qp >> 8); e[n++] = (uint8_t)ql; e[n++] = (uint8_t)(ql >> 8);
    const char *f[3] = { track_title, track_artist, track_album };
    for (uint32_t k = 0; k < 3u; k++) {
        for (uint32_t j = 0; f[k][j] && j < 40u; j++) e[n++] = (uint8_t)f[k][j];
        if (k < 2u) e[n++] = 0u;
    }
    sr_tlv(s, (uint8_t)SR_T_NOWPLAYING, e, n);
}
#define REP_CONTEXT(s, rows) rep_context((s), (rows))
#else
#define REP_CONTEXT(s, rows) ((void)0)
#endif

/* ---- SDRAM mailbox pixel writer (shared by the pixel grid views and the PIXEL GRID TEST page) ---- */
COLD_TEXT static int pg_mb_write(uint32_t word_addr, uint32_t data)
{
    REG(R_SDR_ADDR) = word_addr; REG(R_SDR_DATA) = data;
    REG(R_SDR_CTRL) = 0x3Fu;
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    return 0;
}

#if TAU_TPG
#define TPG_FN COLD_TEXT static
#define TPG_CRC32 sr_crc32
#include "tpg.h"
/* The one drawing path of every report page (Check, Decode Sweep, Meter Sweep, Meter Trace, Blit Test, Info and Meter Config exports), so they all look
 * the same: the code block centred on the screen (robust grid, lossless grid or QR code, X cycles), a frame round a grid, and the same caption lines:
 * title (what), detail (one short page-specific line), view (which code, how many bytes) at the top; the controls at the bottom. A line that would touch
 * the block is left out (a big block fills the screen). The grid rows go into BOTH H2 buffers through the mailbox (the draw engine cannot be asked for
 * one value per pixel), after fb_fence() so no queued command lands over them. */
COLD_TEXT static void rep_line(uint32_t y, uint32_t y0, uint32_t px, uint32_t color, const char *txt)
{
    if (!((int32_t)y + 14 <= (int32_t)y0 - 4 || (int32_t)y >= (int32_t)(y0 + px) + 4)) return;
    fb_set_color(color, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, y, txt, TS_1X, TS_1X, PL_UI_W - 32u);
}

COLD_TEXT static void rep_draw(const char *title, const char *detail, uint32_t qrlen, uint32_t qrver)
{
    tpg_t t;
    uint32_t x0 = 0, y0 = 0, px = 0, fell = 0;
    char view[48], *q = view;
    fb_rect(0u, 0u, FB_W, FB_H, UI_BG);
    if (!REP_GRID(qrlen)) {                                                 /* QR code */
        const uint32_t sz = qrlen;
        uint32_t m = 354u / (sz + 8u);
        if (m > 4u) m = 4u;
        px = (sz + 8u) * m; x0 = (FB_W - px) / 2u; y0 = (FB_H - px) / 2u;
        fb_rect(x0, y0, px, px, 0xFFFFu);                                   /* the 4-module quiet zone is part of the white square */
        for (uint32_t y = 0; y < sz; y++) {
            uint32_t x = 0;
            while (x < sz) {
                if (!qr_get(CHK_QR, sz, (int)x, (int)y)) { x++; continue; }
                uint32_t e = x + 1u;
                while (e < sz && qr_get(CHK_QR, sz, (int)e, (int)y)) e++;
                fb_rect(x0 + (x + 4u) * m, y0 + (y + 4u) * m, (e - x) * m, m, 0x0000u);
                x = e;
            }
        }
        q = set_str(q, "QR CODE V"); q = ui_dec(q, qrver);
    } else {                                                                /* pixel grid: robust by default, lossless when it does not fit */
        uint32_t mode = rep_view == 1u ? TPG_MODE_L : TPG_MODE_R;
        int ok = tpg_init(&t, CHK_REC, rep_len, mode);
        if (!ok && mode == TPG_MODE_R) { fell = 1u; mode = TPG_MODE_L; ok = tpg_init(&t, CHK_REC, rep_len, mode); }
        if (!ok) {
            rep_line(8u, 0u, 0u, UI_RED, "REPORT TOO BIG FOR A PIXEL GRID");
            return;
        }
        x0 = t.x0; y0 = t.y0; px = t.px;
        if (x0 >= 8u && y0 >= 8u) {                                         /* frame: 2 px, 4 px outside the block */
            fb_rect(x0 - 4u, y0 - 4u, px + 8u, 2u, UI_DIM); fb_rect(x0 - 4u, y0 + px + 2u, px + 8u, 2u, UI_DIM);
            fb_rect(x0 - 4u, y0 - 4u, 2u, px + 8u, UI_DIM); fb_rect(x0 + px + 2u, y0 - 4u, 2u, px + 8u, UI_DIM);
        }
        fb_fence();
        for (uint32_t buf = 0; buf < 2u; buf++) {
            const uint32_t base = buf ? DBUF_BASE1_W : 0u;
            for (uint32_t y = y0; y < y0 + px; y++)
                for (uint32_t x = x0; x < x0 + px; x += 2u)
                    if (pg_mb_write(base + y * FB_STRIDE + x, ((uint32_t)tpg_pixel(&t, x + 1u, y) << 16) | tpg_pixel(&t, x, y))) return;
        }
        q = set_str(q, mode == TPG_MODE_R ? "ROBUST GRID" : "LOSSLESS GRID");
    }
    q = set_str(q, "  "); q = ui_dec(q, rep_len); q = set_str(q, " B");
    if (fell) q = set_str(q, "  (TOO BIG FOR ROBUST)");
    *q = 0;
    rep_line(8u, y0, px, ui_accent, title);
    rep_line(22u, y0, px, UI_DIM, view);
    rep_line(36u, y0, px, UI_DIM, detail);
    rep_line(FB_H - 36u, y0, px, UI_FAINT, "X VIEW   B BACK");
    rep_line(FB_H - 22u, y0, px, UI_FAINT, "MENU+START FOR A SCREENSHOT");
}
#endif
