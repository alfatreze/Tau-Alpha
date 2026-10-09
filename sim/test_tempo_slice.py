#!/usr/bin/env python3
"""RAM diet (docs/features/RAM_DIET_PLAN.md): the tempo core's sliced output (ws2_step_begin/_emit/_end) and the 512-entry stage-2 ring must give
the SAME samples as the whole-hop call with the 1024-entry ring. Real fw/wsola_core.h compiled by sim/wsola2_harness.c; inputs are synthetic voice-like signals;
mutants (a slice that ignores its offset, a tail read at the wrong place, a ring that is too small) must each be caught. Red then green."""
import random, subprocess, sys, tempfile, struct, math
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info and not ok else ""))
    if not ok: fails += 1

def signal(n, fs, seed):
    rnd = random.Random(seed); out = []; ph = 0.0
    for i in range(n):
        f0 = 120 + 40 * math.sin(i / fs * 3.1)
        ph += 2 * math.pi * f0 / fs
        v = sum(math.sin(k * ph) / k for k in range(1, 6)) * (0.5 + 0.5 * math.sin(i / fs * 5.7)) * 9000 + rnd.randint(-300, 300)
        out.append(max(-32768, min(32767, int(v))))
    return out

def build(td, name, extra, src_root=ROOT):
    exe = td / name
    r = subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", *extra, "-o", str(exe), str(src_root / "sim/wsola2_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); raise SystemExit("build failed " + name)
    return exe

def run(exe, td, chans, fs, spd, chunk, slice_=0):
    n = len(chans[0]); raw = bytearray()
    for i in range(n):
        for c in chans: raw += struct.pack("<h", c[i])
    (td / "in.raw").write_bytes(bytes(raw))
    r = subprocess.run([str(exe), str(td / "in.raw"), str(td / "out.raw"), str(fs), str(len(chans)), str(spd), str(chunk), str(slice_)], capture_output=True, text=True)
    macs, grains, overrun = (int(v) for v in r.stderr.split())
    return (td / "out.raw").read_bytes(), grains, overrun

with tempfile.TemporaryDirectory() as t:
    td = Path(t)
    whole = build(td, "r1024", [])
    r512 = build(td, "r512", ["-DWS2_RING=512u"])
    cases = [("mono 44.1k", 44100, 1, 384), ("stereo 48k", 48000, 2, 512), ("mono 22.05k", 22050, 1, 256), ("stereo 44.1k 1.5x", 44100, 2, 384), ("mono 16k 3.0x", 16000, 1, 768)]
    for name, fs, nch, spd in cases:
        base = signal(fs * 3, fs, 7)
        chans = [base] if nch == 1 else [base, signal(fs * 3, fs, 11)]
        ref, g, ov = run(whole, td, chans, fs, spd, 64)
        check(f"{name}: reference run produced grains, no overrun", g > 10 and ov == 0, f"grains {g} overrun {ov}")
        for sl in (64, 1, 7, 100, 512):
            got, g2, ov2 = run(whole, td, chans, fs, spd, 64, sl)
            check(f"{name}: slice {sl} == whole hop", got == ref and g2 == g)
        got, g3, ov3 = run(r512, td, chans, fs, spd, 64)
        check(f"{name}: ring 512 == ring 1024", got == ref and ov3 == 0, f"overrun {ov3}")
        got, g4, ov4 = run(r512, td, chans, fs, spd, 64, 64)
        check(f"{name}: ring 512 + slice 64 == reference", got == ref and ov4 == 0)

    # mutants of the sliced path / the ring (the reference stays the unmutated whole-hop run)
    src = (ROOT / "fw/wsola_core.h").read_text()
    muts = [("slice ignores its offset in the Hann window", "ws_hann_q15[(a + i) * stp]", "ws_hann_q15[i * stp]"),
            ("slice ignores its offset in the tail", "c->pend[ch][a + i]", "c->pend[ch][i]"),
            ("slice reads the grain at the hop start", "rd(ctx, ch, cg + a, n, outs[ch]);", "rd(ctx, ch, cg, n, outs[ch]);"),
            ("first grain slice reads from 0", "for (uint32_t ch = 0; ch < c->ch; ch++) rd(ctx, ch, a, n, outs[ch]);", "for (uint32_t ch = 0; ch < c->ch; ch++) rd(ctx, ch, 0u, n, outs[ch]);"),
            ("end forgets the new tail", "for (uint32_t ch = 0; ch < c->ch; ch++) rd(ctx, ch, cg + Hs, Hs, c->pend[ch]);\n    c->prev = cg;", "c->prev = cg;")]
    base = signal(44100 * 3, 44100, 7); chans = [base, signal(44100 * 3, 44100, 11)]
    ref, g, ov = run(whole, td, chans, 44100, 384, 64)
    for name, a, b in muts:
        assert a in src, a
        md = td / "mut"; (md / "fw").mkdir(parents=True, exist_ok=True); (md / "sim").mkdir(exist_ok=True)
        (md / "fw/wsola_core.h").write_text(src.replace(a, b, 1))
        (md / "fw/wsola_tables.h").write_text((ROOT / "fw/wsola_tables.h").read_text())
        (md / "sim/wsola2_harness.c").write_text((ROOT / "sim/wsola2_harness.c").read_text())
        mexe = build(td, "mutexe", [], md)
        got, _, _ = run(mexe, td, chans, 44100, 384, 64, 64)
        check(f"mutant caught: {name}", got != ref)
    # a ring that is too small must show up as an overrun, not as silent wrong audio
    r256 = build(td, "r256", ["-DWS2_RING=256u"])
    got, g5, ov5 = run(r256, td, chans, 44100, 384, 64)
    check("mutant caught: ring 256 is too small (overrun reported)", ov5 > 0 or got != ref)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
