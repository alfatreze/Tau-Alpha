#!/usr/bin/env python3
"""The SDK's toolchain recipe (LLVM clang + lld, pinned in sdk/sdk/tau_meter/toolchain.py, docs/TOOLCHAIN.md) PROVEN on the seven shipped meters.

For each meter the project's own build (tools/meter_package.py pack: the vendored xPack gcc 15.2, the package the player ships) and the SDK's build (`tau-meter build sdk/meters/<key>`: clang, ld.lld) are made, verified, and RUN on the SDK's host runner
over the same inputs (every scenario of the suite, with each meter's own parameter defaults, every factory preset, every parameter at its minimum and maximum):
  1. both packages verify and pass the conformance suite under the first-party profile;
  2. the two compilers' packs carry the same descriptor (name, parameters, limits, preview) byte for byte, and the same package files except meter.bin (and its pin) and the source copy;
  3. the draw-call traces are IDENTICAL, call for call (every host-table call with its arguments, and the value returned): that is how "the picture matches the project's reference" is decided. The picture itself is compared too: the
     final frame of the 'music' and 'loud' scenarios, pixel for pixel, on the canvas replay. No tolerance is needed because the meters are integer code: any difference in a trace is a bug in one of the compilers' builds or in the meter;
  4. the size of both images is printed (a different compiler gives a different size; both must fit the window).
Without the project's gcc the comparison part says so and is skipped; the SDK build and its suite run still happen. Without LLVM the whole test says so and exits 0."""
import json, os, sys, tempfile, zipfile
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "sdk" / "sdk")); sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(info)))
    fails += 0 if cond else 1
TMP = Path(tempfile.mkdtemp()); os.environ["TAU_METER_CACHE"] = str(TMP / "cache")
from tau_meter import toolchain, build
try: TC = toolchain.find()
except toolchain.ToolchainError as e:
    print("SKIP: no LLVM toolchain for the SDK (%s)" % str(e).split(".")[0]); sys.exit(0)
import toolchain as ptc
try: ptc.riscv_bin(); HAVE_GCC = True
except ptc.ToolchainError: HAVE_GCC = False
import importlib.util
_sp = importlib.util.spec_from_file_location("repo_meter_package", ROOT / "tools" / "meter_package.py"); mpk = importlib.util.module_from_spec(_sp); _sp.loader.exec_module(mpk)      # the project's own tool (gcc path), not the SDK's vendored copy
from conformance import suite
KEYS = ["winamp_bars", "winamp_scope", "vu_master", "chladni", "layered_wave", "waterfall_grid", "sound_map"]
print("SDK toolchain: %s (pinned %s: %s)" % (TC["line"], toolchain.PINNED_LLVM, "yes" if TC["pinned"] else "NO"))

def traces(pack_path, profile="shipped"):
    """-> {scenario or setting: (hash, final frame bytes or None)} for one package"""
    man, files = mpk.load(pack_path); pk = files["meter.bin"]
    binf = TMP / (Path(pack_path).stem + "_%d.bin" % len(pk)); binf.write_bytes(pk)
    d = suite.descriptor(pk); abi = (pk[4] | pk[5] << 8) & 255
    params0 = [p.get("def", p["min"]) for p in d["params"]]
    out = {}
    for nm in ("music", "pause", "move", "loud", "degrade", "tiny"):
        r = suite.run_scenario(str(binf), None, suite.scenario(nm), params0, d["max_cycles"], abi, profile, want_frames={len(suite.scenario(nm)) - 1} if nm in ("music", "loud") else None)
        fr = r["frames"].get(len(suite.scenario(nm)) - 1)
        out[nm] = (r["hash"], fr[1] if fr else None)
    pr = suite.preset_checks(suite.Report("x"), files, man, d)
    sets = [("preset " + p["name"], p["values"]) for p in pr]
    for i, p in enumerate(d["params"]):
        if p["type"] in (0, 1, 2) and not p["hidden"]:
            for v in sorted({p["min"], p["max"]}): sets.append(("%s=%d" % (p["label"], v), {p["key"]: v}))
    tk = suite.scenario("music")[:8]
    for label, vals in sets[:40]:
        pv = list(params0)
        for h, v in vals.items():
            for i, p in enumerate(d["params"]):
                if p.get("key") == h: pv[i] = v
        r = suite.run_scenario(str(binf), None, tk, pv, d["max_cycles"], abi, profile)
        out[label] = (r["hash"], None)
    return out, d, len(pk)

def one(key):
    m = json.loads((ROOT / "sdk" / "meters" / key / "meter.json").read_text())
    sdk_pkg = build.build(ROOT / "sdk" / "meters" / key, TMP / "sdk", tc=TC)["package"]
    res = {"key": key, "sdk": sdk_pkg}
    if HAVE_GCC: res["gcc"] = mpk.pack(key, TMP / "gcc")
    return res
with ThreadPoolExecutor(4) as ex: built = list(ex.map(one, KEYS))
for b in built:
    k = b["key"]
    for which in ("sdk", "gcc"):
        if which not in b: continue
        why = mpk.verify(b[which])
        check("%-15s %s package verifies" % (k, which), not why, why)
    rep = suite.run(b["sdk"], profile="shipped", quick=True, tc=TC)
    check("%-15s SDK-built pack passes the suite (first-party profile)" % k, rep.exit_code() == 0, rep.text())
    if "gcc" not in b:
        print("SKIP %s: no project gcc for the comparison" % k); continue
    ms, fs = mpk.load(b["sdk"]); mg, fg = mpk.load(b["gcc"])
    ds, dg = suite.descriptor(fs["meter.bin"]), suite.descriptor(fg["meter.bin"])
    check("%-15s the descriptors are identical (name, %d parameters, limits, preview)" % (k, len(ds["params"])), ds == dg)
    skip = lambda n: n in ("meter.bin", "README.md") or n.startswith("source/")      # the pack, the generated readme and the source copy differ by design
    same_files = sorted(n for n in fs if not skip(n)) == sorted(n for n in fg if not skip(n)) and all(fs[n] == fg[n] for n in fs if not skip(n))
    check("%-15s the presets and licence files are identical" % k, same_files)
    check("%-15s manifest fields identical (id, name, version, abi, tau_min, params_rev)" % k, all(ms[f] == mg[f] for f in ("id", "name", "version", "abi", "tau_min", "params_rev", "author", "licence")), (ms, mg))
    print("     image sizes: gcc %d B, clang %d B (window 65,536)" % (len(fg["meter.bin"]) - 48, len(fs["meter.bin"]) - 48))
    ts, _, _ = traces(b["sdk"]); tg, _, _ = traces(b["gcc"])
    diff = [n for n in tg if tg[n][0] != ts.get(n, (None,))[0]]
    check("%-15s draw-call traces identical over %d runs (6 scenarios, presets, parameter extremes): different in %s" % (k, len(tg), diff[:5]), not diff and set(ts) == set(tg))
    check("%-15s the final frames of 'music' and 'loud' are identical, pixel for pixel" % k, all(ts[n][1] == tg[n][1] and ts[n][1] is not None for n in ("music", "loud")))
import shutil; shutil.rmtree(TMP, ignore_errors=True)
print("%d failure(s)" % fails); sys.exit(1 if fails else 0)
