/* Loads a meter pack (the simulator's data file; -DPACK_BARS, -DPACK_VU or -DPACK_SCOPE picks the trace) through fw2/cores/meter_pack_core.h into a slot at PACK_ORG and, if the load is accepted, runs the pack over the shared trace of sim/p2_pack_trace.h.
 * Prints "LOAD E<n>" (single pack) or the bundle lines ("BUNDLE E<n> SEEN <k>", "SLOT <i> E<n>|none") and, on success, "S <scenario> <commands> <hash>" lines. Built by sim/test_p2_meter_pack.py. */
#include "hostio.h"
#ifndef PACK_ORG
#define PACK_ORG 0x00400000u
#endif
#ifndef SCRATCH_ORG
#define SCRATCH_ORG 0x00300000u
#endif
#define MTR_PACK_SLOT_BASE PACK_ORG          /* the simulator has no PSRAM window: slots are plain RAM */
#define MTR_PACK_SCRATCH_ORG SCRATCH_ORG
#include "../../fw2/ui/meter_pack.h"
#include "../../fw2/ui/theme.h"
#include "../../fw2/generated/meter_presets.h"
#ifdef PACK_LW
#include "../../fw2/generated/lw_presets.h"
#endif
#include "../../sim/p2_pack_trace.h"
#ifndef PACK_SLOT
#define PACK_SLOT 0u
#endif
#ifndef PACK_CAP
#define PACK_CAP 0x00040000u
#endif
#ifndef SCRATCH_CAP
#define SCRATCH_CAP 0x00001000u
#endif
#ifndef PACK_METER
#define PACK_METER 0u
#endif
static uint8_t win[LIB_WIN] __attribute__((aligned(16)));
static uint16_t roles[TR_COUNT];
static int rd(void *ctx, uint32_t off, uint8_t *d, uint32_t len) { (void)ctx; if (off + len > hfilesize()) return 0; return hread(off, d, len) == len; }
#ifndef PACK_PARAMS
#define PACK_PARAMS 48u
#endif
static uint32_t api_stats[MTR_PACK_STATS];
static mtr_pack_entry_fn entry;
static mtr_host_api_t api;
static uint16_t params[PACK_PARAMS];
static void run(const mtr_in_t *in) { entry(in, &api); }
static void report(int s, uint32_t n, uint64_t h)
{
    hputs("S "); hputu((uint32_t)s); hputc(' '); hputu(n); hputc(' ');
    const char *d = "0123456789abcdef";
    for (int i = 60; i >= 0; i -= 4) hputc(d[(h >> i) & 0xFu]);
    hputc(' '); hputu(tr_viol); hnl();
}
int main(void)
{
    uint32_t addr = 0;
#ifdef PACK_BUNDLE
    static mpkb_slot_t slots[MTR_PACK_SLOTS];
    static const uint16_t ids[MTR_PACK_SLOTS] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    uint32_t seen = 0;
    const int be = mpkb_install_all(rd, 0, win, hfilesize(), (volatile uint8_t *)PACK_ORG, ids, SCRATCH_ORG, SCRATCH_CAP, slots, &seen);
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
    for (int i = 0; i < TR_COUNT; i++) roles[i] = (uint16_t)(0x1234u + i * 0x0421u);
    api.abi = MTR_PACK_ABI; api.rect = tr_hash_rect; api.role = roles; api.params = params; api.stats = api_stats;
    api.text = tr_hash_text; api.rect_stash = tr_hash_stash; api.blend_ready = tr_ready; api.blend_on = tr_hash_blend_on; api.blend_blit = tr_hash_blit; api.blend_off = tr_hash_blend_off;
    api.mb_write = tr_mb_write; api.mb_read = tr_mb_read; api.fence = tr_fence; api.blit = tr_hash_blit; api.blit_abs = tr_hash_blit_abs; api.sblit = tr_hash_sblit; api.bar = tr_bar_hash; api.scroll_down = tr_scroll_hash;
    entry = (mtr_pack_entry_fn)addr;
#ifdef PACK_BARS
    bars_trace(params, 0, run, report);
#elif defined(PACK_VU)
    vu_trace(params, 0, run, report);
#elif defined(PACK_WF)
    wf_trace(params, 0, run, report);
#elif defined(PACK_CHL)
    chl_trace(params, 0, run, report);
#elif defined(PACK_LW)
    lw_trace(params, 0, run, report);
#else
    scope_trace(params, 0, run, report);
#endif
    return 0;
}
