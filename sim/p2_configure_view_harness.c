/* Host check of fw2/ui/configure_view.c: the strings it draws (a recording gfx) after enter and after key presses, for a two-parameter fake meter. Prints one line per call of "draw": rows as "label=value" joined by '|'. */
#include <stdio.h>
#include <string.h>
#include "../fw2/ui/configure_view.c"
uint16_t th_role[TR_COUNT]; hl_t helios_dummy;
static const char *const ch[] = { "AA", "BB", "CC" };
static const mpd_t pd[4] = { { "BANDS", 0, 0, 4, 16, 1, 0, 0, 255, 0 }, { "EASE", 1, 0, 0, 2, 1, 3, ch, 255, 0 }, { "PEAK", 2, 0, 0, 1, 1, 0, 0, 255, 0 }, { "HOLD", 0, 0, 0, 800, 20, 0, 0, 2, 1 } };
static uint16_t mv[4] = { 16, 0, 1, 200 };
static uint32_t cur_pre; static int custom;
static void tick(const mtr_in_t *i) { (void)i; }
const meter_def_t meter_table[] = { { "FAKE", tick, 3, 0, 0, mv, pd, 4, 0 } };
void meters_ext_enter(uint32_t m) { (void)m; }
uint32_t meter_count = 1; const meter_def_t *meters_def(uint32_t m) { (void)m; return &meter_table[0]; }
const char *meters_param_label(uint32_t m, uint32_t id, char *buf, uint32_t n) { (void)m; (void)buf; (void)n; return pd[id].label; }
const char *meters_param_choice(uint32_t m, uint32_t id, uint32_t val, char *buf, uint32_t n) { (void)m; (void)buf; (void)n; return pd[id].vals[val]; }
static uint32_t curv, ends, begins, begin_arg, remembers, reopen_ret = 0xFFFFFFFFu;          /* the meter on screen; the calls the page makes to leave, remember and reopen a previewed meter */
int meters_cfg_begin(uint32_t row) { begins++; begin_arg = row; curv = row; return 0; }
void meters_cfg_end(void) { ends++; }
void meters_cfg_remember(void) { remembers++; }
uint32_t meters_cfg_reopen(void) { const uint32_t r = reopen_ret; reopen_ret = 0xFFFFFFFFu; return r; }
uint32_t meters_cur(void) { return curv; } uint32_t meters_preset_of(uint32_t m) { (void)m; return cur_pre; }
void meters_preset_select(uint32_t m, uint32_t p) { (void)m; cur_pre = p; custom = 0; mv[0] = 16; mv[1] = 0; mv[2] = 1; mv[3] = 200; }
uint32_t meters_npre(uint32_t m) { (void)m; return 3; }
void meters_params_changed(uint32_t m) { (void)m; custom = 1; }
uint32_t meters_payload(uint32_t m, uint8_t *out) { (void)m; out[0] = 12; return 1; }
void views_report_payload(const uint8_t *p, uint32_t n) { (void)p; (void)n; }
const char *meters_preset_name(void) { return custom ? "CUSTOM" : cur_pre == 0 ? "FLUID" : cur_pre == 1 ? "CLASSIC" : "BOUNCY"; }
void meters_enter(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; }
void meters_tick(int a) { (void)a; }
void helios_dirty(uint8_t id) { (void)id; }
const np_info_t *mp3play_np(void) { static np_info_t n; return &n; }
static char line[1024]; static uint32_t lastx;
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
uint32_t gfx_text_width(const char *s) { return (uint32_t)strlen(s) * 10u; }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg)
{
    (void)fg; (void)bg;
    if (y >= LIST_Y && y < LIST_Y + CF_VIS * ROW_H) { if (x < 100) { if (line[0]) strcat(line, "|"); strcat(line, s); strcat(line, "="); } else strcat(line, s); }
    return x + lastx;
}
static void show(void) { line[0] = 0; configure_view_draw_list(); printf("%s\n", line); }
int main(void)
{
    configure_view_enter(); show();
    configure_view_input(KEY_DOWN); configure_view_input(KEY_RIGHT); show();          /* BANDS 16 -> clamps at 16 */
    configure_view_input(KEY_LEFT); configure_view_input(KEY_LEFT); show();           /* 14 */
    configure_view_input(KEY_DOWN); configure_view_input(KEY_RIGHT); show();          /* EASE AA -> BB */
    configure_view_input(KEY_DOWN); configure_view_input(KEY_RIGHT); show();          /* PEAK off: HOLD row disappears */
    configure_view_input(KEY_UP); configure_view_input(KEY_UP); configure_view_input(KEY_UP); configure_view_input(KEY_RIGHT); show();   /* up to PRESET, next preset */
    printf("exit-before %u\n", ends); configure_view_exit(); printf("exit-after %u\n", ends);                    /* leaving the page asks the registry to bring the set meter back, once */
    curv = 0; configure_view_enter(); { const int r = configure_view_input(KEY_X); printf("export %d remembers %u\n", r, remembers); }       /* X: the Report; the row is remembered for the way back */
    reopen_ret = 5; configure_view_enter(); printf("reopen begins %u arg %u cur %u\n", begins, begin_arg, curv);             /* back from the Report: the remembered meter is loaded again */
    reopen_ret = curv; configure_view_enter(); printf("reopen-same begins %u\n", begins);                                    /* ... unless it is the meter on screen already */
    configure_view_enter(); show(); curv = 9; configure_view_mark(); show(); { const int r = configure_view_input(KEY_DOWN); printf("lost-key %d\n", r); }      /* the page's meter was struck out: the page says so and any key goes back */
    return 0;
}
