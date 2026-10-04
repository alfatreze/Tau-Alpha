/* Native build of the in-tree MASTER VU (fw/vu_master.inc), run over the shared trace of sim/vu_pack_trace.h; prints "S <scenario> <commands> <hash>". */
#include <stdint.h>
#include <stdio.h>
#define COLD_FN3
#define COLD_DATA
#define WAVE_COLS 64u
#define SPEC_BANDS 16u
enum { TS_1X = 0 };
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#include "meters_gen.h"
#include "meter_core.h"
static uint8_t wviz_force;
static void vp_hash_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static void vp_hash_color(uint16_t fg, uint16_t bg);
static void vp_hash_ch(uint32_t x, uint32_t y, char c, uint32_t sx, uint32_t sy);
static uint32_t vp_hash_text(uint32_t x, uint32_t y, const char *s, uint32_t sx, uint32_t sy, uint32_t w);
static char *vp_dec(char *p, uint32_t v);
static uint32_t vp_cell(uint32_t size);
#define fb_rect vp_hash_rect
#define fb_set_color vp_hash_color
#define fb_char vp_hash_ch
#define fb_text_clipped vp_hash_text
#define ui_dec vp_dec
#define FB_CELL(s) vp_cell(s)
#include "vu_master.inc"
#include "vu_pack_trace.h"
static uint32_t run(const mtr_in_t *in) { vum_tick(in); return 1u; }
static void report(int s, uint32_t n, uint64_t h) { printf("S %d %u %016llx\n", s, n, (unsigned long long)h); }
int main(void)
{
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    vu_trace(mtr_v_vu_master, &wviz_force, run, report);
    return 0;
}
