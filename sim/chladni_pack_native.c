/* Native build of the in-tree Chladni (fw/chladni.inc, fw/chladni_core.h) with every firmware service replaced by the traced ones of sim/chladni_pack_trace.h (the same build
 * switch the pack uses, MTR_PACK); runs the shared trace; prints "S <scenario> <commands> <hash>". */
#include <stdint.h>
#include <stdio.h>
#define COLD_FN3
#define COLD_DATA
#define MTR_PACK 1
#define WAVE_COLS 64u
#define SPEC_BANDS 16u
#define FB_STRIDE 512u
#include "theme.h"
#include "meter_gen_enum.h"
#include "meter_module.h"
#include "meter.h"
#include "meters_gen.h"
#include "meter_core.h"
static uint32_t cp_cycles(void); static int cp_mb_write(uint32_t, uint32_t); static int cp_mb_read(uint32_t, uint32_t *);
static void cp_blit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t); static void cp_sblit(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static void cp_wait(void); static void cp_fence(void); static void cp_set_bases(uint32_t, uint32_t); static void cp_toast(const char *); static int cp_afford(void); static uint32_t cp_cpu_pct(void); static int cp_held(void);
static uint32_t cp_hsb;
static uint16_t ui_accent = 0x3C0Fu;
#define CLK_HZ 66666667u
#define cycles() cp_cycles()
#define hs_base cp_hsb
#define FB_HELD() cp_held()
#define meter_afford() cp_afford()
#define ui_cpu_pct() cp_cpu_pct()
#define ui_toast_msg(m) cp_toast(m)
#define fb_blit cp_blit
#define fb_sblit cp_sblit
#define fb_wait() cp_wait()
#define fb_fence() cp_fence()
#define fb_set_bases cp_set_bases
#define chl_mb_write cp_mb_write
#define chl_mb_read cp_mb_read
#include "chladni.inc"
#include "chladni_pack_trace.h"
static uint8_t wviz_force;
static uint32_t run(const mtr_in_t *in) { return (uint32_t)chladni_tick_box(in); }
static void report(int s, uint32_t n, uint64_t h) { printf("S %d %u %016llx\n", s, n, (unsigned long long)h); }
int main(void)
{
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    chl_trace(mtr_v_chladni, &wviz_force, run, report);
    return 0;
}
