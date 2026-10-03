#!/usr/bin/env python3
"""Host test for the fixed-point WSOLA core (Cymo C7, B-549): fw/wsola_core.h must match the independent integer twin tools/lab/wsola_fixed_ref.py sample for sample.
Core v2 (B-556, the streaming form: input decimated once into a ring, full-rate samples through a reader) must give the same output as v1 and as its own integer twin for
any chunk size. The search is the three-stage one of B-550 (32x / 8x / full rate). Cases: mono 44.1 kHz at 0.8x, 1.25x, 1.5x, 2.0x and 3.0x; stereo; mono 22.05 kHz (the 4x decimation / 512-sample grain); a speed outside the allowed range (clamped).
Also: the table header equals what the generator emits, the output length follows the speed, identical channels give identical output, and seven deliberately
broken copies of the C core must each FAIL the comparison (the test can see what it claims to check). With numpy present the pitch is checked too."""
import math, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/lab"))
import wsola_fixed_ref as ref

fails = 0


def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f"  {info}" if info and not cond else ""))
    fails += 0 if cond else 1


def voice(n, fs, seed=1, drift=True):
    """A deterministic 'voice': harmonics of a drifting pitch with a slow envelope and a little noise, as int16 samples."""
    sin_t = [int(round(math.sin(2 * math.pi * i / 4096) * 32767)) for i in range(4096)]
    out, ph, lcg = [], 0, seed
    for i in range(n):
        f = 130 + (28 if drift else 0) * math.sin(2 * math.pi * 0.6 * i / fs)
        ph = (ph + int(f / fs * 4096 * 65536)) & 0xFFFFFFF
        acc = 0
        for h in (1, 2, 3, 4, 5):
            acc += sin_t[((ph >> 16) * h) & 4095] // (h + 1)
        env = 0.55 + 0.4 * math.sin(2 * math.pi * 1.3 * i / fs)
        lcg = (lcg * 1103515245 + 12345) & 0x7FFFFFFF
        noise = ((lcg >> 8) & 255) - 128
        out.append(max(-32768, min(32767, int(acc * env * 0.55) + noise)))
    return out


def write_raw(path, chans):
    n = len(chans[0])
    data = bytearray()
    for i in range(n):
        for c in chans:
            data += struct.pack("<h", c[i])
    Path(path).write_bytes(bytes(data))


def read_raw(path, ch):
    b = Path(path).read_bytes()
    v = struct.unpack("<%dh" % (len(b) // 2), b)
    return [list(v[c::ch]) for c in range(ch)]


def build(src_dir, exe):
    r = subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src_dir / "sim/wsola_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); raise SystemExit("harness build failed")


def build2(src_dir, exe):
    r = subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", "-o", str(exe), str(src_dir / "sim/wsola2_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); raise SystemExit("v2 harness build failed")


def run_c2(exe, chans, fs, speed_q8, chunk, tmp):
    write_raw(tmp / "in2.raw", chans)
    r = subprocess.run([str(exe), str(tmp / "in2.raw"), str(tmp / "out2.raw"), str(fs), str(len(chans)), str(speed_q8), str(chunk)], capture_output=True, text=True)
    macs, grains, overrun = (int(v) for v in r.stderr.split())
    return read_raw(tmp / "out2.raw", len(chans)), macs, grains, overrun


def run_c(exe, chans, fs, speed_q8, tmp):
    write_raw(tmp / "in.raw", chans)
    r = subprocess.run([str(exe), str(tmp / "in.raw"), str(tmp / "out.raw"), str(fs), str(len(chans)), str(speed_q8)], capture_output=True, text=True)
    macs, grains = (int(v) for v in r.stderr.split())
    return read_raw(tmp / "out.raw", len(chans)), macs, grains


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    check("the table header equals what tools/lab/wsola_fixed_ref.py --emit-header writes", (ROOT / "fw/wsola_tables.h").read_text() == ref.emit_header())
    exe = td / "h"
    build(ROOT, exe)
    # the 32-bit-only 64/32 division against the compiler's 128-bit one: random operands plus the edges
    (td / "d.c").write_text(r'''
#include <stdio.h>
#include "%s/fw/wsola_core.h"
int main(void){ unsigned long long x = 88172645463325252ULL; long bad = 0, n = 0;
  static const unsigned edge[] = {1u,2u,3u,255u,65535u,65536u,65537u,0x7FFFFFFFu,0x80000000u,0xFFFFFFFEu,0xFFFFFFFFu};
  for (long i = 0; i < 3000000; i++) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    unsigned v = (i < 121) ? edge[i %% 11] : (unsigned)(x >> 32) >> ((x >> 5) %% 32);
    if (!v) v = 1;
    unsigned u1 = (unsigned)(x * 2654435761u) %% v;               /* u1 < v */
    unsigned u0 = (i %% 7 == 0) ? 0xFFFFFFFFu : (unsigned)x;
    unsigned __int128 num = ((unsigned __int128)u1 << 32) | u0;
    unsigned want = (unsigned)(num / v);
    if (ws_divlu(u1, u0, v) != want) { if (bad++ < 3) printf("bad %%u %%u %%u\n", u1, u0, v); }
    n++; }
  printf("%%ld %%ld\n", n, bad); return bad != 0; }
''' % str(ROOT))
    r = subprocess.run(["cc", "-O1", "-o", str(td / "d"), str(td / "d.c")], capture_output=True, text=True)
    d = subprocess.run([str(td / "d")], capture_output=True, text=True) if r.returncode == 0 else None
    check("ws_divlu equals the exact 128-bit quotient (3,000,000 random and edge cases)", d is not None and d.returncode == 0, (d.stdout if d else r.stderr))

    mono = voice(44100 * 2, 44100)
    mono_b = voice(44100 * 2, 44100, seed=7, drift=False)
    per2 = [int(12000 * math.sin(2 * math.pi * i / 160)) + int(5000 * math.sin(2 * math.pi * 3 * i / 160)) for i in range(44100)]
    cases = [("mono 44.1k 0.8x", [mono], 44100, 205), ("mono 44.1k 1.25x", [mono], 44100, 320), ("mono 44.1k 1.5x", [mono], 44100, 384),
             ("mono 44.1k 2.0x", [mono], 44100, 512), ("mono 44.1k 3.0x", [mono], 44100, 768), ("stereo 44.1k 1.5x", [mono, mono_b], 44100, 384),
             ("mono 22.05k 1.5x", [voice(22050 * 2, 22050)], 22050, 384), ("speed below the range is clamped to 0.5x", [mono[:44100]], 44100, 64)]
    outs = {}
    for name, chans, fs, spd in cases:
        oc, macs, grains = run_c(exe, chans, fs, spd, td)
        orf, rmacs, rgrains = ref.run(chans, fs, spd)
        same = oc == orf and macs == rmacs and grains == rgrains
        check(f"C == integer twin: {name} ({grains} grains, {macs} MACs)", same)
        outs[name] = (oc, chans)

    oc, chans = outs["mono 44.1k 1.5x"]
    ratio = len(chans[0]) / len(oc[0])
    check("output length follows the speed (1.5x: within 2%)", abs(ratio - 1.5) / 1.5 < 0.02, f"ratio {ratio:.3f}")
    s_oc, _ = outs["stereo 44.1k 1.5x"]
    twin, _, _ = run_c(exe, [mono, mono], 44100, 384, td)
    check("identical channels give identical output", twin[0] == twin[1])
    check("a 0.5x clamp matches the twin and doubles the length", abs(len(outs["speed below the range is clamped to 0.5x"][0][0]) / 44100 - 2.0) < 0.1)
    try:
        import numpy as np
        sys.path.insert(0, str(ROOT / "tools/lab"))
        import cymo_tempo_model as m
        x = np.array(mono, dtype=np.float64) / 32768.0
        for name, sp in (("mono 44.1k 1.25x", 1.25), ("mono 44.1k 1.5x", 1.5), ("mono 44.1k 2.0x", 2.0)):
            y = np.array(outs[name][0][0], dtype=np.float64) / 32768.0
            pr = m.f0_median(y, 44100) / m.f0_median(x, 44100)
            check(f"pitch is preserved at {sp}x (f0 ratio {pr:.3f})", abs(pr - 1.0) < 0.03)
    except ImportError:
        print("skip pitch check (numpy not installed)")

    # ---- core v2 -------------------------------------------------------------------------------------------------------------------------------------
    exe2 = td / "h2"
    build2(ROOT, exe2)
    short = mono[:44100]
    v2cases = [("mono 0.8x chunk 64", [mono], 44100, 205, 64), ("mono 1.5x chunk 64", [mono], 44100, 384, 64), ("mono 1.5x chunk 1152 (an MP3 frame)", [mono], 44100, 384, 1152),
               ("mono 2.0x chunk 64", [mono], 44100, 512, 64), ("mono 3.0x chunk 64", [mono], 44100, 768, 64), ("mono 1.5x one sample at a time", [short], 44100, 384, 1),
               ("stereo 1.5x chunk 64", [mono, mono_b], 44100, 384, 64), ("stereo 1.5x chunk 1152", [mono, mono_b], 44100, 384, 1152),
               ("mono 22.05k 1.5x chunk 64", [voice(22050 * 2, 22050)], 22050, 384, 64), ("exactly periodic input chunk 64", [per2], 44100, 384, 64)]
    for name, chans, fs, spd, chunk in v2cases:
        c_out, c_macs, c_grains, c_over = run_c2(exe2, chans, fs, spd, chunk, td)
        t_out, t_macs, t_grains, t_over = ref.run2(chans, fs, spd, chunk)
        v1_out, v1_macs, v1_grains = ref.run(chans, fs, spd)
        n = min(len(v1_out[0]), len(c_out[0]))
        check(f"v2: C == v2 twin: {name} ({c_grains} grains)", (c_out, c_macs, c_grains, bool(c_over)) == (t_out, t_macs, t_grains, t_over))
        check(f"v2: same samples as v1: {name}", c_over == 0 and c_out[0][:n] == v1_out[0][:n] and (len(c_out[0]) == len(v1_out[0])) and c_macs == v1_macs)
    big = [mono]
    c_out, c_macs, c_grains, c_over = run_c2(exe2, big, 44100, 768, 8192, td)
    t_out, t_macs, t_grains, t_over = ref.run2(big, 44100, 768, 8192)
    check("v2: feeding 8,192 samples between steps at 3.0x loses ring data; C and twin both detect it, identically", c_over == 1 and t_over and c_out == t_out and c_grains == t_grains)

    # five deliberately broken copies of the C core must each fail the comparison
    src = (ROOT / "fw/wsola_core.h").read_text()
    mutants = [("overlap weight off by one", "(WS_Q15 - w)", "(WS_Q15 - w + 1)"),
               ("sliding energy drops only half of the oldest sample", "en += nw * nw - o * o;", "en += nw * nw - ((o * o) >> 1);"),
               ("stage-2 decimation scaled wrongly", "R[i] = (int16_t)(acc >> s->shift);", "R[i] = (int16_t)(acc >> (s->shift - 1u));"),
               ("stage-1 signal derived from three of the four stage-2 samples", "C[b + 2u] + C[b + 3u]) >> 2) >> sc1);", "C[b + 2u]) >> 2) >> sc1);"),
               ("stage 2 looks at a single candidate", "const uint32_t ncand2 = c2 + 4u - lo2 + 1u;", "const uint32_t ncand2 = 1u;"),
               ("no rounding in the overlap-add", "+ 16384) >> 15", ") >> 15"),
               ("refine reference one sample off", "rf[i] = (int16_t)ws_mix(s, l, r, tgt + i);", "rf[i] = (int16_t)ws_mix(s, l, r, tgt + i + 1u);")]
    per = [int(12000 * math.sin(2 * math.pi * i / 160)) + int(5000 * math.sin(2 * math.pi * 3 * i / 160)) for i in range(44100)]   # exactly periodic: equal scores tie
    oc, macs, grains = run_c(exe, [per], 44100, 384, td)
    check("C == integer twin: exactly periodic input (ties between candidates)", (oc, macs, grains) == ref.run([per], 44100, 384))
    chans_by = {}
    for name, a, b in mutants:
        assert a in src, a
        md = td / "mut"; (md / "fw").mkdir(parents=True, exist_ok=True); (md / "sim").mkdir(exist_ok=True)
        (md / "fw/wsola_core.h").write_text(src.replace(a, b, 1))
        (md / "fw/wsola_tables.h").write_text((ROOT / "fw/wsola_tables.h").read_text())
        (md / "sim/wsola_harness.c").write_text((ROOT / "sim/wsola_harness.c").read_text())
        mexe = td / "mutexe"
        build(md, mexe)
        ch = chans_by.get(name, [mono[:44100]])
        got, _, _ = run_c(mexe, ch, 44100, 384, td)
        want, _, _ = ref.run(ch, 44100, 384)
        check(f"mutant caught: {name}", got != want)

    v2muts = [("v2: ring written one entry late", "s->ring[(s->fed / D2) & (WS2_RING - 1u)] = (int16_t)(a >> sh);", "s->ring[(s->fed / D2 + 1u) & (WS2_RING - 1u)] = (int16_t)(a >> sh);"),
              ("v2: stage-1 reference from three of four entries", " + WS2_E(rbase + 4u * i + 3u)) >> 2);", ") >> 2);"),
              ("v2: fine window read one sample late", "rd(ctx, 0u, rlo, nm, mixr);", "rd(ctx, 0u, rlo + 1u, nm, mixr);"),
              ("v2: the grain's second half not kept as the new tail", "rd(ctx, ch, cg + Hs, Hs, c->pend[ch]);", "(void)0;"),
              ("v2: right channel left out of the fine-stage mix", "rd(ctx, 1u, rlo, nm, tmp);", "rd(ctx, 0u, rlo, nm, tmp);"),
              ("v2: ready() ignores the ring", "return (s->fed / s->core.D2) - ws2_first_entry(s) <= WS2_RING;", "return 1;")]
    for name, a, b in v2muts:
        assert a in src, a
        md = td / "mut2"; (md / "fw").mkdir(parents=True, exist_ok=True); (md / "sim").mkdir(exist_ok=True)
        (md / "fw/wsola_core.h").write_text(src.replace(a, b, 1))
        (md / "fw/wsola_tables.h").write_text((ROOT / "fw/wsola_tables.h").read_text())
        (md / "sim/wsola2_harness.c").write_text((ROOT / "sim/wsola2_harness.c").read_text())
        mexe = td / "mutexe2"
        build2(md, mexe)
        if "ignores the ring" in name:
            got = run_c2(mexe, big, 44100, 768, 8192, td)
            want = ref.run2(big, 44100, 768, 8192)
            check(f"mutant caught: {name}", got[0] != want[0] or got[3] != int(want[3]))
        else:
            ch2 = [mono, mono_b]
            got = run_c2(mexe, ch2, 44100, 384, 64, td)
            want = ref.run2(ch2, 44100, 384, 64)
            check(f"mutant caught: {name}", got[0] != want[0])

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
