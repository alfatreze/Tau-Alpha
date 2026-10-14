#!/usr/bin/env python3
"""fw/pcm_ring.h (gapless G1, B-730): the stream the ring drain writes into the FIFO must equal, sample for sample, what the per-pair gain path gives for the pairs that were not
flushed away, through random put sizes, drain timing, a FIFO that fills and empties, wrap-around of a small ring, flushes (with a re-armed fade-in) and a producer abort. It never
overflows the FIFO (the hardware silently drops a push when full) and holds the fill/room invariants. The reference is an independent per-pair model in the harness."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "%s"
#define FADE 2048u
static uint32_t rs = 777u; static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static uint32_t used, overflow, nout, dac_pct; static uint32_t out[1 << 21];
static void dac(void) { uint32_t d = (rnd() %% 100) < dac_pct ? 1 + rnd() %% 4 : 0; used = used > d ? used - d : 0; }
static uint32_t rd_st(void) { return used & 0xFFFu; }
static uint32_t shadow[1 << 21], sh_w, sh_r;            /* raw pairs in put order; the drain takes them in this order, a flush skips what is left */
static uint32_t drained_raw[1 << 21], flush_at[4096], nflush;
static void wr(uint32_t w) { if (used >= PCM_FIFO_DEPTH) { overflow++; return; } used++; drained_raw[nout] = shadow[sh_r++]; out[nout++] = w; }
static pcm_ring_t Q; static pcm_vol_t V; static uint32_t FL;
static int spins, abort_after;
static int spin(void) { dac(); pcm_ring_drain(&Q, rd_st, wr, &V, &FL, FADE); return abort_after >= 0 && ++spins > abort_after; }
int main(void)
{
    static uint32_t mem[3001]; static int16_t pcm[2304]; long bad = 0, cases = 0, pairs = 0;
    for (int c = 0; c < 300; c++) {
        const uint32_t cap = (c %% 4 == 0) ? 2250u : (c %% 4 == 1) ? 97u : (c %% 4 == 2) ? 2048u : 3000u;   /* non power of two, tiny, FIFO-sized, larger */
        pcm_ring_init(&Q, mem, cap);
        V.cur = V.target = (c %% 5 == 0) ? 32768 : (int32_t)(rnd() %% 32769); V.hw = 0; if (c %% 6 == 0) V.target = (int32_t)(rnd() %% 32769);
        const pcm_vol_t V0 = V; FL = (c %% 3 == 0) ? rnd() %% (FADE + 1) : 0; const uint32_t FL0 = FL;
        used = rnd() %% (PCM_FIFO_DEPTH + 1); overflow = nout = nflush = sh_w = sh_r = 0; dac_pct = 5 + rnd() %% 90; abort_after = -1; spins = 0;
        for (int op = 0; op < 60; op++) {
            const uint32_t a = rnd() %% 10;
            if (a < 6) {                                                     /* a decoded frame */
                const uint32_t stereo = rnd() & 1, n = 1 + rnd() %% 1152;
                for (uint32_t i = 0; i < n * 2; i++) pcm[i] = (int16_t)(rnd() & 0xFFFF);
                for (uint32_t i = 0; i < n; i++) shadow[sh_w++] = pcm_pack(pcm[stereo ? 2 * i : i], stereo ? pcm[2 * i + 1] : pcm[i]);
                if (!pcm_ring_put(&Q, pcm, n, stereo, spin)) { bad++; printf("FAIL put aborted without abort\n"); }
                pairs += n;
            } else if (a < 9) { dac(); pcm_ring_drain(&Q, rd_st, wr, &V, &FL, FADE); }
            else { pcm_ring_flush(&Q); sh_r = sh_w; FL = FADE; used = 0; flush_at[nflush++] = nout; }   /* a flush drops the held pairs, re-arms the fade-in, empties the FIFO */
            if (Q.n > Q.cap || Q.n + pcm_ring_room(&Q) != Q.cap || Q.n != sh_w - sh_r) { bad++; printf("FAIL invariant case %%d op %%d n %%u shadow %%u\n", c, op, Q.n, sh_w - sh_r); break; }
        }
        /* the reference: the per-pair gain path over the pairs that were drained, the fade re-armed at each flush */
        pcm_vol_t RV = V0; uint32_t RF = FL0, fi = 0; int bad_here = overflow != 0;
        for (uint32_t i = 0; i < nout; i++) {
            while (fi < nflush && flush_at[fi] == i) { RF = FADE; fi++; }
            const uint32_t w = drained_raw[i]; int32_t l = (int16_t)(w & 0xFFFFu), r = (int16_t)(w >> 16);
            pcm_gain_apply(&l, &r, &RV, &RF, FADE);
            if (out[i] != pcm_pack(l, r)) { bad_here = 1; break; }
        }
        cases++; if (bad_here && bad++ < 5) printf("FAIL case %%d cap %%u nout %%u overflow %%u\n", c, cap, nout, overflow);
    }
    /* a producer abort on a full ring with a FIFO that never drains: put returns 0 and the ring holds exactly cap pairs */
    pcm_ring_init(&Q, mem, 97u); used = PCM_FIFO_DEPTH; dac_pct = 0; abort_after = 2; spins = 0; sh_w = sh_r = 0;
    for (int i = 0; i < 400; i++) { pcm[i] = (int16_t)i; shadow[sh_w++] = pcm_pack(pcm[i], pcm[i]); }
    cases++; if (pcm_ring_put(&Q, pcm, 200u, 0u, spin) != 0 || Q.n != 97u) { bad++; printf("FAIL abort: n %%u\n", Q.n); }
    printf("%%s: %%ld cases, %%ld pairs, %%ld failures\n", bad ? "FAIL" : "ok", cases, pairs, bad);
    return bad != 0;
}
'''
MUTANTS = [
    ("drain ignores the FIFO room (overflow)", "const uint32_t room = PCM_FIFO_DEPTH - PCM_ST_LEVEL(rd_st());", "const uint32_t room = PCM_FIFO_DEPTH;"),
    ("no wrap of the read index", "if (++q->r == q->cap) q->r = 0u;", "++q->r;"),
    ("no wrap of the write index", "if (++q->w == q->cap) q->w = 0u;", "++q->w;"),
    ("gain skipped at the drain", "pcm_gain_apply(&l, &r, vol, fade_left, fade_samples);", ""),
    ("put overruns a full ring", "while (q->n >= q->cap) { if (spin()) return 0u; }", "while (q->n > q->cap) { if (spin()) return 0u; }"),
    ("count not reduced by the drain", "q->n -= moved;", ""),
]
def run(header_text, tmp):
    src = tmp / "t.c"; exe = tmp / "t"; hdr = tmp / "pcm_ring.h"
    hdr.write_text(header_text); (tmp / "pcm_push.h").write_text((ROOT / "fw/pcm_push.h").read_text())
    src.write_text(HARNESS % str(hdr))
    r = subprocess.run(["cc", "-O1", "-Wall", "-o", str(exe), str(src)], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(exe)], capture_output=True, text=True, timeout=20)
def main():
    base = (ROOT / "fw/pcm_ring.h").read_text()
    with tempfile.TemporaryDirectory() as d:
        r = run(base, Path(d)); sys.stdout.write(r.stdout)
        if r.returncode: sys.exit(1)
        for name, a, b in MUTANTS:
            assert a in base, name
            m = Path(d) / "m"; m.mkdir(exist_ok=True)
            try: r = run(base.replace(a, b, 1), m); caught = r.returncode != 0
            except subprocess.TimeoutExpired: caught = True
            print(("ok   mutant caught: " if caught else "FAIL mutant NOT caught: ") + name)
            if not caught: sys.exit(1)
    print("PASSED")
if __name__ == "__main__": main()
