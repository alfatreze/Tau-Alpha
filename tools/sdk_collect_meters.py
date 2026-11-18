#!/usr/bin/env python3
"""Collect the seven shipped meters as SDK projects: sdk/meters/<key>/ (the collection repository `tau-alpha-meters`, see docs/features/meters/RELEASE_075_PLAN.md W3).

  python3 tools/sdk_collect_meters.py [OUT_DIR]        default sdk/meters; writes meters/<key>/{meter.json, presets.json, thumb.json, LICENSE, README.md, src/fw2/...}

A shipped meter is the player's own source: fw2/ui/meter_pack_<key>.c (the wrapper that redirects gfx_* to the host table), meter_<key>.c, and the player headers they include (the closure of their quoted includes
is copied under src/ with the player's folder layout, so the wrapper builds unchanged). The manifest is meters/<key>/meter.json plus a `build` block ("sources", "include", "descriptor": "source": the wrapper brings
the descriptor made by tools/player2/meter_desc.py). Nothing here needs the player's toolchain: `tau-meter build meters/<key>` builds it with the SDK's clang recipe. New meters start from the template instead."""
import json, re, shutil, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
KEYS = ["winamp_bars", "winamp_scope", "vu_master", "chladni", "layered_wave", "waterfall_grid", "sound_map"]
INCDIRS = [ROOT / "fw2/ui", ROOT / "fw2/cores", ROOT / "fw"]


def closure(start):
    seen, work = {}, [start]
    while work:
        f = work.pop()
        if f in seen: continue
        seen[f] = True
        for m in re.finditer(r'^\s*#\s*include\s+"([^"]+)"', f.read_text(), re.M):
            cands = [f.parent / m.group(1)] + [d / m.group(1) for d in INCDIRS]
            for c in cands:
                if c.is_file(): work.append(c.resolve()); break
    return sorted(seen)


def collect(out):
    out = Path(out)
    lic = (ROOT / "LICENSE").read_text()
    for key in KEYS:
        man = json.loads((ROOT / "meters" / key / "meter.json").read_text())
        dst = out / key
        if dst.exists(): shutil.rmtree(dst)
        (dst / "src").mkdir(parents=True)
        wrapper = ROOT / "fw2/ui" / ("meter_pack_%s.c" % key)
        files = closure(wrapper.resolve())
        for f in files:
            rel = f.relative_to(ROOT)
            (dst / "src" / rel).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(f, dst / "src" / rel)
        need = re.search(r"#define MTR_PACK_NEEDS_MINOR (\d+)", wrapper.read_text())      # the minor the pack needs is declared in its wrapper (tools/player2/pack_meter.py reads it the same way)
        man["abi"] = "1.%d" % (int(need.group(1)) if need else 0)
        man["sources"] = ["src/fw2/ui/%s" % wrapper.name]
        man["build"] = {"include": ["src/fw2/ui", "src/fw2/cores", "src/fw"], "descriptor": "source", "cflags": ["-w"]}
        (dst / "meter.json").write_text(json.dumps(man, indent=2) + "\n")
        for n in ("presets.json", "thumb.json"):
            if (ROOT / "meters" / key / n).exists(): shutil.copyfile(ROOT / "meters" / key / n, dst / n)
        (dst / "LICENSE").write_text(lic)
        (dst / "README.md").write_text("# %s\n\nOne of the meters that ship with Tau Alpha (id `%s`, version %s, MIT).\n\nThis folder is a meter project for the SDK: `tau-meter build .` makes `dist/%s.tmeter`.\n"
                                       "The source is the player's own: `src/fw2/ui/meter_pack_%s.c` redirects the player's drawing calls to the host table, and `meter_%s.c` is the drawing. The player headers it includes are copied under `src/` "
                                       "in the player's folder layout. **To write a new meter, start from the template (`tau-alpha-meter-template`) and `tau_meter.h`, not from these.**\n"
                                       % (man["name"], man["id"], man["version"], man["id"], key, key))
    return KEYS


if __name__ == "__main__":
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "sdk/meters"
    out.mkdir(parents=True, exist_ok=True)
    for k in collect(out): print("collected", k)
