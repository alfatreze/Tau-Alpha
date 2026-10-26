#!/usr/bin/env python3
"""fw/pcm_push.h pcm_push_pairs (B-592): the burst push must put EXACTLY the same stream into the FIFO as the per-pair path it replaces, never overflow the FIFO
(the hardware silently drops a push when full), honour an abort, and read the status register far less often. A FIFO model drains at random rates between operations."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "%s"
#define FADE 2048u
static uint32_t rs = 12345u; static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static uint32_t used, drain_pct, st_reads, overflow, waits; static uint32_t out[1 << 22]; static uint32_t nout;
static int abort_after = -1, spins;
static void drain(void) { /* the DAC consumes while we run */ uint32_t d = (rnd() %% 100) < drain_pct ? 1 + rnd() %% 3 : 0; used = used > d ? used - d : 0; }
static uint32_t rd_st(void) { st_reads++; drain(); return (used & 0xFFFu) | (used == 0 ? 1u << 16 : 0) | (used >= PCM_FIFO_DEPTH ? 1u << 17 : 0); }
static void wr(uint32_t w) { if (used >= PCM_FIFO_DEPTH) { overflow++; return; } used++; out[nout++] = w; drain_or_not: ; }
static void wb(void) { waits++; } static void we(void) {}
static int spin(void) { drain(); drain(); return abort_after >= 0 && ++spins > abort_after; }
static void note(uint32_t a, uint32_t b) { (void)a; (void)b; }
static const pcm_hooks_t H = { rd_st, wr, wb, we, spin, note };
int main(void)
{
    long bad = 0, cases = 0;
    static int16_t pcm[2304];
    static uint32_t ref[1 << 22];
    for (int c = 0; c < 400; c++) {
        const uint32_t stereo = c & 1, npairs = (c %% 7 == 0) ? 1 + rnd() %% 60 : 100 + rnd() %% 1052;
        pcm_vol_t vol_st = { 0, 0 }; vol_st.cur = vol_st.target = (c %% 5 == 0) ? 32768 : (int32_t)(rnd() %% 32769); if (c %% 6 == 0) vol_st.target = (int32_t)(rnd() %% 32769);   /* some cases ramp */
        const pcm_vol_t vol0 = vol_st;
        uint32_t fade0 = (c %% 3 == 0) ? rnd() %% (FADE + 1) : 0;
        drain_pct = 5 + rnd() %% 90; used = (c %% 4 == 0) ? 0 : (rnd() %% (PCM_FIFO_DEPTH + 1)); st_reads = overflow = waits = nout = 0; abort_after = -1; spins = 0;
        for (uint32_t i = 0; i < npairs * 2; i++) pcm[i] = (int16_t)(rnd() & 0xFFFF);
        uint32_t f1 = fade0, f2 = fade0; pcm_vol_t vr = vol0;
        for (uint32_t i = 0; i < npairs; i++) {                              /* reference: the per-pair path, no FIFO limits */
            int32_t l = pcm[stereo ? 2 * i : i], r = stereo ? pcm[2 * i + 1] : l;
            pcm_gain_apply(&l, &r, &vr, &f1, FADE); ref[i] = pcm_pack(l, r);
        }
        pcm_vol_t vb = vol0; uint8_t ok = pcm_push_pairs(&H, pcm, npairs, stereo, &vb, &f2, FADE);
        cases++;
        int bad_here = !ok || nout != npairs || overflow || f1 != f2 || vb.cur != vr.cur;
        for (uint32_t i = 0; i < nout && i < npairs; i++) if (out[i] != ref[i]) bad_here = 1;
        if (bad_here && bad++ < 5) printf("FAIL case %%d stereo %%u n %%u ok %%d nout %%u overflow %%u\n", c, stereo, npairs, ok, nout, overflow);
        /* abort: a full FIFO that never drains enough; the pushed pairs must be a prefix of the reference and the call must return 0 */
        drain_pct = 0; used = PCM_FIFO_DEPTH - 3; nout = 0; overflow = 0; abort_after = 3; spins = 0; f2 = fade0;
        vb = vol0; ok = pcm_push_pairs(&H, pcm, npairs, stereo, &vb, &f2, FADE);
        bad_here = (npairs > 3 ? ok != 0 : 0) || overflow;
        for (uint32_t i = 0; i < nout && i < npairs; i++) if (out[i] != ref[i]) bad_here = 1;
        cases++; if (bad_here && bad++ < 5) printf("FAIL abort case %%d ok %%d nout %%u overflow %%u\n", c, ok, nout, overflow);
    }
    /* status reads: a frame of 1152 pairs into a half-empty FIFO needs one read, not 1152 */
    used = 0; drain_pct = 0; st_reads = 0; nout = 0; uint32_t f = 0;
    for (int i = 0; i < 2304; i++) pcm[i] = (int16_t)i;
    { pcm_vol_t vv = { 32768, 32768 }; pcm_push_pairs(&H, pcm, 1152, 1, &vv, &f, FADE); }
    cases++; if (st_reads != 1 || nout != 1152) { bad++; printf("FAIL status reads %%u (want 1) nout %%u\n", st_reads, nout); }
    printf("%%s: %%ld cases, %%ld failures; a 1152-pair frame into an empty FIFO read the status %%u time(s)\n", bad ? "FAIL" : "ok", cases, bad, st_reads);
    return bad != 0;
}
'''
def main():
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; exe = Path(d) / "t"
        src.write_text(HARNESS % str(ROOT / "fw/pcm_push.h"))
        r = subprocess.run(["cc", "-O1", "-Wall", "-Wno-unused-label", "-o", str(exe), str(src)], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode: sys.exit(1)
    print("PASSED")
if __name__ == "__main__": main()
