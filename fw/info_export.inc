/* Info page: report export of the live diagnostic values (B-301), on demand (Settings > Diagnostics >
 * Info > A = EXPORT CODE). Diagnostic Build only, same shape as fw/wvcfg_export.inc's own report export
 * (build the SR_T_INFOEXPORT record, encode, show a titled grid until any key closes it). */
/* info_rep_up itself is forward-declared in settingsui.inc (set_info_tick() needs it before this
 * file's own #include point). */

COLD_FN static void info_build_rep(void)
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
    /* SR_T_INFOTEXT: every row of the Info page exactly as it reads on screen (the page is the reason the grid exists: far more than a report code
     * holds). One entry per row: row index, label, 0, value text. A new Info row needs no change here or in the decoder. */
    for (uint32_t i = 0; i < SET_INFO_ROWS; i++) {
        uint8_t e[72]; char v[40]; uint32_t n = 0;
        set_info_value(i, v);
        e[n++] = (uint8_t)i;
        for (const char *p = set_info_label[i]; *p && n < 28u; p++) e[n++] = (uint8_t)*p;
        e[n++] = 0u;
        const uint32_t vs = n;
        for (const char *p = v; *p && n < vs + 28u; p++) e[n++] = (uint8_t)*p;   /* values cut at 28 characters: keeps the 34 rows inside the record */
        sr_tlv(&s, (uint8_t)SR_T_INFOTEXT, e, n);
    }
    REP_CONTEXT(&s, 0);                      /* the rows are all in already */
    uint32_t len = sr_finish(&s);
    REP_SET(len);
}

/* Same rendering as wvcfg_draw_rep()/bt_draw_rep(): a plain module grid plus a titled caption. */
COLD_TEXT static void info_draw_rep(void)
{
    { char d[48], *q = d; q = ui_dec(q, SET_INFO_ROWS); q = set_str(q, " ROWS"); *q = 0; rep_draw("INFO EXPORT", d); }
}
