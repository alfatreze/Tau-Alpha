#!/usr/bin/env python3
"""Build tau-packs.bin: the shipped meters of Player 2 as a v2 meter bin (docs/features/meters/EXTERNAL_METERS_SPEC.md section 2, tools/meter_bin.py), the file a PACKS=1 core ships next to tau.rom and tau-cold.bin
(data slot 9, the core's own folder). The firmware reads only its TOC at boot and one pack at a time, when its meter is selected.

  python3 tools/player2/build_packs.py OUT.bin [--hang KEY[:MODE]]

Since P2.6 the bin is composed from the seven `.tmeter` packages (tools/meter_package.py pack, then compose): the shipped path and a third party's path are the same code. Every meter with a p2 block in meters/<key>/meter.json
is linked for the ONE execution window (0x24970000, the 64 KiB slot P0 proved on a Pocket) with its permanent 128-bit id (the same file), in the order of the manifests' p2 slot (Winamp Bars first and flagged as the built-in
fallback). The TOC row takes the meter's name from its pack's own descriptor and params_rev, version and tau_min from the manifest (the single source; fw2/cores/shipped_meter_ids.h is generated from it, tools/gen_shipped_meter_ids.py).
The pack header's `meter id` is the low 16 bits of the id hash (informational). `build_entries` (the same bin straight from the sources) stays as the reference: sim/test_meter_package.py proves the two give the same bytes.

--hang swaps one meter's pack for its hang test variant (fw2/ui/meter_pack_<KEY>_hang.c, the pack watchdog's Pocket test, docs/features/PACK_WATCHDOG_PLAN.md W4; MODE 0 = plain endless loop, 1 = endless loop issuing
engine commands; default 0). Only a test package asks for it; no release does.

The packs are built from the CURRENT source, so call this from the same tree and commit as the firmware it ships with (tools/package_dev_build.py does, for a PACKS=1 build): a pack is linked against the host table of the
firmware's ABI and refused by any other."""
import json, re, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent)); sys.path.insert(0, str(ROOT / "tools"))
import pack_meter as pm, meter_bin as mb, meter_package as mpk

FALLBACK = "winamp_bars"                 # the built-in meter the firmware falls back to (flag bit 1 of its TOC row)


def manifest(key):
    return json.loads((ROOT / "meters" / key / "meter.json").read_text())


def build_entries(workdir: Path, hang=None):
    """[meter_bin.Entry] for every shipped meter in p2 slot order, each pack linked for the window; hang = (key, mode) builds that meter's hang test pack instead"""
    out = []
    for key in sorted(pm.SLOT, key=lambda k: pm.SLOT[k]):
        m = manifest(key); hid = int(m["id"][:8], 16)
        if hang and key == hang[0]:
            blob, _ = pm.build(key, mb.WINDOW, workdir / (key + ".elf"), ["-DHANG_MODE=%d" % hang[1]], source=key + "_hang", header_id=hid & 0xFFFF)
        else:
            blob, _ = pm.build(key, mb.WINDOW, workdir / (key + ".elf"), header_id=hid & 0xFFFF)
        name = mb.desc_info(blob[mb.PACK_HDR:])["name"]
        out.append(mb.Entry(id=m["id"], name=name, pack=blob, flags=mb.FLAG_VISIBLE | (mb.FLAG_FALLBACK if key == FALLBACK else 0), params_rev=m["params_rev"],
                            version=mb._semver(m["version"]), tau_min=_tau(m)))
    return out


def _tau(m):
    t = m.get("tau_min")
    if not t: return 0
    a, b = (int(x) for x in t.split(".")[:2]); return a << 8 | b


def build_from_packages(workdir: Path, hang=None):
    """the shipped bin, composed from the seven .tmeter packages made in workdir (hang = (key, mode) packs that meter's hang test variant); returns (bytes, report)"""
    pkgs = [mpk.pack(key, workdir, hang=hang[1] if hang and hang[0] == key else None) for key in sorted(pm.SLOT, key=lambda k: pm.SLOT[k])]
    out = workdir / "shipped.bin"
    rep = mpk.compose(out, pkgs)
    return out.read_bytes(), rep


def main():
    argv = sys.argv[1:]
    hang = None
    if len(argv) == 3 and argv[1] == "--hang":
        k, _, mode = argv[2].partition(":"); hang = (k, int(mode or 0)); argv = argv[:1]
        if k not in pm.SLOT: print("no such meter pack: " + k); return 2
    if len(argv) != 1:
        print(__doc__); return 2
    out = Path(argv[0]); out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as td:
        data, rep = build_from_packages(Path(td), hang)
    out.write_bytes(data)
    print("%s: %d B, v2 bin from %d packages" % (out, out.stat().st_size, rep["rows"]))
    for w in rep["warnings"]: print("warning: " + w)
    return 0


if __name__ == "__main__":
    sys.exit(main())
