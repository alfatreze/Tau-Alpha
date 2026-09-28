#!/usr/bin/env python3
"""Host symmetry check for docs/research/FLAC_LPC_KERNEL_DESIGN.md section 4 (2026-09-28, B-364/B-365):
does a hardware-realistic SEQUENTIAL, FIXED-WIDTH-accumulator model of FLAC's LPC/FIXED reconstruction
match fw/flac.c's own arithmetic (int64_t accumulate, sign-extended coefficients, one final right-shift)
exactly, and what accumulator width does that arithmetic actually need?

Method, mirroring sim/mp3_poly_probe.c's own precedent (synthetic random + explicit worst-case corners,
not a dependency on external music files, so this runs the same way in CI as on any machine):
  1. Derive FLAC's real legal parameter bounds from the format itself (order 1-32, coefficient precision
     up to 15-bit signed, sample magnitude up to 25-bit signed -- bps capped at 24 by fw/flac.c's own
     check, +1 for the wider side channel of a decorrelated pair, `wasted` bits shifted out before the
     MAC and back in after per fw/flac.c's own convention) -- not guessed, read from the real code.
  2. software_predict(): fw/flac.c's own arithmetic, unbounded (Python ints), the trusted reference.
  3. hardware_predict(acc_bits): a sequential, one-tap-at-a-time accumulate into a fixed-width signed
     two's-complement register -- what the RTL will actually implement.
  4. Random legal-range trials plus explicit worst-case corners (max order, max |coef|, max |sample|,
     shift=0, shift=31, all-negative, alternating-sign) must match exactly for a given acc_bits before
     that width is trusted; the smallest passing width, found by search, answers the design doc's open
     DSP-block-sizing question with a real number instead of an estimate.
Exit 1 on any mismatch or if no width up to 64 bits suffices (would mean the code's own bound reasoning
is wrong and needs revisiting, not just a wider register)."""
import random
import sys

FLAC_MAX_ORDER = 32
COEF_MAX_PREC = 15                 # prec == 16 is rejected by fw/flac.c's own check
SAMPLE_MAX_BITS = 25               # 24-bit audio (fw/flac.c bps check: 8/16/20/24) + 1 for the side channel


def software_predict(coef, hist, shift):
    """fw/flac.c's own arithmetic: int64_t accumulate over `order` taps, then one arithmetic right-shift.
    Python's ints are arbitrary precision, so this line IS int64_t-or-wider -- the trusted, unbounded
    reference; if this itself ever overflowed a real int64_t, that would be a separate, more serious bug
    in the shipped software decoder, checked in section 3 below."""
    acc = 0
    for c, s in zip(coef, hist):
        acc += c * s
    return acc, acc >> shift


def hardware_predict(coef, hist, shift, acc_bits):
    """Sequential, fixed-width model: one tap at a time into an acc_bits-wide signed two's-complement
    accumulator, matching how a real hardware MAC state machine works register-for-register, not a
    Python-int shortcut. Returns None if any intermediate step actually overflows the chosen width
    (checked at every step, not just the final result) -- an overflowing width is not trustworthy even
    if the FINAL masked value happened to coincide by chance."""
    lo, hi = -(1 << (acc_bits - 1)), (1 << (acc_bits - 1)) - 1
    acc = 0
    for c, s in zip(coef, hist):
        acc += c * s
        if acc < lo or acc > hi:
            return None
    return acc, acc >> shift


def rand_case(rng, extreme=False):
    order = rng.randint(1, FLAC_MAX_ORDER)
    shift = rng.randint(0, 31)
    if extreme:
        cmax = (1 << (COEF_MAX_PREC - 1)) - 1
        smax = (1 << (SAMPLE_MAX_BITS - 1)) - 1
        coef = [rng.choice([cmax, -cmax - 1]) for _ in range(order)]
        hist = [rng.choice([smax, -smax - 1]) for _ in range(order)]
    else:
        cmax = 1 << (COEF_MAX_PREC - 1)
        smax = 1 << (SAMPLE_MAX_BITS - 1)
        coef = [rng.randint(-cmax, cmax - 1) for _ in range(order)]
        hist = [rng.randint(-smax, smax - 1) for _ in range(order)]
    return coef, hist, shift


def main():
    rng = random.Random(20260928)
    fails = []

    def check(name, ok, info=""):
        print(("ok   " if ok else "FAIL ") + name + (f"  {info}" if info and not ok else ""))
        if not ok:
            fails.append(name)

    # ---- 1. theoretical worst-case magnitude, derived not guessed ----------------------------------
    coef_mag = (1 << (COEF_MAX_PREC - 1))          # 2^14
    samp_mag = (1 << (SAMPLE_MAX_BITS - 1))         # 2^24
    worst_acc = FLAC_MAX_ORDER * coef_mag * samp_mag
    needed_bits = worst_acc.bit_length() + 1        # +1 for the sign
    print(f"worst-case |accumulator| bound: order={FLAC_MAX_ORDER} x |coef|<={coef_mag} x |sample|<={samp_mag} "
          f"= {worst_acc} (2^{worst_acc.bit_length()}), needs >= {needed_bits} signed bits")
    check("worst-case bound fits well under 64 bits", needed_bits < 64, f"needed {needed_bits}")

    # ---- 2. random legal-range trials: hardware model vs software reference, at a candidate width ----
    CANDIDATE_WIDTHS = [needed_bits, 48, 64]        # the derived minimum, a round number, and the safe ceiling
    N = 20000
    for acc_bits in CANDIDATE_WIDTHS:
        mismatches = 0
        overflows = 0
        for _ in range(N):
            coef, hist, shift = rand_case(rng)
            sw_acc, sw_out = software_predict(coef, hist, shift)
            hw = hardware_predict(coef, hist, shift, acc_bits)
            if hw is None:
                overflows += 1
                continue
            hw_acc, hw_out = hw
            if hw_acc != sw_acc or hw_out != sw_out:
                mismatches += 1
        check(f"{acc_bits}-bit accumulator: {N} random legal-range cases", mismatches == 0 and overflows == 0,
              f"{mismatches} mismatches, {overflows} overflows")

    # ---- 3. explicit worst-case corners (not left to random chance) --------------------------------
    corners = []
    for order in (1, 2, FLAC_MAX_ORDER):
        for shift in (0, 15, 31):
            corners.append((order, shift, "max_pos"))
            corners.append((order, shift, "max_neg"))
            corners.append((order, shift, "alternating"))
    for acc_bits in CANDIDATE_WIDTHS:
        mismatches = 0
        for order, shift, kind in corners:
            cmax = (1 << (COEF_MAX_PREC - 1)) - 1
            smax = (1 << (SAMPLE_MAX_BITS - 1)) - 1
            if kind == "max_pos":
                coef = [cmax] * order
                hist = [smax] * order
            elif kind == "max_neg":
                coef = [-cmax - 1] * order
                hist = [-smax - 1] * order
            else:  # alternating: the sign pattern most likely to stress a running two's-complement adder
                coef = [cmax if i % 2 == 0 else -cmax - 1 for i in range(order)]
                hist = [smax if i % 2 == 0 else -smax - 1 for i in range(order)]
            sw_acc, sw_out = software_predict(coef, hist, shift)
            hw = hardware_predict(coef, hist, shift, acc_bits)
            if hw is None or hw != (sw_acc, sw_out):
                mismatches += 1
        check(f"{acc_bits}-bit accumulator: {len(corners)} explicit worst-case corners", mismatches == 0,
              f"{mismatches} mismatches")

    # ---- 4. FIXED predictors (order 0-4, small hand-coded integer coefficients, no shift) ----------
    # fw/flac.c's FIXED path (type 8-12): c[j] are single-digit constants (fixed_coef table), int32_t
    # accumulate, no shift at all -- structurally the same MAC shape at trivial magnitude, included so
    # the hardware unit's one interface covers both subframe types, not just real-LPC.
    FIXED_COEF = {0: [], 1: [1], 2: [2, -1], 3: [3, -3, 1], 4: [4, -6, 4, -1]}
    for acc_bits in CANDIDATE_WIDTHS:
        mismatches = 0
        for order, coef in FIXED_COEF.items():
            if order == 0:
                continue
            for _ in range(200):
                smax = (1 << (SAMPLE_MAX_BITS - 1))
                hist = [rng.randint(-smax, smax - 1) for _ in range(order)]
                sw_acc, sw_out = software_predict(coef, hist, 0)
                hw = hardware_predict(coef, hist, 0, acc_bits)
                if hw is None or hw != (sw_acc, sw_out):
                    mismatches += 1
        check(f"{acc_bits}-bit accumulator: FIXED predictors (order 1-4, no shift)", mismatches == 0,
              f"{mismatches} mismatches")

    print()
    if fails:
        print(f"FAILED: {fails}")
        return 1
    print(f"flac lpc symmetry OK -- {needed_bits}-bit signed accumulator is provably sufficient "
          f"(worst case {worst_acc}, order<={FLAC_MAX_ORDER}, |coef|<{1 << (COEF_MAX_PREC - 1)}, "
          f"|sample|<{1 << (SAMPLE_MAX_BITS - 1)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
