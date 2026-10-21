/* The reference: a meter's own source compiled straight in and run over the pack trace (sim/p2_pack_trace.h). Built with -DPACK_BARS, -DPACK_VU or -DPACK_SCOPE. Prints "S <scenario> <commands> <hash>" lines. */
#include <stdio.h>
#include <stdint.h>
#ifdef PACK_CHL
#define P2_HOST 1                /* cycles() reads 0: the shedding stays off on both sides */
#define REG_RD(a) 0u
#define REG_WR(a, v) ((void)(a), (void)(v))
#endif
#ifdef PACK_LW
#define P2_HOST 1                /* the PSRAM status words as the dev model returns them; cycles() reads 0 */
#define REG_RD(a) ((a) == 0x80000088u ? 0x50535231u : (a) == 0x800000A0u ? 0x10000u : 0u)
#define REG_WR(a, v) ((void)(a), (void)(v))
static uint32_t lw_state_dummy[700];
#define __psram_state_start lw_state_dummy
#define __psram_state_end (lw_state_dummy + 700)
#endif
#ifdef PACK_SCOPE
#define P2_HOST 1
/* the capture-block model: the same function as tools/p2_dev_model.py */
static uint32_t dm_arms, dm_idx;
static uint32_t dm_data(uint32_t a, uint32_t i)
{
    uint32_t x = a * 0x9E3779B1u + i * 0x85EBCA6Bu + 12345u; x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    const uint32_t mode = a % 4u, sc = mode == 3u ? 32000u : 6000u;
    int32_t m = (int32_t)((x >> 16) % sc), lo = -(int32_t)((x & 0xFFFFu) % sc);
    if (mode == 1u) { lo += 12000; m += 12000; }
    if (mode == 2u) lo = 0;
    if (lo < -32768) lo = -32768; if (lo > 32767) lo = 32767; if (m < -32768) m = -32768; if (m > 32767) m = 32767;
    return ((uint32_t)(lo & 0xFFFF) << 16) | (uint32_t)(m & 0xFFFF);
}
static uint32_t dev_rd(uint32_t a) { if ((a & 0xFFFu) == 0xFCu) return 1u; if ((a & 0xFFFu) == 0xF4u) return dm_data(dm_arms, dm_idx & 255u); return 0u; }
static void dev_wr(uint32_t a, uint32_t v) { if ((a & 0xFFFu) == 0xF0u) dm_idx = v; else if ((a & 0xFFFu) == 0xECu && (v & 2u)) dm_arms++; }
#define REG_RD(a) dev_rd(a)
#define REG_WR(a, v) dev_wr((a), (v))
#endif
#ifdef PACK_BARS
#include "../fw2/ui/meter_winamp_bars.c"
#elif defined(PACK_VU)
#include "../fw2/ui/meter_vu_master.c"
#elif defined(PACK_CHL)
#include "../fw2/ui/meter_chladni.c"
#elif defined(PACK_LW)
#include "../fw2/ui/meter_layered_wave.c"
#else
#include "../fw2/ui/meter_winamp_scope.c"
#endif
#include "p2_pack_trace.h"
uint16_t th_role[TR_COUNT];
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { tr_hash_rect(x, y, w, h, c); }
uint32_t gfx_text(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg) { return tr_hash_text(x, y, s, fg, bg); }
#ifdef PACK_CHL
int gfx_mb_write(uint32_t a, uint32_t d) { return tr_mb_write(a, d); }
int gfx_mb_read(uint32_t a, uint32_t *d) { return tr_mb_read(a, d); }
void gfx_fence(void) { tr_fence(); }
void gfx_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { tr_hash_blit(sx, sy, dx, dy, w, h); }
void gfx_blit_abs(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { tr_hash_blit_abs(sx, sy, dx, dy, w, h); }
void gfx_sblit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t sc) { tr_hash_sblit(sx, sy, dx, dy, w, h, sc); }
#endif
#ifdef PACK_SCOPE
void gfx_rect_stash(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { tr_hash_stash(x, y, w, h, c); }
int gfx_blend_ready(void) { return tr_ready(); }
void gfx_blend_on(uint32_t a) { tr_hash_blend_on(a); }
void gfx_blend_off(void) { tr_hash_blend_off(); }
void gfx_blend_blit(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h) { tr_hash_blit(sx, sy, dx, dy, w, h); }
#endif
static void report(int s, uint32_t n, uint64_t h) { printf("S %d %u %016llx\n", s, n, (unsigned long long)h); }
#ifdef PACK_BARS
static void run(const mtr_in_t *in) { meter_winamp_bars_tick(in); }
#define MV meter_winamp_bars_mv
#define APPLY meter_winamp_bars_apply
#define TRACE bars_trace
#elif defined(PACK_LW)
static void run(const mtr_in_t *in) { meter_layered_wave_tick(in); }
#define MV meter_layered_wave_mv
static void lw_noapply(void) { }
#define APPLY lw_noapply
#define TRACE lw_trace
#elif defined(PACK_CHL)
static void run(const mtr_in_t *in) { meter_chladni_tick(in); }
#define MV meter_chladni_mv
#define APPLY meter_chladni_apply
#define TRACE chl_trace
#elif defined(PACK_VU)
static void run(const mtr_in_t *in) { meter_vu_master_tick(in); }
#define MV meter_vu_master_mv
#define APPLY meter_vu_master_apply
#define TRACE vu_trace
#else
static void run(const mtr_in_t *in) { meter_winamp_scope_tick(in); }
#define MV meter_winamp_scope_mv
#define APPLY meter_winamp_scope_apply
#define TRACE scope_trace
#endif
int main(void)
{
    for (int i = 0; i < TR_COUNT; i++) th_role[i] = (uint16_t)(0x1234u + i * 0x0421u);
    TRACE(MV, APPLY, run, report);
    return 0;
}
