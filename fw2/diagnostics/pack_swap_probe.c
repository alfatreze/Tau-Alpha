#pragma GCC optimize ("Os")
#include "pack_swap_probe.h"
#ifdef P2_PACKS
#include "../ui/meter.h"
#include "../ui/meter_pack.h"
#include "../ui/theme.h"
#include "../stream/packfile.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../transport/mp3play.h"
/* The things a host test replaces (sim/test_p2_pack_swap_probe.py): the bundle reader, the window slot and the scratch, the call into the pack, the watchdog, the instruction-cache flush, the counters' section. */
#ifndef PSP_RD
#define PSP_RD packfile_rd
#endif
#ifndef PSP_SLOT_DATA
#define PSP_SLOT_DATA ((volatile uint8_t *)(uintptr_t)((MTR_PACK_SLOT_BASE + PSP_SLOT * MTR_PACK_SLOT_SIZE) | 0x80000000u))
#endif
#ifndef PSP_SCRATCH
#define PSP_SCRATCH ((uint8_t *)(uintptr_t)MTR_PACK_SCRATCH_ORG)
#endif
#ifndef PSP_CALL
#define PSP_CALL(e, in, api) ((mtr_pack_entry_fn)(uintptr_t)(e))(in, api)
#endif
#ifndef PSP_FLUSH
#define PSP_FLUSH() __asm__ volatile (".word 0x0000100f" ::: "memory")       /* fence.i: the CPU decodes it as the instruction cache's flush (VexRiscv_Full.v: IBusCachedPlugin_cache_io_flush from decode_FLUSH_ALL): a lead until a Pocket proves it */
#endif
#ifndef PSP_STATE
#define PSP_STATE __attribute__((section(".psram_state")))
#endif
#ifndef PSP_COLD
#define PSP_COLD P2_COLD_TEXT
#endif
#define PSP_SLOT 7u                                  /* the window: the free pack slot (the registry uses 0..6) */
#define PSP_METERS 7u
#define PSP_MAGIC 0x50535031u                        /* "PSP1": the state below has been cleared */
#define PSP_WD_LIMIT 12000000u                       /* clk cycles the hardware watchdog allows a probe tick (180 ms) */

PSP_STATE uint32_t psp_runs, psp_ok, psp_bad, psp_hang, psp_err, psp_inst_err, psp_inst_cyc, psp_inst_max, psp_flush_cyc, psp_run_cyc, psp_run_max, psp_fill_min, psp_dry, psp_found, psp_last_k;
PSP_STATE static uint32_t magic, scanned, next_k, pk_off[PSP_METERS], ref_calls[PSP_METERS], ref_ret[PSP_METERS], ref_have, calls, run_ret, stat_words[MTR_PACK_STATS];

/* The stub host calls: they count themselves and draw nothing. One function stands in for every pointer of the table (the calls ignore their arguments; the result 0 is what "nothing happened" means for each). */
PSP_COLD static uint32_t psp_stub(void) { calls++; return 0u; }
PSP_COLD static int psp_mb_read(uint32_t a, uint32_t *two) { (void)a; calls++; if (two) *two = 0u; return 0; }

typedef struct { const mpk_info_t *info; const uint16_t *params; } psp_ctx_t;

/* What a pack gets for one tick: a full small box, every band lit by a fixed ramp, force set (it paints everything), the stubs for the host table. The same input every time, so the same pack makes the same calls. */
PSP_COLD static void psp_tick(const void *arg)
{
    const psp_ctx_t *c = (const psp_ctx_t *)arg;
    mtr_in_t in; for (uint32_t i = 0; i < sizeof in; i++) ((uint8_t *)&in)[i] = 0u;
    uint8_t spec[MTR_BANDS]; for (uint32_t i = 0; i < MTR_BANDS; i++) spec[i] = (uint8_t)(200u - i * 12u);                  /* a fixed ramp, the same every time */
    in.spec = spec; in.frame = 7u; in.dt_ms = 25u; in.x = 0u; in.y = 0u; in.w = 368u; in.h = 122u; in.bg = 0u; in.force = 1u; in.peak_l = 12000u; in.peak_r = 9000u;
    mtr_host_api_t api; for (uint32_t i = 0; i < sizeof api; i++) ((uint8_t *)&api)[i] = 0u;
    api.abi = MTR_PACK_ABI; api.role = th_role; api.params = c->params; api.stats = stat_words;
#define STUB(f) api.f = (__typeof__(api.f))(uintptr_t)psp_stub
    STUB(rect); STUB(text); STUB(rect_stash); STUB(blend_ready); STUB(blend_on); STUB(blend_blit); STUB(blend_off); STUB(mb_write); STUB(fence); STUB(blit); STUB(blit_abs); STUB(sblit);
    STUB(bar); STUB(scroll_down); STUB(clut_load); STUB(cblit); STUB(scroll_left);
#undef STUB
    api.mb_read = psp_mb_read;
    calls = 0u;
    run_ret = PSP_CALL(c->info->entry, &in, &api);
}

PSP_COLD void pack_swap_probe_init(void)
{
    psp_runs = psp_ok = psp_bad = psp_hang = psp_err = psp_inst_err = psp_inst_cyc = psp_inst_max = psp_flush_cyc = psp_run_cyc = psp_run_max = psp_dry = psp_found = psp_last_k = 0u;
    psp_fill_min = 0xFFFFFFFFu; scanned = next_k = ref_have = 0u;
    for (uint32_t i = 0; i < PSP_METERS; i++) { pk_off[i] = 0u; ref_calls[i] = ref_ret[i] = 0u; }
    magic = PSP_MAGIC;
}

/* Walks the bundle's pack headers once and notes where each window pack starts (id 0x100 + meter). Uses the scratch as its read window: only called when the live meter will be re-activated anyway. */
PSP_COLD static void scan(void)
{
    scanned = 1u;
    uint8_t *win = PSP_SCRATCH;
    if (!PSP_RD(0, 0u, win, MPKB_HDR) || lib_ld32(win) != MPKB_MAGIC || lib_ld16(win + 4) != 1u) return;
    const uint32_t count = lib_ld16(win + 6);
    uint32_t off = MPKB_HDR;
    for (uint32_t k = 0; k < count; k++) {
        if (!PSP_RD(0, off, win, MPK_HDR) || lib_ld32(win) != MPK_MAGIC) return;
        const uint32_t id = lib_ld16(win + 6), size = lib_ld32(win + 8);
        if (id >= MTR_PACK_PROBE_ID_BASE && id < MTR_PACK_PROBE_ID_BASE + PSP_METERS) { pk_off[id - MTR_PACK_PROBE_ID_BASE] = off + 1u; psp_found++; }     /* +1: 0 means "none" */
        if (size == 0u || size > 0x100000u) return;
        off += MPK_HDR + size;
    }
}

PSP_COLD void pack_swap_probe_run(void)
{
    if (magic != PSP_MAGIC) pack_swap_probe_init();
    if (!scanned) { scan(); meters_pack_probe_done(); }                      /* the scan used the live meter's scratch as its read window */
    if (!psp_found) { psp_err = 1u; return; }
    uint32_t k = next_k, tries = 0u;                                          /* the next meter that has a window pack AND a row to take parameters from */
    while (tries < PSP_METERS && (!pk_off[k] || !meters_present(k) || !meters_def(k)->mv)) { k = (k + 1u) % PSP_METERS; tries++; }
    if (tries == PSP_METERS) { psp_err = 3u; return; }
    next_k = (k + 1u) % PSP_METERS; psp_last_k = k;
    mp3play_io_quiesce();
    const aout_snap_t s0 = aout_snap(); const uint32_t fill = aout_fill(&s0), dry0 = aout_stats.dry + aout_stats.under_ring;
    psp_runs++; if (fill < psp_fill_min) psp_fill_min = fill;
    mpk_info_t info;
    mpkb_rd_t r = { PSP_RD, 0, pk_off[k] - 1u };
    if (!(ref_have & (1u << k))) {                                            /* the oracle: the same pack running from its OWN slot (never rewritten, so nothing stale can be in the cache for it) */
        const mpk_info_t *ni = (const mpk_info_t *)meters_pack_probe_info(k);
        if (ni) {
            mpk_activate(ni, meters_pack_probe_slot(k), (volatile uint8_t *)PSP_SCRATCH);
            psp_ctx_t rc = { ni, meters_def(k)->mv };
            if (!wd_run(PSP_WD_LIMIT, psp_tick, &rc)) { ref_have |= 1u << k; ref_calls[k] = calls; ref_ret[k] = run_ret; }
        }
    }
    const uint32_t t0 = cycles();
    const int e = mpk_install(mpkb_rd, &r, PSP_SCRATCH, PSP_SLOT_DATA, MTR_PACK_SLOT_BASE + PSP_SLOT * MTR_PACK_SLOT_SIZE, MTR_PACK_SLOT_SIZE, MTR_PACK_SCRATCH_ORG, MTR_PACK_SCRATCH_SIZE,
                              MTR_PACK_PROBE_ID_BASE + k, &info);
    const uint32_t t1 = cycles(); psp_inst_cyc = t1 - t0; if (psp_inst_cyc > psp_inst_max) psp_inst_max = psp_inst_cyc;
    if (e) { psp_err = 2u; psp_inst_err = (uint32_t)e; meters_pack_probe_done(); return; }
    PSP_FLUSH();
    const uint32_t t2 = cycles(); psp_flush_cyc = t2 - t1;
    mpk_activate(&info, PSP_SLOT_DATA, (volatile uint8_t *)PSP_SCRATCH);
    psp_ctx_t ctx = { &info, meters_def(k)->mv };
    const int cut = wd_run(PSP_WD_LIMIT, psp_tick, &ctx);
    psp_run_cyc = cycles() - t2; if (psp_run_cyc > psp_run_max) psp_run_max = psp_run_cyc;
    if (cut) { psp_hang++; psp_bad++; }
    else if (!(ref_have & (1u << k))) { ref_have |= 1u << k; ref_calls[k] = calls; ref_ret[k] = run_ret; psp_ok++; }       /* no oracle (the pack does not run from its own slot): the first swapped run is the reference */
    else if (ref_calls[k] == calls && ref_ret[k] == run_ret) psp_ok++;
    else psp_bad++;
    psp_dry += aout_stats.dry + aout_stats.under_ring - dry0;
    meters_pack_probe_done();                                                 /* the scratch and the live meter's state are gone: it is activated again on its next tick, with a forced repaint */
}
#else
uint32_t psp_runs, psp_ok, psp_bad, psp_hang, psp_err, psp_inst_err, psp_inst_cyc, psp_inst_max, psp_flush_cyc, psp_run_cyc, psp_run_max, psp_fill_min, psp_dry, psp_found, psp_last_k;
void pack_swap_probe_init(void) { psp_fill_min = 0xFFFFFFFFu; }
void pack_swap_probe_run(void) { }
#endif
