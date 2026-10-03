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
#include "../../sim/lw_pack_trace.h"

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
static void api_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { lw_hash_rect(x, y, w, h, c); }
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
    if (slots[0].status != MPK_OK) return 0;
    mpk_activate(&slots[0].info, (volatile uint8_t *)PACK_ORG, (volatile uint8_t *)SCRATCH_ORG);
    addr = slots[0].info.entry;
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
    api.accent = &accent; api.role = roles; api.force = &force_flag; api.params = mtr_v_layered_wave;
    api_p = &api; entry = (mtr_pack_entry_fn)addr;
    lw_trace(mtr_v_layered_wave, &force_flag, run, report);
    return 0;
}
