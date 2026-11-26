#!/usr/bin/env python3
"""MP3 v2 WP-04: rail and headroom count over an MP3 library (own code). Host only, read-only on the library.

Per track it runs three decoders and compares them:
  * the float oracle (tools/mp3_v2/oracle.py: pristine minimp3, 1.0 = 16-bit full scale) -> peak, samples beyond +-1.0, a float dump used as the reference;
  * tools/mp3_v2/rail_xr.c (the same decoder with a probe in L3_decode, on a probed COPY of the header, verified to differ only by the probe) -> the largest
    spectral magnitude |xr| after requantisation and stereo processing (input to the fixed-point format study WP-12);
  * tools/mp3_v2/rail_count.c (Helix as Player 2 builds it, TAU_POLY_FW/REQUIRED, the window unit as its golden model, one model instance per gain) -> samples at
    the 16-bit rails, the largest word pushed to the window unit, the error against the ideal clamp16(oracle * g) for a pre-synthesis gain and for the same gain
    applied after the clamp.

  rail_count.py [--json OUT.json] [--markdown OUT.md] [--jobs N] [--quiet] [PATH ...]
PATH is a directory (searched recursively for .mp3) or a file; without a PATH the owner's known library locations are used when they exist.
Library use: analyse_file(path, exes) -> dict; summarise(list) -> dict; markdown(list, roots) -> text; build() -> exes.

DEFINITIONS (all per 16-bit output sample, both channels counted; o16 = oracle float * 32768; "rail" = the 16-bit value 32767 or -32768):
  helix_rail          Helix's 16-bit output (the window unit's clamp) is at a rail.
  oracle_rail         the oracle's 16-bit output (minimp3's own conversion) is at a rail, i.e. o16 >= 32766.5 or <= -32767.5.
  both_rail           both: clipping the master and any decoder share.
  helix_only_rail     Helix at a rail, oracle not: the candidates for clipping CREATED by the decoder (the integer pipeline) -- an UPPER BOUND, because the two decoders
                      differ by a LSB or two at every sample and a sample at 32766.4 versus 32766.6 lands on opposite sides of the line.
  helix_only_clear    the same with the oracle at least 2 LSB inside the rails: a tighter count, still not proof of a decoder error.
  oracle_only_rail    oracle at a rail, Helix not: clipping the oracle has and Helix lacks (Helix rounds towards a lower value: a LOWER bound of Helix's loss).
  err_gt1/2(g)        samples where the pre-gain Helix output differs from the ideal clamp16(o16 * g) by more than 1 (2) LSB. Helix and the float oracle differ by at most
                      2 LSB on ordinary samples, so more than 2 is a real defect, not rounding.
  post_err_gt1/2(g)   the same for round(Helix_clamped * g): the damage of applying a gain AFTER the clamp (clipped tops stay flat; also two roundings).
  xr                  largest |xr| after requantisation and MS/intensity, before reorder, antialias and IMDCT, in minimp3's internal scale; minimp3 scales its dequantiser
                      output by 2^(BITS_DEQUANTIZER_OUT) = 1/2 against the standard (minimp3.h line 80 and 732), so the standard's own scale is twice the printed number [DERIVED].
  max_word            the largest |word| Helix pushes to the window unit (words = V * 2^23, WP-02), signed 32-bit legal range 2^31.
"""
import json, os, re, subprocess, sys, tempfile, hashlib
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / "tools"))
import oracle
OUT = ROOT / "build/mp3_v2"
KNOWN_SDK = "/Library/Developer/CommandLineTools/SDKs/MacOSX26.5.sdk"
DEFAULT_LIBRARIES = ["/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/test music", "/Users/abel.santos/Downloads/DEV PROJECTS/test music"]
GAIN_DB = [-1.0, -3.0, -6.0, -9.0]          # fixed pre-gains tried next to unity; then the per-track no-clip gain and the estimated ReplayGain (min(-18 LUFS - loudness, 0 dB))
# letters of the WP-03 report (docs/features/MP3_LIBRARY_SCAN_2026-11-25.md section 3): folder-name prefix -> letter; anything else falls into F/G/H (test-signal folders)
FOLDER_LETTERS = [("Cowboy Bebop", "A"), ("Ghost in the Shell", "B"), ("Nausicaa of the Valley of the Wind Image", "C"), ("Nausicaä", "D"), ("Nausicaa", "D"), ("Test Album", "E")]

# the two insertions of the probed header copy (L3_decode in minimp3.h): globals before the function, the measurement between the stereo processing and the per-channel loop
XR_GLOBALS = ("static float tau_xr_max; static unsigned long long tau_xr_count, tau_xr_hist[25];\n"
              "static void tau_xr_probe(const float *g, int nch) { int c, i, e; for (c = 0; c < nch; c++) { float m = 0; for (i = 0; i < 576; i++) { float a = g[c*576+i] < 0 ? -g[c*576+i] : g[c*576+i]; if (a > m) m = a; }"
              " if (m > tau_xr_max) tau_xr_max = m; tau_xr_count++; { float t = 1.0f/524288.0f; for (e = 0; e < 24 && m >= t; e++) t *= 2.0f; } tau_xr_hist[e]++; } }\n")
XR_CALL = "    tau_xr_probe(s->grbuf[0], nch);\n"
XR_ANCHOR_1 = "static void L3_decode(mp3dec_t *h, mp3dec_scratch_t *s, L3_gr_info_t *gr_info, int nch)\n"
XR_ANCHOR_2 = "    for (ch = 0; ch < nch; ch++, gr_info++)\n    {\n        int aa_bands = 31;\n"


def _sdk():
    sdk = os.environ.get("SDKROOT") or (KNOWN_SDK if Path(KNOWN_SDK).is_dir() else "")
    return ["-isysroot", sdk] if sdk and sys.platform == "darwin" else []


def probed_header(out_dir):
    """write the probed copy of minimp3.h; fail unless removing the two insertions gives back the vendored file byte for byte"""
    src = (ROOT / "third_party/minimp3/minimp3.h").read_text()
    assert src.count(XR_ANCHOR_1) == 1 and src.count(XR_ANCHOR_2) == 1, "minimp3.h changed: the probe anchors no longer match"
    patched = src.replace(XR_ANCHOR_1, XR_GLOBALS + XR_ANCHOR_1).replace(XR_ANCHOR_2, XR_CALL + XR_ANCHOR_2)
    assert patched.replace(XR_GLOBALS, "").replace(XR_CALL, "") == src, "the probe changes more than its insertions"
    d = out_dir / "xr_probe"; d.mkdir(parents=True, exist_ok=True)
    (d / "minimp3.h").write_text(patched)
    return d


def helix_host_dir(out_dir):
    """the Helix tree every consumer compiles, with the portable host assembly.h in place of the RISC-V one (as sim/test_p2_mp3.py does)"""
    from helix_tree import helix_tree
    H = helix_tree()
    d = out_dir / "helix_host"; d.mkdir(parents=True, exist_ok=True)
    for f in list((H / "real").glob("*.c")) + list((H / "real").glob("*.h")): (d / f.name).write_bytes(f.read_bytes())
    (d / "assembly.h").write_bytes((ROOT / "sim/helix_host/assembly.h").read_bytes())
    for f in ("mp3dec.c", "mp3tabs.c"): (d / f).write_bytes((H / f).read_bytes())
    return H, d


def build(out_dir=None):
    """-> {'rail_count': exe, 'rail_xr': exe, 'oracle': exe}; rebuilt only when an input changed"""
    out = Path(out_dir) if out_dir else OUT
    out.mkdir(parents=True, exist_ok=True)
    cc = os.environ.get("CC", "cc")
    exes = {"oracle": oracle.build(out)}
    # rail_xr: oracle's flags on the probed header copy
    pd = probed_header(out)
    xr = out / "rail_xr"; xs = out / "rail_xr.stamp"
    key = hashlib.sha256((pd / "minimp3.h").read_bytes() + (Path(__file__).with_name("rail_xr.c")).read_bytes() + " ".join(oracle.FLAGS).encode()).hexdigest()
    if not (xr.exists() and xs.exists() and xs.read_text() == key):
        subprocess.run([cc, *_sdk(), *oracle.FLAGS, "-I", str(pd), "-o", str(xr), str(Path(__file__).with_name("rail_xr.c")), "-lm"], check=True)
        xs.write_text(key)
    exes["rail_xr"] = xr
    # rail_count: Helix as Player 2 builds it + the unit model
    import gen_mp3_poly_rom as G
    H, hd = helix_host_dir(out)
    (out / "rail_tables.h").write_text(G.render_h(G.build_map(), G.coefs()))
    srcs = [hd / f for f in ["mp3dec.c", "mp3tabs.c"] + [f"{n}.c" for n in "bitstream buffers dct32 dequant dqchan huffman hufftabs imdct polyphase scalfact stproc subband trigtabs".split()]]
    rc = out / "rail_count"; rs = out / "rail_count.stamp"
    me = Path(__file__).parent
    key = hashlib.sha256(b"".join(p.read_bytes() for p in srcs + [me / "rail_count.c", me / "rail_unit_inst.h", ROOT / "sim/mp3_poly_model_core.h", ROOT / "fw/mp3_poly_hw.h", out / "rail_tables.h"])).hexdigest()
    if not (rc.exists() and rs.exists() and rs.read_text() == key):
        subprocess.run([cc, *_sdk(), "-std=gnu11", "-O2", "-w", "-DTAU_POLY_FW=1", "-DTAU_POLY_REQUIRED=1", f'-DTABLES_H="{out / "rail_tables.h"}"',
                        "-I", str(hd), "-I", str(H / "pub"), "-I", str(ROOT / "fw"), "-I", str(ROOT / "sim"), "-I", str(me), "-o", str(rc), str(me / "rail_count.c")] + [str(s) for s in srcs] + ["-lm"], check=True)
        rs.write_text(key)
    exes["rail_count"] = rc
    return exes


def _json(cmd):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    if r.returncode: raise RuntimeError(f"{cmd[0]} failed ({r.returncode}): {r.stderr.strip()[:200]}")
    return json.loads(r.stdout.strip().splitlines()[-1])


def analyse_file(path, exes, gain_db=GAIN_DB):
    """one track -> dict (counts only; the caller adds names)"""
    with tempfile.TemporaryDirectory(prefix="railcount_") as td:
        f32 = Path(td) / "o.f32"
        o = _json([exes["oracle"], path, "--f32", f32])
        x = _json([exes["rail_xr"], path])
        peak = o["peak"]
        nc = 1.0 if peak <= 1.0 else (32766.4 / 32768.0) / peak           # the largest gain that keeps every sample off the rail (by a hair, for the rounding)
        gains = [1.0] + [10 ** (d / 20.0) for d in gain_db] + [nc, 1.0]      # the last one is replaced by the estimated ReplayGain (--rg)
        h = _json([exes["rail_count"], path, "--ref", f32, "--gains", ",".join(f"{g:.12g}" for g in gains), "--rg", f"{o['hz']},{o['channels']}"])
    return {"path": str(path), "oracle": o, "xr": x, "helix": h}


def letter_of(path, roots):
    for root in roots:
        try: rel = Path(path).relative_to(root)
        except ValueError: continue
        if len(rel.parts) > 1:
            for pre, let in FOLDER_LETTERS:
                if rel.parts[0].startswith(pre): return let
        return "F"
    return "F"


def summarise(results):
    S = {"files": len(results), "samples": 0, "helix_rails": 0, "oracle_rails": 0, "oracle_over_1": 0, "both": 0, "helix_only": 0, "helix_only_clear": 0, "oracle_only": 0, "oracle_only_far": 0,
         "files_helix_rail": 0, "files_over_1": 0, "peak": 0.0, "xr_max": 0.0, "max_word": 0, "align_bad": 0, "unmatched": 0, "lag_nonzero": 0, "frames_differ": 0, "gains": {}}
    for r in results:
        h, o, x = r["helix"], r["oracle"], r["xr"]
        S["samples"] += o["pairs"] * o["channels"]
        S["helix_rails"] += h["instances"][0]["rails"]; S["oracle_rails"] += o["rail16"]; S["oracle_over_1"] += o["float_over_1"]
        for k in ("both", "helix_only", "helix_only_clear", "oracle_only", "oracle_only_far"):
            S[k] += h[{"both": "both_rail", "helix_only": "helix_only_rail", "helix_only_clear": "helix_only_rail_clear", "oracle_only": "oracle_only_rail", "oracle_only_far": "oracle_only_rail_far"}[k]]
        S["files_helix_rail"] += h["instances"][0]["rails"] > 0; S["files_over_1"] += o["float_over_1"] > 0
        S["peak"] = max(S["peak"], o["peak"]); S["xr_max"] = max(S["xr_max"], x["xr_max"]); S["max_word"] = max(S["max_word"], h["max_word"])
        S["align_bad"] += h["align_bad"]; S["unmatched"] += h["unmatched"]; S["lag_nonzero"] += h["lag"] != 0
        S["frames_differ"] += h["helix_samples"] != h["oracle_samples"]
        for i, inst in enumerate(h["instances"]):
            key = "rg" if i == len(h["instances"]) - 1 else "nc" if i == len(h["instances"]) - 2 else f"{20 * __import__('math').log10(inst['gain']):.1f}"
            g = S["gains"].setdefault(key, {"rails": 0, "err_gt1": 0, "err_gt2": 0, "post_err_gt1": 0, "post_err_gt2": 0, "post_rails": 0, "err_max": 0, "post_err_max": 0, "files_post_damage": 0, "files_rails": 0})
            g["rails"] += inst["rails"]; g["err_gt1"] += inst["err_gt1"]; g["err_gt2"] += inst["err_gt2"]; g["post_err_gt1"] += inst["post_err_gt1"]; g["post_err_gt2"] += inst["post_err_gt2"]; g["post_rails"] += inst["post_rails"]
            g["err_max"] = max(g["err_max"], inst["err_max"]); g["post_err_max"] = max(g["post_err_max"], inst["post_err_max"])
            g["files_post_damage"] += inst["post_err_gt2"] > 0; g["files_rails"] += inst["rails"] > 0
    return S


def markdown(results, roots):
    """per-track tables (lettered folder and track number; counts only) and the aggregate"""
    import math
    rows = []; seq = {}
    for r in sorted(results, key=lambda r: (letter_of(r["path"], roots), r["path"])):
        let = letter_of(r["path"], roots); seq[let] = seq.get(let, 0) + 1
        rows.append((f"{let}{seq[let]}", r))
    t1 = ["| Track | Frames | Float peak | Float samples > 1.0 | Helix rail samples | Oracle rail samples | Both | Helix only | Helix only (2 LSB clear) | Oracle only | Frames/lag differ | Samples off by > 2 LSB |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|"]
    t2 = ["| Track | max abs xr (minimp3 units) | xr in the standard's scale (x2) | log2 of that | max abs window word | word / 2^31 | word headroom (bits) | no-clip gain (dB) |", "|---|---|---|---|---|---|---|---|"]
    t3 = ["| Track | Rails at -3 dB pre-gain | Post-clamp damage at -3 dB (samples off by > 2 LSB) | Post-clamp damage at -6 dB | Post-clamp damage at -9 dB | Pre-gain error > 2 LSB at -6 dB | Rails at no-clip pre-gain | Post-clamp damage at the no-clip gain |", "|---|---|---|---|---|---|---|---|"]
    t4 = ["| Track | Loudness (LUFS) | Estimated ReplayGain 2.0 (dB, capped at 0) | Rails at that pre-gain | Pre-gain error > 2 LSB | Post-clamp damage (> 2 LSB) | Post-clamp rails | Gain needed to clear the rails (dB) |", "|---|---|---|---|---|---|---|---|"]
    for name, r in rows:
        h, o, x = r["helix"], r["oracle"], r["xr"]; I = h["instances"]
        t1.append(f"| {name} | {h['helix_frames']:,} | {o['peak']:.4f} | {o['float_over_1']:,} | {I[0]['rails']:,} | {o['rail16']:,} | {h['both_rail']:,} | {h['helix_only_rail']:,} | {h['helix_only_rail_clear']:,} | {h['oracle_only_rail']:,} | "
                  f"{'yes' if (h['lag'] or h['helix_samples'] != h['oracle_samples']) else 'no'} | {h['align_bad']:,} |")
        mw = max(h["max_word"], 1)
        ncdb = 0.0 if o["peak"] <= 1.0 else -20 * math.log10(o["peak"])
        t2.append(f"| {name} | {x['xr_max']:.4f} | {2 * x['xr_max']:.4f} | {math.log2(max(2 * x['xr_max'], 1e-9)):.2f} | {h['max_word']:,} | {h['max_word'] / 2 ** 31:.4f} | {31 - math.log2(mw):.2f} | {ncdb:.2f} |")
        # instance order: 0 unity, 1 -1 dB, 2 -3 dB, 3 -6 dB, 4 -9 dB, 5 no-clip
        t3.append(f"| {name} | {I[2]['rails']:,} | {I[2]['post_err_gt2']:,} | {I[3]['post_err_gt2']:,} | {I[4]['post_err_gt2']:,} | {I[3]['err_gt2']:,} | {I[5]['rails']:,} | {I[5]['post_err_gt2']:,} |")
        R = I[6]; rgdb = min(0.0, -18.0 - h["lufs"])
        t4.append(f"| {name} | {h['lufs']:.1f} | {rgdb:.1f} | {R['rails']:,} | {R['err_gt2']:,} | {R['post_err_gt2']:,} | {R['post_rails']:,} | {ncdb:.2f} |")
    return "\n".join(t1) + "\n\n" + "\n".join(t2) + "\n\n" + "\n".join(t3) + "\n\n" + "\n".join(t4) + "\n"


def find_mp3(paths):
    out = []
    for p in paths:
        p = Path(p)
        if p.is_file(): out.append(p)
        elif p.is_dir(): out.extend(sorted(q for q in p.rglob("*") if q.suffix.lower() == ".mp3" and q.is_file()))
    return out


_EXES = None
def _work(path):
    return analyse_file(path, _EXES)
def _init(exes):
    global _EXES; _EXES = exes


def main():
    a = sys.argv[1:]
    def opt(name):
        if name in a:
            i = a.index(name); v = a[i + 1]; del a[i:i + 2]; return v
    jout, mout, jobs = opt("--json"), opt("--markdown"), int(opt("--jobs") or (os.cpu_count() or 2))
    quiet = "--quiet" in a; a = [x for x in a if x != "--quiet"]
    paths = a or [p for p in DEFAULT_LIBRARIES if Path(p).exists()]
    files = find_mp3(paths)
    if not files: print("no MP3 files found in", paths or DEFAULT_LIBRARIES); return 0
    exes = build()
    with ProcessPoolExecutor(max_workers=jobs, initializer=_init, initargs=(exes,)) as ex:
        results = []
        for r in ex.map(_work, files):
            results.append(r)
            if not quiet: print(f"{Path(r['path']).name[:44]:44s} peak {r['oracle']['peak']:.4f} helix rails {r['helix']['instances'][0]['rails']:7d} oracle rails {r['oracle']['rail16']:7d} xr {r['xr']['xr_max']:9.0f}", flush=True)
    S = summarise(results)
    roots = [p for p in paths if Path(p).is_dir()]
    md = markdown(results, roots)
    print(json.dumps(S, indent=1)); print(md)
    if jout: Path(jout).write_text(json.dumps({"summary": S, "files": results}, indent=1))
    if mout: Path(mout).write_text(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
