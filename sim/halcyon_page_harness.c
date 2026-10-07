/* B-644: compiles fw/halcyon.inc + fw/halcyon_page.inc (the real page code) against stub drawing primitives and a logging register interface, then drives the page: draws it, moves
 * controls, steps presets, compares, resets. Prints one line per event for sim/test_halcyon_page.py:
 *   R x y w h            a rectangle drawn (every fb_rect and round rect)
 *   W addr value         a write to a Halcyon register
 *   T text               a text string drawn
 *   S sel c0..c5 hal_sel cmp   state after an input
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define COLD_FN
#define COLD_TEXT
#define COLD_DATA
#define TAU_HALCYON_FW 1
#define FB_W 400u
#define FB_H 360u
#define PL_UI_X 8u
#define PL_UI_W (FB_W - 2u * PL_UI_X)
#define PL_UI_ROW_H 22u
#define PL_UI_LIST_Y 52u
#define PL_UI_TEXT_X (PL_UI_X + 16u)
#define TS_1X 1u
#define OV_HEAD_H 28u
#define OV_HINT_H 28u
#define OV_HINT_Y (FB_H - OV_HINT_H)
#define OV_BODY 0x1111u
#define OV_CHROME_BG 0x1212u
#define UI_DIM 0x2222u
#define UI_FAINT 0x3333u
#define UI_WHITE 0xFFFFu
#define UI_PANEL 0x4444u
#define KEY_UP (1u << 0)
#define KEY_DOWN (1u << 1)
#define KEY_LEFT (1u << 2)
#define KEY_RIGHT (1u << 3)
#define KEY_A (1u << 4)
#define KEY_B (1u << 5)
#define KEY_X (1u << 6)
#define KEY_Y (1u << 7)
#define KEY_L1 (1u << 8)
#define KEY_R1 (1u << 9)
#define KEY_START (1u << 15)
#define R_HAL_CTRL 0x80000178u
#define R_HAL_IDX  0x8000017Cu
#define R_HAL_DATA 0x80000180u
static uint32_t mock_reg[256];
#define REG(a) (mock_reg[((a) - 0x80000000u) >> 2])
#define HAL_WR(a, v) do { printf("W %x %x\n", (unsigned)(a), (unsigned)(v)); } while (0)
static uint16_t ui_accent = 0x5555u;
static uint8_t hw_hal = 1, set_dirty, set_page, hal_sel;   /* hal_sel lives in player.c in the firmware */
enum { SET_AUDIO = 2 };
static uint32_t dip_calls;
static void gain_dip_begin(void) { dip_calls++; }
static void gain_dip_end(void) { dip_calls++; }
static void set_close(void) { printf("CLOSE\n"); }
static uint16_t ui_mix(uint16_t a, uint16_t b, uint32_t t, uint32_t n) { (void)b; (void)t; (void)n; return a ^ 0x0101u; }
static char *ui_dec(char *p, uint32_t v) { char t[12]; int n = 0; if (!v) t[n++] = '0'; while (v) { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (n) *p++ = t[--n]; return p; }
static char *set_str(char *p, const char *s) { while (*s) *p++ = *s++; return p; }
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)c; printf("R %u %u %u %u\n", x, y, w, h); }
static void fb_round_rect_on(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t r, uint16_t c, uint16_t b) { (void)r; (void)c; (void)b; printf("R %u %u %u %u\n", x, y, w, h); }
static void fb_set_color(uint16_t f, uint16_t b) { (void)f; (void)b; }
static void fb_text_clipped(uint32_t x, uint32_t y, const char *s, uint32_t a, uint32_t b, uint32_t w) { (void)a; (void)b; printf("T %u %u %u %s\n", x, y, w, s); }
static uint32_t fb_text_width(const char *s, uint32_t a) { (void)a; return (uint32_t)strlen(s) * 7u; }
static void ov_hint_repaint(const char *hint) { printf("H %s\n", hint); }
static void ov_frame(const char *title, const char *right, const char *hint) { printf("F %s|%s|%s\n", title, right, hint); }
/* what halcyon.inc takes from the player */
#include "halcyon.inc"
#include "halcyon_page.inc"
static void st(void)
{
    const int8_t *c = (const int8_t *)&hal_c;
    printf("S %u %d %d %d %d %d %d %u %u\n", hal_pg_sel, c[0], c[1], c[2], c[3], c[4], c[5], hal_sel, hal_cmp);
}
int main(void)
{
    char cmd[16];
    unsigned k;
    hal_user_n = 0;
    hal_pg_open();
    while (scanf("%15s", cmd) == 1) {
        if (!strcmp(cmd, "draw")) { printf("BEGIN draw\n"); hal_pg_draw(); printf("END draw\n"); }
        else if (!strcmp(cmd, "key") && scanf("%u", &k) == 1) { printf("BEGIN key %u\n", k); hal_pg_input(k); st(); printf("END key\n"); }
        else if (!strcmp(cmd, "nounit")) hw_hal = 0;
        else if (!strcmp(cmd, "unit")) hw_hal = 1;
    }
    printf("DIPS %u\n", dip_calls);
    return 0;
}
