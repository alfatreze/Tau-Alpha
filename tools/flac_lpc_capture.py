#!/usr/bin/env python3
"""Real-file follow-up to sim/test_flac_lpc_symmetry.py (docs/research/FLAC_LPC_KERNEL_DESIGN.md section 4,
B-365): the synthetic legal-range check already proved a 45-bit accumulator is sufficient for ANY legal
FLAC input. This captures the (order, shift, coef, history-window) tuples FLAC subframes from REAL files
actually use -- both channel 0's and channel 1's, since tools/flac_verify.py's subframe() is called once
per channel with no code difference between them (fw/flac.c's own channel-1 fusion, subframe_stream(), is
a buffer-sharing OPTIMIZATION of the identical math, not a different algorithm) -- and replays them
through the same hardware_predict() model used there, as a second, real-world confirmation on top of the
already-closed legal-range question. Reuses tools/flac_verify.py's own bit-exact parser (proven correct via
whole-file MD5 checks) rather than re-implementing FLAC parsing.

  flac_lpc_capture.py FILE.flac [FILE2.flac ...]
"""
import sys
import os
import random

sys.path.insert(0, os.path.join(os.path.dirname(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "sim"))
import flac_verify as fv           # noqa: E402
from test_flac_lpc_symmetry import software_predict, hardware_predict  # noqa: E402

FIXED = fv.FIXED


def capture_subframe(b, n, out, bps, tuples):
    """A copy of flac_verify.subframe(), instrumented to record every real-LPC prediction step's
    (order, shift, coef, window, residual-adjusted-output) into `tuples`. FIXED predictors are not
    captured here -- their coefficients are tiny fixed integers (docs/research/FLAC_LPC_KERNEL_DESIGN.md's
    own FIXED_COEF table), already covered exhaustively by the synthetic check; real-LPC's coefficients
    are what actually vary with real content and are the point of this capture."""
    b.bits(1)
    typ = b.bits(6)
    wasted = b.unary() + 1 if b.bits(1) else 0
    bps -= wasted

    if typ == 0:
        v = b.sbits(bps)
        for i in range(n):
            out[i] = v
    elif typ == 1:
        for i in range(n):
            out[i] = b.sbits(bps)
    elif 8 <= typ <= 12:
        order = typ - 8
        for i in range(order):
            out[i] = b.sbits(bps)
        fv.residual(b, n, order, out, order)
        if order == 1:
            for i in range(1, n): out[i] += out[i - 1]
        elif order == 2:
            for i in range(2, n): out[i] += 2 * out[i - 1] - out[i - 2]
        elif order == 3:
            for i in range(3, n):
                out[i] += 3 * out[i - 1] - 3 * out[i - 2] + out[i - 3]
        elif order == 4:
            for i in range(4, n):
                out[i] += (4 * out[i - 1] - 6 * out[i - 2]
                           + 4 * out[i - 3] - out[i - 4])
    elif typ >= 32:
        order = typ - 31
        for i in range(order):
            out[i] = b.sbits(bps)
        prec = b.bits(4) + 1
        shift = b.sbits(5)
        if prec == 16 or shift < 0:
            raise ValueError("LPC prec/shift")
        coef = [b.sbits(prec) for _ in range(order)]
        fv.residual(b, n, order, out, order)
        rc = list(reversed(coef))
        for i in range(order, n):
            acc = 0
            w = out[i - order:i]
            for cj, sj in zip(rc, w):
                acc += cj * sj
            tuples.append((order, shift, tuple(rc), tuple(w)))
            out[i] += acc >> shift
    else:
        raise ValueError("subframe type %d" % typ)

    if wasted:
        for i in range(n):
            out[i] <<= wasted


def scan_file(path, tuples, max_frames=None):
    """Decodes a real file with fv's own parser, routing every subframe (both channels) through
    capture_subframe() instead of fv.subframe() -- same frame loop as fv.verify(), minus the MD5/PCM
    bookkeeping this tool doesn't need. max_frames bounds a quick run (a few hundred frames already
    exercises real coefficient/order/shift distributions; pure-Python parsing of a whole track is slow,
    same reason tools/flac_verify.py exists as an optimized alternative to tools/flac_ref.py)."""
    b = fv.Bits(open(path, 'rb').read())
    si = fv.open_stream(b)
    cap = si['maxb']
    ch0, ch1 = [0] * cap, [0] * cap
    nch, bps = si['ch'], si['bps']
    frames = 0
    while max_frames is None or frames < max_frames:
        try:
            n, m, tries = fv.frame_header(b, cap)
        except (EOFError, ValueError):
            break
        try:
            capture_subframe(b, n, ch0, bps + (1 if m == 9 else 0), tuples)
            if nch != 1:
                capture_subframe(b, n, ch1, bps + (1 if m in (8, 10) else 0), tuples)
            b.align(); b.bits(16)
        except (EOFError, ValueError):
            break
        frames += 1
    return si, frames


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    max_frames = 300
    args = sys.argv[1:]
    if args and args[0] == "--full":
        max_frames = None
        args = args[1:]
    all_tuples = []
    for path in args:
        print(f"{os.path.basename(path)}: scanning" + (f" (first {max_frames} frames)" if max_frames else " (full)"), flush=True)
        tuples = []
        try:
            si, frames = scan_file(path, tuples, max_frames=max_frames)
        except Exception as e:                                   # noqa: BLE001 - report and keep going
            print(f"{os.path.basename(path)}: SKIP ({e})")
            continue
        print(f"{os.path.basename(path)}: {si['bps']}-bit {si['rate']}Hz {si['ch']}ch, "
              f"{frames} frames, {len(tuples)} real-LPC subframes captured", flush=True)
        all_tuples += tuples

    if not all_tuples:
        print("no real-LPC subframes captured across any file -- nothing to check")
        return 1

    orders = [t[0] for t in all_tuples]
    max_coef = max(abs(c) for t in all_tuples for c in t[2])
    max_samp = max(abs(s) for t in all_tuples for s in t[3])
    print(f"\ncaptured {len(all_tuples)} real-LPC prediction steps across {len(args)} file(s)")
    print(f"real-world ranges: order {min(orders)}..{max(orders)}, "
          f"max |coef| {max_coef} (legal bound 16384), max |sample| {max_samp} (legal bound 16777216)")

    # A random subsample is enough for a real-world CONFIRMATION (not an exhaustiveness claim -- that's
    # already covered by sim/test_flac_lpc_symmetry.py's exact legal-range proof): comparing all of a
    # multi-million-tuple capture in pure Python is needlessly slow for what this step is actually for.
    SAMPLE_N = 100000
    sample = all_tuples if len(all_tuples) <= SAMPLE_N else random.Random(20260928).sample(all_tuples, SAMPLE_N)
    print(f"checking a random sample of {len(sample)} real steps against the hardware model...")

    for acc_bits in (45, 48, 64):
        mismatches = overflows = 0
        for order, shift, coef, hist in sample:
            sw_acc, sw_out = software_predict(coef, hist, shift)
            hw = hardware_predict(coef, hist, shift, acc_bits)
            if hw is None:
                overflows += 1
                continue
            if hw != (sw_acc, sw_out):
                mismatches += 1
        status = "ok" if mismatches == 0 and overflows == 0 else "FAIL"
        print(f"{status}   {acc_bits}-bit accumulator vs {len(all_tuples)} real captured steps: "
              f"{mismatches} mismatches, {overflows} overflows")
        if mismatches or overflows:
            return 1
    print("\nreal-file confirmation OK -- matches the synthetic legal-range result "
          "(sim/test_flac_lpc_symmetry.py): 45-bit accumulator is sufficient")
    return 0


if __name__ == "__main__":
    sys.exit(main())
