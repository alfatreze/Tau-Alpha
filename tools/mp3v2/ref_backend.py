#!/usr/bin/env python3
"""MP3 decoder v2, WP-06: float64 reference model of the Layer III BACK END, written from the formulas of ISO/IEC 11172-3 (Layer III decoding, annex C synthesis).
Own code (Tau Alpha). Input: a requantised spectrum (the oracle's stage dump, tools/mp3_v2/oracle.c --stage). Output: PCM in the standard's units (1.0 = 16-bit full scale)
and, in the second variant, what the hardware window unit would output from the same subband samples (docs/features/MP3_WINDOW_UNIT_ABI_2026-11-25.md).

Formulas (all indices as in the standard; X = 18 spectral values of one subband, long blocks, n = 36; 6 values per window, short blocks, n = 12):
  alias reduction, for each of the 31 (long) subband boundaries sb | sb+1 (only the first boundary of a mixed block) and i = 0..7:
      lo = xr[18 sb + 17 - i], up = xr[18 (sb+1) + i];   lo' = lo cs_i - up ca_i;   up' = up cs_i + lo ca_i;   cs_i = 1/sqrt(1+c_i^2), ca_i = c_i/sqrt(1+c_i^2),
      c = -0.6 -0.535 -0.33 -0.185 -0.095 -0.041 -0.0142 -0.0037
  IMDCT (direct, O(n^2)):   x_i = sum_{k=0}^{n/2-1} X_k cos( pi/(2n) (2i + 1 + n/2) (2k + 1) ),   i = 0..n-1
  windows (block_type):  0 normal  w_i = sin(pi/36 (i + 1/2));   i = 0..35
                         1 start   w_i = sin(pi/36 (i+1/2)) i<18;  1 for 18..23;  sin(pi/12 (i-18+1/2)) for 24..29;  0 for 30..35
                         3 stop    w_i = 0 for i<6;  sin(pi/12 (i-6+1/2)) for 6..11;  1 for 12..17;  sin(pi/36 (i+1/2)) for 18..35
                         2 short   w_i = sin(pi/12 (i + 1/2)), i = 0..11
  short blocks: z[6 + 6 w + i] += x^(w)_i w_i  (three windows, i = 0..11, w = 0..2, onto 36 points); mixed blocks: the lowest n_long_bands subbands are long with window 0
  overlap-add: out_i = z_i + prev_i (i = 0..17),  prev = z_18..35 (state per channel and subband)
  frequency inversion: in every odd subband, every odd time sample is negated
  polyphase synthesis (per time slot, 32 subband samples S_k): shift V by 64;  V_i = sum_{k=0}^{31} cos((16 + i)(2k + 1) pi / 64) S_k, i = 0..63;
      U_{64 a + j} = V_{128 a + j}, U_{64 a + 32 + j} = V_{128 a + 96 + j}, a = 0..7, j = 0..31;   out_j = sum_{i=0}^{15} U_{j + 32 i} D_{j + 32 i}
  D: tools/data/mp3_synthesis_window.txt (Table 3-B.3, multiples of 2^-16).
Short-block spectrum order of the dump: per scale-factor band, three runs (window 0, 1, 2) of `width` values; the model reorders them itself (band widths come with the dump).
Unit arithmetic (variant "unit"): word[P] = round(2^23 X_m(P)) saturated to int32 with X_m = sum_k S_k cos(m (2k+1) pi / 64), pushed in the unit's order; V words from them by the
cosine symmetries; sum = sum D_int(i) word, out = clip16( floor((4 sum + 2^25) / 2^26) ) with 32-bit truncation of the 64-bit sum >> 20 (exact integer arithmetic).

  ref_backend.py gate STAGE.bin ORACLE.f32 [--unit]        the hard gate (model fed with the oracle's spectra vs the oracle's float PCM) and the stage check
  ref_backend.py metrics REF.f32 TEST.s16 [--channels N]   both comparison points between a float PCM and a 16-bit PCM
Library: read_stage, Backend, unit_synth, metrics, compare_domains."""
import math, struct, sys
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mp3_window_derive as W

D_INT = np.array(W.window(), dtype=np.int64)                  # D[i] * 65536, i = 0..511
D = D_INT / 65536.0
CI = [-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037]
CS = np.array([1 / math.sqrt(1 + c * c) for c in CI])
CA = np.array([c / math.sqrt(1 + c * c) for c in CI])
MINIMP3_SPEC_UNIT = 0.5          # minimp3's requantised spectrum is the standard's times 2^-1 (BITS_DEQUANTIZER_OUT = -1: it carries the missing factor 2 in its synthesis);
                                 # fixed by the encode/decode test of sim/test_mp3v2_tables.py (|q| = 1 at a known global_gain), not fitted to the PCM

_i36 = np.arange(36); _k18 = np.arange(18); _i12 = np.arange(12); _k6 = np.arange(6)
C36 = np.cos(np.pi / 72 * (2 * _i36[:, None] + 1 + 18) * (2 * _k18[None, :] + 1))         # (36, 18)
C12 = np.cos(np.pi / 24 * (2 * _i12[:, None] + 1 + 6) * (2 * _k6[None, :] + 1))            # (12, 6)
SIN36 = np.sin(np.pi / 36 * (_i36 + 0.5))
SIN12 = np.sin(np.pi / 12 * (np.arange(12) + 0.5))
WIN = {0: SIN36.copy(), 2: SIN12.copy()}
w1 = np.zeros(36); w1[:18] = SIN36[:18]; w1[18:24] = 1; w1[24:30] = SIN12[6:12]; WIN[1] = w1
w3 = np.zeros(36); w3[6:12] = SIN12[:6]; w3[12:18] = 1; w3[18:] = SIN36[18:]; WIN[3] = w3
NMAT = np.cos((16 + np.arange(64))[:, None] * (2 * np.arange(32)[None, :] + 1) * np.pi / 64)   # (64, 32): V_i = NMAT S
XMAT = np.cos(np.arange(32)[:, None] * (2 * np.arange(32)[None, :] + 1) * np.pi / 64)           # (32, 32): X_m = XMAT S


# ------------------------------------------------------------------ stage dump reader
def read_stage(path):
    """-> list of granules {nch, qmf_zero, ch: [{bt, nlb, nls, nss, ovz, sfb(40), spec(576), sub(576)}]}"""
    data = Path(path).read_bytes()
    out, p = [], 0
    while p < len(data):
        magic, nch, qz = struct.unpack_from("<III", data, p); p += 12
        assert magic == 0x4C4E5247, "bad stage record"
        chs = []
        for _ in range(nch):
            bt, nlb, nls, nss, ovz = struct.unpack_from("<5I", data, p); p += 20
            sfb = list(data[p:p + 40]); p += 40
            spec = np.frombuffer(data, dtype="<f4", count=576, offset=p).astype(np.float64); p += 2304
            sub = np.frombuffer(data, dtype="<f4", count=576, offset=p).astype(np.float64); p += 2304
            chs.append(dict(bt=bt, nlb=nlb, nls=nls, nss=nss, ovz=ovz, sfb=sfb, spec=spec, sub=sub))
        out.append(dict(nch=nch, qmf_zero=qz, ch=chs))
    return out


# ------------------------------------------------------------------ the back end
class Backend:
    """Float64 back end. mutation (tests only): None or one of 'alias', 'window_stop', 'overlap', 'short_order', 'inversion', 'synth_window'."""
    def __init__(self, mutation=None, spec_scale=1.0):
        self.mut = mutation
        self.scale = spec_scale                       # divide the dump's spectrum by this (a power of two, see calibrate)
        self.cs, self.ca = CS.copy(), CA.copy()
        self.win = {k: v.copy() for k, v in WIN.items()}
        self.d = D.copy()
        if mutation == "alias": self.ca[2] = -0.35 / math.sqrt(1 + 0.35 ** 2)
        if mutation == "window_stop": self.win[3] = WIN[1].copy()                         # stop blocks use the start window
        if mutation == "synth_window": self.d[100] *= 1.02
        self.overlap = np.zeros((2, 32, 18))
        self.hist = np.zeros((2, 15, 64))             # the 15 previous matrixed vectors per channel (V holds 16 blocks of 64)

    def reset(self, ch=None):
        for c in ([0, 1] if ch is None else [ch]):
            self.overlap[c] = 0; self.hist[c] = 0

    # ---- stages (public: the tests and the stage check use them one by one)
    def alias(self, xr, nbound):
        xr = xr.copy()
        i = np.arange(8)
        for b in range(nbound):
            lo = 18 * b + 17 - i; up = 18 * (b + 1) + i
            l, u = xr[lo].copy(), xr[up].copy()
            xr[lo] = l * self.cs - u * self.ca
            xr[up] = u * self.cs + l * self.ca
        return xr

    def short_spectrum(self, spec, nlb, sfb, nls, nss):
        """dump order (per band: window 0 run, window 1 run, window 2 run) -> X[w][f] with f the short frequency line (f = 6 * subband + k)"""
        X = np.zeros((3, 192))
        p = 18 * nlb; f = 6 * nlb
        widths = [sfb[nls + 3 * j] for j in range(nss // 3)]
        for wd in widths:
            for w in range(3):
                if p + wd > 576: break
                if self.mut == "short_order":
                    X[(w + 1) % 3, f:f + wd] = spec[p:p + wd]
                else:
                    X[w, f:f + wd] = spec[p:p + wd]
                p += wd
            f += wd
        return X

    def imdct_granule(self, ch, bt, nlb, spec, sfb, nls, nss):
        """-> subband samples [32][18] after alias, IMDCT, overlap-add, frequency inversion (updates the overlap state of channel ch)"""
        spec = spec / self.scale
        short = (bt == 2)
        nl = (nlb if nlb else 0) if short else 32                     # number of long subbands
        if short and nlb == 0: nl = 0
        xr = spec.copy()
        if short:
            xs = self.short_spectrum(spec, nlb, sfb, nls, nss)
            nbound = max(nlb - 1, 0)
        else:
            nbound = 31
        xr = self.alias(xr, nbound)
        z = np.zeros((32, 36))
        if nl:
            X = xr[:18 * nl].reshape(nl, 18)
            z[:nl] = (X @ C36.T) * self.win[0 if short else bt][None, :]
        if short and nl < 32:
            for sb in range(nl, 32):
                for w in range(3):
                    x = xs[w, 6 * sb:6 * sb + 6]
                    y = (C12 @ x) * self.win[2]
                    z[sb, 6 + 6 * w:18 + 6 * w] += y
        prev = self.overlap[ch]
        if self.mut == "overlap":
            out = z[:, :18] + prev[:, ::-1]
        else:
            out = z[:, :18] + prev
        self.overlap[ch] = z[:, 18:]
        if self.mut != "inversion":
            out[1::2, 1::2] *= -1
        return out

    def synth(self, ch, sub):
        """sub [32][18] -> 576 PCM samples (float64, standard units) for this channel; updates the V history"""
        S = sub.T                                                     # (18 slots, 32 subbands)
        Vn = S @ NMAT.T                                               # (18, 64)
        A = np.concatenate([self.hist[ch], Vn], axis=0)               # (33, 64): row r = slot r - 15
        out = np.zeros((18, 32))
        d = self.d
        for k in range(16):
            blk = A[15 - k:15 - k + 18, 32 * (k & 1):32 * (k & 1) + 32]
            out += blk * d[32 * k:32 * k + 32][None, :]
        self.hist[ch] = A[18:33]
        return out.reshape(576)

    def granule(self, g, with_sub=False):
        """one dump record -> (pcm[576][nch] float64 in standard units, list of subband arrays)"""
        pcm, subs = [], []
        if g["qmf_zero"]: self.hist[:] = 0
        for c, ch in enumerate(g["ch"]):
            if ch["ovz"]: self.overlap[c] = 0
            sub = self.imdct_granule(c, ch["bt"], ch["nlb"], ch["spec"], ch["sfb"], ch["nls"], ch["nss"])
            subs.append(sub)
            pcm.append(self.synth(c, sub))
        return np.stack(pcm, axis=1), subs

    def run(self, granules):
        pcms, stage_err = [], 0.0
        for g in granules:
            p, subs = self.granule(g)
            pcms.append(p)
            for c, s in enumerate(subs):
                stage_err = max(stage_err, float(np.max(np.abs(s.reshape(576) - g["ch"][c]["sub"] / self.scale))))
        return np.concatenate(pcms, axis=0), stage_err


# ------------------------------------------------------------------ the window unit's arithmetic
def m_of_p(P): return 0 if P == 0 else (15 + P if P <= 16 else 32 - P)


def _vmap():
    """V[n] = sign * word[P] (n = 0..63) from the cosine symmetries (the closed form of the ABI document, section 3); P = -1 for the zero entry V[16]"""
    P = np.zeros(64, dtype=np.int64); sg = np.zeros(64, dtype=np.int64)
    for n in range(64):
        k = 16 + n
        if k == 32: P[n] = 0; sg[n] = 0; continue
        q, s = (k, 1) if k < 32 else ((64 - k, -1) if k < 64 else (k - 64, -1))
        P[n] = 0 if q == 0 else (q - 15 if q >= 16 else 32 - q); sg[n] = s
    return P, sg


VP, VS = _vmap()
MORDER = np.array([m_of_p(P) for P in range(32)])


def unit_words(S, scale_bits=23):
    """S (T, 32) subband samples (1.0 = full scale) -> (T, 32) int64 words in the unit's push order, saturated to int32 (and the saturation count)"""
    X = S @ XMAT.T                                                    # (T, 32) X[m]
    w = np.rint(X[:, MORDER] * float(1 << scale_bits))                # round half to even, like Python round() in the ABI check
    lim = (1 << 31)
    sat = int(np.sum((w > lim - 1) | (w < -lim)))
    return np.clip(w, -lim, lim - 1).astype(np.int64), sat


def unit_window(words, history=None):
    """words (T, 32) pushed slots of ONE channel -> (pre, q16): `pre` the exact value sum / 2^24 in LSB (before the +1/2, floor and clip), `q16` the unit's 16-bit output.
    history: the previous 15 slots' V words (15, 64) or None (zeros); returns also the new history."""
    T = words.shape[0]
    V = words[:, VP] * VS[None, :]                                    # (T, 64) V words (V[16] = 0)
    hist = np.zeros((15, 64), dtype=np.int64) if history is None else history
    A = np.concatenate([hist, V], axis=0)
    acc = np.zeros((T, 32), dtype=np.int64)
    for k in range(16):
        blk = A[15 - k:15 - k + T, 32 * (k & 1):32 * (k & 1) + 32]
        acc += blk * D_INT[32 * k:32 * k + 32][None, :]
    pre = acc / float(1 << 24)
    s = 4 * acc + (1 << 25)                                           # the unit's 64-bit sum with coefficient 4 D_int and rounding constant 2^25
    x = (s >> 20).astype(np.int32).astype(np.int64)                   # bits 51:20 kept as a signed 32-bit value (cannot wrap for 32-bit words, see the ABI document)
    q = np.clip(x >> 6, -32768, 32767)
    return pre, q, A[T:T + 15]


def unit_synth(sub_by_ch):
    """sub_by_ch: list over channels of subband sample arrays (T, 32) in standard units -> (pre (T*32, nch), q16 (T*32, nch), saturation count)"""
    pres, qs, sat = [], [], 0
    for sub in sub_by_ch:
        w, s = unit_words(sub); sat += s
        pre, q, _ = unit_window(w)
        pres.append(pre.reshape(-1)); qs.append(q.reshape(-1))
    return np.stack(pres, axis=1), np.stack(qs, axis=1), sat


# ------------------------------------------------------------------ metrics
def s16_of(x):
    """float PCM in standard units -> 16-bit, rounded half up, clipped (the unit's rounding)"""
    return np.clip(np.floor(np.asarray(x) * 32768.0 + 0.5), -32768, 32767).astype(np.int64)


def metrics(ref_lsb, test_lsb):
    """error statistics in LSB (1 LSB = 2^-15 of full scale): rms, max abs, and the count of |error| > 0.5 / > 1 LSB"""
    e = np.asarray(test_lsb, dtype=np.float64) - np.asarray(ref_lsb, dtype=np.float64)
    return dict(n=int(e.size), rms=float(np.sqrt(np.mean(e * e))) if e.size else 0.0, max=float(np.max(np.abs(e))) if e.size else 0.0,
                over_half=int(np.sum(np.abs(e) > 0.5)), over_one=int(np.sum(np.abs(e) > 1.0)))


def compare_domains(ref_float, pre=None, q16=None):
    """The two comparison points of the expert review (Q7):
       (a) WIDE: float reference (x 32768, unclipped) against the unit's value before the final rounding and clip (`pre`, LSB);
       (b) 16-BIT: float reference rounded to 16 bits (half up, clipped) against the unit's 16-bit output (`q16`): rms, max, and counts of 0 / 1 / more LSB differences.
       ISO full-accuracy bounds in LSB: rms < 1/sqrt(12) = 0.2887 (2^-15/sqrt(12) of full scale), max <= 2 (2^-14)."""
    ref_lsb = np.asarray(ref_float) * 32768.0
    out = {}
    if pre is not None: out["wide"] = metrics(ref_lsb, pre)
    if q16 is not None:
        r16 = s16_of(ref_float)
        d = np.asarray(q16, dtype=np.int64) - r16
        m = metrics(r16, q16)
        m.update(equal=int(np.sum(d == 0)), one_lsb=int(np.sum(np.abs(d) == 1)), more=int(np.sum(np.abs(d) > 1)))
        out["s16"] = m
        out["s16_vs_unrounded"] = metrics(np.clip(ref_lsb, -32768, 32767), q16)   # the 16-bit output against the wide reference clipped to the 16-bit range (the ISO-style number)
    return out


# ------------------------------------------------------------------ gate
def calibrate(granules):
    """Consistency check of the dump's SUBBAND samples against the model run at unit scale (returns the power of two relating the two; expected 1.0: the dump's spectrum and
    subband arrays share minimp3's unit). Not used to set the gate's scale (MINIMP3_SPEC_UNIT is)."""
    for g in granules:
        for c, ch in enumerate(g["ch"]):
            if np.max(np.abs(ch["sub"])) > 1e-3 and ch["ovz"]:
                b = Backend(spec_scale=1.0)
                sub = b.imdct_granule(c, ch["bt"], ch["nlb"], ch["spec"], ch["sfb"], ch["nls"], ch["nss"])
                num = float(np.dot(sub.reshape(-1), ch["sub"])); den = float(np.dot(sub.reshape(-1), sub.reshape(-1)))
                r = num / den if den else 0.0
                if r > 0:
                    e = round(math.log2(1.0 / r))
                    return float(2.0 ** e)
    return 1.0


def gate(stage, f32, mutation=None, scale=MINIMP3_SPEC_UNIT, tol=1e-5):
    """HARD GATE: the model fed with the dumped spectra reproduces the oracle's float PCM within tol of full scale.
    -> dict(ok, max_err, rms, stage_err (subband samples), scale, samples)"""
    gr = read_stage(stage)
    ora = np.fromfile(f32, dtype="<f4").astype(np.float64)
    if not gr:
        return dict(ok=ora.size == 0, max_err=0.0, rms=0.0, stage_err=0.0, scale=1.0, samples=0)
    nch = gr[0]["nch"]
    b = Backend(mutation, scale)
    pcm, st = b.run(gr)
    # minimp3 units: PCM float = standard / 32768 * 32768 ... the scale found for the spectrum applies to the PCM too (same linear chain)
    mine = pcm.reshape(-1)
    if mine.size != ora.size:
        return dict(ok=False, max_err=float("inf"), rms=float("inf"), stage_err=st, scale=scale, samples=int(ora.size))
    err = mine - ora
    me = float(np.max(np.abs(err)))
    return dict(ok=me < tol, max_err=me, rms=float(np.sqrt(np.mean(err * err))), stage_err=st, scale=scale, samples=int(ora.size), nch=nch)


LEGAL_NEXT = {0: (0, 1), 1: (2, 3), 2: (2, 3), 3: (0, 1)}


def block_statistics(granules):
    """counts per channel sequence: granules by block type, mixed granules, transitions that are not in the legal set of the standard's window-switching sequences"""
    cnt = {0: 0, 1: 0, 2: 0, 3: 0}; mixed = 0; illegal = 0; prev = [None, None]; prev_mixed = [False, False]; mixed_edges = 0
    for g in granules:
        if g["qmf_zero"]: prev = [None, None]; prev_mixed = [False, False]
        for c, ch in enumerate(g["ch"]):
            bt = ch["bt"]; cnt[bt] += 1
            m = bool(ch["nlb"]) and bt == 2
            mixed += m
            if prev[c] is not None and bt not in LEGAL_NEXT[prev[c]]: illegal += 1
            if prev[c] is not None and (m != prev_mixed[c]) and (m or prev_mixed[c]) and prev[c] in (1, 2, 3) and bt in (1, 2, 3): mixed_edges += 1
            prev[c] = bt; prev_mixed[c] = m
    return dict(types=cnt, mixed=mixed, illegal_transitions=illegal, mixed_boundary_transitions=mixed_edges)


def library(root, frames=None, verbose=True):
    """gate + block statistics over every .mp3 below root (the owner's library, read-only); the stage dump of each file goes to a temporary directory"""
    import tempfile, time
    sys.path.insert(0, str(ROOT / "tools/mp3_v2"))
    import oracle
    exe = oracle.build_stage()
    files = sorted(Path(root).rglob("*.mp3"))
    tot = dict(files=0, ok=0, samples=0, worst=0.0, illegal=0, mixed=0, types={0: 0, 1: 0, 2: 0, 3: 0}, secs=0.0)
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        for f in files:
            t0 = time.time()
            info = oracle.decode(exe, f, f32=td / "o.f32", stage=td / "o.stage", frames=frames)
            gr = read_stage(td / "o.stage")
            r = gate(td / "o.stage", td / "o.f32")
            st = block_statistics(gr)
            dt = time.time() - t0
            tot["files"] += 1; tot["ok"] += bool(r["ok"]); tot["samples"] += r["samples"]; tot["worst"] = max(tot["worst"], r["max_err"]); tot["secs"] += dt
            tot["illegal"] += st["illegal_transitions"]; tot["mixed"] += st["mixed"]
            for k in range(4): tot["types"][k] += st["types"][k]
            if verbose:
                print("%-60s ok=%s max=%.2e stage=%.1e peak=%.3f hz=%d ch=%d types=%s mixed=%d illegal=%d %.1fs" % (f.name[:60], r["ok"], r["max_err"], r["stage_err"], info["peak"], info["hz"], info["channels"],
                      st["types"], st["mixed"], st["illegal_transitions"], dt), flush=True)
    return tot


def domains(mp3, frames=None):
    """Both comparison points on a real file: the model's float64 PCM is the reference; the unit arithmetic is fed with the model's own subband samples (so only the unit's
    rounding shows), and the oracle's 16-bit output (minimp3 float -> s16) is measured against the same reference. -> dict of compare_domains results"""
    import tempfile, time
    sys.path.insert(0, str(ROOT / "tools/mp3_v2"))
    import oracle
    exe = oracle.build_stage()
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        info = oracle.decode(exe, mp3, pcm=td / "o.s16", f32=td / "o.f32", stage=td / "o.stage", frames=frames)
        gr = read_stage(td / "o.stage")
        t0 = time.time()
        b = Backend(spec_scale=MINIMP3_SPEC_UNIT)
        subs = [[], []]; pcm = []
        for g in gr:
            if g["qmf_zero"]: b.hist[:] = 0
            p = []
            for c, ch in enumerate(g["ch"]):
                if ch["ovz"]: b.overlap[c] = 0
                sub = b.imdct_granule(c, ch["bt"], ch["nlb"], ch["spec"], ch["sfb"], ch["nls"], ch["nss"])
                subs[c].append(sub.T); p.append(b.synth(c, sub))
            pcm.append(np.stack(p, axis=1))
        ref = np.concatenate(pcm, axis=0)
        t_model = time.time() - t0
        nch = gr[0]["nch"]
        S = [np.concatenate(subs[c], axis=0) for c in range(nch)]
        pre, q16, sat = unit_synth(S)
        s16 = np.fromfile(td / "o.s16", dtype="<i2").astype(np.int64).reshape(-1, nch)
        out = dict(info=info, unit=compare_domains(ref, pre=pre, q16=q16), oracle_s16=compare_domains(ref, q16=s16), word_saturation=sat, model_seconds=t_model,
                   samples=int(ref.size), clipped_ref=int(np.sum(np.abs(ref) >= 1.0)))
    return out


def main(argv):
    if len(argv) >= 3 and argv[1] == "domains":
        print(domains(argv[2], int(argv[3]) if len(argv) > 3 else None)); return 0
    if len(argv) >= 3 and argv[1] == "library":
        tot = library(argv[2], frames=int(argv[3]) if len(argv) > 3 else None)
        print(tot)
        return 0 if tot["ok"] == tot["files"] else 1
    if len(argv) >= 4 and argv[1] == "gate":
        r = gate(argv[2], argv[3])
        print(r)
        return 0 if r["ok"] else 1
    if len(argv) >= 4 and argv[1] == "metrics":
        ref = np.fromfile(argv[2], dtype="<f4").astype(np.float64)
        t = np.fromfile(argv[3], dtype="<i2").astype(np.int64)
        n = min(ref.size, t.size)
        print(compare_domains(ref[:n], q16=t[:n]))
        return 0
    print(__doc__); return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
