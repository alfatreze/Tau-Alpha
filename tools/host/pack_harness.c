/* Loads a meter pack (the simulator's data file) through fw/meter_pack_core.h into a slot at PACK_ORG and, if the load is accepted, runs the pack over the
 * shared trace of sim/lw_pack_trace.h. Prints "LOAD E<n>" and, on success, "S <scenario> <commands> <hash>" lines. Built by sim/test_meter_pack.py. */
#include "hostio.h"
#ifndef PACK_ORG
#define PACK_ORG 0x00400000u
#endif
#ifndef SCRATCH_ORG
#define SCRATCH_ORG 0x00300000u
#endif
#define MTR_PACK_SLOT_BASE PACK_ORG          /* the simulator has no PSRAM window: slots are plain RAM */
#define MTR_PACK_SCRATCH_ORG SCRATCH_ORG
#define COLD_DATA
#define WAVE_COLS 64u
#include "../../fw/theme.h"
#include "../../fw/meter_gen_enum.h"
#include "../../fw/meter_module.h"
#include "../../fw/meters_gen.h"
#include "../../fw/meter_pack_core.h"
#ifdef PACK_BARS
#include "../../sim/bars_pack_trace.h"       /* Winamp Bars: -DPACK_BARS -DPACK_METER=12 */
#elif defined(PACK_SCOPE)
#include "../../sim/scope_pack_trace.h"      /* Winamp Scope: -DPACK_SCOPE -DPACK_METER=13 */
#else
#include "../../sim/lw_pack_trace.h"
#endif
#ifndef PACK_SLOT
#define PACK_SLOT 0u                            /* the bundle slot whose pack is activated and run */
#endif

#ifndef PACK_CAP
#define PACK_CAP 0x00040000u
#endif
#ifndef SCRATCH_CAP
#define SCRATCH_CAP 0x00001000u
#endif
#ifndef PACK_METER
#define PACK_METER 16u
#endif
static uint8_t win[LIB_WIN] __attribute__((aligned(16)));
static uint16_t accent;
static uint8_t force_flag;

static int rd(void *ctx, uint32_t off, uint8_t *d, uint32_t len)
{
    (void)ctx;
    if (off + len > hfilesize()) return 0;
    return hread(off, d, len) == len;
}
#ifdef PACK_SCOPE
static void api_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { sp_hash_rect(x, y, w, h, c); }
static void api_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg) { (void)x; (void)y; (void)w; (void)h; (void)lit; (void)fg; (void)bg; }
#elif defined(PACK_BARS)
static void api_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { bp_hash_rect(x, y, w, h, c); }
static void api_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg) { bp_hash_bar(x, y, w, h, lit, fg, bg); }
#else
static void api_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { lw_hash_rect(x, y, w, h, c); }
static void api_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg) { (void)x; (void)y; (void)w; (void)h; (void)lit; (void)fg; (void)bg; }
#endif
static int api_bar_ready(void) { return 1; }
static uint32_t api_stats[MTR_PACK_STATS];
static uint32_t api_cycles(void) { return 0u; }
static int api_psram(void) { return 1; }
static mtr_pack_entry_fn entry;
static const mtr_host_api_t *api_p;
static uint32_t run(const mtr_in_t *in) { return entry(in, api_p); }
static void report(int s, uint32_t n, uint64_t h)
{
    hputs("S "); hputu((uint32_t)s); hputc(' '); hputu(n); hputc(' ');
    const char *d = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) hputc(d[(h >> i) & 0xFu]);
    hnl();
}

int main(void)
{
    uint32_t addr = 0;
#ifdef PACK_BUNDLE
    static mpkb_slot_t slots[MTR_PACK_SLOTS];
    uint32_t seen = 0;
    const int be = mpkb_install_all(rd, 0, win, hfilesize(), (volatile uint8_t *)PACK_ORG, SCRATCH_ORG, SCRATCH_CAP, slots, &seen);
    hputs("BUNDLE E"); hputu((uint32_t)be); hputs(" SEEN "); hputu(seen); hnl();
    for (uint32_t i = 0; i < MTR_PACK_SLOTS; i++) { hputs("SLOT "); hputu(i); if (slots[i].status == 0xFFu) hputs(" none"); else { hputs(" E"); hputu(slots[i].status); } hnl(); }
    if (slots[PACK_SLOT].status != MPK_OK) return 0;
    mpk_activate(&slots[PACK_SLOT].info, (volatile uint8_t *)PACK_ORG + PACK_SLOT * MTR_PACK_SLOT_SIZE, (volatile uint8_t *)SCRATCH_ORG);
    addr = slots[PACK_SLOT].info.entry;
#else
    const int e = mpk_load(rd, 0, win, (volatile uint8_t *)PACK_ORG, PACK_ORG, PACK_CAP, (volatile uint8_t *)SCRATCH_ORG, SCRATCH_ORG, SCRATCH_CAP, PACK_METER, &addr);
    hputs("LOAD E"); hputu((uint32_t)e); hnl();
    if (e) return 0;
#endif
    accent = 0x3C0Fu;
    static uint16_t roles[TR_COUNT];
    for (int i = 0; i < TR_COUNT; i++) roles[i] = (uint16_t)(0x1234u + i * 0x0421u);
    static mtr_host_api_t api;
    api.abi = MTR_PACK_ABI; api.fb_rect = api_rect; api.cycles = api_cycles; api.psram_ready = api_psram;
    api.stats = api_stats; api.clk_hz = 66666667u;
    api.rect_clip = api_rect; api.bar_clip = api_bar; api.bar_ready = api_bar_ready;
    api.accent = &accent; api.role = roles; api.force = &force_flag;
#ifdef PACK_SCOPE
    static uint8_t grad_flag, fs_flag;
    api.grad = &grad_flag; api.fullscreen = &fs_flag; api.bg_restore = sp_hash_restore; api.bg_blend = sp_hash_blend; api.scope_note = sp_hash_note;
    api.params = mtr_v_winamp_scope;
    api_p = &api; entry = (mtr_pack_entry_fn)addr;
    scope_trace(mtr_v_winamp_scope, &force_flag, &grad_flag, &fs_flag, run, report);
#elif defined(PACK_BARS)
    api.params = mtr_v_winamp_bars;
    api_p = &api; entry = (mtr_pack_entry_fn)addr;
    bars_trace(mtr_v_winamp_bars, &force_flag, run, report);
#else
    api.params = mtr_v_layered_wave;
    api_p = &api; entry = (mtr_pack_entry_fn)addr;
    lw_trace(mtr_v_layered_wave, &force_flag, run, report);
#endif
#ifdef PACK_STATS
    hputs("STATS "); hputu(api_stats[0]); hputc(' '); hputu(api_stats[3]); hnl();
#endif
    return 0;
}
