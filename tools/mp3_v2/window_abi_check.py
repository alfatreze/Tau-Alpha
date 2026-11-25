#!/usr/bin/env python3
"""MP3 v2 WP-02: executable check of the window unit's ABI (docs/features/MP3_WINDOW_UNIT_ABI_2026-11-25.md).

Proves, against the unit's golden model (sim/mp3_poly_model_core.h, compiled through tools/mp3_v2/window_model_cli.c), and against the RTL
(src/fpga/core/tau_mp3_poly.sv in iverilog, when iverilog is installed), that the unit expects

    word[P] = round( 2^23 * X[m(P)] ),   X[m] = sum_k S[k] cos(m (2k+1) pi / 64),   P0 = X[0], P1..P16 = X[16..31], P17..P31 = X[15..1]

per slot and channel (S[k] = the standard's subband samples, 1.0 = 16-bit full scale in the output), and that its 32 PCM words are then the standard's
synthesis filterbank output (V vector of 1,024, window D[i] of Table 3-B.3, out[j] = sum_i U[j+32i] D[j+32i]) times 32768, rounded half up and clipped to 16 bits.
Everything on the "standard" side is float64 written from the formulas of ISO/IEC 11172-3 (annex C, synthesis); D is the data file of the window provenance document.

  window_abi_check.py [--report]      prints every finding; exit 1 on any failed check
Library use (sim/test_mp3_v2_window_abi.py): run_checks(mutation) -> list of (name, ok, detail); mutations are the red cases."""
import math, os, random, shutil, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mp3_window_derive as W
import gen_mp3_poly_rom as G

SCALE_BITS = 23
D_INT = W.window()                                   # D[i] * 65536, i = 0..511 (the standard's Table 3-B.3 values are exact multiples of 2^-16)
D = [d / 65536.0 for d in D_INT]
COS = [[math.cos((16 + i) * (2 * k + 1) * math.pi / 64) for k in range(32)] for i in range(64)]      # the standard's N[i][k]
CX = [[math.cos(m * (2 * k + 1) * math.pi / 64) for k in range(32)] for m in range(32)]
MASK32 = 0xFFFFFFFF


# ---------------------------------------------------------------- the standard (float64)
class StdSynth:
    """Synthesis subband filter of ISO/IEC 11172-3: shift V by 64, matrix the 32 new samples into V[0..63], build U, window, sum."""
    def __init__(self): self.V = [0.0] * 1024
    def matrix(self, S):
        return [sum(COS[i][k] * S[k] for k in range(32)) for i in range(64)]
    def slot(self, S):
        V = self.V
        V[64:] = V[:-64]
        V[:64] = self.matrix(S)
        U = [0.0] * 512
        for i in range(8):
            for j in range(32):
                U[64 * i + j] = V[128 * i + j]; U[64 * i + 32 + j] = V[128 * i + 96 + j]
        return [sum(U[j + 32 * i] * D[j + 32 * i] for i in range(16)) for j in range(32)]


def x_values(S):
    return [sum(CX[m][k] * S[k] for k in range(32)) for m in range(32)]


def m_of_p(P):
    """push order of the unit: P0 = X[0], P1..P16 = X[16..31], P17..P31 = X[15..1]"""
    return 0 if P == 0 else (15 + P if P <= 16 else 32 - P)


def v_map():
    """the standard's V[n] (n = 0..63) as sign * one pushed word: n -> (P, sign) or None for the zero entry V[16], found numerically and
    then compared with the closed form (symmetries of the cosine matrix)."""
    rnd = random.Random(5)
    trials = []
    for _ in range(4):
        S = [rnd.uniform(-1, 1) for _ in range(32)]
        trials.append((StdSynth().matrix(S), x_values(S)))
    out = {}
    for n in range(64):
        found = None
        if all(abs(v[n]) < 1e-9 for v, _ in trials): out[n] = None; continue
        for P in range(32):
            for sg in (1, -1):
                if all(abs(v[n] - sg * x[m_of_p(P)]) < 1e-9 for v, x in trials): found = (P, sg)
        out[n] = found
    return out


def closed_form_vmap():
    m = {}
    for n in range(64):
        k = 16 + n                                   # V[n] = X[16+n] with X extended: X[32] = 0, X[64-q] = -X[q], X[64+q] = -X[q]
        if k == 32: m[n] = None
        else:
            q, sg = (k, 1) if k < 32 else ((64 - k, -1) if k < 64 else (k - 64, -1))
            P = 0 if q == 0 else (q - 15 if q >= 16 else 32 - q)
            m[n] = (P, sg)
    return m


# ---------------------------------------------------------------- the unit (integer model of the arithmetic, from the RTL and the standard-derived ROM)
class UnitPy:
    """Exact integer model of one channel pair: history of 16 slots, 64-bit sum, rounding constant 2^25, bits 51:20 (32-bit wrap), >>6, clip."""
    def __init__(self):
        self.taps, self.coef = W.derive(D_INT)
        self.ring = [[[0] * 32 for _ in range(16)] for _ in range(2)]
        self.head = [0, 0]
    def push(self, ch, w):
        self.head[ch] = (self.head[ch] + 1) & 15
        self.ring[ch][self.head[ch]] = list(w)
    @staticmethod
    def clip16(s):
        s &= (1 << 64) - 1                                   # 64-bit accumulator
        x = (s >> 20) & MASK32                               # bits 51:20 (and above, truncated to 32 bits like the (int) cast / the RTL's s[51:20] ...
        x = x - (1 << 32) if x & 0x80000000 else x           # ... = the low 32 bits of s >> 20)
        x >>= 6
        return 32767 if x > 32767 else (-32768 if x < -32768 else x)
    def word(self, ch, row, tap):
        age, P = self.taps[(row, tap)]
        return self.ring[ch][(self.head[ch] - age) & 15][P]
    def window(self, ch):
        c, rnd = self.coef, 1 << 25
        out = [0] * 32
        s = rnd
        for x in range(8):
            s += self.word(ch, 0, x) * c[2 * x] + self.word(ch, 0, 8 + x) * -c[2 * x + 1]
        out[0] = self.clip16(s)
        s = rnd
        for x in range(8): s += self.word(ch, 16, x) * c[256 + x]
        out[16] = self.clip16(s)
        for i in range(1, 16):
            s1 = s2 = rnd
            for x in range(8):
                c1, c2 = c[16 * i + 2 * x], c[16 * i + 2 * x + 1]
                lo, hi = self.word(ch, i, x), self.word(ch, i, 8 + x)
                s1 += lo * c1 - hi * c2; s2 += lo * c2 + hi * c1
            out[i], out[32 - i] = self.clip16(s1), self.clip16(s2)
        return out
    def slot(self, w0, w1):
        self.push(0, w0); self.push(1, w1)
        return self.window(0) + self.window(1)


# ---------------------------------------------------------------- tooling: the C model and the RTL
def build_model(td):
    td = Path(td)
    (td / "tables.h").write_text(G.render_h(*W.tables()))
    exe = td / "window_model"
    cmd = ["cc", "-std=gnu11", "-O1", "-w", '-DTABLES_H="tables.h"', "-I", str(td), "-I", str(ROOT / "sim"), "-o", str(exe), str(ROOT / "tools/mp3_v2/window_model_cli.c")]
    sdk = os.environ.get("SDKROOT") or ("/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk" if Path("/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk").is_dir() else "")
    if sdk and sys.platform == "darwin": cmd[1:1] = ["-isysroot", sdk]
    subprocess.run(cmd, check=True)
    return exe


def run_model(exe, slots, td, mono=False):
    """slots: list of 64-int lists (ch0 P0..31, ch1 P0..31) -> list of 64-int lists (ch0 pcm 32, ch1 pcm 32)"""
    wf, pf = Path(td) / "w.bin", Path(td) / "p.bin"
    wf.write_bytes(b"".join(struct.pack("<64i", *s) for s in slots))
    subprocess.run([str(exe), str(wf), str(pf), "1" if mono else "0"], check=True)
    raw = pf.read_bytes()
    return [list(struct.unpack_from("<64h", raw, 128 * i)) for i in range(len(slots))]


def run_rtl(slots, td, mono=False):
    """-> (list of 64-int pcm lists, clear_clocks, slot_clocks list) or None when iverilog is missing"""
    iv, vvp = shutil.which("iverilog"), shutil.which("vvp")
    if not iv or not vvp: return None
    td = Path(td)
    (td / "w.hex").write_text("".join("%08x\n" % (w & MASK32) for s in slots for w in s))
    sim = td / "abi_tb.vvp"
    subprocess.run([iv, "-g2012", "-I", str(ROOT / "src/fpga/core"), "-o", str(sim), str(ROOT / "tools/mp3_v2/window_abi_tb.v"), str(ROOT / "src/fpga/core/tau_mp3_poly.sv")], check=True)
    args = [vvp, str(sim), f"+words={td / 'w.hex'}", f"+n={len(slots)}", f"+out={td / 'o.hex'}"] + (["+mono"] if mono else [])
    r = subprocess.run(args, capture_output=True, text=True, check=True)
    clr = int(next(l for l in r.stdout.splitlines() if l.startswith("CLEAR_CLOCKS")).split()[1])
    sc = [int(l.split()[1]) for l in r.stdout.splitlines() if l.startswith("SLOT_CLOCKS")]
    vals = [int(l, 16) for l in (td / "o.hex").read_text().split()]
    res = []
    for s in range(len(slots)):
        ch0 = [(vals[32 * s + k] & 0xFFFF) for k in range(32)]; ch1 = [(vals[32 * s + k] >> 16) & 0xFFFF for k in range(32)]
        res.append([v - 65536 if v >= 32768 else v for v in ch0 + ch1])
    return res, clr, sc


# ---------------------------------------------------------------- stimulus
def scenarios(n_scale=1):
    """list of subband-sample slots S[32] (float64), covering quiet, normal, clipping, impulses, alternating extremes, a sweep and silence"""
    rnd = random.Random(2026)
    out = []
    def rep(count, f): out.extend(f(i) for i in range(int(count * n_scale)))
    rep(120, lambda i: [rnd.uniform(-0.05, 0.05) for _ in range(32)])
    rep(120, lambda i: [rnd.uniform(-0.5, 0.5) for _ in range(32)])
    rep(120, lambda i: [rnd.uniform(-1.0, 1.0) for _ in range(32)])
    rep(64, lambda i: [0.97 if k == i % 32 else 0.0 for k in range(32)])
    rep(64, lambda i: [(0.99 if (k + i) & 1 else -0.99) for k in range(32)])
    rep(120, lambda i: [0.6 * math.sin(0.07 * i * (k + 1) + k) for k in range(32)])
    rep(40, lambda i: [0.0] * 32)
    return out


def words_for(S, mutation=None, scale_bits=SCALE_BITS):
    X = x_values(S)
    sc = float(1 << (scale_bits + (-1 if mutation == "scale_low" else 1 if mutation == "scale_high" else 0)))
    w = [0] * 32
    for P in range(32):
        m = m_of_p(P)
        if mutation == "order": m = P                                   # natural order X[0..31] instead of the unit's push order
        v = round(sc * X[m])
        if mutation == "sign" and P >= 17: v = -v
        w[P] = max(-(1 << 31), min((1 << 31) - 1, v))
    return w


def expected_pcm(val):
    """the standard's output (1.0 = full scale) -> 16-bit, rounded half up, clipped"""
    x = math.floor(32768.0 * val + 0.5)
    return 32767 if x > 32767 else (-32768 if x < -32768 else x)


def error_bound():
    """worst-case output error (in LSB) caused by rounding each of the 16 words of a sample to an integer: 0.5 * sum|D| / 256 over the sample's 16 window taps"""
    return 0.5 * max(sum(abs(D[j + 32 * k]) for k in range(16)) for j in range(32)) / 256.0


def abs_coef_sums():
    taps, cf = W.derive(D_INT)
    best = 0
    for r in range(16):
        best = max(best, sum(abs(cf[16 * r + 2 * x]) + abs(cf[16 * r + 2 * x + 1]) for x in range(8)))
    return best, max(abs(c) for c in cf)


def selftest_slots():
    """The unit self-test stimulus of the ABI (plan 3.4 item 6): after a clear, 16 slots of fixed pseudo-random words (LCG 1664525/1013904223, seed 0x5EED,
    value = (state >> 8) % 2^23 - 2^22, i.e. +-2^22 = 0.5 of the standard's V, channel 0 then channel 1, P0..P31); the PCM of the 16th slot is the 64-sample constant."""
    st = 0x5EED
    slots = []
    for s_ in range(16):
        w = []
        for _ in range(64):
            st = (st * 1664525 + 1013904223) & MASK32
            w.append(((st >> 8) % (1 << 23)) - (1 << 22))
        slots.append(w)
    return slots


def selftest_expected():
    u = UnitPy(); last = None
    for s_ in selftest_slots(): last = u.slot(s_[:32], s_[32:])
    return last


# ---------------------------------------------------------------- the checks
def run_checks(mutation=None, with_rtl=True, report=None):
    res = []
    def check(name, ok, detail=""): res.append((name, bool(ok), detail))
    say = report or (lambda *a: None)
    # 1. V symmetries and the unique words
    vm, cf_ = v_map(), closed_form_vmap()
    check("V[n] = +-one pushed word (or 0 for V[16]) for every n, numerically, and equals the closed form from the cosine symmetries", vm == cf_, str([n for n in vm if vm[n] != cf_[n]][:5]))
    uniq = sorted({P for v in vm.values() if v for P in [v[0]]})
    check("the 63 non-zero V entries use exactly the 32 push indices P0..P31", uniq == list(range(32)), str(uniq))
    idx0_15 = [vm[n] for n in range(16)]
    check("V[0..15] = X[16..31] = P1..P16 with sign +", idx0_15 == [(n + 1, 1) for n in range(16)], str(idx0_15))
    check("V[33..47] = -X[15..1] = -P17..-P31 (the second independent half) and V[48] = -X[0] = -P0", vm[48] == (0, -1) and all(vm[n] == (n - 16, -1) for n in range(33, 48)), str([vm[n] for n in range(33, 49)]))
    check("symmetry V[32-i] = -V[i] for i=0..16 and V[96-i] = V[i] for i=33..63",
          all((vm[32 - i] is None and vm[i] is None) or (vm[32 - i][0] == vm[i][0] and vm[32 - i][1] == -vm[i][1]) for i in range(17) if vm[i] or vm[32 - i]) and
          all(vm[96 - i] == vm[i] for i in range(33, 64)))
    say("V map", {n: vm[n] for n in sorted(vm)})
    # 2. the stimulus, the standard, the words
    slots = scenarios()
    ref = StdSynth()
    ref_out = [ref.slot(S) for S in slots]
    words = [words_for(S, mutation) for S in slots]
    if mutation == "age": words = [words[0]] + words[:-1]                 # history one slot late
    wmax = max(abs(w) for ws in words for w in ws)
    check("every word fits int32 (|w| <= 2^31-1) without saturating at the nominal scale", mutation or wmax < (1 << 31) - 1, f"max |w| = {wmax} = 2^{math.log2(wmax):.2f}")
    say("max |word|", wmax, f"2^{math.log2(wmax):.3f}")
    bound = error_bound()
    say("word-rounding error bound (LSB)", bound)
    with tempfile.TemporaryDirectory() as td:
        exe = build_model(td)
        # both channels get the same subband samples in the main run (mono-style check below uses channel 0 only)
        pcm = run_model(exe, [ws + ws for ws in words], td)
        bad = exact = tie = clipped = 0; maxd = 0; total = 0
        for s, (ws, ro) in enumerate(zip(words, ref_out)):
            for ch in (0, 1):
                for j in range(32):
                    got = pcm[s][32 * ch + j]; x = 32768.0 * ro[j]
                    want = expected_pcm(ro[j]); total += 1
                    if want in (32767, -32768): clipped += 1
                    d = abs(got - want); maxd = max(maxd, d)
                    near = abs((x - math.floor(x)) - 0.5) < bound + 1e-9 and -32768 < want < 32767
                    if near: tie += 1
                    if d == 0: exact += 1
                    elif not near: bad += 1
        say("samples", total, "exact", exact, "within bound of a rounding tie", tie, "clipped", clipped, "max |diff|", maxd, "unexplained", bad)
        check("model PCM == the standard's float64 output x 32768 (round half up, clip): max |difference| <= 1 LSB", maxd <= 1, f"max diff {maxd}")
        check(f"model PCM is EXACT everywhere except within {bound:.4f} LSB of a rounding tie ({tie} of {total} samples are that close)", bad == 0, f"{bad} unexplained differences")
        check("the stimulus exercises clipping (rail samples present) and ordinary values", clipped > 20 and exact > 0.9 * total, f"clipped {clipped}, exact {exact}/{total}")
        # 3. integer-exact model of the arithmetic against the C model (random words, extremes, ties)
        rnd = random.Random(99)
        ext = []
        for i in range(120):
            mode = i % 4
            if mode == 0: ext.append([[rnd.randint(-(1 << 31), (1 << 31) - 1) for _ in range(32)] for _ in range(2)])
            elif mode == 1: ext.append([[((1 << 31) - 1) if (p + i) & 1 else -(1 << 31) for p in range(32)] for _ in range(2)])
            elif mode == 2: ext.append([[rnd.randint(-(1 << 24), 1 << 24) for _ in range(32)] for _ in range(2)])
            else: ext.append([[0] * 32, [rnd.randint(-(1 << 20), 1 << 20) if p == i % 32 else 0 for p in range(32)]])
        up = UnitPy()
        py = [up.slot(a, b) for a, b in ext]
        cm = run_model(exe, [a + b for a, b in ext], td)
        check("integer model of the stated arithmetic == the unit's C model, bit for bit (random, +-2^31 extremes, impulses; 120 slots)", py == cm,
              str(next(((i, [k for k in range(64) if py[i][k] != cm[i][k]][:4]) for i in range(len(py)) if py[i] != cm[i]), "")))
        cs, cmax = abs_coef_sums()
        say("max sum|c| per output", cs, f"= {cs / 2 ** 18:.4f} * 2^18", "max |c|", cmax, "2^51/(sum|c|) =", (1 << 51) // cs)
        check("no 64-bit-sum wrap is possible with 32-bit words: (2^31) * max sum|c| + 2^25 < 2^51", (1 << 31) * cs + (1 << 25) < (1 << 51), f"{(1 << 31) * cs / 2 ** 51:.3f} of 2^51")
        # 4. rounding and saturation on single words: out = clip(floor((w*c + 2^25) / 2^26)), ties go up
        up = UnitPy(); taps, coef = up.taps, up.coef
        P00 = taps[(0, 0)][1]; w0 = [0] * 32; w0[P00] = 1 << 23          # the word read at age 0 by row 0, tap 0 (coefficient index 0)
        o = up.slot(w0, [0] * 32)[:32]
        check("a single newest word (row 0, tap 0) shows up in output sample 0 exactly as floor((w*c + 2^25) / 2^26)",
              taps[(0, 0)][0] == 0 and o[0] == (w0[P00] * coef[0] + (1 << 25)) >> 26, str((taps[(0, 0)], o[0])))
        c1r = coef[16]; P1x = taps[(1, 0)][1]; assert taps[(1, 0)][0] == 0 and c1r != 0   # row 1, tap 0: age 0, coefficient index 16 -> output sample 1
        n_tie = 0
        for k in (-2, -1, 1, 2):
            num = k * (1 << 26) - (1 << 25)
            if num % c1r: continue
            w = num // c1r; w_lo = w - 1 if c1r > 0 else w + 1                 # w*c + 2^25 = k*2^26 exactly (a tie), and one coefficient-step below it
            o_t = UnitPy(); a_ = [0] * 32; a_[P1x] = w; ot = o_t.slot(a_, [0] * 32)[1]
            o_l = UnitPy(); b_ = [0] * 32; b_[P1x] = w_lo; ol = o_l.slot(b_, [0] * 32)[1]
            n_tie += 1
            check(f"a sum exactly at a rounding tie (k={k}) rounds up to {k}, one step below it rounds to {k - 1}", ot == k and ol == k - 1, str((w, ot, ol)))
        check("at least one engineered tie was exercised", n_tie >= 1)
        # 5. mono: one channel pushed as both channels -> identical halves, equal to the left channel of a stereo run with the same words
        mono_slots = [ws + [0] * 32 for ws in words[:200]]
        pm = run_model(exe, mono_slots, td, mono=True)
        ps = run_model(exe, [ws + ws for ws in words[:200]], td)
        check("mono (the words of one channel pushed as both): R half equals L half in every slot, and L equals the stereo run's L", all(a[:32] == a[32:] for a in pm) and all(a[:32] == b[:32] for a, b in zip(pm, ps)))
        # 5b. self-test vector: 16 fixed slots; the 16th slot's 64 PCM values; sensitivity to ROM and tap-map errors
        if mutation is None:
            exp = selftest_expected()
            got_st = run_model(exe, selftest_slots(), td)[-1]
            check("self-test vector: the C model's 16th-slot PCM equals the integer model's (the 64-value constant of the ABI)", got_st == exp)
            check("self-test vector exercises all 16 history ages (no PCM value is clipped, so every tap changes the result)", all(-32768 < v < 32767 for v in exp), str(exp[:6]))
            base_taps, base_coef = W.derive(D_INT)
            undetected_c = []
            for i in range(264):
                for dlt in (256, -256):
                    u = UnitPy(); u.coef = list(base_coef); u.coef[i] += dlt
                    last = None
                    for s_ in selftest_slots(): last = u.slot(s_[:32], s_[32:])
                    if last == exp: undetected_c.append((i, dlt))
            check("self-test vector detects a +-256 error (0.09 % of the peak coefficient 300,152) in each of the 264 ROM coefficients", not undetected_c, str(undetected_c[:6]))
            undetected_t = []
            for key in base_taps:
                a_, P_ = base_taps[key]
                for alt in ((a_ ^ 1, P_), (a_, P_ ^ 1)):
                    u = UnitPy(); u.taps = dict(base_taps); u.taps[key] = alt
                    last = None
                    for s_ in selftest_slots(): last = u.slot(s_[:32], s_[32:])
                    if last == exp: undetected_t.append((key, alt))
            def tap_coefs(key):                                     # the coefficients a tap's word is multiplied with (rows 1..15 use both c1 and c2 for lo and hi)
                r, t = key
                if r == 16: return (abs(base_coef[256 + t]),)
                if r == 0: return (abs(base_coef[2 * t]),) if t < 8 else (abs(base_coef[2 * (t - 8) + 1]),)
                return (abs(base_coef[16 * r + 2 * (t % 8)]), abs(base_coef[16 * r + 2 * (t % 8) + 1]))
            weak = [(k, alt) for k, alt in undetected_t if max(tap_coefs(k)) > 64]
            check(f"self-test vector detects a wrong age (age^1) or wrong word (P^1) in every tap-map entry whose window coefficients exceed 64 (2e-4 of the peak): "
                  f"{len(undetected_t)} of 528 variants stay invisible, all on taps with coefficient 0 or <= 64 (a possible error of at most about 1 LSB at +-0.5 V)", not weak, str(weak[:6]))
            say("self-test: invisible tap variants", undetected_t)
            say("self-test PCM", exp)
        # 6. RTL
        if with_rtl:
            sub = [a + b for a, b in ext[:24]] + [ws + ws for ws in words[:40]] + [ws + ws for ws in words[240:260]]
            sub_py = UnitPy(); want = [sub_py.slot(s[:32], s[32:]) for s in sub]
            r = run_rtl(sub, td)
            if r is None:
                check("RTL (iverilog) skipped: iverilog not installed", True)
            else:
                got, clr, sc = r
                check(f"RTL tau_mp3_poly.sv == integer model on {len(sub)} slots (random/extreme/ABI words), every PCM word", got == want,
                      str(next(((i, [k for k in range(64) if got[i][k] != want[i][k]][:3]) for i in range(len(want)) if got[i] != want[i]), "")))
                check("RTL: every slot takes the same number of clocks, 4,691 busy states derived from the sequencer (2 x 2,345 + 1), 4,692 as the bench counts them", len(set(sc)) == 1 and 4690 <= sc[0] <= 4694, f"{sorted(set(sc))}")
                check("RTL: a clear takes 1,024 + a few clocks", 1024 <= clr <= 1032, str(clr))
                say("RTL clocks per slot", sorted(set(sc)), "clear", clr)
                if mutation is None:
                    rm = run_rtl([ws + [0] * 32 for ws in words[:30]], td, mono=True)
                    check("RTL mono (one channel pushed twice): left == right in every slot", rm is not None and all(a[:32] == a[32:] for a in rm[0]))
    return res


def main():
    if "--selftest" in sys.argv:
        e = selftest_expected()
        print("ch0:", ", ".join(str(v) for v in e[:32])); print("ch1:", ", ".join(str(v) for v in e[32:])); return 0
    rep = "--report" in sys.argv
    res = run_checks(None, report=(lambda *a: print("  .", *a)) if rep else None)
    bad = 0
    for n, ok, d in res:
        print(("ok   " if ok else "FAIL ") + n + ("" if ok else "  " + d)); bad += (not ok)
    print("PASSED" if not bad else f"FAILED ({bad})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
