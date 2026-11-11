/* Host check of fw2/diagnostics/pack_swap_probe.c (external-meters plan, P0): the real probe source and the real pack loader (cores/meter_pack_core.h) run against a bundle file (argv[1]) with the Pocket
 * stubbed: the bundle reader, the window slot and the scratch (plain buffers), the call into the pack (a function of the pack's entry address, so "the same pack" gives the same answer and a stale instruction
 * cache can be imitated by answering for the previous pack), the watchdog and the instruction-cache flush (counted). Prints one line per scenario. The packs themselves are rv32 code and are not executed here
 * (sim/test_p2_meter_pack.py runs them); this checks the probe's logic: the scan, the round robin, the install into slot 7 byte for byte, the reference comparison, the counters. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define P2_HOST 1
#define P2_PACKS 1
uint32_t clk;
int wd_cut;
#define PSP_STATE
#define PSP_COLD
static uint8_t *bundle; static uint32_t bundle_len;
static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len) { (void)ctx; if (!bundle || off + len > bundle_len) return 0; memcpy(dst, bundle + off, len); return 1; }
#define PSP_RD rd
static uint8_t slot_buf[0x10000], scratch_buf[0x1000];
#define PSP_SLOT_DATA ((volatile uint8_t *)slot_buf)
#define PSP_SCRATCH ((uint8_t *)scratch_buf)
static uint32_t stale, prev_k, flushes, probe_done;
static uint32_t fake_call(uint32_t entry, const void *in, const void *api);
#define PSP_CALL(e, in, api) fake_call((e), (in), (api))
static uint32_t window_k = 0xFFu, after_flush;
#define PSP_FLUSH() (prev_k = window_k, window_k = psp_last_k, after_flush = 1, flushes++)           /* the pack that was in the window until this swap is what stale cache lines would still run */
static uint16_t th_role_tab[8];
#define th_role th_role_tab
#include "../fw2/diagnostics/pack_swap_probe.c"
/* the pack's behaviour: a function of WHICH pack it is (the packs share one entry address in the window, so the address cannot tell them apart; the probe's psp_last_k says which was loaded), or, when `stale` is set,
 * of the pack loaded before it (what stale cache lines would run) */
static uint32_t fake_call(uint32_t entry, const void *in, const void *api)
{
    (void)entry; (void)in; (void)api;
    const uint32_t id = (stale && after_flush) ? prev_k : psp_last_k;     /* only the swapped run can be stale: the oracle runs from the pack's own slot */
    after_flush = 0;
    calls += 3u + id * 5u;
    return 100u + id * 7u;
}
typedef struct { uint32_t cnt, fifo, ridx, wptr; } snap_t;
aout_stats_t aout_stats; aout_snap_t aout_snap(void) { aout_snap_t s = { 0, 0, 100, 5100 }; return s; }
void mp3play_io_quiesce(void) { }
static uint32_t absent_mask; static uint16_t mv_tab[40];
int meters_present(uint32_t row) { return !(absent_mask & (1u << row)); }
static meter_def_t defs[8];
const meter_def_t *meters_def(uint32_t row) { defs[row].mv = mv_tab; return &defs[row]; }
void meters_pack_probe_done(void) { probe_done++; }
static mpk_info_t own_info; static uint8_t own_slot[0x1000];
int no_oracle;
const void *meters_pack_probe_info(uint32_t row) { (void)row; return no_oracle ? 0 : &own_info; }
volatile uint8_t *meters_pack_probe_slot(uint32_t row) { (void)row; return own_slot; }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    fseek(f, 0, SEEK_END); bundle_len = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET); bundle = malloc(bundle_len); if (fread(bundle, 1, bundle_len, f) != bundle_len) return 2; fclose(f);
    const int mode = argc > 2 ? atoi(argv[2]) : 0;
    pack_swap_probe_init();
    if (mode == 5) no_oracle = 1;                                           /* no pack runs from its own slot: the first swapped run is the reference */
    if (mode == 3) absent_mask = 0x7Fu;                                   /* no meter row at all */
    if (mode == 4) { for (uint32_t i = 0; i < 200; i++) bundle[bundle_len - 1 - i] ^= 0x5A; }       /* damage the tail: the last pack's image (sound map's window copy) */
    for (uint32_t i = 0; i < 14; i++) {
        if (mode == 1 && i == 9) stale = 1;
        if (mode == 6 && i == 2) stale = 1;                               /* stale on the FIRST sight of a pack: the oracle (its own slot) still says what it should do */                               /* a stale instruction cache: this swap runs the code of the pack before it */
        if (mode == 2 && i == 5) wd_cut = 1;                              /* the watchdog cuts one probe tick */
        pack_swap_probe_run();
        stale = 0; wd_cut = 0;
        printf("run %u k %u ok %u bad %u hang %u err %u inst_err %u found %u done %u flush %u\n", i, psp_last_k, psp_ok, psp_bad, psp_hang, psp_err, psp_inst_err, psp_found, probe_done, flushes);
    }
    /* the window slot holds the LAST pack installed, byte for byte */
    if (mode == 0 && psp_found) { const uint32_t o = pk_off[psp_last_k] - 1u, size = lib_ld32(bundle + o + 8); printf("slot_match %d size %u\n", memcmp((const void *)slot_buf, bundle + o + 48, size) == 0, size); }
    printf("fill_min %u dry %u inst_cyc_nonzero %d run_cyc_nonzero %d\n", psp_fill_min, psp_dry, psp_inst_cyc != 0, psp_run_cyc != 0);
    return 0;
}
