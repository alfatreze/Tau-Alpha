/* fw/halcyon_page.inc -- Settings > Audio > HALCYON (B-644): the six-control page with a live response curve (design B of the 2026-10-07 proposal). Cold code; included by
 * fw/settingsui.inc where the page table can see it. Top: the response of the cascade you are setting (the six selected table rows added in dB, the infrasonic stage and the
 * preamp's give-back, fw/halcyon_curve.h: no trigonometry on the CPU) on a log frequency axis with the selected control's band lit. Below: the six controls as centre-zero
 * sliders. Controls apply LIVE (a bank written and committed per step, no state clear so no click); L1/R1 step the presets inside a gain dip as Diagnostics > HALCYON does;
 * X compares against the old EQ (engine out of the path), Y resets all six. Not persisted yet (decision D-S05: one saved word is still to be assigned). */
#if TAU_HALCYON_FW
#include "halcyon_curve.h"

static uint8_t hal_pg_sel;              /* selected control row 0..5 */
static uint8_t hal_cmp;                 /* 1 = the engine is out of the path (the X compare) */

#define HP_X  PL_UI_TEXT_X
#define HP_W  (PL_UI_X + PL_UI_W - 8u - HP_X)           /* 360 */
#define HP_Y  38u
#define HP_H  120u
#define HP_ROWS_Y (HP_Y + HP_H + 10u)                    /* 168: six rows of PL_UI_ROW_H */
#define HP_FOOT_Y (HP_ROWS_Y + 6u * PL_UI_ROW_H + 3u)    /* the preamp / high-pass line, 14 px high */
_Static_assert(HP_FOOT_Y + 16u <= OV_HINT_Y - 6u, "the Halcyon page must stay above the action bar");
_Static_assert(HP_Y >= OV_HEAD_H, "the Halcyon plot must start below the header");

/* Which stage's band to light for each control (x position in the plot of the stage's centre: 20 Hz to 20 kHz log, 360 px), and the half width. */
COLD_DATA static const uint16_t hal_band_x[6] = { 125u, 84u, 234u, 269u, 301u, 324u };     /* warmth (body 220 Hz), bass 100, vocal 1.8k, punch 3.5k, sibilance 6.5k, air 10k */
COLD_DATA static const char *const hal_ctl_name[6] = { "WARMTH", "BASS", "VOCAL", "PUNCH", "SIBILANCE", "AIR" };

COLD_FN static int8_t *hal_ctl_at(uint32_t i) { return &((int8_t *)&hal_c)[i]; }       /* warmth, bass, vocal, punch, sibilance, air: the struct's order */

/* Writes the current controls to the engine as a LIVE move (no clear). */
COLD_FN static void hal_pg_apply_live(void)
{
    if (!hw_hal) return;
    hal_sel = HAL_SEL_CUSTOM;
    hal_cmp = 0u;
    hal_hw_apply_ctl_live(&hal_c);
}

/* Takes the controls of a control preset (built-in or user) into hal_c; a raw user preset has no controls, so they stay as they were. */
COLD_FN static void hal_pg_load_sel(void)
{
    if (hal_sel >= 1u && hal_sel <= HAL_NPRESET) {
        const int8_t *p = hal_preset_ctl[hal_sel - 1u];
        hal_c.warmth = p[0]; hal_c.bass = p[1]; hal_c.vocal = p[2]; hal_c.punch = p[3]; hal_c.sibilance = p[4]; hal_c.air = p[5];
    } else if (hal_sel > HAL_NPRESET && hal_sel < HAL_SEL_CUSTOM) {
        int32_t coef[HP_MAX_RAW_STAGES * 5u], pre = 0;
        hal_ctl_t uc;
        if (hp_entry(HAL_USER + hal_user_off[hal_sel - HAL_NPRESET - 1u], &uc, coef, &pre) == 0u) hal_c = uc;
    }
}

COLD_FN static void hal_pg_open(void)
{
    hal_pg_sel = 0u;
    hal_cmp = 0u;
    hal_pg_load_sel();
}

COLD_FN static const char *hal_pg_preset_text(void)
{
    if (!hw_hal) return "NO UNIT";
    if (hal_cmp) return "A/B: EQ";
    if (hal_sel == HAL_SEL_CUSTOM) return "CUSTOM";
    if (hal_sel == 0u) return "OFF";
    if (hal_sel > HAL_NPRESET) return hp_name(HAL_USER + hal_user_off[hal_sel - HAL_NPRESET - 1u]);
    return hal_preset_name[hal_sel - 1u];
}

/* The plot: grid, the lit band, and the curve as 3-pixel columns joined to their neighbour (about 120 rectangles per redraw). */
COLD_FN static void hal_pg_plot(int32_t atten_out[1])
{
    int16_t db[HAL_NGRID];
    const int32_t cy = (int32_t)HP_Y + (int32_t)HP_H / 2;
    const uint16_t bg = OV_BODY, grid = ui_mix(OV_BODY, UI_DIM, 1u, 4u), axis = ui_mix(OV_BODY, UI_DIM, 1u, 2u);
    atten_out[0] = hal_curve(&hal_c, db);
    fb_rect(HP_X - 4u, HP_Y - 4u, HP_W + 8u, HP_H + 8u, bg);
    /* the lit band of the selected control */
    { int32_t bx = (int32_t)hal_band_x[hal_pg_sel] - 22, bw = 44;
      if (bx < 0) { bw += bx; bx = 0; }
      if (bx + bw > (int32_t)HP_W) bw = (int32_t)HP_W - bx;
      fb_rect(HP_X + (uint32_t)bx, HP_Y, (uint32_t)bw, HP_H, ui_mix(bg, ui_accent, 1u, 5u)); }
    /* grid: decades at 100 Hz, 1 kHz and 10 kHz; 0 dB axis, +-6 and +-12 dB */
    static const uint16_t gx[3] = { 84u, 204u, 324u };
    for (uint32_t i = 0; i < 3u; i++) fb_rect(HP_X + gx[i], HP_Y, 1u, HP_H, grid);
    fb_rect(HP_X, (uint32_t)cy, HP_W, 1u, axis);
    for (int32_t d = 24; d <= 48; d += 24) { fb_rect(HP_X, (uint32_t)(cy - d), HP_W, 1u, grid); fb_rect(HP_X, (uint32_t)(cy + d), HP_W, 1u, grid); }
    fb_set_color(UI_FAINT, bg);
    fb_text_clipped(HP_X + 2u, HP_Y + 1u, "+14", TS_1X, TS_1X, 40u);
    fb_text_clipped(HP_X + 2u, (uint32_t)cy - 14u, "0", TS_1X, TS_1X, 20u);
    fb_text_clipped(HP_X + 2u, HP_Y + HP_H - 14u, "-14", TS_1X, TS_1X, 40u);
    fb_text_clipped(HP_X + gx[0] + 3u, HP_Y + HP_H - 14u, "100", TS_1X, TS_1X, 30u);
    fb_text_clipped(HP_X + gx[1] + 3u, HP_Y + HP_H - 14u, "1K", TS_1X, TS_1X, 30u);
    fb_text_clipped(HP_X + gx[2] + 3u, HP_Y + HP_H - 14u, "10K", TS_1X, TS_1X, 30u);
    /* the curve: 4 px per dB (db is 1/64 dB), 3-pixel columns joined to the previous one */
    int32_t prev = 0;
    for (uint32_t x = 0; x < HP_W; x += 3u) {
        const uint32_t k = (x * (HAL_NGRID - 1u)) / (HP_W - 1u), k1 = (k + 1u < HAL_NGRID) ? k + 1u : k;
        const int32_t x0 = (int32_t)((k * (HP_W - 1u)) / (HAL_NGRID - 1u)), x1 = (int32_t)((k1 * (HP_W - 1u)) / (HAL_NGRID - 1u));
        int32_t v = db[k];
        if (x1 > x0) v += (int32_t)(db[k1] - db[k]) * ((int32_t)x - x0) / (x1 - x0);
        int32_t y = cy - v / 16;                                   /* 1/64 dB * 4 px/dB */
        if (y < (int32_t)HP_Y) y = (int32_t)HP_Y;
        if (y > (int32_t)(HP_Y + HP_H - 2u)) y = (int32_t)(HP_Y + HP_H - 2u);
        int32_t top = y, h = 3;
        if (x) { int32_t lo = prev < y ? prev : y, hi = prev < y ? y : prev; top = lo; h = hi - lo + 3; }
        if (top + h > (int32_t)(HP_Y + HP_H)) h = (int32_t)(HP_Y + HP_H) - top;
        fb_rect(HP_X + x, (uint32_t)top, 3u, (uint32_t)h, hal_cmp ? UI_DIM : ui_accent);
        prev = y;
    }
}

COLD_FN static void hal_pg_row(uint32_t i, int sel)
{
    const uint32_t y = HP_ROWS_Y + i * PL_UI_ROW_H;
    const uint16_t bg = sel ? ui_accent : OV_BODY;
    if (sel) fb_round_rect_on(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, PL_UI_ROW_H, 5u, bg, OV_BODY);
    else     fb_rect(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, PL_UI_ROW_H, OV_BODY);
    fb_set_color(sel ? UI_PANEL : UI_DIM, bg);
    fb_text_clipped(PL_UI_TEXT_X, y, hal_ctl_name[i], TS_1X, TS_1X, 90u);
    const int32_t v = *hal_ctl_at(i);
    const uint32_t bx = PL_UI_TEXT_X + 94u, bw = 200u, by = y + 5u;
    fb_rect(bx, by, bw, 6u, sel ? ui_mix(ui_accent, UI_PANEL, 1u, 3u) : ui_mix(OV_BODY, UI_DIM, 1u, 3u));
    const uint16_t fill = sel ? UI_PANEL : ui_mix(UI_DIM, UI_WHITE, 1u, 2u);
    if (i == 4u) { if (v > 0) fb_rect(bx, by, (uint32_t)v * 40u, 6u, fill); }                  /* sibilance 0..5: a dip depth, from the left */
    else {
        fb_rect(bx + bw / 2u, by - 2u, 1u, 10u, fill);                                          /* centre mark */
        if (v > 0) fb_rect(bx + bw / 2u, by, (uint32_t)v * 20u, 6u, fill);
        else if (v < 0) fb_rect(bx + bw / 2u - (uint32_t)(-v) * 20u, by, (uint32_t)(-v) * 20u, 6u, fill);
    }
    char t[8], *q = t;
    if (v < 0) { *q++ = '-'; q = ui_dec(q, (uint32_t)(-v)); } else { if (v) *q++ = '+'; q = ui_dec(q, (uint32_t)v); }
    *q = 0;
    const uint32_t w = fb_text_width(t, TS_1X);
    fb_set_color(sel ? UI_PANEL : UI_WHITE, bg);
    fb_text_clipped(PL_UI_X + PL_UI_W - 16u - w, y, t, TS_1X, TS_1X, w + 2u);
}

COLD_FN static void hal_pg_draw(void)
{
    static const char hint[] = "UP DOWN ROW  L R ADJUST  L1 R1 PRESET  X A/B  Y RESET";
    ov_frame("HALCYON", hal_pg_preset_text(), hint);
    if (!hw_hal) {
        fb_set_color(UI_DIM, OV_BODY);
        fb_text_clipped(HP_X, HP_Y + 20u, "NO UNIT", TS_1X, TS_1X, HP_W);
        fb_text_clipped(HP_X, HP_Y + 40u, "THIS BITSTREAM HAS NO HALCYON ENGINE", TS_1X, TS_1X, HP_W);
        return;
    }
    int32_t atten[1];
    hal_pg_plot(atten);
    for (uint32_t i = 0; i < 6u; i++) hal_pg_row(i, i == hal_pg_sel);
    /* footer: what the preamp gives back, and the always-on high-pass */
    char t[40], *q = t;
    const uint32_t tenths = ((uint32_t)atten[0] * 10u) / 8u;                    /* 1/8 dB to 1/10 dB */
    q = set_str(q, "PREAMP -"); q = ui_dec(q, tenths / 10u); *q++ = '.'; q = ui_dec(q, tenths % 10u);
    q = set_str(q, atten[0] || hal_c.warmth || hal_c.bass || hal_c.vocal || hal_c.punch || hal_c.sibilance || hal_c.air ? " DB   17 HZ HIGH-PASS ON" : " DB   FLAT: BYPASS"); *q = 0;
    fb_rect(PL_UI_X + 4u, HP_FOOT_Y - 1u, PL_UI_W - 8u, 16u, OV_BODY);
    fb_set_color(UI_FAINT, OV_BODY);
    fb_text_clipped(PL_UI_TEXT_X, HP_FOOT_Y, t, TS_1X, TS_1X, PL_UI_W - 32u);
    ov_hint_repaint(hint);
}

/* L1/R1: the next / previous preset (never OFF), inside a gain dip, with the clear of a recall. */
COLD_FN static void hal_pg_preset_step(int32_t dir)
{
    const uint32_t n = HAL_NPRESET + hal_user_n;
    uint32_t cur = (hal_sel >= 1u && hal_sel <= n) ? hal_sel : 0u;
    cur = dir > 0 ? (cur % n) + 1u : (cur <= 1u ? n : cur - 1u);
    hal_sel = (uint8_t)cur;
    hal_cmp = 0u;
    hal_pg_load_sel();
    hal_apply_sel_dipped();
}

COLD_FN static int hal_pg_input(uint32_t edge)
{
    if (edge & KEY_START) { set_close(); return 1; }
    if (edge & KEY_B) { set_page = SET_AUDIO; set_dirty = 1u; return 1; }
    if (!hw_hal) return 1;
    if (edge & KEY_UP)   { hal_pg_sel = hal_pg_sel ? (uint8_t)(hal_pg_sel - 1u) : 5u; set_dirty = 1u; }
    if (edge & KEY_DOWN) { hal_pg_sel = (uint8_t)((hal_pg_sel + 1u) % 6u); set_dirty = 1u; }
    if (edge & (KEY_LEFT | KEY_RIGHT)) {
        int8_t *c = hal_ctl_at(hal_pg_sel);
        const int32_t lo = hal_pg_sel == 4u ? 0 : -5;
        int32_t v = *c + ((edge & KEY_RIGHT) ? 1 : -1);
        if (v < lo) { v = lo; }
        if (v > 5) { v = 5; }
        if (v != *c) { *c = (int8_t)v; hal_pg_apply_live(); }
        set_dirty = 1u;
    }
    if (edge & KEY_R1) { hal_pg_preset_step(1); set_dirty = 1u; }
    if (edge & KEY_L1) { hal_pg_preset_step(-1); set_dirty = 1u; }
    if (edge & KEY_X) {                                                   /* A/B against the old EQ */
        if (!hal_cmp) { hal_cmp = 1u; hal_hw_off(); } else hal_pg_apply_live();
        set_dirty = 1u;
    }
    if (edge & KEY_Y) { hal_c.warmth = hal_c.bass = hal_c.vocal = hal_c.punch = hal_c.sibilance = hal_c.air = 0; hal_pg_apply_live(); set_dirty = 1u; }
    return 1;
}
#endif
