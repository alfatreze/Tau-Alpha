#!/usr/bin/env python3
"""Cymo C7 host prototype: speech-tuned WSOLA tempo (pitch-preserving), pause shortening, and objective scores (B-535).

Why this exists: docs/features/CYMO_AUDIO_ENGINE.md section 7 says the first step for audiobook tempo is a host prototype scored with objective measures and an
owner listening pass BEFORE any firmware or RTL. This is that prototype. Nothing here runs on the Pocket.

What it does
  wsola(x, speed)        Waveform-similarity overlap-add. 23 ms Hann grains at 50% overlap; each grain's start is chosen by a normalised cross-correlation search of
                         +-10 ms around the nominal position, coarse on a signal decimated 8x (about 5.5 kHz) then refined at the full rate over +-8 samples.
  ola(x, speed)          The same without the search (fixed grains) -- the control that shows why the search is needed.
  varispeed(x, speed)    Today's behaviour (resample): tempo AND pitch scale.
  shorten_pauses(x, ..)  Energy-envelope pause detection (relative to a tracked noise floor, with hysteresis) and removal of the MIDDLE of a long pause, never the
                         edges, joined with a short crossfade.
Scores printed per run: output/target duration, pitch ratio (median f0 of output over input; 1.00 is correct, varispeed gives `speed`), splice quality
(normalised correlation at each join; the share under 0.5 are the audible ones), worst sample-to-sample jump against the local level, and the correlation-search
cost in multiply-accumulates per output second -- the number that decides firmware versus a hardware correlator.

Standard library plus numpy. `python3 tools/lab/cymo_tempo_model.py selftest` checks the maths on synthetic signals.
"""
import argparse, sys, wave
from pathlib import Path
import numpy as np

N_WIN = 1024          # grain length, 23.2 ms at 44.1 kHz
HS = N_WIN // 2       # synthesis hop (50% overlap)
DEC = 8               # coarse-search decimation: 44.1 kHz -> 5.5 kHz
REF_LEN = 256         # full-rate refinement window (samples)
REF_SPAN = DEC        # refinement is +-DEC samples around the coarse winner


def read_wav(path):
    w = wave.open(str(path))
    ch, sw, fs, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(n)
    if sw == 2:
        a = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    elif sw == 3:
        b = np.frombuffer(raw, dtype=np.uint8).reshape(-1, 3).astype(np.int32)
        v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
        a = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float64) / (1 << 23)
    else:
        raise SystemExit("unsupported sample width %d" % sw)
    return a.reshape(-1, ch).mean(axis=1), fs


def write_wav(path, x, fs):
    y = np.clip(np.round(x * 32767.0), -32768, 32767).astype("<i2")
    w = wave.open(str(path), "wb")
    w.setnchannels(1); w.setsampwidth(2); w.setframerate(fs)
    w.writeframes(y.tobytes()); w.close()


def _decimate(x):
    """Low-pass (windowed sinc, 63 taps, cutoff just under the new Nyquist) then keep every DEC-th sample."""
    m = 63
    n = np.arange(m) - (m - 1) / 2.0
    h = np.sinc(n / DEC) * np.hamming(m)
    h /= h.sum()
    return np.convolve(x, h, mode="same")[::DEC]


def _ncc_best(sig, ref, lo, hi):
    """Index in [lo, hi] (inclusive) of the window of `sig` best matching `ref` by normalised cross-correlation. Returns (index, score, macs)."""
    n = len(ref)
    lo = max(lo, 0); hi = min(hi, len(sig) - n)
    if hi < lo:
        return max(0, min(lo, len(sig) - n)), 0.0, 0
    win = np.lib.stride_tricks.sliding_window_view(sig, n)[lo:hi + 1]
    num = win @ ref
    en = np.einsum("ij,ij->i", win, win)
    sc = num / np.sqrt(en * float(ref @ ref) + 1e-12)
    k = int(np.argmax(sc))
    return lo + k, float(sc[k]), (hi - lo + 1) * n


def wsola(x, speed, search=0.010, fs=44100, search_on=True):
    """Returns (y, info). info: splice scores, macs, grain count."""
    xd = _decimate(x)
    delta = int(search * fs)
    L = len(x)
    nout = int(L / speed)
    y = np.zeros(nout + 2 * N_WIN)
    w = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(N_WIN) / N_WIN)
    y[:N_WIN] += x[:N_WIN] * w
    prev = 0
    scores, macs = [], 0
    k = 1
    while k * HS < nout and prev + HS + N_WIN < L:
        p = int(round(k * HS * speed))
        p = max(0, min(p, L - N_WIN))
        if search_on:
            tgt = prev + HS
            ref_d = xd[tgt // DEC: tgt // DEC + N_WIN // DEC]
            lo = (p - delta) // DEC; hi = (p + delta) // DEC
            cd, _, m1 = _ncc_best(xd, ref_d, lo, hi)
            c0 = cd * DEC
            ref_f = x[tgt: tgt + REF_LEN]
            c, sc, m2 = _ncc_best(x, ref_f, c0 - REF_SPAN, c0 + REF_SPAN)
            c = max(0, min(c, L - N_WIN))
            macs += m1 + m2
            scores.append(sc)
        else:
            c = p
            tgt = prev + HS
            a = x[tgt: tgt + REF_LEN]; b = x[c: c + REF_LEN]
            scores.append(float(a @ b) / np.sqrt(float(a @ a) * float(b @ b) + 1e-12))
        y[k * HS: k * HS + N_WIN] += x[c: c + N_WIN] * w
        prev = c
        k += 1
    y = y[:min(len(y), k * HS + N_WIN)]
    return y, {"scores": np.array(scores), "macs": macs, "grains": k}


def ola(x, speed, fs=44100):
    return wsola(x, speed, fs=fs, search_on=False)


def varispeed(x, speed):
    t = np.arange(0, len(x) - 1, speed)
    return np.interp(t, np.arange(len(x)), x)


def envelope(x, fs, win_ms=10.0):
    n = int(fs * win_ms / 1000)
    e = np.sqrt(np.convolve(x * x, np.ones(n) / n, mode="same"))
    return e


def shorten_pauses(x, fs, min_pause=0.250, keep_frac=0.4, floor=0.120, max_cut=0.700, margin=0.060, xfade=0.010, thr_db=9.0, floor_pct=5.0):
    """Remove the middle of long pauses. Returns (y, info). A pause is a run where the 10 ms envelope stays below (noise_floor * thr) for > min_pause, with
    hysteresis (it must rise 3 dB above the threshold to end a pause). Kept = max(floor, keep_frac * length); at most max_cut is removed; `margin` seconds on
    each side of the pause are never touched, so a word onset is not clipped."""
    e = envelope(x, fs)
    hop = int(fs * 0.010)
    ee = e[::hop]
    # noise floor: the 5th percentile of the envelope over +-4 s. The first version used the window MINIMUM, which on an MP3 sits on digital silence (-70 dBFS on
    # the Twain clip) far below the room/encoder noise of a real pause (-55 dBFS), so a threshold of 'floor + 9 dB' missed most pauses (12 detected >= 250 ms
    # instead of 55). A low percentile tracks the pause level and ignores a few deep-silent frames (B-536).
    wl = int(4.0 / 0.010)
    floor_env = np.array([np.percentile(ee[max(0, i - wl): i + wl], floor_pct) for i in range(len(ee))]) + 1e-6
    thr = floor_env * (10 ** (thr_db / 20.0))
    hi = thr * (10 ** (3.0 / 20.0))
    quiet = np.zeros(len(ee), bool)
    state = False
    for i in range(len(ee)):
        if not state and ee[i] < thr[i]: state = True
        elif state and ee[i] > hi[i]: state = False
        quiet[i] = state
    cuts = []
    i = 0
    while i < len(quiet):
        if quiet[i]:
            j = i
            while j < len(quiet) and quiet[j]: j += 1
            dur = (j - i) * 0.010
            if dur > min_pause:
                keep = max(floor, keep_frac * dur)
                remove = min(dur - keep, max_cut)
                usable = dur - 2 * margin
                remove = min(remove, max(0.0, usable))
                if remove > 0.020:
                    mid = (i + j) / 2.0 * 0.010
                    cuts.append((mid - remove / 2.0, mid + remove / 2.0, dur))
            i = j
        else:
            i += 1
    out, pos, xf = [], 0, int(xfade * fs)
    removed = 0.0
    for a, b, _ in cuts:
        a_i, b_i = int(a * fs), int(b * fs)
        seg = x[pos:a_i]
        out.append(seg)
        pos = b_i
        removed += (b_i - a_i) / fs
    out.append(x[pos:])
    # crossfade the joins: rebuild with overlap
    if len(cuts) == 0:
        return x.copy(), {"cuts": [], "removed_s": 0.0}
    y = out[0].copy()
    for s in out[1:]:
        n = min(xf, len(y), len(s))
        if n > 0:
            f = np.linspace(0, 1, n)
            y[-n:] = y[-n:] * (1 - f) + s[:n] * f
            y = np.concatenate([y, s[n:]])
        else:
            y = np.concatenate([y, s])
    return y, {"cuts": cuts, "removed_s": removed}


# The three user-facing pause settings (Off is "do not call it"). Each is a set of the same four constants; the detector threshold is shared. Starting values to be
# chosen by listening (B-536): Small only trims clear sentence pauses, Medium is the original default, High also trims short breaths and caps cuts higher.
PRESETS = {
    "small":  dict(min_pause=0.400, keep_frac=0.60, floor=0.200, max_cut=0.400, thr_db=6.0),
    "medium": dict(min_pause=0.250, keep_frac=0.40, floor=0.120, max_cut=0.700, thr_db=9.0),
    "high":   dict(min_pause=0.150, keep_frac=0.25, floor=0.080, max_cut=1.000, thr_db=12.0),
}


def f0_median(x, fs, lo=70.0, hi=400.0):
    """Median f0 over voiced frames (40 ms, normalised autocorrelation peak > 0.5)."""
    n = int(0.040 * fs); hop = n // 2
    l0, l1 = int(fs / hi), int(fs / lo)
    vals = []
    thr = 0.05 * np.sqrt(np.mean(x * x))
    for s in range(0, len(x) - n - l1, hop):
        f = x[s: s + n + l1]
        a = f[:n]
        if np.sqrt(np.mean(a * a)) < thr: continue
        win = np.lib.stride_tricks.sliding_window_view(f, n)[l0:l1 + 1]
        num = win @ a
        sc = num / np.sqrt(np.einsum("ij,ij->i", win, win) * float(a @ a) + 1e-12)
        k = int(np.argmax(sc))
        if sc[k] > 0.5: vals.append(fs / (l0 + k))
    return float(np.median(vals)) if vals else float("nan")


def worst_jump(x):
    """Largest sample-to-sample step relative to the local RMS (a click shows up as a large ratio)."""
    d = np.abs(np.diff(x))
    n = 441
    rms = np.sqrt(np.convolve(x * x, np.ones(n) / n, mode="same"))[1:] + 1e-4
    return float(np.max(d / rms))


def score(name, x_in, y, speed, fs, info=None):
    f_in, f_out = f0_median(x_in, fs), f0_median(y, fs)
    row = {"name": name, "dur_ratio": len(x_in) / max(1, len(y)), "target": speed, "pitch_ratio": f_out / f_in if f_in == f_in else float("nan"),
           "jump": worst_jump(y)}
    if info is not None and len(info["scores"]):
        s = info["scores"]
        row.update(mean_ncc=float(s.mean()), low=float((s < 0.5).mean()), macs_per_s=info["macs"] / (len(y) / fs) if info["macs"] else 0.0)
    return row


def selftest():
    fs = 44100
    t = np.arange(fs * 6) / fs
    # a harmonic "voice": 140 Hz fundamental with 8 harmonics and a slow amplitude wobble
    x = sum(np.sin(2 * np.pi * 140 * k * t) / k for k in range(1, 9)) * (0.6 + 0.3 * np.sin(2 * np.pi * 1.7 * t))
    x /= np.abs(x).max()
    ok = True
    for sp in (0.8, 1.25, 1.5, 2.0):
        y, info = wsola(x, sp)
        r = score("w", x, y, sp, fs, info)
        good = abs(r["dur_ratio"] - sp) / sp < 0.03 and abs(r["pitch_ratio"] - 1.0) < 0.02 and r["mean_ncc"] > 0.95
        print("%s speed %.2f: dur_ratio %.3f pitch_ratio %.3f mean_ncc %.3f" % ("ok  " if good else "FAIL", sp, r["dur_ratio"], r["pitch_ratio"], r["mean_ncc"]))
        ok &= good
    v = varispeed(x, 1.5)
    pr = f0_median(v, fs) / f0_median(x, fs)
    good = abs(pr - 1.5) < 0.03
    print("%s varispeed 1.5: pitch_ratio %.3f (should be 1.5)" % ("ok  " if good else "FAIL", pr)); ok &= good
    # the search must matter: a signal with a slowly drifting period makes fixed grains misalign
    td = np.cumsum(140 + 25 * np.sin(2 * np.pi * 0.7 * t)) / fs
    xd = sum(np.sin(2 * np.pi * td * k) / k for k in range(1, 7)); xd /= np.abs(xd).max()
    _, iw = wsola(xd, 1.5); _, io = ola(xd, 1.5)
    good = iw["scores"].mean() > io["scores"].mean() + 0.05
    print("%s search beats fixed grains on a drifting period: ncc %.3f vs %.3f" % ("ok  " if good else "FAIL", iw["scores"].mean(), io["scores"].mean())); ok &= good
    # pause shortening: tone bursts separated by silence; pauses shrink, bursts untouched
    sil = np.zeros(int(0.9 * fs)); burst = (0.5 * np.sin(2 * np.pi * 200 * np.arange(int(0.6 * fs)) / fs))
    z = np.concatenate([burst, sil, burst, sil, burst]) + np.random.RandomState(1).randn(3 * len(burst) + 2 * len(sil)) * 1e-4
    zs, zi = shorten_pauses(z, fs)
    good = len(zi["cuts"]) == 2 and 0.8 < zi["removed_s"] < 1.4 and len(zs) < len(z)
    print("%s pause shortening: %d cuts, %.2f s removed" % ("ok  " if good else "FAIL", len(zi["cuts"]), zi["removed_s"])); ok &= good
    print("PASSED" if ok else "FAILED")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=["selftest", "run", "pauses"])
    ap.add_argument("wav", nargs="?")
    ap.add_argument("--speeds", type=float, nargs="+", default=[1.25, 1.5, 2.0])
    ap.add_argument("--start", type=float, default=30.0)
    ap.add_argument("--dur", type=float, default=60.0)
    ap.add_argument("--out", default=None, help="write WAVs here (wsola, ola and varispeed per speed, plus pause-shortened versions)")
    a = ap.parse_args()
    if a.cmd == "selftest":
        sys.exit(selftest())
    if a.cmd == "pauses":
        x, fs = read_wav(a.wav)
        full = x
        x = x[int(a.start * fs): int((a.start + a.dur) * fs)]
        out = Path(a.out) if a.out else None
        if out: out.mkdir(parents=True, exist_ok=True)
        print("whole file %.0f s; excerpt %.0f s from %.0f s" % (len(full) / fs, len(x) / fs, a.start))
        print("%-8s %-9s %6s %9s %9s %9s %s" % ("preset", "scope", "cuts", "removed", "saved", "shortest", "pause kept (min)"))
        for name, kw in PRESETS.items():
            for scope, sig in (("file", full), ("excerpt", x)):
                ys, si = shorten_pauses(sig, fs, **kw)
                tot = len(sig) / fs
                kept = min([c[2] - (c[1] - c[0]) for c in si["cuts"]] or [0.0])
                print("%-8s %-9s %6d %8.1fs %8.1f%% %8.2fs" % (name, scope, len(si["cuts"]), si["removed_s"], 100 * si["removed_s"] / tot, kept))
                if out and scope == "excerpt":
                    write_wav(out / ("pauses_%s.wav" % name), ys, fs)
                    y15, _ = wsola(ys, 1.5, fs=fs); write_wav(out / ("pauses_%s_plus_wsola_1.50.wav" % name), y15, fs)
        return
    x, fs = read_wav(a.wav)
    x = x[int(a.start * fs): int((a.start + a.dur) * fs)]
    print("input: %.1f s at %d Hz" % (len(x) / fs, fs))
    rows = []
    out = Path(a.out) if a.out else None
    if out: out.mkdir(parents=True, exist_ok=True); write_wav(out / "original.wav", x, fs)
    for sp in a.speeds:
        yw, iw = wsola(x, sp, fs=fs); rows.append(score("wsola %.2fx" % sp, x, yw, sp, fs, iw))
        yo, io = ola(x, sp, fs=fs);   rows.append(score("fixed-grain %.2fx" % sp, x, yo, sp, fs, io))
        yv = varispeed(x, sp);        rows.append(score("varispeed %.2fx" % sp, x, yv, sp, fs))
        if out:
            tag = "%.2f" % sp
            write_wav(out / ("wsola_%s.wav" % tag), yw, fs); write_wav(out / ("fixedgrain_%s.wav" % tag), yo, fs); write_wav(out / ("varispeed_%s.wav" % tag), yv, fs)
    print("%-20s %9s %9s %9s %9s %9s %12s" % ("method", "dur x", "target", "pitch x", "mean ncc", "ncc<0.5", "MAC/out-s"))
    for r in rows:
        print("%-20s %9.3f %9.2f %9.3f %9s %9s %12s" % (r["name"], r["dur_ratio"], r["target"], r["pitch_ratio"],
              "%.3f" % r["mean_ncc"] if "mean_ncc" in r else "-", "%.1f%%" % (100 * r["low"]) if "low" in r else "-",
              "%.2fM" % (r["macs_per_s"] / 1e6) if r.get("macs_per_s") else "-"))
    print("worst sample jump / local rms:", ", ".join("%s %.1f" % (r["name"], r["jump"]) for r in rows))
    # pause shortening, then WSOLA at 1.5x
    ys, si = shorten_pauses(x, fs)
    print("pause shortening (default 'Normal'): %d cuts, %.1f s removed of %.1f s (%.1f%%)" % (len(si["cuts"]), si["removed_s"], len(x) / fs, 100 * si["removed_s"] / (len(x) / fs)))
    if out:
        write_wav(out / "pauses_only.wav", ys, fs)
        y15, _ = wsola(ys, 1.5, fs=fs); write_wav(out / "pauses_plus_wsola_1.50.wav", y15, fs)
        print("total at 1.5x with pause shortening: %.1f s (without: %.1f s)" % (len(y15) / fs, len(x) / fs / 1.5))


if __name__ == "__main__":
    main()
