#!/usr/bin/env python3
"""The release path carries data slot 9 (tau-packs.bin, a v2 bin of the seven shipped meters) and data slot 10 (tau-meters.bin, the user's meter bin) (P2.6, decision D-M24).

Release-shaped packages are STAGED with the same functions make_release.py --player2 reaches through tools/package_dev_build.py (declare_packs_slots, build_packs_bin; the firmware build is the only step left out: the ROM,
cold image, loading image and bitstream are dist/'s, the v0.7.1 release firmware). Each staged package is zipped by make_release.make_zip and checked with make_release.verify, tau_compat.read_zip and
tau_compat.layout, tools/check_fw_bitstream_pair.py and tools/check_tau_package.check_meter_slots. For the normal core, the Diagnostics core and a Preview core:

  1. data slot 9 `tau-packs.bin` core-specific (parameter bit 1), optional, file in the core's own Assets folder, a v2 bin with the seven shipped meters, Winamp Bars the fallback row;
  2. data slot 10 `tau-meters.bin`: NOT core-specific, optional, deferload, parameters 0x0 on the media platform (Preview: platform index 1 = TAU's common/, like slot 5), no file in the zip;
  3. tau-compat.json `layout` has the `role user, slot 10` entry for Assets/tau/common/tau-meters.bin with format TPKB v2 (max_bytes 4456448): the capability Omega reads (D-M24);
  4. tau-meters.bin is never inside a zip, and a zip that ships it is refused by tau_compat.layout (the user's copy would be overwritten);
  5. NEGATIVE: the v0.7.1 shape (slot 9, no slot 10) has no layout entry for tau-meters.bin: the core ignores the file;
  6. check_tau_package.check_meter_slots refuses a slot 10 that is core-specific, required, or has a file in the package."""
import json, os, shutil, sys, tempfile, zipfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import make_release as mr, package_dev_build as pdb, tau_compat, tau_layout, tau_data_slots as sl, meter_bin as mb, check_tau_package as ctp
fails = 0
def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "  " + str(info)))
    fails += 0 if cond else 1

DIST = ROOT / "dist"
REV = bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))
RAW = (DIST / "Cores/alfatreze.TAU/bitstream.rbf_r").read_bytes().translate(REV)       # the raw RBF the release was built from (dist holds it bit-reversed)
SEVEN = ["WINAMP BARS", "WINAMP SCOPE", "CHLADNI", "LAYERED WAVE", "VU MASTER", "WATERFALL", "SOUND MAP"]


def stage(root, core_id, channel, slot10=True):
    """a package directory the way package_dev_build.py main() makes one for a Player 2 release core; returns (package dir, rom path)"""
    ch = tau_layout.CHANNELS[channel]; platform, platforms = ch["platform"], list(ch["platforms"]); short = core_id.split(".", 1)[1]
    pkg = root / "pkg"; c = pkg / "Cores" / core_id
    shutil.copytree(DIST / "Cores/alfatreze.TAU", c)
    cj = json.loads((c / "core.json").read_text()); cj["core"]["metadata"]["shortname"] = short; cj["core"]["metadata"]["platform_ids"] = platforms
    (c / "core.json").write_text(json.dumps(cj, indent=4) + "\n")
    sl.add_library_slot(c, persist_words=False); sl.add_cold_slot(c)
    if slot10: pdb.declare_packs_slots(c)
    else: sl.add_packs_slot(c)                                                              # the v0.7.1 release: slot 9 only
    sl.add_assets_slot(c); sl.add_cover_slot(c)
    if len(platforms) > 1: (c / "data.json").write_text(json.dumps(tau_layout.read_shared_media(json.loads((c / "data.json").read_text())), indent=4) + "\n")
    cd = tau_layout.core_dir(pkg, platform, core_id); cd.mkdir(parents=True)
    for n in ("tau.rom", "tau-cold.bin", "tau-loading.bin"): shutil.copy2(DIST / "Assets/tau/alfatreze.TAU" / n, cd / n)
    pdb.build_packs_bin(cd)
    (pkg / "Platforms/_images").mkdir(parents=True); shutil.copy2(DIST / "Platforms/_images/tau.bin", pkg / "Platforms/_images" / (platform + ".bin"))
    pj = DIST / "Platforms" / (platform + ".json")
    if pj.is_file(): shutil.copy2(pj, pkg / "Platforms" / (platform + ".json"))
    else: (pkg / "Platforms" / (platform + ".json")).write_text(json.dumps({"platform": {"category": "Media Players", "name": ch["name"], "year": 2026, "manufacturer": "alfatreze"}}))
    mr.add_factory_presets(pkg, ch["platforms"][-1])
    return pkg, cd / "tau.rom"


def zipped(pkg, core_id, rom, media):
    mr.OUT = Path(tempfile.mkdtemp())
    z = mr.make_zip(pkg, "t.zip")
    n = mr.verify(z, core_id, json.loads((pkg / "Cores" / core_id / "core.json").read_text())["core"]["metadata"]["version"], RAW, rom, True, media)
    return z, n


for label, core_id, channel in (("normal core (alfatreze.TAU)", "alfatreze.TAU", "stable"), ("Diagnostics core", "alfatreze.TAU Diagnostics", "stable"), ("Preview core", "alfatreze.TAU Preview", "preview")):
    with tempfile.TemporaryDirectory() as td:
        pkg, rom = stage(Path(td), core_id, channel)
        media = tau_layout.CHANNELS[channel]["platforms"][-1]
        z, nfiles = zipped(pkg, core_id, rom, media)                                       # make_release.verify: identity, ROM, bitstream, factory presets, platform files
        check("%s: make_release.verify accepts the staged package (%d files)" % (label, nfiles), nfiles > 10)
        names = zipfile.ZipFile(z).namelist(); zf = zipfile.ZipFile(z)
        data = json.loads(zf.read("Cores/%s/data.json" % core_id))["data"]["data_slots"]
        s9, s10, s5 = (next((s for s in data if s["id"] == i), None) for i in (9, 10, 5))
        check("%s: data slot 9 is tau-packs.bin, core-specific, optional" % label, s9 and s9["filename"] == "tau-packs.bin" and int(s9["parameters"], 16) & 2 and not s9["required"], s9)
        check("%s: data slot 10 is tau-meters.bin: not core-specific, optional, deferload" % label, s10 and s10["filename"] == "tau-meters.bin" and not int(s10["parameters"], 16) & 2 and not s10["required"] and s10.get("deferload"), s10)
        check("%s: slot 10 reads from the same platform as slot 5 (the media platform)" % label, tau_layout.slot_platform_index(s10["parameters"]) == tau_layout.slot_platform_index(s5["parameters"]), (s10, s5))
        check("%s: tau-meters.bin is nowhere in the zip" % label, not any(n.endswith("tau-meters.bin") for n in names))
        packs = [n for n in names if n.endswith("/tau-packs.bin")]
        check("%s: tau-packs.bin is in the core's own Assets folder" % label, packs == ["Assets/%s/%s/tau-packs.bin" % (tau_layout.CHANNELS[channel]["platform"], core_id)], packs)
        info = mb.parse(zf.read(packs[0]))
        check("%s: tau-packs.bin is a v2 bin of the seven shipped meters, Winamp Bars the fallback" % label, info["version"] == 2 and [r["name"] for r in info["rows"]] == SEVEN and info["rows"][0]["flags"] & mb.FLAG_FALLBACK)
        pk = tau_compat.read_zip(z); lay = tau_compat.layout(pk)
        e10 = [e for e in lay if e["path"].endswith("tau-meters.bin")]
        check("%s: tau-compat layout has the role user, slot 10 entry for Assets/tau/common/tau-meters.bin" % label, len(e10) == 1 and e10[0]["path"] == "Assets/tau/common/tau-meters.bin" and e10[0]["role"] == "user" and e10[0]["slot"] == 10 and e10[0]["required"] is False, e10)
        check("%s: that entry's format is TPKB v2 (max_bytes 4456448): the capability of D-M24" % label, e10 and e10[0]["format"]["name"] == "TPKB" and e10[0]["format"]["version"] == 2 and e10[0]["format"]["max_bytes"] == 4456448, e10)
        e9 = [e for e in lay if e["path"].endswith("tau-packs.bin")]
        check("%s: the layout lists tau-packs.bin as owned by the core, slot 9" % label, len(e9) == 1 and e9[0]["role"] == "owned" and e9[0]["slot"] == 9 and core_id in e9[0]["path"], e9)
        check("%s: tools/check_tau_package.check_meter_slots accepts the slots" % label, ctp.check_meter_slots(data, [n for n in names]) is None)
        r = os.popen("cd '%s' && python3 tools/check_fw_bitstream_pair.py '%s' 2>&1; echo rc=$?" % (ROOT, pkg)).read()
        check("%s: tools/check_fw_bitstream_pair.py passes on the staged package" % label, r.strip().endswith("rc=0"), r[-300:])
        # a zip that ships the user's bin is refused
        extra = Path(td) / "pkg" / "Assets" / "tau" / "common"; extra.mkdir(parents=True, exist_ok=True); (extra / "tau-meters.bin").write_bytes(b"x")
        mr.OUT = Path(tempfile.mkdtemp()); z2 = mr.make_zip(pkg, "bad.zip")
        try: tau_compat.layout(tau_compat.read_zip(z2)); refused = None
        except tau_compat.CompatError as e: refused = str(e)
        check("%s: mutant: a zip that ships tau-meters.bin is refused by name" % label, refused and "tau-meters.bin" in refused, refused)
        (extra / "tau-meters.bin").unlink()
    if channel == "preview":
        check("Preview core: the shared slots point at platform index 1 (TAU's common/), slot 10 included", tau_layout.slot_platform_index(s10["parameters"]) == 1 and e10[0]["path"].startswith("Assets/tau/common/"))

# ---- the v0.7.1 shape: no slot 10 (the negative case) ----
with tempfile.TemporaryDirectory() as td:
    pkg, rom = stage(Path(td), "alfatreze.TAU", "stable", slot10=False)
    z, _ = zipped(pkg, "alfatreze.TAU", rom, "tau")
    data = json.loads(zipfile.ZipFile(z).read("Cores/alfatreze.TAU/data.json"))["data"]["data_slots"]
    lay = tau_compat.layout(tau_compat.read_zip(z))
    check("v0.7.1 shape: no data slot 10", not any(s["id"] == 10 for s in data))
    check("v0.7.1 shape: the layout has NO tau-meters.bin entry (a core without one ignores the file, D-M24)", not any("tau-meters.bin" in e["path"] for e in lay), [e["path"] for e in lay if "meters" in e["path"]])
    check("v0.7.1 shape: slot 9 is still there", any(s["id"] == 9 for s in data))

# ---- check_meter_slots mutants ----
good = [{"id": 10, "filename": "tau-meters.bin", "parameters": "0x0", "required": False, "deferload": True, "name": "Meters"}]
def bad(slot_edit, names=()):
    s = dict(good[0]); s.update(slot_edit)
    try: ctp.check_meter_slots([s], list(names)); return False
    except AssertionError: return True
check("check_meter_slots refuses a core-specific slot 10 (two cores would each have their own list)", bad({"parameters": "0x2"}))
check("check_meter_slots refuses a required slot 10 (a missing user file must not stop the core)", bad({"required": True}))
check("check_meter_slots refuses a package that holds a tau-meters.bin", bad({}, ["Assets/tau/common/tau-meters.bin"]))
check("check_meter_slots refuses slot 10 under another file name", bad({"filename": "meters.bin"}))
check("check_meter_slots accepts a core without slot 10 (v0.7.1)", ctp.check_meter_slots([], []) is None)
check("wants_packs: a PACKS=1 Player 2 build, and only that", pdb.wants_packs("player2", "PLAYER2=1,PACKS=1,DIAG=0") and not pdb.wants_packs("player2", "PLAYER2=1") and not pdb.wants_packs("diagnostic", "PACKS=1") and not pdb.wants_packs("player2", None))
mk = (ROOT / "tools/make_release.py").read_text()
check("make_release.py --player2 builds both release cores with PACKS=1 (slots 9 and 10 come with it)", mk.count('PACKS=1,DIAG=0') == 1 and mk.count('PACKS=1,DIAG=1') == 1)
print("FAILED" if fails else "PASSED"); sys.exit(1 if fails else 0)
