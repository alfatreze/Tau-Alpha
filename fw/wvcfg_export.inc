/* Meter configure page: QR export of the meter being edited (B-218, generic since M2).
 * Diagnostic Build only: included from settingsui.inc under #if TAU_DIAGNOSTIC (never compiled into `release`).
 * Moved out of that file unchanged (B-251); behaviour and generated code are identical. */
static uint32_t wvcfg_qr_len, wvcfg_qr_ver;
static uint8_t  wvcfg_qr_up;

COLD_FN static void wvcfg_build_qr(void)
{
    /* SR_T_METERCFG (fw/suite_core.h): meter id, schema, preset, nparams, then each value (u16 parameters are two bytes, LE). */
    uint8_t buf[4 + 2 * WVCFG_MAX_ROWS];
    uint32_t n = 0;
    const mtr_data_t *md = wvcfg_md;
    buf[n++] = (uint8_t)md->viz;
    buf[n++] = 1u;
    buf[n++] = *md->pre_idx;
    buf[n++] = md->n;
    for (uint32_t i = 0; i < md->n; i++) {
        buf[n++] = (uint8_t)(md->val[i] & 0xFFu);
        if (md->p[i].type == MTR_U16) buf[n++] = (uint8_t)(md->val[i] >> 8);
    }

    sr_t s;
    sr_init(&s, CHK_REC, CHK_REC_CAP, 0u);
    sr_tlv(&s, (uint8_t)SR_T_METERCFG, buf, n);
    REP_CONTEXT(&s, 1);
    uint32_t len = sr_finish(&s);
    REP_SET(len);
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
#if TAU_TPG
    { char d[48], *q = d; q = set_str(q, "LIVE EDITOR"); *q = 0; rep_draw("METER CONFIG", d, wvcfg_qr_len, wvcfg_qr_ver); return; }
#endif
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
