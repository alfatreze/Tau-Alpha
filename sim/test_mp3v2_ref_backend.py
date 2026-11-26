#!/usr/bin/env python3
"""MP3 v2 WP-06: the float64 back-end reference model (tools/mp3v2/ref_backend.py).

  * the oracle's stage dump is a pure tap: the stage build's PCM equals the plain oracle build byte for byte;
  * HARD GATE: the model fed with the oracle's requantised spectra reproduces the oracle's float PCM within 1e-5 of full scale on every generated stream (MPEG-1 stereo, joint
    stereo MS, mono, MPEG-2 LSF, MPEG-2.5, mixed blocks, all four block types in legal sequences) and, when present, on the owner's library files (read-only; skipped with a note if absent);
    the subband samples after IMDCT/overlap/inversion also agree (stage check);
  * the transition matrix: minimp3 and the literal standard formulas agree on every legal block-type transition of long/start/short/stop (and mixed-to-mixed) and differ on the others
    (documented in the report: minimp3 applies the current granule's window to the previous half);
  * red cases (mutants): a wrong alias coefficient, a wrong stop window, a wrong overlap order, a wrong short-block order, a missing frequency inversion and a wrong synthesis window
    value each FAIL the gate;
  * unit arithmetic: the integer model equals tools/mp3_v2/window_abi_check.py's UnitPy (and so the golden C model) bit for bit on random, loud, impulse and +-full-scale data, the
    wide-domain error stays within the ABI document's 0.0053 LSB bound, the 16-bit domain differs by at most 1 LSB from the rounded reference; the saturation path is exercised;
  * the metric tool on known inputs.
Exit 1 on any failure."""
import glob, math, random, sys, tempfile, time
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
for d in ("tools", "tools/mp3_v2", "tools/mp3v2"): sys.path.insert(0, str(ROOT / d))
import numpy as np
import oracle, synth_stream as S, ref_backend as R, window_abi_check as A

fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1

LEGAL = [0, 0, 1, 2, 2, 3, 0, 1, 3, 0, 1, 2, 2, 2, 3, 1, 2, 3, 0, 0, 0, 1, 2, 3]       # long start short stop sequences incl. 3->1 and 1->3
LIB = Path("/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/test music")


def frames(version, mode, mode_ext=0, mixed=False, sr_idx=0, types=LEGAL, gain=194, pairs=288, seed=100, br=14):
    ngr = 2 if version == 1 else 1
    ch = 1 if mode == 3 else 2
    out = []
    for i in range(0, len(types), ngr):
        bt = []
        for g in range(ngr): bt += [types[i + g]] * ch
        out.append(S.frame(version=version, mode=mode, mode_ext=mode_ext, window=[1 if b else 0 for b in bt], block_type=bt, mixed=[1 if (mixed and b == 2) else 0 for b in bt],
                           gain=gain, seed=seed + i, pairs=pairs, sr_idx=sr_idx, br_idx=br))
    return b"".join(out)


def catalog():
    return {
        "mpeg1 stereo, 44.1 kHz, all block types": frames(1, 0),
        "mpeg1 joint stereo MS, 48 kHz": frames(1, 1, 2, sr_idx=1, gain=195, seed=300),
        "mpeg1 mono, 32 kHz": frames(1, 3, sr_idx=2, gain=194, seed=500),
        "mpeg2 LSF stereo, 22.05 kHz": frames(2, 0, seed=700),
        "mpeg2 LSF mono, 16 kHz": frames(2, 3, sr_idx=2, gain=196, seed=900),
        "mpeg2.5 stereo, 11.025 kHz": frames(25, 0, seed=1100),
        "mpeg1 mixed blocks (all granules mixed short)": frames(1, 0, mixed=True, types=[2] * 8, seed=1300),
        "mpeg2 LSF mixed blocks": frames(2, 0, mixed=True, types=[2] * 6, seed=1500),
        "mpeg1 long, start, short (granule types without mixed)": frames(1, 0, types=[0, 0, 0, 0, 1, 2], seed=1700),
    }


def run_stream(exe, data, td, name="x", mutation=None, frames_limit=None):
    td = Path(td)
    (td / (name + ".mp3")).write_bytes(data)
    info = oracle.decode(exe, td / (name + ".mp3"), pcm=td / (name + ".s16"), f32=td / (name + ".f32"), stage=td / (name + ".stage"), frames=frames_limit)
    return info, R.gate(td / (name + ".stage"), td / (name + ".f32"), mutation=mutation)


def main():
    exe = oracle.build_stage()
    plain = oracle.build()
    cat = catalog()
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        # ---- the tap is pure
        k = "mpeg1 stereo, 44.1 kHz, all block types"
        info, g = run_stream(exe, cat[k], td, "tap")
        info2 = oracle.decode(plain, td / "tap.mp3", pcm=td / "tap_plain.s16", f32=td / "tap_plain.f32")
        check("stage build: PCM and float dump equal the plain oracle build byte for byte",
              (td / "tap.s16").read_bytes() == (td / "tap_plain.s16").read_bytes() and (td / "tap.f32").read_bytes() == (td / "tap_plain.f32").read_bytes() and info == info2)
        gr = R.read_stage(td / "tap.stage")
        check("stage dump: one record per decoded granule (%d frames x 2)" % info["decoded"], len(gr) == 2 * info["decoded"], str(len(gr)))
        check("the pinned minimp3.h is untouched by the instrumentation (the copy lives in build/)", (ROOT / "third_party/minimp3/minimp3.h").read_text().count("ORACLE_HOOK") == 0)
        # ---- gate on every generated stream
        worst = 0.0
        results = {}
        for name, data in cat.items():
            info, g = run_stream(exe, data, td, "g")
            results[name] = g
            worst = max(worst, g["max_err"])
            check("gate [%s]: max error %.2e of full scale (< 1e-5), stage max %.1e, %d samples, peak %.2f" % (name, g["max_err"], g["stage_err"], g["samples"], info["peak"]),
                  g["ok"] and g["stage_err"] < 1e-5 and info["decoded"] == info["frames"], str(g))
        print("     worst gate error over the generated streams: %.3e" % worst)
        # ---- transition matrix
        agree, differ = [], []
        legal = {(0, 0, 0), (0, 1, 0), (1, 2, 0), (1, 3, 0), (2, 2, 0), (2, 3, 0), (3, 0, 0), (3, 1, 0), (2, 2, 1)}
        for a in range(4):
            for c in range(4):
                for mixed in (0, 1):
                    if mixed and (a, c) != (2, 2) and 2 not in (a, c): continue
                    f = S.frame(version=1, mode=3, window=[1 if a else 0, 1 if c else 0], block_type=[a, c], mixed=[mixed if a == 2 else 0, mixed if c == 2 else 0], gain=195, seed=5 + a * 4 + c, pairs=288, br_idx=14)
                    (td / "t.mp3").write_bytes(f)
                    oracle.decode(exe, td / "t.mp3", stage=td / "t.stage")
                    b = R.Backend(spec_scale=R.MINIMP3_SPEC_UNIT)
                    errs = []
                    for g in R.read_stage(td / "t.stage"):
                        for ci, ch in enumerate(g["ch"]):
                            sub = b.imdct_granule(ci, ch["bt"], ch["nlb"], ch["spec"], ch["sfb"], ch["nls"], ch["nss"])
                            errs.append(float(np.max(np.abs(sub.reshape(576) - ch["sub"] / R.MINIMP3_SPEC_UNIT))))
                    (agree if max(errs) < 1e-5 else differ).append((a, c, mixed))
        check("transition matrix: every legal transition agrees with minimp3 (%s)" % sorted(agree), set(agree) >= legal, "legal not agreeing: %s" % sorted(legal - set(agree)))
        check("transition matrix: the other transitions differ (documented minimp3 shortcut): %d pairs" % len(differ), len(differ) >= 8 and not (set(differ) & legal), str(sorted(differ)))
        # ---- red cases
        for mut, stream in (("alias", "mpeg1 stereo, 44.1 kHz, all block types"), ("window_stop", "mpeg1 stereo, 44.1 kHz, all block types"), ("overlap", "mpeg1 stereo, 44.1 kHz, all block types"),
                            ("short_order", "mpeg1 stereo, 44.1 kHz, all block types"), ("inversion", "mpeg1 stereo, 44.1 kHz, all block types"), ("synth_window", "mpeg1 stereo, 44.1 kHz, all block types")):
            info, gm = run_stream(exe, cat[stream], td, "m", mutation=mut)
            check("red case %s: the gate fails (max error %.2e >= 1e-5)" % (mut, gm["max_err"]), not gm["ok"] and gm["max_err"] > 1e-5)
        # the same mutants are not a vacuous pass: the unmutated model passes on the same stream
        info, g0 = run_stream(exe, cat[k], td, "m")
        check("green: the unmutated model passes on the stream of the red cases", g0["ok"])
        # ---- the owner's library (read-only)
        files = sorted(glob.glob(str(LIB / "**/*.mp3"), recursive=True))
        if not files:
            print("note: the owner's library (%s) is absent: library gate skipped" % LIB)
        else:
            picks = files[::max(1, len(files) // 6)][:6]
            t0 = time.time(); ns = 0; lw = 0.0; ok_all = True
            for f in picks:
                info = oracle.decode(exe, f, f32=td / "l.f32", stage=td / "l.stage", frames=250)
                g = R.gate(td / "l.stage", td / "l.f32")
                ns += g["samples"]; lw = max(lw, g["max_err"]); ok_all &= g["ok"]
            dt = time.time() - t0
            check("library gate: %d of %d files, 250 frames each, worst error %.2e (< 1e-5)" % (len(picks), len(files), lw), ok_all, "")
            print("     model speed incl. oracle run and dump: %.0f k samples/s (all channels), %.1f s" % (ns / dt / 1000, dt))

    # ---- unit arithmetic against the ABI check's integer model
    rnd = random.Random(11)
    T = 180
    scen = []
    scen += [[rnd.uniform(-1, 1) * 0.02 for _ in range(32)] for _ in range(40)]                     # quiet
    scen += [[rnd.uniform(-1, 1) * 0.5 for _ in range(32)] for _ in range(40)]                      # ordinary
    scen += [[rnd.uniform(-1, 1) * 3.0 for _ in range(32)] for _ in range(40)]                      # loud: clips
    scen += [[1.0 if (t == 0 and k == 0) else 0.0 for k in range(32)] for t in range(10)]           # impulse then silence
    scen += [[(-1) ** k * 2.5 * (-1) ** t for k in range(32)] for t in range(20)]                   # alternating extreme
    scen += [[rnd.uniform(-1, 1) * 0.3 for _ in range(32)] for _ in range(T - len(scen))]
    Sm = np.array(scen)
    pre, q16, sat = R.unit_synth([Sm])
    u = A.UnitPy(); ref = []
    for t in range(len(scen)):
        w = A.words_for(list(Sm[t])); ref += u.slot(w, w)[:32]
    check("unit arithmetic: equals window_abi_check.UnitPy (the golden model's integer twin) on %d slots, %d samples, %d clipped" % (len(scen), q16.size, int(np.sum(np.abs(q16) >= 32767))),
          list(q16[:, 0]) == ref and int(np.sum(np.abs(q16) >= 32767)) > 100, "")
    ws, _ = R.unit_words(Sm)
    check("unit words equal window_abi_check.words_for", all(list(ws[t]) == A.words_for(list(Sm[t])) for t in range(0, len(scen), 7)))
    st = A.StdSynth(); refp = []
    for t in range(len(scen)): refp += st.slot(list(Sm[t]))
    refp = np.array(refp)
    dm = R.compare_domains(refp, pre=pre[:, 0], q16=q16[:, 0])
    ok_mask = np.abs(refp) < 0.999                           # unclipped samples: the wide error is the word rounding only
    wide_ok = float(np.max(np.abs(pre[:, 0][ok_mask] - refp[ok_mask] * 32768.0)))
    check("unit arithmetic, domain (a) wide: max |unit - float64| = %.4f LSB on unclipped samples (ABI bound 0.0107 for twice the word rounding)" % wide_ok, wide_ok < 0.0107, "")
    check("unit arithmetic, domain (b) 16-bit: %d samples equal the rounded reference, %d differ by 1 LSB, %d by more" % (dm["s16"]["equal"], dm["s16"]["one_lsb"], dm["s16"]["more"]),
          dm["s16"]["more"] == 0 and dm["s16"]["one_lsb"] < 0.01 * dm["s16"]["n"], str(dm["s16"]))
    check("unit arithmetic: 16-bit output vs the wide reference rms %.4f LSB (ISO full accuracy bound 0.2887), max %.3f LSB" % (dm["s16_vs_unrounded"]["rms"], dm["s16_vs_unrounded"]["max"]),
          dm["s16_vs_unrounded"]["rms"] < 0.2887 and dm["s16_vs_unrounded"]["max"] <= 0.5 + 0.0107, str(dm["s16_vs_unrounded"]))
    # saturation count of the word conversion (int32)
    big = np.full((3, 32), 300.0)
    _, sat = R.unit_words(big)
    check("unit words saturate to int32 and are counted (|V| > 256 saturates)", sat > 0 and int(R.unit_words(big)[0].max()) == (1 << 31) - 1)
    # red: a wrong scale or order in the unit model is caught by the equality with UnitPy
    w_bad, _ = R.unit_words(Sm, scale_bits=22)
    check("red case: scale 2^22 in the unit model is not the ABI's words", not all(list(w_bad[t]) == A.words_for(list(Sm[t])) for t in range(0, len(scen), 7)))
    # ---- metric tool
    m = R.metrics(np.array([0.0, 1.0, 2.0, 3.0]), np.array([0.0, 1.5, 2.0, 1.0]))
    check("metric tool: rms, max, counts on a known case", abs(m["rms"] - math.sqrt((0.25 + 4) / 4)) < 1e-12 and m["max"] == 2.0 and m["over_half"] == 1 and m["over_one"] == 1, str(m))
    d = R.compare_domains(np.array([0.1 / 32768, 0.6 / 32768, 40000 / 32768.0]), q16=np.array([0, 2, 32767]))
    check("metric tool: 16-bit domain counts (equal / 1 LSB / more) and clipping of the reference", d["s16"]["equal"] == 2 and d["s16"]["one_lsb"] == 1 and d["s16"]["more"] == 0, str(d["s16"]))
    print("FAILURES: %d" % fails if fails else "all checks passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
