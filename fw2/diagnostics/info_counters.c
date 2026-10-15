#include "info_counters.h"
#include "../platform/platform.h"

/* Shadow values: written only here (one writer, P2), read through the table. */
static struct { uint32_t uptime_s, loops, loop_max_cyc, pcm_level, pcm_flags, ring_cnt, ring_ridx, ring_wptr, ring_ctrl, gain_cur, cymo_st, keys, stack_peak, image_b, free_b; } v;
uint32_t counters_loops, counters_loop_max_cyc, counters_keys;            /* the loop's own measurements (app writes, this file publishes) */
static uint32_t boot_cycles0;

#define U32(nm, unit, p) { nm, CK_U32, unit, (const volatile void *)(p) }
const cnt_t counters_table[] = {
    { "FIRMWARE",     CK_STR,   CU_NONE,  p2_ver_field + 7 },       /* read through a volatile pointer at run time: the packagers patch these bytes after the build */
    { "CORE VERSION", CK_HEX32, CU_NONE,  &caps.core_version },
    { "CAP RING",     CK_U8,    CU_NONE,  &caps.ring },
    { "CAP GAIN",     CK_U8,    CU_NONE,  &caps.gain },
    { "CAP CYMO",     CK_U8,    CU_NONE,  &caps.cymo },
    { "CAP HALCYON",  CK_U8,    CU_NONE,  &caps.halcyon },
    { "CAP POLY",     CK_U8,    CU_NONE,  &caps.poly },
    { "CAP LPC",      CK_U8,    CU_NONE,  &caps.lpc },
    { "CAP WAVE",     CK_U8,    CU_NONE,  &caps.wave },
    { "CAP SPEC",     CK_U8,    CU_NONE,  &caps.spec },
    { "CAP DBUF",     CK_U8,    CU_NONE,  &caps.dbuf },
    U32("UPTIME",      CU_NONE,   &v.uptime_s),
    U32("LOOPS",       CU_NONE,   &v.loops),
    U32("LOOP MAX",    CU_CYC,    &v.loop_max_cyc),
    U32("KEYS",        CU_NONE,   &v.keys),
    U32("PCM LEVEL",   CU_PAIRS,  &v.pcm_level),
    U32("PCM FLAGS",   CU_NONE,   &v.pcm_flags),
    U32("RING CNT",    CU_PAIRS,  &v.ring_cnt),
    U32("RING RIDX",   CU_NONE,   &v.ring_ridx),
    U32("RING WPTR",   CU_NONE,   &v.ring_wptr),
    { "RING CTRL",    CK_HEX32, CU_NONE,  &v.ring_ctrl },
    U32("GAIN CUR",    CU_NONE,   &v.gain_cur),
    { "CYMO STATUS",  CK_HEX32, CU_NONE,  &v.cymo_st },
    U32("STACK PEAK",  CU_BYTES,  &v.stack_peak),
    U32("IMAGE",       CU_BYTES,  &v.image_b),
    U32("FREE",        CU_BYTES,  &v.free_b),
};
const uint32_t counters_count = sizeof counters_table / sizeof counters_table[0];

void counters_init(void) { boot_cycles0 = cycles(); }

void counters_refresh(void)
{
    v.uptime_s = (cycles() - boot_cycles0) / CLK_HZ;
    v.loops = counters_loops; v.loop_max_cyc = counters_loop_max_cyc; v.keys = counters_keys;
    uint32_t st = REG(R_PCM_ST);
    v.pcm_level = PCM_LEVEL(st); v.pcm_flags = (st >> 16) & 7u;
    v.ring_cnt = REG(R_RING_CNT); v.ring_ridx = REG(R_RING_RIDX); v.ring_wptr = REG(R_RING_WPTR); v.ring_ctrl = REG(R_RING_CTRL);
    v.gain_cur = REG(R_GAIN_CUR); v.cymo_st = REG(R_CYMO_STATUS);
    v.stack_peak = platform_stack_peak(); v.image_b = platform_image_bytes(); v.free_b = platform_free_bytes();
}
