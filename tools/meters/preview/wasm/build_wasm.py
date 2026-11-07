#!/usr/bin/env python3
"""Build the WebAssembly twins of the meter lab: the firmware's OWN drawing source of a meter, compiled through a small wrapper (tools/meters/preview/wasm/<key>_wasm.c) and written, base64, with the SHA-256 of every
file it was built from, to tools/meters/preview/meters/<key>_wasm.js (committed: the lab and Tau Omega need no toolchain). Modules: sound_map (fw2/ui/meter_sound_map.c), layered_wave (fw2/ui/meter_layered_wave.c) and waterfall_grid (fw2/ui/meter_waterfall_grid.c).

  python3 tools/meters/preview/wasm/build_wasm.py [KEY ...]           rebuild (needs Homebrew llvm + lld, see memory/reference-wasm-toolchain: clang --target=wasm32 and wasm-ld)
  python3 tools/meters/preview/wasm/build_wasm.py --check [KEY ...]   exit 1 when a file changed since the committed module was built (no toolchain needed); with the toolchain it also rebuilds and compares the bytes
The files a module depends on are found by the compiler itself (clang -MM) at build time and recorded in the committed module, so the staleness check covers exactly what the module includes (B-959)."""
import base64, hashlib, json, re, subprocess, sys, tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
CLANG, WASM_LD = "/opt/homebrew/opt/llvm/bin/clang", "/opt/homebrew/opt/lld/bin/wasm-ld"
MODULES = {
    "sound_map": {"wrapper": "sound_map_wasm.c", "global": "TauSoundMapWasm", "extra": [], "about": "SOUND MAP compiled to WebAssembly from fw2/ui/meter_sound_map.c by tools/meters/preview/wasm/build_wasm.py"},
    "waterfall_grid": {"wrapper": "waterfall_grid_wasm.c", "global": "TauWaterfallGridWasm", "extra": [], "about": "WATERFALL GRID compiled to WebAssembly from fw2/ui/meter_waterfall_grid.c (the pack's own source, P2_PACK) by tools/meters/preview/wasm/build_wasm.py"},
    "winamp_bars": {"wrapper": "winamp_bars_wasm.c", "global": "TauWinampBarsWasm", "extra": [], "about": "WINAMP BARS compiled to WebAssembly from fw2/ui/meter_winamp_bars.c (the pack own source, P2_PACK) by tools/meters/preview/wasm/build_wasm.py"},
    "winamp_scope": {"wrapper": "winamp_scope_wasm.c", "global": "TauWinampScopeWasm", "extra": [], "about": "WINAMP SCOPE compiled to WebAssembly from fw2/ui/meter_winamp_scope.c (the pack own source, P2_PACK) by tools/meters/preview/wasm/build_wasm.py"},
    "vu_master": {"wrapper": "vu_master_wasm.c", "global": "TauVuMasterWasm", "extra": [], "about": "MASTER VU compiled to WebAssembly from fw2/ui/meter_vu_master.c (the pack own source, P2_PACK) by tools/meters/preview/wasm/build_wasm.py"},
    "chladni": {"wrapper": "chladni_wasm.c", "global": "TauChladniWasm", "extra": [], "about": "CHLADNI compiled to WebAssembly from fw2/ui/meter_chladni.c (the pack own source, P2_PACK) by tools/meters/preview/wasm/build_wasm.py"},
    "layered_wave": {"wrapper": "layered_wave_wasm.c", "global": "TauLayeredWaveWasm", "extra": ["-I", str(ROOT / "fw")], "about": "LAYERED WAVE compiled to WebAssembly from fw2/ui/meter_layered_wave.c (fw2/ui/layered_wave.inc) by tools/meters/preview/wasm/build_wasm.py"},
    "layered_wave_timed": {"wrapper": "layered_wave_wasm.c", "global": "TauLayeredWaveTimedWasm", "extra": ["-I", str(ROOT / "fw"), "-DWASM_TIMED=1"], "about": "LAYERED WAVE (modelled CPU-time cost guard, -DWASM_TIMED) compiled to WebAssembly from fw2/ui/meter_layered_wave.c (fw2/ui/layered_wave.inc) by tools/meters/preview/wasm/build_wasm.py"},
}
def _discover():
    """a meter whose module is just its wrapper <key>_wasm.c (tools/player2/new_meter.py writes one) needs no entry above: the adapter's global is Tau<CamelKey>Wasm"""
    for w in sorted(HERE.glob("*_wasm.c")):
        key = w.name[:-len("_wasm.c")]
        if key not in MODULES:
            MODULES[key] = {"wrapper": w.name, "global": "Tau" + "".join(x.capitalize() for x in key.split("_")) + "Wasm", "extra": [], "about": "%s compiled to WebAssembly from fw2/ui/meter_%s.c (the pack own source, P2_PACK) by tools/meters/preview/wasm/build_wasm.py" % (key.upper().replace("_", " "), key)}


_discover()
INCLUDES = ["-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2")]


def out_path(key): return ROOT / ("tools/meters/preview/meters/%s_wasm.js" % key)
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()


def dependencies(key):
    """the repo files the wrapper includes (the compiler's own list), relative to the repo root"""
    cmd = [CLANG, "--target=wasm32", "-MM", "-ffreestanding", "-Wno-comment"] + INCLUDES + MODULES[key]["extra"] + [str(HERE / MODULES[key]["wrapper"])]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit("dependency scan failed:\n" + r.stderr)
    import re
    toks = [t.replace("\\ ", " ") for t in re.split(r"(?<!\\) +", r.stdout.replace("\\\n", " ").strip())][1:]      # the compiler escapes spaces in paths; drop "target.o:"
    paths = sorted({str(Path(t).resolve().relative_to(ROOT)) for t in toks if t})
    return paths


def compile_wasm(key, dst):
    cmd = [CLANG, "--target=wasm32", "-O2", "-nostdlib", "-ffreestanding", "-Wno-comment", "-fuse-ld=" + WASM_LD, "-Wl,--no-entry", "-Wl,--allow-undefined", "-Wl,--strip-all"] + INCLUDES + MODULES[key]["extra"] + ["-o", str(dst), str(HERE / MODULES[key]["wrapper"])]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: sys.exit("wasm build failed (%s):\n%s" % (key, r.stderr))
    return Path(dst).read_bytes()


def committed(key):
    """(meta, wasm bytes) recorded in the committed module"""
    txt = out_path(key).read_text()
    meta = json.loads(txt[txt.index("/*META") + 6:txt.index("META*/")])
    b64 = txt[txt.index('b64: "') + 6:txt.index('"', txt.index('b64: "') + 6)]
    return meta, base64.b64decode(b64)


def check(key):
    meta, wasm = committed(key)
    stale = [s for s, h in meta["sources"].items() if not (ROOT / s).exists() or sha(ROOT / s) != h]
    if hashlib.sha256(wasm).hexdigest() != meta["wasm_sha256"]: stale.append("the module's own checksum")
    if stale: print("STALE (%s): rebuild with tools/meters/preview/wasm/build_wasm.py; changed:" % key, ", ".join(stale)); return 1
    if Path(CLANG).exists() and Path(WASM_LD).exists():
        if dependencies(key) != sorted(meta["sources"]): print("STALE (%s): the files it includes changed; rebuild" % key); return 1
        with tempfile.TemporaryDirectory() as td:
            fresh = compile_wasm(key, Path(td) / (key + ".wasm"))
        if fresh != wasm: print("%s_wasm.js differs from a fresh build (same sources: another toolchain version?)" % key); return 1
    print("%s_wasm.js is current (%d B)" % (key, len(wasm))); return 0


ENUMS = {"winamp_bars": ("meter_presets.h", "MPP_BARS_"), "winamp_scope": ("meter_presets.h", "MPP_SCOPE_"), "vu_master": ("meter_presets.h", "MPP_VU_"), "chladni": ("meter_presets.h", "MPP_CHL_"), "layered_wave": ("lw_presets.h", "MP_LAYERED_WAVE_"), "layered_wave_timed": ("lw_presets.h", "MP_LAYERED_WAVE_"), "waterfall_grid": ("meter_presets.h", "MPP_WF_")}


def param_keys(key):
    """the parameter keys of a module in the order of its params buffer (the generated enum of the meter's parameters), so the lab adapter needs no copy of the list"""
    if key not in ENUMS:                                     # a meter made by new_meter.py: its parameters are the generated MPP_<TAG>_ enum, the tag from its manifest
        mf = ROOT / "meters" / key / "meter.json"
        if not mf.is_file(): return None
        ENUMS[key] = ("meter_presets.h", "MPP_%s_" % json.loads(mf.read_text())["p2"]["tag"])
    f, pre = ENUMS[key]
    t = (ROOT / "fw2/generated" / f).read_text()
    e = re.search(r"enum \{([^}]*%sN[^}]*)\}" % re.escape(pre), t).group(1)
    return [m.lower() for m in re.findall(re.escape(pre) + r"([A-Z0-9_]+)", e) if m != "N"]


def build(key):
    deps = dependencies(key)
    with tempfile.TemporaryDirectory() as td:
        wasm = compile_wasm(key, Path(td) / (key + ".wasm"))
    meta = {"about": MODULES[key]["about"], "wasm_sha256": hashlib.sha256(wasm).hexdigest(), "bytes": len(wasm), "sources": {s: sha(ROOT / s) for s in deps}}
    out_path(key).write_text("/*META" + json.dumps(meta, indent=1) + "META*/\n/* GENERATED by tools/meters/preview/wasm/build_wasm.py: do not edit. */\n(function (root) {\n  const api = { b64: \"" + base64.b64encode(wasm).decode()
                             + "\", sha256: \"" + meta["wasm_sha256"] + "\"" + (", params: " + json.dumps(param_keys(key)) if param_keys(key) else "") + " };\n  if (typeof module !== 'undefined') module.exports = api; else root." + MODULES[key]["global"] + " = api;\n})(typeof globalThis !== 'undefined' ? globalThis : window);\n")
    print("wrote %s (%d B of wasm, %d source files)" % (out_path(key).relative_to(ROOT), len(wasm), len(deps))); return 0


def main():
    keys = [a for a in sys.argv[1:] if not a.startswith("--")] or [k for k, m in MODULES.items() if (HERE / m["wrapper"]).exists()]        # a meter that was removed takes its wrapper with it
    bad = [k for k in keys if k not in MODULES]
    if bad: sys.exit("unknown module %s (have: %s)" % (", ".join(bad), ", ".join(MODULES)))
    if "--check" in sys.argv: return max(check(k) for k in keys)
    return max(build(k) for k in keys)


if __name__ == "__main__":
    sys.exit(main())
