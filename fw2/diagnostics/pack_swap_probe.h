/* Pack-swap probe (diagnostic only; docs/features/meters/EXTERNAL_METERS_PLAN.md, phase P0). The lazy-loading design keeps ONE pack resident in one execution window and reads the others from the bundle when
 * they are selected. This probe measures what that costs on a Pocket, with the player running: on every new track it loads one window-linked pack (a copy of a normal pack linked for the free slot 7, id
 * 0x100 + the meter's id; `tools/player2/build_packs.py --window-probe`, packaged by `package_dev_build.py --pack-swap-probe`) from the bundle into slot 7, flushes the instruction cache, runs one tick of
 * it against stub host calls (the watchdog armed), and checks what it did against what the same pack did the first time (host calls made, value returned). The packs are loaded one after the other in
 * the same slot, so the code at those addresses is replaced every time: wrong code after a swap (a stale instruction cache) shows as BAD, a hang as HANG. The live meter's scratch is the loader's
 * read window, so it is re-activated afterwards (one forced repaint).
 * The counters live in PSRAM (the .psram_state region), not in hot RAM: the Diagnostic Build has no RAM to spare. Report rows "PS ...". Without a PACKS build, or a bundle with no window packs, nothing runs. */
#ifndef P2_PACK_SWAP_PROBE_H
#define P2_PACK_SWAP_PROBE_H
#include <stdint.h>
void pack_swap_probe_init(void);      /* once at boot: clears the counters (the PSRAM state is not zeroed by the loader) */
void pack_swap_probe_run(void);       /* once per new track */
/* psp_err: 1 no bundle or no window packs, 2 the install was refused (the refusal code is in psp_inst_err), 3 no meter row to take parameters from */
extern uint32_t psp_runs, psp_ok, psp_bad, psp_hang, psp_err, psp_inst_err, psp_inst_cyc, psp_inst_max, psp_flush_cyc, psp_run_cyc, psp_run_max, psp_fill_min, psp_dry, psp_found, psp_last_k;
#endif
