/* Winamp meter configure page: QR export of the session config (B-218).
 * Diagnostic Build only: included from settingsui.inc under #if TAU_DIAGNOSTIC (never compiled into `release`).
 * Moved out of that file unchanged (B-251); behaviour and generated code are identical. */
static uint32_t wvcfg_qr_len, wvcfg_qr_ver;
static uint8_t  wvcfg_qr_up;

COLD_FN static void wvcfg_build_qr(void)
{
    uint8_t buf[13];
    buf[0]  = (viz_mode == VIZ_WINAMP_SCOPE) ? 1u : 0u;
    buf[1]  = (viz_mode == VIZ_WINAMP_SCOPE) ? wvcfg_preset_idx_scope : wvcfg_preset_idx_bars;
    buf[2]  = wviz_cfg_bars.bands;
    buf[3]  = wviz_cfg_bars.ease_mode;
    buf[4]  = wviz_cfg_bars.attack;
    buf[5]  = wviz_cfg_bars.release;
    buf[6]  = wviz_cfg_bars.peak_on;
    buf[7]  = wviz_cfg_bars.peak_gravity;
    buf[8]  = (uint8_t)(wviz_cfg_bars.peak_hold_ms & 0xFFu);
    buf[9]  = (uint8_t)(wviz_cfg_bars.peak_hold_ms >> 8);
    buf[10] = wviz_cfg_bars.peak_fall;
    buf[11] = wviz_cfg_scope.scope_smooth;
    buf[12] = wviz_cfg_scope.scope_trail;

    sr_t s;
    sr_init(&s, CHK_REC, CHK_REC_CAP, 0u);
    sr_tlv(&s, (uint8_t)SR_T_WVIZCFG, buf, sizeof(buf));
    uint32_t len = sr_finish(&s);
    wvcfg_qr_len = 0;
    if (len && sr_text(CHK_REC, len, CHK_TXT, CHK_TXT_CAP)) {
        uint32_t txtlen = 0; while (CHK_TXT[txtlen]) txtlen++;
        uint32_t ver = 0, mask = 0;
        uint32_t sz = qr_encode((const uint8_t *)CHK_TXT, txtlen, 1u, CHK_QR_MAXV, 0, CHK_QR, &ver, &mask);
        if (sz) { wvcfg_qr_len = sz; wvcfg_qr_ver = ver; }
    }
}

/* Same rendering as bt_draw_qr() (fw/suite.inc) -- a plain module grid plus
 * a caption -- kept as its own small copy rather than a shared helper: the
 * two QR screens have different captions/hints and this is the only thing
 * that differs. */
COLD_TEXT static void wvcfg_draw_qr(void)
{
    const uint32_t sz = wvcfg_qr_len;
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
    fb_text_clipped(PL_UI_TEXT_X, side + 6u, "METER CONFIG", TS_1X, TS_1X, PL_UI_W - 32u);
    fb_set_color(UI_FAINT, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, FB_H - 22u, "B BACK", TS_1X, TS_1X, PL_UI_W - 32u);
}
