/* Info page: QR export of the live diagnostic values (B-301), on demand (Settings > Diagnostics >
 * Info > A = EXPORT QR). Diagnostic Build only, same shape as fw/wvcfg_export.inc's own QR export
 * (build the SR_T_INFOEXPORT record, encode, show a titled grid until any key closes it). */
static uint32_t info_qr_len, info_qr_ver;
/* info_qr_up itself is forward-declared in settingsui.inc (set_info_tick() needs it before this
 * file's own #include point). */

COLD_FN static void info_build_qr(void)
{
    uint32_t fpga_rev = REG(R_VERSION);
    uint32_t free_ram = (uint32_t)(&_heap_end - &_heap_start);
    uint16_t underruns = (uint16_t)(pcm_under_n - dg_base_under);
    uint16_t stall_ms = (uint16_t)((REG(R_FB_STALL) - dg_base_stall) / (CLK_HZ / 1000u));
    uint16_t load_ms = (uint16_t)ld_total;
    uint8_t buf[20] = {
        (uint8_t)(APP_VER[0] - '0'), (uint8_t)(APP_VER[2] - '0'), (uint8_t)(APP_VER[4] - '0'),
        (uint8_t)fpga_rev, (uint8_t)(fpga_rev >> 8), (uint8_t)(fpga_rev >> 16), (uint8_t)(fpga_rev >> 24),
        (uint8_t)pl_win_rd, (uint8_t)(pl_win_rd >> 8),
        (uint8_t)free_ram, (uint8_t)(free_ram >> 8), (uint8_t)(free_ram >> 16), (uint8_t)(free_ram >> 24),
        (uint8_t)underruns, (uint8_t)(underruns >> 8),
        (uint8_t)stall_ms, (uint8_t)(stall_ms >> 8),
        (uint8_t)load_ms, (uint8_t)(load_ms >> 8),
        (uint8_t)ui_cpu_pct(),
    };
    sr_t s;
    sr_init(&s, CHK_REC, CHK_REC_CAP, 0u);
    sr_tlv(&s, (uint8_t)SR_T_INFOEXPORT, buf, sizeof(buf));
    uint32_t len = sr_finish(&s);
    info_qr_len = 0;
    if (len && sr_text(CHK_REC, len, CHK_TXT, CHK_TXT_CAP)) {
        uint32_t txtlen = 0; while (CHK_TXT[txtlen]) txtlen++;
        uint32_t ver = 0, mask = 0;
        uint32_t sz = qr_encode((const uint8_t *)CHK_TXT, txtlen, 1u, CHK_QR_MAXV, 0, CHK_QR, &ver, &mask);
        if (sz) { info_qr_len = sz; info_qr_ver = ver; }
    }
}

/* Same rendering as wvcfg_draw_qr()/bt_draw_qr(): a plain module grid plus a titled caption. */
COLD_TEXT static void info_draw_qr(void)
{
    const uint32_t sz = info_qr_len;
    uint32_t m = 354u / (sz + 8u);
    if (m > 4u) m = 4u;
    const uint32_t side = (sz + 8u) * m, x0 = (FB_W - side) / 2u;
    fb_rect(0u, 0u, FB_W, FB_H, UI_BG);
    fb_rect(x0, 0u, side, side, 0xFFFFu);
    for (uint32_t y = 0; y < sz; y++) {
        uint32_t x = 0;
        while (x < sz) {
            if (!qr_get(CHK_QR, sz, (int)x, (int)y)) { x++; continue; }
            uint32_t e = x + 1u;
            while (e < sz && qr_get(CHK_QR, sz, (int)e, (int)y)) e++;
            fb_rect(x0 + (x + 4u) * m, (y + 4u) * m, (e - x) * m, m, 0x0000u);
            x = e;
        }
    }
    if (side + 44u > FB_H) return;
    fb_set_color(ui_accent, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, side + 6u, "INFO EXPORT", TS_1X, TS_1X, PL_UI_W - 32u);
    fb_set_color(UI_FAINT, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, FB_H - 22u, "ANY KEY BACK", TS_1X, TS_1X, PL_UI_W - 32u);
}
