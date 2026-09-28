#!/usr/bin/env python3
"""B-370: host redirect check for the FLAC LPC hardware glue (docs/research/FLAC_LPC_KERNEL_DESIGN.md
section 7 item 4). Hand-builds one valid LPC subframe (reusing tools/flac_make_test.py's own bit writer,
so the encoding is proven against the same conventions that file's whole-file tests already exercise),
computes the expected reconstructed samples directly in Python, then drives the REAL fw/flac.c
subframe()/subframe_stream() code (via sim/flac_lpc_fw_harness.c, built with -DFLAC_TEST_EXPOSE) six ways:
{subframe, stream} x {TAU_LPC_FW=0, TAU_LPC_FW=1 clean, TAU_LPC_FW=1 with a mid-subframe induced hardware
failure}. All six must match the Python-computed expectation exactly -- this is not re-proving the LPC
arithmetic (sim/test_flac_lpc_symmetry.py and tau_flac_lpc.sv's own testbench, B-365/B-368, already did
that two independent ways); it proves fw/flac.c's own new glue (warm-up reversal, the residual/out[]
handoff, and the software-completion fallback) against real code, not a hand-simulated stand-in."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from flac_make_test import BitWriter  # noqa: E402

ORDER = 2
BPS = 16
N = 16                       # 2 warm-up + 14 reconstructed
PREC = 8
SHIFT = 6                    # flac.c reads shift via sbits(f,5) and rejects a negative result, so only
                              # 0-15 round-trips cleanly through this decoder's own convention -- see
                              # docs/AUDIT_TRAIL.md B-370 for the reasoning; keep well under that.
RICE_K = 4
COEF = [40, 20]               # gain (40+20)/64 = 0.9375 < 1: a stable predictor, chosen so the
                               # reconstructed sequence stays bounded (a real encoder would never emit
                               # a >1 gain predictor either) -- an earlier, unchecked choice of
                               # coefficients here blew up exponentially within a few samples and
                               # silently overflowed int32 in the C harness, caught only by comparing
                               # against Python's own unbounded-int result, not assumed safe.
WARM = [50, 80]                # read order: WARM[0] oldest .. WARM[ORDER-1] most recent
RESIDUALS = [3, -7, 12, -2, 0, 5, -15, 8, 1, -1, 20, -20, 4, -9]
assert len(RESIDUALS) == N - ORDER


def zigzag(v):
    return (v << 1) if v >= 0 else (((-v - 1) << 1) | 1)


def expected_samples():
    out = list(WARM)
    for i in range(ORDER, N):
        p = sum(COEF[j] * out[i - 1 - j] for j in range(ORDER))
        out.append(RESIDUALS[i - ORDER] + (p >> SHIFT))
    return out[ORDER:]


def build_blob():
    w = BitWriter()
    w.write(0, 1)                       # subframe padding bit
    w.write(ORDER + 31, 6)              # type: LPC, this order
    w.write(0, 1)                       # no wasted bits
    for v in WARM:
        w.write_signed(v, BPS)
    w.write(PREC - 1, 4)
    w.write_signed(SHIFT, 5)
    for c in COEF:
        w.write_signed(c, PREC)
    w.write(0, 2)                       # rice method 0 (4-bit params)
    w.write(0, 4)                       # partition order 0 -> one partition
    w.write(RICE_K, 4)                  # rice parameter (not the escape code 15)
    for r in RESIDUALS:
        u = zigzag(r)
        q = u >> RICE_K
        w.write(0, q)                   # q zero bits
        w.write(1, 1)                   # terminating 1
        w.write(u & ((1 << RICE_K) - 1), RICE_K)
    w.align()
    blob = w.bytes() + b"\x00" * 8   # trailing pad so a 4-byte refill never runs dry mid-field
    return blob


def build_harness(build_dir, lpc_fw):
    exe = build_dir / f"flac_lpc_fw_harness_{lpc_fw}"
    cflags = ["-DFLAC_TEST_EXPOSE", f"-DTAU_LPC_FW={lpc_fw}"]
    r = subprocess.run(
        ["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-parameter",
         "-I", str(ROOT / "fw"), *cflags,
         "-o", str(exe), str(ROOT / "sim/flac_lpc_fw_harness.c"), str(ROOT / "fw/flac.c")],
        capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        return None
    return exe


def run_harness(exe, mode, fail_at):
    r = subprocess.run([str(exe), "blob.bin", mode, str(ORDER), str(BPS), str(N), str(fail_at)],
                        cwd=exe.parent, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        return None
    return [int(x) for x in r.stdout.split()]


def main():
    build_dir = ROOT / "build/host/flac_lpc_fw"
    build_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "blob.bin").write_bytes(build_blob())

    want = expected_samples()
    ok = True

    exe0 = build_harness(build_dir, 0)
    exe1 = build_harness(build_dir, 1)
    if not exe0 or not exe1:
        print("FAIL build")
        return 1

    for mode in ("subframe", "stream"):
        got0 = run_harness(exe0, mode, -1)
        if got0 != want:
            print(f"FAIL {mode} TAU_LPC_FW=0: got {got0} want {want}")
            ok = False
        else:
            print(f"ok   {mode} TAU_LPC_FW=0 matches Python-computed expectation ({len(want)} samples)")

        got1 = run_harness(exe1, mode, -1)
        if got1 != want:
            print(f"FAIL {mode} TAU_LPC_FW=1 (clean hardware path): got {got1} want {want}")
            ok = False
        else:
            print(f"ok   {mode} TAU_LPC_FW=1 clean hardware path matches")

        got_fb = run_harness(exe1, mode, 5)   # fail on the 6th hardware sample -- mid-subframe
        if got_fb != want:
            print(f"FAIL {mode} TAU_LPC_FW=1 mid-subframe fallback: got {got_fb} want {want}")
            ok = False
        else:
            print(f"ok   {mode} TAU_LPC_FW=1 mid-subframe hardware-failure fallback matches")

        got_fb0 = run_harness(exe1, mode, 0)   # fail on the VERY FIRST hardware sample
        if got_fb0 != want:
            print(f"FAIL {mode} TAU_LPC_FW=1 immediate-failure fallback: got {got_fb0} want {want}")
            ok = False
        else:
            print(f"ok   {mode} TAU_LPC_FW=1 immediate-failure (sample 0) fallback matches")

    print("PASSED" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
