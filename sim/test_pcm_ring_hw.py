#!/usr/bin/env python3
"""fw/pcm_ring_hw.h (the CPU side of the hardware PCM ring, B-746) against a model of the reader's contract (src/fpga/core/tau_pcm_ring.sv): the reader sees ONLY pairs up to the
published write index and reads slot ridx while ridx != published. Over random put sizes (stereo and mono, 1 to 2304 pairs), a small ring that wraps, a reader that advances a
random amount between and during puts, and pairs put one at a time: the stream the reader delivers equals the pairs put, in order, none lost, twice or stale; a slot that is still
unread is never overwritten (the ring never fills completely); an aborted wait leaves everything stored so far published; the lead arithmetic equals unread plus FIFO."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include "%s"
#define SZ 97u
#define MARGIN 9u
static uint32_t slot[SZ]; static uint8_t occ[SZ];
static uint32_t ridx, pub_idx, pend_reads;
static uint32_t rs = 4242u; static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static uint32_t got[1 << 21], ng; static uint32_t want[1 << 21], nw;
static int overwrite, stale, abort_after = -1, spins;
static uint32_t rd_ridx(void) { return ridx; }
static int reader_off;
static void reader(uint32_t k) { if (reader_off) return; while (k-- && ridx != pub_idx) { if (!occ[ridx]) stale++; got[ng++] = slot[ridx]; occ[ridx] = 0; if (++ridx == SZ) ridx = 0; } }
static void wr_word(uint32_t i, uint32_t w) { { uint32_t d = (ridx + SZ - i) %% SZ; if (d >= 1 && d <= MARGIN && ng > 0) overwrite++; }   /* the margin behind the read index is never touched */
    if (occ[i]) overwrite++; slot[i] = w; occ[i] = 1; want[nw++] = w; }
static void wr_wptr(uint32_t i) { pub_idx = i; reader(rnd() %% 9); }   /* the hardware runs concurrently: it may read right after the index moves */
static int spin(void) { reader(1 + rnd() %% 40); return abort_after >= 0 && ++spins > abort_after; }
static int spin_stalled(void) { return ++spins > abort_after; }   /* the reader does not move */
static const prh_hooks_t H = { rd_ridx, wr_word, wr_wptr, spin };
int main(void)
{
    static int16_t pcm[4608]; prh_t r; long bad = 0, cases = 0;
    prh_init(&r, SZ, MARGIN, 0);
    for (int c = 0; c < 2000; c++) {
        const uint32_t act = rnd() %% 10;
        if (act < 6) {
            const uint32_t st = rnd() & 1, n = 1 + rnd() %% 2304;
            for (uint32_t i = 0; i < n * (st ? 2u : 1u); i++) pcm[i] = (int16_t)(rnd() & 0xFFFF);
            uint32_t w0 = nw;
            if (!prh_put(&r, &H, pcm, n, st)) { bad++; printf("FAIL put aborted\n"); }
            for (uint32_t i = 0; i < n; i++) {                            /* what the words must have been */
                int32_t l = pcm[st ? 2 * i : i], rr = st ? pcm[2 * i + 1] : l;
                if (want[w0 + i] != (((uint32_t)(uint16_t)(int16_t)rr << 16) | (uint32_t)(uint16_t)(int16_t)l)) { bad++; printf("FAIL word %%u\n", i); break; }
            }
            cases++;
        } else if (act < 9) {                                              /* single pairs, publishing every 8th */
            for (uint32_t i = 0; i < 20; i++) { int32_t l = (int32_t)(rnd() %% 60000) - 30000, rr = (int32_t)(rnd() %% 60000) - 30000; if (!prh_put1(&r, &H, l, rr, (i & 7) == 7)) bad++; }
            wr_wptr(r.w);
        } else reader(rnd() %% 300);
        if (pub_idx != r.w && act < 6) { bad++; printf("FAIL index not published after a put\n"); }
        if (prh_lead(&r, ridx, 100) != prh_fill(&r, ridx) + 100) bad++;
    }
    wr_wptr(r.w); reader(1u << 30);                                         /* drain the rest */
    uint32_t total = ng;
    if (ng != nw) { bad++; printf("FAIL delivered %%u of %%u\n", ng, nw); }
    for (uint32_t i = 0; i < ng && i < nw; i++) if (got[i] != want[i]) { bad++; printf("FAIL stream at %%u\n", i); break; }
    if (overwrite || stale) { bad++; printf("FAIL overwrite %%d stale %%d\n", overwrite, stale); }
    /* an aborted wait: the reader never moves, the ring fills, spin aborts after 3 calls; what was stored is published and nothing was lost */
    prh_init(&r, SZ, MARGIN, 0); ridx = 0; pub_idx = 0; nw = ng = 0; for (int i = 0; i < (int)SZ; i++) occ[i] = 0; spins = 0; abort_after = 3; reader_off = 1;
    for (uint32_t i = 0; i < 400; i++) pcm[i] = (int16_t)i;
    {
        prh_hooks_t hs = { rd_ridx, wr_word, wr_wptr, spin_stalled };
        uint8_t ok = prh_put(&r, &hs, pcm, 200u, 0u);
        if (ok || pub_idx != r.w || prh_fill(&r, ridx) != SZ - 1u - MARGIN || nw != SZ - 1u - MARGIN) { bad++; printf("FAIL abort: ok %%d pub %%u w %%u fill %%u stored %%u\n", ok, pub_idx, r.w, prh_fill(&r, ridx), nw); }
    }
    printf("%%s: %%ld puts, %%u pairs, %%d failures\n", bad ? "FAIL" : "ok", cases, total, (int)bad);
    return bad != 0;
}
'''
MUTANTS = [("room ignores the unused slot", "r->size - 1u - r->margin - f;", "r->size - r->margin - f;"),
           ("room ignores the margin", "return f + r->margin + 1u >= r->size ? 0u : r->size - 1u - r->margin - f;", "return f + 1u >= r->size ? 0u : r->size - 1u - f;"),
           ("index published before the data", "h->wr_word(r->w, ((uint32_t)(uint16_t)(int16_t)rr << 16) | (uint32_t)(uint16_t)(int16_t)l);\n        if (++r->w == r->size) r->w = 0u;\n        room--;", "h->wr_wptr((r->w + 1u) % r->size); h->wr_word(r->w, ((uint32_t)(uint16_t)(int16_t)rr << 16) | (uint32_t)(uint16_t)(int16_t)l);\n        if (++r->w == r->size) r->w = 0u;\n        room--;"),
           ("the tail of a put is never published", "    if (pending) h->wr_wptr(r->w);\n    return 1u;", "    return 1u;"),
           ("mono not duplicated", "const int32_t rr = stereo ? pcm[2u * done + 1u] : l;", "const int32_t rr = stereo ? pcm[2u * done + 1u] : 0;"),
           ("no wrap of the write index", "if (++r->w == r->size) r->w = 0u;\n        room--;", "++r->w;\n        room--;")]

HARNESS_B = r"""
#include <stdio.h>
#include <stdlib.h>
#include "%s"
#define SZ 211u
#define DEPTH 30u            /* the FIFO model's depth: pairs it can hold */
#define MAXN 8u              /* the meter window */
#define MARGIN 41u           /* DEPTH + MAXN + slack, as RG_MARGIN = 2048 + 1152 + 128 */
static uint32_t rs = 99u; static uint32_t rnd(void) { rs = rs * 1664525u + 1013904223u; return rs >> 8; }
static uint32_t slot[SZ];                    /* the id (a running number) stored in each slot */
static uint32_t pushed[1 << 20];             /* pushed[p] = the id of the p-th pair pushed (RING_CNT = p + 1 after it) */
static uint32_t cnt, ridx, wid, fed, level;  /* wid = the next id to store */
static prh_t r;
int main(void)
{
    long bad = 0, feeds = 0, flushes = 0;
    prh_init(&r, SZ, MARGIN, 0); ridx = 0; cnt = 0; fed = 0; level = 0; wid = 0;
    for (int it = 0; it < 400000; it++) {
        const uint32_t a = rnd() %% 1000;
        if (a < 400) {                                          /* the CPU puts one pair if there is room (margin included) */
            if (prh_room(&r, ridx)) { slot[r.w] = wid++; if (++r.w == SZ) r.w = 0u; }
        } else if (a < 780) {                                   /* the reader pushes one pair into the FIFO if there is data and room */
            if (ridx != r.w && level < DEPTH) { pushed[cnt] = slot[ridx]; cnt++; level++; if (++ridx == SZ) ridx = 0u; }
        } else if (a < 920) {                                   /* the DAC releases a pair */
            if (level) level--;
        } else if (a < 921) {                                   /* a flush (rare, so the ring also runs full): unread pairs dropped, FIFO emptied; the firmware moves fed to cnt */
            ridx = r.w; level = 0; fed = cnt; flushes++;
        } else {                                                /* a meter feed */
            uint32_t pos, nf; const uint32_t n = prh_meter_span(cnt, level, fed, ridx, SZ, MAXN, &pos, &nf);
            const uint32_t heard = cnt - level;
            if (n > MAXN) { bad++; printf("FAIL n %%u > max\n", n); }
            if (n && nf != heard && (heard - fed) <= MAXN) { bad++; printf("FAIL new_fed %%u != heard %%u\n", nf, heard); }
            const uint32_t first = nf - n;                      /* the number of the first pair fed */
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t want = pushed[first + i], got = slot[(pos + i) %% SZ];
                if (got != want) { bad++; if (bad < 6) printf("FAIL feed pair %%u: slot holds %%u, wanted %%u (it %%d)\n", first + i, got, want, it); break; }
            }
            if (nf != fed) fed = nf;
            feeds++;
        }
    }
    /* the clock helper */
    if (prh_heard_pos(100u, 30u) != 70u || prh_heard_pos(30u, 30u) != 0u || prh_heard_pos(10u, 30u) != 0u || prh_heard_pos(0u, 0u) != 0u) { bad++; printf("FAIL heard_pos\n"); }
    /* the natural-end wait: reload leaves; paused waits; done only when both empty; timeout leaves */
    for (uint32_t m = 0; m < 32u; m++) {
        const uint32_t rl = m & 1, pa = (m >> 1) & 1, re = (m >> 2) & 1, fe = (m >> 3) & 1, to = (m >> 4) & 1;
        const uint32_t got = prh_playout_step(rl, pa, re, fe, to ? 100u : 1u, 50u);
        const uint32_t want = rl ? 2u : pa ? 0u : (re && fe) ? 1u : to ? 2u : 0u;
        if (got != want) { bad++; printf("FAIL playout_step mask %%u: %%u vs %%u\n", m, got, want); }
    }
    printf("%%s: %%ld meter feeds, %%ld flushes, %%d failures\n", bad ? "FAIL" : "ok", feeds, flushes, (int)bad);
    return bad != 0;
}
"""
MUTANTS_B = [("meter read-back ignores the heard offset (level)", "const uint32_t heard = cnt - level;", "const uint32_t heard = cnt;"),
             ("slots behind the read index may be overwritten (margin 0)", "return f + r->margin + 1u >= r->size ? 0u : r->size - 1u - r->margin - f;", "return f + 1u >= r->size ? 0u : r->size - 1u - f;"),
             ("window not clamped to max_n", "if ((uint32_t)n > max_n) { fed = heard - max_n; n = (int32_t)max_n; }", ""),
             ("slot position counted from the wrong side", "*pos = ridx >= back ? ridx - back : ridx + size - back;", "*pos = ridx + back < size ? ridx + back : ridx + back - size;"),
             ("the clock may go negative", "return decoded_pairs > lead ? decoded_pairs - lead : 0u;", "return decoded_pairs - lead;"),
             ("a pause does not hold the natural-end wait", "if (paused) return 0u;", ""),
             ("a reload request does not leave the wait", "if (reload_pending) return 2u;", "")]
def run_b(hdr_text, tmp):
    (tmp / "pcm_ring_hw.h").write_text(hdr_text); (tmp / "tb.c").write_text(HARNESS_B % str(tmp / "pcm_ring_hw.h"))
    r = subprocess.run(["cc", "-O1", "-w", "-o", str(tmp / "tb"), str(tmp / "tb.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    try: return subprocess.run([str(tmp / "tb")], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        from types import SimpleNamespace
        return SimpleNamespace(returncode=1, stdout="timeout\n")

def run(hdr_text, tmp):
    (tmp / "pcm_ring_hw.h").write_text(hdr_text); (tmp / "t.c").write_text(HARNESS % str(tmp / "pcm_ring_hw.h"))
    r = subprocess.run(["cc", "-O1", "-w", "-o", str(tmp / "t"), str(tmp / "t.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    try: return subprocess.run([str(tmp / "t")], capture_output=True, text=True, timeout=20)
    except subprocess.TimeoutExpired:
        from types import SimpleNamespace
        return SimpleNamespace(returncode=1, stdout="timeout\n")
def main():
    base = (ROOT / "fw/pcm_ring_hw.h").read_text(); fails = 0
    with tempfile.TemporaryDirectory() as d:
        d = Path(d); r = run(base, d); sys.stdout.write(r.stdout); fails += r.returncode != 0
        r = run_b(base, d); sys.stdout.write(r.stdout); fails += r.returncode != 0
        for name, a, b in MUTANTS_B:
            assert a in base, name
            m = d / "mb"; m.mkdir(exist_ok=True)
            caught = run_b(base.replace(a, b, 1), m).returncode != 0
            print(("ok   mutant caught: " if caught else "FAIL mutant NOT caught: ") + name); fails += not caught
        for name, a, b in MUTANTS:
            assert a in base, name
            m = d / "m"; m.mkdir(exist_ok=True)
            caught = run(base.replace(a, b, 1), m).returncode != 0
            print(("ok   mutant caught: " if caught else "FAIL mutant NOT caught: ") + name); fails += not caught
    print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
if __name__ == "__main__": main()
