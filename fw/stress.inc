/* SDRAM stress pump: pattern/CRC, start/stop, the per-loop pump and its status (A-102/A-127).
 * Diagnostic Build only: included from player.c under #if TAU_DIAGNOSTIC (never compiled into `release`).
 * Moved out of that file unchanged (B-251); behaviour and generated code are identical. */
static uint32_t stress_pattern(uint32_t a) { return 0xA55AA55Au ^ (a * 0x9E3779B9u); }
static uint32_t stress_crc_word(uint32_t crc, uint32_t v)
{
    for (uint32_t n = 0; n < 4u; n++, v >>= 8) {
        crc ^= v & 0xFFu;
        for (uint32_t b = 0; b < 8u; b++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return crc;
}
static void stress_stop(const char *why)
{
    stress_on = 0; stress_failures++;
    stress_fault = why;
    ui_toast_msg(why);
}

static __attribute__((optimize("Os"))) const char *stress_viz_name(void)
{
    static const char *const names[] = {
        "BARS", "WATER", "-", "SCOPE", "WAVE", "VU",
        "SCROLL", "-", "DOTS", "-", "LED", "-", "WINAMP B", "WINAMP S", "CHLADNI"
    };
    return (viz_mode < VIZ_COUNT) ? names[viz_mode] : "?";
}

/* The HUD is deliberately tiny and clocks at 1 Hz. It is part of the stress
 * workload, so drawing it every frame would alter the contention it reports.
 * The bottom 16 px is otherwise unused; its text cell ends exactly at FB_H. */
static __attribute__((optimize("Os"))) void stress_hud_draw(void)
{
    if (screen_blank) return;
    uint32_t now = cycles(), tick = now / CLK_HZ;
    if (tick == stress_hud_tick) return;
    stress_hud_tick = tick;

    uint32_t done = 0, elapsed = 0;
    if (stress_on) {
        done = (stress_addr - STRESS_BASE) / 2u;
        if (done > STRESS_WORDS_PER_PASS) done = STRESS_WORDS_PER_PASS;
        elapsed = stress_pass_secs;
    }
    uint32_t pct = (done * 100u) / STRESS_WORDS_PER_PASS;
    uint32_t fill = (UI_INNER_W * pct) / 100u;
    uint16_t bg = ui_grad_at(UI_STRESS_HUD_Y);
    fb_rect(UI_MARGIN, UI_STRESS_BAR_Y, UI_INNER_W, 2u, UI_TRACK);
    if (fill) fb_rect(UI_MARGIN, UI_STRESS_BAR_Y, fill, 2u, ui_accent);

    /* Window mode: the live counters come FIRST so the 360 px line cannot clip them.
     * U = audio underruns since the run started (since boot while off), M = worst
     * window access in cycles, S = draw-engine stall in ms, R = rate level. */
    char b[56], *q = b;
    if (stress_fault) {
        const char *f = "FAIL ";
        while (*f) *q++ = *f++;
        q = ui_dec(q, stress_failures); *q++ = ' ';
    }
    /* Achieved rate over the last HUD tick (about 1 s). */
    stress_rate = stress_words - stress_rate_words;
    stress_rate_words = stress_words;
    *q++ = 'E'; q = ui_dec(q, stress_und_early);
    *q++ = ' '; *q++ = 'L'; q = ui_dec(q, stress_und_late);
    *q++ = ' '; *q++ = 'M';
    q = ui_dec(q, stress_rd_max > stress_wr_max ? stress_rd_max : stress_wr_max);
    *q++ = ' '; *q++ = 'S';
    q = ui_dec(q, (REG(R_FB_STALL) - stress_fb0) / (CLK_HZ / 1000u));
    *q++ = ' '; *q++ = 'R'; q = ui_dec(q, stress_level);
    *q++ = ' '; *q++ = 'K'; q = ui_dec(q, stress_rate / 1000u);
    *q++ = '.'; q = ui_dec(q, (stress_rate % 1000u) / 100u);
    if (stress_on) {
        *q++ = ' '; *q++ = 'P'; q = ui_dec(q, stress_passes + 1u);
        *q++ = ' '; q = ui_dec(q, pct); *q++ = '%';
    }
    *q = 0;
    fb_rect(UI_MARGIN, UI_STRESS_HUD_Y, UI_INNER_W, FB_CELL(TS_1X), bg);
    fb_set_color(stress_fault ? UI_RED : (stress_on ? ui_accent : UI_DIM), bg);
    fb_text_clipped(UI_MARGIN, UI_STRESS_HUD_Y, b, TS_1X, TS_1X, UI_INNER_W);
}

/* Accumulate a pass duration in whole seconds without 64-bit helpers.  The
 * main loop samples R_CYCLES far more frequently than its 71.58 s wrap, and
 * unsigned subtraction makes each sampled delta wrap-safe. */
static __attribute__((optimize("Os"))) void stress_time_tick(void)
{
    uint32_t now = cycles(), delta = now - stress_clock_prev;
    uint32_t secs = delta / CLK_HZ, rem = delta % CLK_HZ;
    stress_clock_prev = now;
    stress_pass_secs += secs;
    if (rem && stress_pass_rem >= CLK_HZ - rem) {
        stress_pass_secs++;
        stress_pass_rem -= CLK_HZ - rem;
    } else {
        stress_pass_rem += rem;
    }
}

/* Write one pattern word through the mailbox, then read it back through the CPU
 * window. On a bitstream without the window the alias decodes to MMIO and this
 * fails harmlessly (a read), so the pump never issues window STORES blind. */
static int stress_window_preflight(void)
{
    uint32_t t = cycles();
    REG(R_SDR_ADDR) = STRESS_BASE; REG(R_SDR_DATA) = 0x43505550u;
    REG(R_SDR_CTRL) = 0x3Fu;
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u)
        if ((uint32_t)(cycles() - t) > STRESS_TIMEOUT) return 0;
    return *(volatile uint32_t *)(uintptr_t)(0xA0000000u + (STRESS_BASE << 1)) == 0x43505550u;
}

static void stress_pump(void)
{
    /* Level 1 is PACED at 16k ops/s (3,750 cycles per operation), the Phase 1 rate.
     * Levels 2 and 3 are UNPACED bursts of 8 and 32 operations on every call (about
     * 20 and 90 us): this pump runs from poll_input(), mostly inside the loop that
     * waits for the audio FIFO to drain, so bursts consume only idle time, and the
     * achieved rate (K on the HUD) is whatever that idle time allows. An earlier
     * paced version reached only 13k ops/s at its top level because the wait loop
     * calls this rarely. */
    uint32_t now = cycles(), n, g = 3750u;
    if (!stress_on) return;
    if (stress_level <= 1u) {
        if ((int32_t)(now - stress_due) < 0) return;
        n = 1u + (uint32_t)((int32_t)(now - stress_due)) / g;
        if (n > 32u) n = 32u;
    } else {
        n = (stress_level == 2u) ? 8u : 32u;
    }
    for (uint32_t k = 0; k < n; k++) {
        uint32_t t0, d, got, exp;
        volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)(0xA0000000u + (stress_addr << 1));
        exp = stress_pattern(stress_addr);
        t0 = cycles(); *p = exp;  d = cycles() - t0; if (d > stress_wr_max) stress_wr_max = d;
        t0 = cycles(); got = *p;  d = cycles() - t0; if (d > stress_rd_max) stress_rd_max = d;
        if (got != exp) { stress_stop("SDRAM MISMATCH"); return; }
        stress_crc = stress_crc_word(stress_crc, got);
        stress_words++;
        stress_addr += 2u;
        if (stress_addr > STRESS_LAST) {
            stress_last_secs = stress_pass_secs;
            stress_addr = STRESS_BASE; stress_passes++; stress_last_pass = stress_passes;
            stress_pass_secs = stress_pass_rem = 0; stress_crc ^= 0xFFFFFFFFu;
            ui_toast_set("SDRAM PASS", stress_passes, 0);
        }
    }
    if (stress_level <= 1u) {
        stress_due += n * g;
        if ((int32_t)(cycles() - stress_due) > (int32_t)(32u * g)) stress_due = cycles();
    }
}
static void stress_tick(void)
{
    if (stress_on) stress_time_tick();
    stress_pump();
    stress_hud_draw();
}
static void stress_toggle(void)
{
    stress_level = (uint8_t)((stress_level + 1u) & 3u);        /* off,1,2,3 */
    if (stress_level == 1u && !stress_win_ok) {
        stress_win_ok = (uint8_t)stress_window_preflight();
        if (!stress_win_ok) {
            stress_level = 0u; stress_on = 0u;
            ui_toast_msg("NO SDRAM WINDOW");
            return;
        }
    }
    stress_on = (uint8_t)(stress_level != 0u);
    if (stress_on && stress_level > 1u) {
        stress_due = cycles();
        ui_toast_set("WIN STRESS RATE", stress_level, 0);
        return;
    }
    if (stress_on) stress_rd_max = stress_wr_max = 0u;
    if (stress_on) {
        stress_read = 0; stress_addr = STRESS_BASE; stress_due = cycles();
        stress_words = stress_passes = stress_failures = 0; stress_crc = 0xFFFFFFFFu;
        stress_last_secs = stress_last_pass = 0; stress_fault = 0;
        stress_clock_prev = stress_due; stress_pass_secs = stress_pass_rem = 0;
        stress_hud_tick = 0xFFFFFFFFu;
        stress_under0 = pcm_under_n; stress_fb0 = REG(R_FB_STALL);
        stress_und_early = stress_und_late = 0u; stress_rate_words = 0u; stress_rate = 0u;
        ui_toast_msg("SDRAM STRESS ON");
    } else {
        stress_hud_tick = 0xFFFFFFFFu;
        ui_toast_msg("SDRAM STRESS OFF");
    }
}

/* Menu entry point (Diagnostic Build): set the pump to an absolute level, 0 = off. A change
 * between running levels keeps the counters; starting from off zeroes them, as stress_toggle()
 * does. */
static void stress_set_level(uint8_t lvl)
{
    if (lvl > 3u) lvl = 3u;
    if (lvl && !stress_win_ok) {
        stress_win_ok = (uint8_t)stress_window_preflight();
        if (!stress_win_ok) {
            stress_level = 0u; stress_on = 0u;
            ui_toast_msg("NO SDRAM WINDOW");
            return;
        }
    }
    uint8_t was = stress_on;
    stress_level = lvl;
    stress_on = (uint8_t)(lvl != 0u);
    stress_hud_tick = 0xFFFFFFFFu;
    if (!stress_on) return;
    stress_due = cycles();
    if (was) return;
    stress_rd_max = stress_wr_max = 0u;
    stress_read = 0; stress_addr = STRESS_BASE;
    stress_words = stress_passes = stress_failures = 0; stress_crc = 0xFFFFFFFFu;
    stress_last_secs = stress_last_pass = 0; stress_fault = 0;
    stress_clock_prev = stress_due; stress_pass_secs = stress_pass_rem = 0;
    stress_under0 = pcm_under_n; stress_fb0 = REG(R_FB_STALL);
    stress_und_early = stress_und_late = 0u; stress_rate_words = 0u; stress_rate = 0u;
}
