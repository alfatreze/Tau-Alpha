/* Meter configure page: report export of the meter being edited (B-218, generic since M2).
 * Diagnostic Build only: included from settingsui.inc under #if TAU_DIAGNOSTIC (never compiled into `release`).
 * Moved out of that file unchanged (B-251); behaviour and generated code are identical. */
static uint8_t  wvcfg_rep_up;

COLD_FN static void wvcfg_build_rep(void)
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
}

/* Same rendering as bt_draw_rep() (fw/suite.inc) -- a plain module grid plus
 * a caption -- kept as its own small copy rather than a shared helper: the
 * two report screens have different captions/hints and this is the only thing
 * that differs. */
COLD_TEXT static void wvcfg_draw_rep(void)
{
    { char d[48], *q = d; q = set_str(q, "LIVE EDITOR"); *q = 0; rep_draw("METER CONFIG", d); }
}
