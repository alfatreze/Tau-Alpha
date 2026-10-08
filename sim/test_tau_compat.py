#!/usr/bin/env python3
"""tau-compat.json (tools/tau_compat.py, RELEASE_SYSTEM_SPEC section 10): build a release-shaped set of zips, write the file,
read it back, recompute every hash from the zips and check each field; then mutate inputs and confirm each is caught."""
import json, sys, tempfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_compat as tc  # noqa: E402

fails = 0
def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    fails += 0 if cond else 1

def raises(fn, what, needle=""):
    try:
        fn()
    except tc.CompatError as e:
        check(needle in str(e), f"{what} (refused: {e})")
        return
    check(False, f"{what} (was NOT refused)")

RAW = bytes(range(256)) * 64                                   # stand-in raw RBF
REVB = RAW.translate(tc.REV)

def interact(ids):
    return {"interact": {"magic": "APF_VER_1", "variables": [
        {"name": n, "id": i, "type": "slider_u32", "persist": True, "defaultval": 0, "graphical": {"min": 0, "max": mx}}
        for i, (n, mx) in ids.items()]}}

def make(d, core, plat, rom, version="0.6.0", date="2026-10-08", bit=REVB, ids=None):
    author, short = core.split(".")
    p = d / f"{core}_{version}_{date}.zip"
    with zipfile.ZipFile(p, "w") as z:
        z.writestr(f"Cores/{core}/core.json", json.dumps({"core": {"metadata": {"author": author, "shortname": short,
                   "version": version, "date_release": date, "platform_ids": [plat]}}}))
        z.writestr(f"Cores/{core}/bitstream.rbf_r", bit)
        z.writestr(f"Cores/{core}/interact.json", json.dumps(interact(ids or {16: ("Halcyon EQ preset", 30), 17: ("Theme", 3)})))
        z.writestr(f"Assets/{plat}/common/tau.rom", rom)
        z.writestr(f"Assets/{plat}/common/tau-cold.bin", b"cold" + rom[:8])
    return p

ROM_N = b"\x13\x00" * 40 + b"TAUFWPAIR:4D50331A;" + b"TAUFWNEED:HALCYON,LPC;" + b"\0" * 16
ROM_D = ROM_N + b"diag"

with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    old, new = t / "old", t / "new"
    old.mkdir(); new.mkdir()
    prev = [make(old, "alfatreze.TAU", "tau", ROM_N, date="2026-10-07", ids={16: ("EQ preset", 7), 17: ("Theme", 3), 18: ("Gone", 1)}),
            make(old, "alfatreze.TAU_DIAGNOSTIC", "tau_diagnostic", ROM_D, date="2026-10-07")]
    zips = [make(new, "alfatreze.TAU", "tau", ROM_N), make(new, "alfatreze.TAU_DIAGNOSTIC", "tau_diagnostic", ROM_D)]
    rbf = t / "ap_core.rbf"; rbf.write_bytes(RAW)
    (t / "ap_core.rbf.json").write_text(json.dumps({"macros": ["TAU_RAM_192K", "TAU_CLK66", "TAU_HALCYON"], "rbf_sha256": tc.sha(RAW)}))
    cl = t / "CHANGELOG.md"
    cl.write_text("# Changelog\n\n## v0.6.0-alpha.5 — 8 October 2026\n\n- Halcyon only.\n- Omega: persist id 16 is the Halcyon preset now.\n\n## v0.6.0-alpha.4\n- Omega: older note.\n")
    kw = dict(release="v0.6.0-alpha.5", zips=zips, previous=prev, rbf=rbf, changelog=cl)

    # 1. the CORE_VERSION ifdef evaluator against the real mp3_soc.v (four contracts, not the first literal)
    for macros, want in (([], "4D503317"), (["TAU_RAM_192K"], "4D503318"), (["TAU_CLK66"], "4D503319"),
                         (["TAU_RAM_192K", "TAU_CLK66", "TAU_BLIT"], "4D50331A")):
        check(tc.core_version_for_macros(macros) == want, f"CORE_VERSION for {macros or 'no macros'} = {want}")

    # 2. build, write, read back, recompute every hash from the zips
    out = t / "tau-compat.json"
    out.write_text(tc.dumps(tc.build(**kw)))
    doc = json.loads(out.read_text())
    check(list(doc) == ["schema", "release", "date_release", "prerelease", "packages", "requires_omega", "notes"], "top-level keys in schema order")
    check(doc["schema"] == 1 and doc["release"] == "v0.6.0-alpha.5" and doc["prerelease"] is True and doc["date_release"] == "2026-10-08", "release fields")
    check(len(doc["packages"]) == 2, "one packages entry per zip")
    for p, z in zip(doc["packages"], zips):
        with zipfile.ZipFile(z) as f:
            core = p["core_id"]; plat = "tau" if core == "alfatreze.TAU" else "tau_diagnostic"
            rom = f.read(f"Assets/{plat}/common/tau.rom")
            ok = (p["zip"] == z.name and p["zip_sha256"] == tc.sha(z.read_bytes())
                  and p["bitstream_sha256"] == tc.sha(f.read(f"Cores/{core}/bitstream.rbf_r"))
                  and p["rom_sha256"] == tc.sha(rom) and p["cold_sha256"] == tc.sha(f.read(f"Assets/{plat}/common/tau-cold.bin"))
                  and p["rom_accepts"] == ["4D50331A"] and p["rom_needs"] == ["HALCYON", "LPC"]
                  and p["bitstream_core_version"] == "4D50331A" and p["bitstream_core_version"] in p["rom_accepts"])
        check(ok, f"{core}: every hash and marker matches the zip")
    r = doc["requires_omega"]
    check(list(r) == ["library_index_version", "assets_sections", "assets_read_limit_bytes", "report_tags_max", "persist_ids_changed", "min_omega"], "requires_omega keys in schema order")
    check(r["library_index_version"] == 1 and r["assets_read_limit_bytes"] == 65536, "index version 1, assets limit 64 KiB (fw)")
    check(set(r["assets_sections"]) == {"THEM", "METR", "PRST"}, f"assets sections read by the firmware: {r['assets_sections']}")
    check(r["report_tags_max"] == 27, f"last report tag {r['report_tags_max']} (SR_T_NOWPLAYING = 27)")
    check(r["persist_ids_changed"] == [16, 18], f"persist ids changed/removed vs previous: {r['persist_ids_changed']}")
    check(r["min_omega"] == json.loads((ROOT / "tools/omega_compat.json").read_text())["min_omega"], "min_omega from tools/omega_compat.json")
    check(doc["notes"] == "persist id 16 is the Halcyon preset now.", "notes from this release's CHANGELOG 'Omega:' lines only")
    check(tc.verify(out, **kw) == [], "verify: file matches the zips and the tree")

    # 3. mutations
    mrom = bytearray(ROM_N); mrom[3] ^= 1
    make(new, "alfatreze.TAU", "tau", bytes(mrom))                       # rebuild the normal zip with one ROM byte changed
    errs = tc.verify(out, **kw)
    check(any("rom_sha256" in e for e in errs) and any("zip_sha256" in e for e in errs), f"one ROM byte changed is caught ({len(errs)} differences)")
    make(new, "alfatreze.TAU", "tau", ROM_N)                             # restore
    check(tc.verify(out, **kw) == [], "restored zip verifies again")
    bad = dict(doc); bad["requires_omega"] = dict(r, report_tags_max=28); (t / "hand.json").write_text(tc.dumps(bad))
    check(any("report_tags_max" in e for e in tc.verify(t / "hand.json", **kw)), "a hand-edited field is caught")
    old_rom = ROM_N.replace(b"4D50331A", b"4D503317")
    make(new, "alfatreze.TAU", "tau", old_rom)
    raises(lambda: tc.build(**kw), "ROM that does not accept the bitstream's CORE_VERSION", "black screen")
    make(new, "alfatreze.TAU", "tau", ROM_N)
    (t / "ap_core.rbf.json").write_text(json.dumps({"macros": ["TAU_RAM_192K"], "rbf_sha256": tc.sha(RAW)}))
    raises(lambda: tc.build(**kw), "bitstream rev 24 (192 KB only) against rev-26 ROMs", "4D503318")
    (t / "ap_core.rbf.json").write_text(json.dumps({"macros": ["TAU_RAM_192K", "TAU_CLK66"], "rbf_sha256": "00" * 32}))
    raises(lambda: tc.build(**kw), "fit manifest of a different RBF", "different RBF")
    (t / "ap_core.rbf.json").unlink()
    raises(lambda: tc.build(**kw), "RBF without a fit manifest and no --bitstream-version", "--bitstream-version")
    check(tc.build(**dict(kw, rbf=None, bitstream_version="4D50331A"))["packages"][0]["bitstream_core_version"] == "4D50331A", "--bitstream-version is accepted instead")
    other = t / "other.rbf"; other.write_bytes(RAW[::-1])
    (t / "other.rbf.json").write_text(json.dumps({"macros": ["TAU_RAM_192K", "TAU_CLK66"], "rbf_sha256": tc.sha(other.read_bytes())}))
    raises(lambda: tc.build(**dict(kw, rbf=other)), "RBF that is not the bitstream in the zips", "not the bitstream")
    raises(lambda: tc.build(**dict(kw, release="v0.6.0-alpha.9", rbf=None, bitstream_version="4D50331A")), "release missing from the changelog", "heading")
    cl7 = t / "CL7.md"; cl7.write_text("## v0.7.0 — later\n- x\n")
    raises(lambda: tc.build(**dict(kw, release="v0.7.0", changelog=cl7, rbf=None, bitstream_version="4D50331A")), "release whose X.Y.Z is not core.json's", "core.json version")
    raises(lambda: tc.build(**dict(kw, previous=prev[:1], rbf=None, bitstream_version="4D50331A")), "missing previous zip for a core", "--previous")
    check(tc.build(**dict(kw, previous=None, rbf=None, bitstream_version="4D50331A"))["requires_omega"]["persist_ids_changed"] == [], "--no-previous gives an empty list")

print(f"tau-compat: {fails} failure(s)")
sys.exit(1 if fails else 0)
