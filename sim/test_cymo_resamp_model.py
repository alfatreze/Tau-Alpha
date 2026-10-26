#!/usr/bin/env python3
"""Golden-model test for the Cymo polyphase FIR resampler (docs/features/CYMO_AUDIO_ENGINE.md section
15/9, docs/AUDIT_TRAIL.md B-467+). sim/cymo_resamp_model.c IS the reference (this is a new design, not a
port of an existing decoder -- see that file's own header) -- so this test's job is this project's own
"prove it twice, differently" discipline (B-365/B-366/B-367's precedent): an INDEPENDENT re-implementation
of the exact same fixed-point algorithm, in Python rather than C, against the SAME generated ROM and the
SAME input stream (regenerated here with an identical LCG, not reused from the C program's memory), cross-
checked bit-for-bit against sim/cymo_resamp_model.c's own output. Also writes RTL testbench vectors to
build/rtl/cymo_resamp_vectors.txt, mirroring sim/test_flac_lpc_model.py's exact pattern."""
import importlib.util
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))


def _load_gen():
    spec = importlib.util.spec_from_file_location("gen_cymo_resamp_rom", ROOT / "tools/gen_cymo_resamp_rom.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


NIN = 4000
NHOT = 1000
Q_STEP = 147


def python_reference(rom, p_banks, taps, out_w=16):
    """Independent re-implementation of sim/cymo_resamp_model.c's exact algorithm: same LCG (so the SAME
    input stream, not a different one -- the point is checking the ARITHMETIC, not the test data), same
    phase/history/MAC/shift/clip sequence, pure Python integers (which are already arbitrary-precision --
    no accumulator width to get wrong here, which is itself a reason this check is only a partial
    substitute for the C model's own int64/ACC_WIDTH-bound overflow check, not a replacement for it)."""
    rng = 20260930

    def rnd32():
        nonlocal rng
        rng = (rng * 1664525 + 1013904223) & 0xFFFFFFFF
        return rng >> 8

    def rnd_s16():
        v = rnd32() & 0xFFFF
        return v - 0x10000 if v >= 0x8000 else v

    # Must interleave L then R per sample, matching sim/cymo_resamp_model.c's own call order exactly
    # (`in_l[i] = rnd_s16(); in_r[i] = rnd_s16();` inside one loop) -- generating all of in_l first and
    # all of in_r second would consume the LCG stream in a different order and silently diverge.
    in_l, in_r = [], []
    for _ in range(NIN):
        in_l.append(rnd_s16())
        in_r.append(rnd_s16())

    pr = (32767, 32767, -32767, 32767, -32767)
    for i in range(NIN - NHOT, NIN):      # the hot tail, identical to sim/cymo_resamp_model.c
        in_l[i] = 32767 if i % 8 < 4 else -32767
        in_r[i] = pr[i % 5]
    hist = [[0] * taps, [0] * taps]
    phase = 0
    consumed = 0
    out_l, out_r, pop = [], [], []

    def clip16(v):
        mx, mn = (1 << (out_w - 1)) - 1, -(1 << (out_w - 1))
        return mx if v > mx else mn if v < mn else v

    while consumed < NIN:
        bank = rom[phase * taps:(phase + 1) * taps]
        acc_l = sum(bank[t] * hist[0][t] for t in range(taps))
        acc_r = sum(bank[t] * hist[1][t] for t in range(taps))
        out_l.append(clip16(acc_l >> 15))
        out_r.append(clip16(acc_r >> 15))
        phase += Q_STEP
        if phase >= p_banks:
            phase -= p_banks
            hist[0] = [in_l[consumed]] + hist[0][:-1]
            hist[1] = [in_r[consumed]] + hist[1][:-1]
            consumed += 1
            pop.append(1)
        else:
            pop.append(0)
    return in_l, in_r, out_l, out_r, pop


def run_width(out_w, out_dir, gen, rom, p_banks, taps):
    exe = out_dir / f"cymo_resamp_model{out_w}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", f"-DOUT_W={out_w}",
                         "-I", str(ROOT / "sim"), "-o", str(exe), str(ROOT / "sim/cymo_resamp_model.c")],
                        capture_output=True, text=True)
    if r.returncode:
        print(r.stderr)
        return False
    vectors = out_dir / ("cymo_resamp_vectors.txt" if out_w == 16 else f"cymo_resamp_vectors{out_w}.txt")
    p = subprocess.run([str(exe), str(vectors)], capture_output=True, text=True)
    print(p.stdout, end="")
    c_ok = p.returncode == 0 and "overflows 0" in p.stdout
    print(f"ok   cymo_resamp_model.c (OUT_W={out_w}): 0 accumulator-width overflows (ACC_WIDTH=40 proven sufficient)"
          if c_ok else f"FAIL cymo_resamp_model.c (OUT_W={out_w})")
    _, _, py_out_l, py_out_r, py_pop = python_reference(rom, p_banks, taps, out_w)
    words = [int(x, 16) for x in open(vectors)]
    nin, nout = words[0], words[1]
    c_out = words[2 + nin * 2:]
    assert len(c_out) == nout * 3, (len(c_out), nout)

    def s32(x):
        return x - (1 << 32) if x >= (1 << 31) else x

    mism = 0
    clipped = 0
    if len(py_out_l) != nout:
        print(f"FAIL vector-count mismatch: C nout={nout} Python nout={len(py_out_l)}")
        mism += 1
    else:
        lim = (1 << (out_w - 1)) - 1
        for k in range(nout):
            c_l, c_r, c_pop = s32(c_out[3 * k]), s32(c_out[3 * k + 1]), c_out[3 * k + 2]
            if abs(c_l) >= lim or abs(c_r) >= lim or c_l == -lim - 1 or c_r == -lim - 1:
                clipped += 1
            if (c_l, c_r, c_pop) != (py_out_l[k], py_out_r[k], py_pop[k]):
                mism += 1
                if mism <= 8:
                    print(f"FAIL vector {k}: C=({c_l},{c_r},{c_pop}) Python=({py_out_l[k]},{py_out_r[k]},{py_pop[k]})")
    py_ok = mism == 0
    print(f"ok   Python cross-check matches C bit-for-bit on {nout} outputs (OUT_W={out_w}; {clipped} outputs at the clip limit)"
          if py_ok else f"FAIL Python cross-check (OUT_W={out_w}): {mism} mismatches")
    # the hot tail must actually exercise the clip at 16 bits and NOT at 18 bits, or the width test proves nothing
    clip_ok = (clipped > 0) if out_w == 16 else (clipped == 0)
    print(("ok   " if clip_ok else "FAIL ") + f"hot tail {'reaches' if out_w == 16 else 'stays clear of'} the {out_w}-bit clip limit ({clipped} outputs)")
    if c_ok and py_ok and clip_ok:
        lines = sum(1 for _ in open(vectors))
        print(f"ok   {nin} input + {nout} output RTL vectors ({lines} words) written to {vectors.relative_to(ROOT)}")
    return c_ok and py_ok and clip_ok


def main():
    out_dir = ROOT / "build/rtl"
    out_dir.mkdir(parents=True, exist_ok=True)
    gen = _load_gen()
    crm = gen._load_lab_model()
    rom, p_banks = gen.build_rom(crm)
    ok = all([run_width(16, out_dir, gen, rom, p_banks, gen.TAPS), run_width(18, out_dir, gen, rom, p_banks, gen.TAPS)])
    print("PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
