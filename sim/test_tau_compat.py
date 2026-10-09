#!/usr/bin/env python3
"""tau-compat.json (tools/tau_compat.py, RELEASE_SYSTEM_SPEC sections 10-11): build a release-shaped set of zips, write the file,
read it back, recompute every hash from the zips and check each field; check the schema-2 layout and the expected card state
(an unpacked install checks clean); then mutate inputs and cards and confirm each is caught."""
import json, subprocess, sys, tempfile, zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_compat as tc  # noqa: E402
import tau_library  # noqa: E402

HEAD = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT, capture_output=True, text=True).stdout.strip()
def fitman(macros, rbf_sha, **extra):
    return json.dumps(dict({"macros": macros, "rbf_sha256": rbf_sha, "commit": HEAD, "rtl_dirty": False}, **extra))

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

REAL_INTERACT = json.loads((ROOT / "dist/Cores/alfatreze.TAU/interact.json").read_text())   # the shipped persist ids

def interact(ids=None):
    """The shipped interact.json; `ids` = {id: (name, max) or None} overrides or removes entries (an older release's view)."""
    j = json.loads(json.dumps(REAL_INTERACT))
    for i, ov in (ids or {}).items():
        vs = j["interact"]["variables"]
        if ov is None:
            j["interact"]["variables"] = [v for v in vs if v["id"] != i]
            continue
        v = next((v for v in vs if v["id"] == i), None)
        if v is None:
            v = {"id": i, "type": "slider_u32", "persist": True, "defaultval": 0, "graphical": {"min": 0}}; vs.append(v)
        v["name"], v["graphical"]["max"] = ov
    return j

DATA = (ROOT / "dist/Cores/alfatreze.TAU/data.json").read_text()      # the shipped slot map

def make(d, core, plat, rom, version="0.6.0", date="2026-10-08", bit=REVB, ids=None, extra=None, data=DATA):
    author, short = core.split(".")
    p = d / f"{core}_{version}_{date}.zip"
    with zipfile.ZipFile(p, "w") as z:
        z.writestr = (lambda w: (lambda name, data: w(zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0)), data)))(z.writestr)   # fixed time: same inputs, same zip hash
        z.writestr(f"Cores/{core}/core.json", json.dumps({"core": {"metadata": {"author": author, "shortname": short,
                   "version": version, "date_release": date, "platform_ids": [plat]}}}))
        z.writestr(f"Cores/{core}/bitstream.rbf_r", bit)
        z.writestr(f"Cores/{core}/interact.json", json.dumps(interact(ids)))
        z.writestr(f"Assets/{plat}/{core}/tau.rom", rom)                    # H4: build-bound files are core-specific
        z.writestr(f"Assets/{plat}/{core}/tau-cold.bin", b"cold" + rom[:8])
        z.writestr(f"Cores/{core}/data.json", data)
        z.writestr(f"Assets/{plat}/{core}/tau-loading.bin", b"splash")
        z.writestr(f"Platforms/{plat}.json", "{}")
        z.writestr(f"Platforms/_images/{plat}.bin", b"img")
        for k, v in (extra or {}).items():
            z.writestr(k, v)
    return p

ROM_N = b"\x13\x00" * 40 + b"TAUFWPAIR:4D50331A;" + b"TAUFWNEED:HALCYON,LPC;" + b"\0" * 16
ROM_D = ROM_N + b"diag"

with tempfile.TemporaryDirectory() as t:
    t = Path(t)
    old, new = t / "old", t / "new"
    old.mkdir(); new.mkdir()
    # alpha.4's view: Repeat had a smaller range, and an id 18 that this release no longer persists
    prev = [make(old, "alfatreze.TAU", "tau", ROM_N, date="2026-10-07", ids={12: ("Repeat", 1), 18: ("Gone", 1)}),
            make(old, "alfatreze.TAU_DIAGNOSTIC", "tau_diagnostic", ROM_D, date="2026-10-07")]
    zips = [make(new, "alfatreze.TAU", "tau", ROM_N), make(new, "alfatreze.TAU_DIAGNOSTIC", "tau_diagnostic", ROM_D)]
    rbf = t / "ap_core.rbf"; rbf.write_bytes(RAW)
    (t / "ap_core.rbf.json").write_text(fitman(["TAU_RAM_192K", "TAU_CLK66", "TAU_HALCYON", "TAU_LPC", "TAU_POLY"], tc.sha(RAW)))
    cl = t / "CHANGELOG.md"
    real_cl = (ROOT / "CHANGELOG.md").read_text()
    # the test inserts its own v0.6.0-preview.1 section below: once the real changelog has released that tag, its heading must not collide (2026-10-09)
    real_cl = real_cl.replace("## v0.6.0-preview.1", "## v0.6.0-preview.0", 1) if "## v0.6.0-preview.1" in real_cl else real_cl
    cl.write_text(real_cl.replace("## v0.6.0-alpha.4", "## v0.6.0-preview.1 — 8 October 2026\n\n- Halcyon only.\n- Omega: persist id 16 is the Halcyon preset now.\n\n## v0.6.0-alpha.4", 1))
    kw = dict(release="v0.6.0-preview.1", zips=zips, previous=prev, rbf=rbf, changelog=cl, previous_release="v0.6.0-alpha.4")

    # 1. the CORE_VERSION ifdef evaluator against the real mp3_soc.v (four contracts, not the first literal)
    for macros, want in (([], "4D503317"), (["TAU_RAM_192K"], "4D503318"), (["TAU_CLK66"], "4D503319"),
                         (["TAU_RAM_192K", "TAU_CLK66", "TAU_BLIT"], "4D50331A")):
        check(tc.core_version_for_macros(macros) == want, f"CORE_VERSION for {macros or 'no macros'} = {want}")

    # 2. build, write, read back, recompute every hash from the zips
    out = t / "tau-compat.json"
    out.write_text(tc.dumps(tc.build(**kw)))
    doc = json.loads(out.read_text())
    check(list(doc) == ["schema", "release", "date_release", "prerelease", "packages", "requires_omega", "notes", "source", "previous_release", "persist_registry"], "top-level keys in schema order")
    check(doc["schema"] == 2 and doc["release"] == "v0.6.0-preview.1" and doc["prerelease"] is True and doc["date_release"] == "2026-10-08", "release fields")
    check(len(doc["packages"]) == 2, "one packages entry per zip")
    for p, z in zip(doc["packages"], zips):
        with zipfile.ZipFile(z) as f:
            core = p["core_id"]; plat = "tau" if core == "alfatreze.TAU" else "tau_diagnostic"
            rom = f.read(f"Assets/{plat}/{core}/tau.rom")
            ok = (p["zip"] == z.name and p["zip_sha256"] == tc.sha(z.read_bytes())
                  and p["bitstream_sha256"] == tc.sha(f.read(f"Cores/{core}/bitstream.rbf_r"))
                  and p["rom_sha256"] == tc.sha(rom) and p["cold_sha256"] == tc.sha(f.read(f"Assets/{plat}/{core}/tau-cold.bin"))
                  and p["rom_accepts"] == ["4D50331A"] and p["rom_needs"] == ["HALCYON", "LPC"]
                  and p["bitstream_core_version"] == "4D50331A" and p["bitstream_core_version"] in p["rom_accepts"])
        check(ok, f"{core}: every hash and marker matches the zip")
    r = doc["requires_omega"]
    check(list(r) == ["library_index_version", "assets_sections", "assets_read_limit_bytes", "report_tags_max", "persist_ids_changed", "min_omega"], "requires_omega keys in schema order")
    check(r["library_index_version"] == 1 and r["assets_read_limit_bytes"] == 65536, "index version 1, assets limit 64 KiB (fw)")
    check(set(r["assets_sections"]) == {"THEM", "METR", "PRST"}, f"assets sections read by the firmware: {r['assets_sections']}")
    check(r["report_tags_max"] == 27, f"last report tag {r['report_tags_max']} (SR_T_NOWPLAYING = 27)")
    check(r["persist_ids_changed"] == [12, 16, 18], f"persist ids changed (12 range), removed (18) and re-meant per the registry (16) vs alpha.4: {r['persist_ids_changed']}")
    check(doc["previous_release"] == "v0.6.0-alpha.4", "previous_release recorded")
    check(doc["persist_registry"]["16"] == {"name": "Halcyon EQ preset", "meaning": 2, "since": "v0.6.0-preview.1"}
          and set(doc["persist_registry"]) == {str(v["id"]) for v in REAL_INTERACT["interact"]["variables"] if v.get("persist")},
          "persist_registry lists every persisted id with its meaning and since")
    check(r["min_omega"] == json.loads((ROOT / "tools/omega_compat.json").read_text())["min_omega"], "min_omega from tools/omega_compat.json")
    check(doc["notes"] == "persist id 16 is the Halcyon preset now.", "notes from this release's CHANGELOG 'Omega:' lines only")
    check(tc.verify(out, **kw) == [], "verify: file matches the zips and the tree")

    check(tc.schema_errors(doc) == [], "the file matches the published JSON Schema (docs/schemas/tau-compat.schema.json)")

    # 2a. review fixes (2026-10-08): source, features, root, preserve flag, schema keywords, fixture
    check(doc["source"] == {"commit": HEAD, "dirty": tc.source_state()["dirty"]}, f"source = this tree's commit {HEAD} and its dirty state")
    check(all(p["bitstream_features"] == ["HALCYON", "LPC", "POLY"] for p in doc["packages"]), "bitstream_features from the fit manifest macros (H, L, P)")
    nd = tc.build(**dict(kw, rbf=None, bitstream_version="4D50331A"))
    check(all(p["bitstream_features"] is None for p in nd["packages"]), "bitstream_features is null when CORE_VERSION is given by hand")
    taua = next(e for e in doc["packages"][0]["layout"] if e["path"].endswith("tau-assets.bin"))
    check(taua["format"].get("preserve_unknown_sections") is True, "TAUA format tells writers to carry unknown sections (H1)")
    tdbe = next(e for e in doc["packages"][0]["layout"] if e["path"].endswith("tau-library.tdb"))
    check(tdbe["format"].get("root") == "/Assets/tau/common/", "library index format carries its root (M8)")
    supported = {"$schema", "$id", "title", "description", "type", "required", "properties", "items", "const", "enum", "pattern", "minimum", "additionalProperties"}
    def kw_used(node):
        out = set(node)
        for v in node.get("properties", {}).values(): out |= kw_used(v)
        if isinstance(node.get("items"), dict): out |= kw_used(node["items"])
        return out
    schema = json.loads(tc.SCHEMA_FILE.read_text())
    check(kw_used(schema) <= supported, f"the published schema uses only keywords tau_compat checks (M7): extra {sorted(kw_used(schema) - supported)}")
    try:
        import jsonschema
        jsonschema.validate(doc, schema)
        check(True, "a real JSON Schema validator accepts the file (M7)")
    except ImportError:
        print("note: jsonschema not installed; real-validator cross-check skipped (M7)")
    check(subprocess.run([sys.executable, str(ROOT / "tools/make_compat_fixtures.py"), "--check"], capture_output=True).returncode == 0,
          "docs/schemas/fixtures/tau-assets-roundtrip.bin matches the reference packers (H1)")
    import tau_assets
    fx = tau_assets.parse((ROOT / "docs/schemas/fixtures/tau-assets-roundtrip.bin").read_bytes())
    check(list(fx["sections"]) == ["THEM", "METR", "PRST"], "the round-trip fixture carries THEM, METR and PRST")
    soc = (ROOT / "src/fpga/core/mp3_soc.v").read_text()
    check(tc.selecting_macros(soc) == ["TAU_CLK66", "TAU_RAM_192K"], f"CORE_VERSION is selected by {tc.selecting_macros(soc)}")
    check(tc.zero_defined('set_global_assignment -name VERILOG_MACRO "TAU_CLK66=0"\n', soc) == ["TAU_CLK66"]
          and tc.zero_defined('set_global_assignment -name VERILOG_MACRO "TAU_CLK66=1"\nset_global_assignment -name VERILOG_MACRO "TAU_SPEC=0"\n', soc) == [],
          "a CORE_VERSION-selecting macro written as =0 is detected (only those)")

    # 2b. schema 2: the layout of each package
    for p, z in zip(doc["packages"], zips):
        lay = {e["path"]: e for e in p["layout"]}
        with zipfile.ZipFile(z) as f:
            files = {n: tc.sha(f.read(n)) for n in f.namelist()}
        shipped = {k: e["sha256"] for k, e in lay.items() if e["role"] in ("owned", "shared")}
        check(shipped == files, f"{p['core_id']}: owned+shared entries are exactly the zip's files with their hashes")
        plat = "tau" if p["core_id"] == "alfatreze.TAU" else "tau_diagnostic"
        c = f"Assets/{plat}/common/"
        cs = f"Assets/{plat}/{p['core_id']}/"
        check(lay[cs + "tau.rom"]["role"] == "owned" and lay[cs + "tau.rom"]["required"] and lay[cs + "tau.rom"]["slot"] == 1
              and lay[cs + "tau-cold.bin"]["slot"] == 6 and lay[cs + "tau-loading.bin"]["slot"] == 4, "tau.rom, cold image, splash: owned, core-specific, their slots (H4)")
        check(lay[f"Platforms/{plat}.json"]["role"] == "shared", "platform files are shared")
        check(lay[c + "tau-library.tdb"]["role"] == "generated" and lay[c + "tau-library.tdb"]["format"] == {"name": "tau-library", "version": 1, "root": "/" + c}, "library index: generated, format v1")
        check(lay[c + "tau-assets.bin"]["role"] == "user" and lay[c + "tau-assets.bin"]["format"]["name"] == "TAUA", "tau-assets.bin: user data, TAUA")
        cov = [e for e in p["layout"] if e.get("pattern") and e["role"] == "generated"]
        check(len(cov) == 1 and cov[0]["path"].endswith("tau-art/cover_128.pal256.timg"), "covers: generated pattern with the firmware's own file name (fw/timg.inc)")
        check(any(e.get("pattern") and e["role"] == "user" and e["slot"] == 2 for e in p["layout"]), "media: user pattern from the audio slot")

    # 2c. expected card state: an unpacked install checks clean, then card mutations are caught
    card = t / "card"; card.mkdir()
    with zipfile.ZipFile(zips[0]) as f:
        f.extractall(card)
    (card / "Assets/tau/common").mkdir(parents=True, exist_ok=True)       # a real card always has the shared media folder
    res = tc.check_card(doc, card)
    check(res == [], f"unpacked install of the normal zip checks clean ({res})")
    check(tc.check_card(doc, card, "alfatreze.TAU_DIAGNOSTIC") != [], "the Diagnostic package is reported missing on a normal-only card")
    rom = card / "Assets/tau/alfatreze.TAU/tau.rom"; good = rom.read_bytes()
    rom.write_bytes(good[:-1] + b"X")
    check(any(l == "error" and "tau.rom differs" in m for l, m in tc.check_card(doc, card)), "card: changed ROM is an error")
    rom.unlink()
    check(any(l == "error" and "tau.rom is missing" in m for l, m in tc.check_card(doc, card)), "card: missing ROM is an error")
    rom.write_bytes(good)
    (card / "Platforms/tau.json").write_text('{"other": 1}')
    check([l for l, m in tc.check_card(doc, card)] == ["warn"], "card: a shared file from another release is only a warning")
    with zipfile.ZipFile(zips[0]) as f:
        (card / "Platforms/tau.json").write_bytes(f.read("Platforms/tau.json"))
    (card / "Cores/alfatreze.TAU/old.bin").write_bytes(b"x")
    check(any(l == "warn" and "stale" in m for l, m in tc.check_card(doc, card)), "card: a stray file in the core folder is a stale-file warning")
    (card / "Cores/alfatreze.TAU/old.bin").unlink()
    tdb = card / "Assets/tau/common/tau-library.tdb"
    tdb.write_bytes((0x42494C54).to_bytes(4, "little") + (2).to_bytes(2, "little") + (2).to_bytes(2, "little") + bytes(120))
    check(any("reader version 2" in m for l, m in tc.check_card(doc, card)), "card: an index needing reader version 2 is refused")
    tdb.write_bytes((0x42494C54).to_bytes(4, "little") + (1).to_bytes(2, "little") + (1).to_bytes(2, "little") + bytes(120))
    check(any("does not parse" in m for l, m in tc.check_card(doc, card)), "card: a damaged v1 index is reported (root cannot be read)")
    ent = tau_library.synth(5, 2, 1)
    tdb.write_bytes(tau_library.build_index(ent, root_prefix="/Assets/tau_other/common/"))
    check(any("index root is /Assets/tau_other/common/" in m for l, m in tc.check_card(doc, card)), "card: an index built for another platform's root is refused (B-136, review M8)")
    tdb.write_bytes(tau_library.build_index(ent))
    check(tc.check_card(doc, card) == [], "card: a real v1 index with this core's root is accepted")
    asb = card / "Assets/tau/common/tau-assets.bin"
    asb.write_bytes(b"TAUA" + (2).to_bytes(2, "little") + bytes(6))
    check(any("TAUA version 2" in m for l, m in tc.check_card(doc, card)), "card: a TAUA v2 file is refused")
    asb.write_bytes(b"TAUA" + (1).to_bytes(2, "little") + bytes(70000))
    check(any("at most 65536" in m for l, m in tc.check_card(doc, card)), "card: a TAUA file over the read limit is refused")
    asb.unlink()
    art = card / "Assets/tau/common/Album/tau-art"; art.mkdir(parents=True)
    (art / "cover_128.pal256.timg").write_bytes(b"JPEG....")
    check(any("not a TIM1 image" in m for l, m in tc.check_card(doc, card)), "card: a cover that is not TIM1 is refused")
    (art / "cover_128.pal256.timg").write_bytes(b"TIM1" + bytes(12))
    (card / "Settings/alfatreze.TAU/Interact").mkdir(parents=True)
    (card / "Settings/alfatreze.TAU/Interact/interact_persist.json").write_text("{broken")
    check(any("not valid JSON" in m for l, m in tc.check_card(doc, card)), "card: a corrupt settings file is reported")
    (card / "Settings/alfatreze.TAU/Interact/interact_persist.json").write_text("{}")
    check(tc.check_card(doc, card) == [], "card: TIM1 cover and valid settings check clean")

    # 2d. layout refusals at build time
    cfg = t / "omega.json"
    cfg.write_text(json.dumps({"min_omega": "0.3.0", "obsolete": [{"core_id": "alfatreze.TAU", "path": "Assets/tau/alfatreze.TAU/TAU.json"},
                                                                {"core_id": "alfatreze.TAU_DIAGNOSTIC", "path": "Assets/tau_diagnostic/x/old.json"}]}))
    lay = tc.build(**dict(kw, omega_cfg=cfg))["packages"][0]["layout"]
    check(any(e["role"] == "obsolete" and e["path"] == "Assets/tau/alfatreze.TAU/TAU.json" for e in lay), "obsolete paths from the config appear in the layout")
    check(not any(e["path"] == "Assets/tau_diagnostic/x/old.json" for e in lay), "an obsolete path of another core is not in this core's layout")
    real = json.loads((ROOT / "tools/omega_compat.json").read_text())["obsolete"]
    check({"core_id": "alfatreze.TAU", "path": "Assets/tau/alfatreze.TAU/TAU.json"} in real, "the retired TAU.json is listed obsolete in tools/omega_compat.json")
    check(not (ROOT / "dist/Assets/tau/alfatreze.TAU/TAU.json").exists(), "dist/ no longer ships TAU.json")

    # 2e. the installer's match test: a manifest applies only to the exact package it describes
    pk = t / "pkg"; pk.mkdir()
    with zipfile.ZipFile(zips[0]) as f:
        f.extractall(pk)
    ent, why = tc.package_match(doc, pk, "alfatreze.TAU")
    check(ent is not None and why == [], "package_match: the unpacked zip matches its manifest entry")
    (pk / "Assets/tau/alfatreze.TAU/tau.rom").write_bytes(b"other")
    check(tc.package_match(doc, pk, "alfatreze.TAU")[0] is None, "package_match: a different ROM does not match")
    with zipfile.ZipFile(zips[0]) as f:
        (pk / "Assets/tau/alfatreze.TAU/tau.rom").write_bytes(f.read("Assets/tau/alfatreze.TAU/tau.rom"))
    (pk / "Cores/alfatreze.TAU/extra.txt").write_text("x")
    check(tc.package_match(doc, pk, "alfatreze.TAU")[0] is None, "package_match: an unlisted file in the core folder does not match")
    check(tc.package_match(doc, pk, "alfatreze.TAU_X")[0] is None, "package_match: a core the release does not have does not match")
    (card / "Assets/tau/alfatreze.TAU").mkdir(parents=True, exist_ok=True); (card / "Assets/tau/alfatreze.TAU/TAU.json").write_text("{}")
    check(any(l == "warn" and "obsolete" in m for l, m in tc.check_card(tc.build(**dict(kw, omega_cfg=cfg)), card)), "card: an obsolete file still present is reported")
    zx = make(new, "alfatreze.TAU", "tau", ROM_N, extra={"Assets/tau/alfatreze.TAU/TAU.json": "{}"})
    raises(lambda: tc.build(**dict(kw, omega_cfg=cfg)), "a path listed obsolete that the release still ships", "obsolete")
    zx = make(new, "alfatreze.TAU", "tau", ROM_N, extra={"README.txt": "hi"})
    raises(lambda: tc.build(**kw), "a zip file with no layout role", "no layout role")
    dj = json.loads(DATA); dj["data"]["data_slots"].append({"name": "New", "id": 9, "deferload": True, "parameters": "0x0", "filename": "tau-new.bin"})
    make(new, "alfatreze.TAU", "tau", ROM_N, data=json.dumps(dj))
    raises(lambda: tc.build(**kw), "a data slot naming a file nobody ships or generates", "NOT_SHIPPED")
    make(new, "alfatreze.TAU", "tau", ROM_N)
    hand = json.loads(out.read_text()); hand["packages"][0]["layout"][0]["role"] = "bogus"
    (t / "hand2.json").write_text(json.dumps(hand))
    check(any(e.startswith("schema:") for e in tc.verify(t / "hand2.json", **kw)), "verify reports a schema violation")

    # 2f. review refusals: dirty RTL fit, no commit, missing feature, a shipped user file
    (t / "ap_core.rbf.json").write_text(fitman(["TAU_RAM_192K", "TAU_CLK66", "TAU_HALCYON", "TAU_LPC"], tc.sha(RAW), rtl_dirty=True))
    raises(lambda: tc.build(**kw), "a fit built from a dirty RTL tree (H3)", "rtl_dirty")
    (t / "ap_core.rbf.json").write_text(json.dumps({"macros": ["TAU_RAM_192K", "TAU_CLK66"], "rbf_sha256": tc.sha(RAW)}))
    raises(lambda: tc.build(**kw), "a fit manifest without a commit (H3)", "no commit")
    (t / "ap_core.rbf.json").write_text(fitman(["TAU_RAM_192K", "TAU_CLK66", "TAU_HALCYON"], tc.sha(RAW)))
    raises(lambda: tc.build(**kw), "a ROM needing LPC on a bitstream built without TAU_LPC (M4)", "NO UNIT")
    (t / "ap_core.rbf.json").write_text(fitman(["TAU_RAM_192K", "TAU_CLK66", "TAU_HALCYON", "TAU_LPC", "TAU_POLY"], tc.sha(RAW)))
    make(new, "alfatreze.TAU", "tau", ROM_N, extra={"Assets/tau/common/tau-assets.bin": b"TAUA"})
    raises(lambda: tc.build(**kw), "a zip shipping the user's tau-assets.bin (M6)", "overwrite the user")
    make(new, "alfatreze.TAU", "tau", ROM_N)
    check(tc.verify(out, **kw) == [], "after the refusals the original inputs verify again")

    # 2g. step 3 of the review: the persist registry (H2) and the previous release (M2)
    reg = tc.load_registry()
    rel = tc.changelog_releases()
    check(tc.registry_errors([{"core_id": "dist", "interact": REAL_INTERACT}], reg, rel) == [], "the shipped interact.json matches tools/persist_registry.json")
    dj = json.loads(json.dumps(REAL_INTERACT)); next(v for v in dj["interact"]["variables"] if v["id"] == 27)["name"] = "(internal) other"
    check(any("named" in e for e in tc.registry_errors([{"core_id": "x", "interact": dj}], reg, rel)), "a persisted id renamed without a registry update is refused")
    dj = interact({29: ("New thing", 3)})
    check(any("does not describe" in e for e in tc.registry_errors([{"core_id": "x", "interact": dj}], reg, rel)), "a new persisted id missing from the registry is refused")
    check(tc.tag_key("v0.6.0-alpha.4") < tc.tag_key("v0.6.0-alpha.10") < tc.tag_key("v0.6.0-preview.1") < tc.tag_key("v0.6.0-rc.1")
          < tc.tag_key("v0.6.0") < tc.tag_key("v0.6.1") and tc.tag_key("v0.6.0-dev.385") < tc.tag_key("v0.6.0-preview.1"),
          "tag order: alpha.4 < alpha.10 < preview.1 < rc.1 < 0.6.0 < 0.6.1, dev < preview")
    check(tc.changelog_releases()["v0.6.0-alpha.4"] == "2026-10-07" and "v1.4.0" not in tc.changelog_releases(), "changelog headings parse to dates; inherited HarpMudd v1.x headings are ignored")
    raises(lambda: tc.build(**dict(kw, release="v0.6.0-alpha.9", changelog=t / "c9.md", rbf=None, bitstream_version="4D50331A"))
           if (t / "c9.md").write_text(real_cl.replace("## v0.6.0-alpha.4", "## v0.6.0-alpha.9 — 8 October 2026\n- x\n\n## v0.6.0-alpha.4", 1)) else None,
           "a registry `since` later than the release being built", "later than the release")
    raises(lambda: tc.build(**dict(kw, previous_release=None)), "--previous without --previous-release", "--previous-release")
    raises(lambda: tc.build(**dict(kw, previous_release="v0.6.0-alpha.3")), "previous zips that are not that release (date differs)", "wrong previous zip")
    raises(lambda: tc.build(**dict(kw, previous_release="v0.6.0-preview.1")), "a previous release not older than the release", "not older")
    # Omega's union rule over a skipped release: changes since alpha.3 include the alpha.4 meanings (10 Volume, 28 theme mode)
    since3 = sorted(int(i) for i, v in doc["persist_registry"].items() if tc.tag_key(v["since"]) > tc.tag_key("v0.6.0-alpha.3"))
    check(since3 == [10, 16, 28], f"from persist_registry, a card on alpha.3 must treat ids {since3} as changed (union rule)")

    # 2h. dev packages get their own manifest (review M5)
    dev = t / "devpkg"; (dev / "pocket").mkdir(parents=True)
    with zipfile.ZipFile(zips[0]) as f:
        f.extractall(dev / "pocket")
    (dev / "pocket/bitstream-manifest.json").write_text("{}")             # package-root files are not part of the card layout
    cp = tc.build_dev(dev / "pocket", 412, rbf=rbf)
    dd = json.loads(cp.read_text())
    check(cp == dev / "tau-compat.json" and dd["release"] == "v0.6.0-dev.412" and dd["prerelease"] is True and dd["previous_release"] is None,
          f"dev manifest written next to the package as {dd['release']}")
    z1 = (dev / f"alfatreze.TAU_0.6.0_dev.zip").read_bytes()
    tc.build_dev(dev / "pocket", 412, rbf=rbf)
    check(z1 == (dev / "alfatreze.TAU_0.6.0_dev.zip").read_bytes(), "the dev zip is deterministic (same package, same bytes)")
    check(tc.package_match(dd, dev / "pocket", "alfatreze.TAU")[0] is not None, "the installer accepts the dev manifest for its own package")
    check(tc.dev_release_tag("0.6.0", "0.5.0-alpha.1") == "v0.6.0-dev.0.5.0.alpha.1" and tc.tag_key("v0.6.0-dev.412") < tc.tag_key("v0.6.0-preview.1"),
          "dev tags are tag-safe and sort before previews")

    # 2i. H4: build-bound files are core-specific
    legacy = {"Assets/tau/common/tau.rom": ROM_N}
    make(new, "alfatreze.TAU", "tau", ROM_N, extra=legacy)
    raises(lambda: tc.build(**kw), "a zip shipping tau.rom in the platform-wide common/ folder", "common/ folder")
    dj = json.loads(DATA); [sl.update(parameters="0x108") for sl in dj["data"]["data_slots"] if sl["id"] == 1]
    make(new, "alfatreze.TAU", "tau", ROM_N, data=json.dumps(dj))
    raises(lambda: tc.build(**kw), "a build-bound data slot without the core-specific bit", "not core-specific")
    make(new, "alfatreze.TAU", "tau", ROM_N)
    lay = {e["path"]: e for e in doc["packages"][0]["layout"]}
    check(all(lay.get(f"Assets/tau/common/{n}", {}).get("role") == "obsolete" for n in ("tau.rom", "tau-cold.bin", "tau-loading.bin")),
          "the old common/ copies are listed obsolete for the normal core")
    c2 = t / "card2"
    for core, params in (("alfatreze.TAU", "0x10A"), ("alfatreze.TAU_OLD", "0x108")):
        (c2 / "Cores" / core).mkdir(parents=True)
        (c2 / "Cores" / core / "core.json").write_text(json.dumps({"core": {"metadata": {"platform_ids": ["tau"]}}}))
        (c2 / "Cores" / core / "data.json").write_text(json.dumps({"data": {"data_slots": [{"id": 1, "filename": "tau.rom", "parameters": params}]}}))
    check(tc.still_read_by_other_core(c2, "Assets/tau/common/tau.rom", "alfatreze.TAU"), "an old-layout core on the same platform still reads common/tau.rom")
    (c2 / "Cores/alfatreze.TAU_OLD/data.json").write_text(json.dumps({"data": {"data_slots": [{"id": 1, "filename": "tau.rom", "parameters": "0x10A"}]}}))
    check(not tc.still_read_by_other_core(c2, "Assets/tau/common/tau.rom", "alfatreze.TAU"), "nobody else reads it once every core is core-specific")
    check(tc.verify(out, **kw) == [], "after the H4 refusals the original inputs verify again")

    # 2j. the full version (owner, 2026-10-08): the ROM's TAUVER stamp must agree with core.json
    tv_field = b"TAUVER:0.6.0-dev.9+abc1234" + b"\0" * (48 - 26)
    make(new, "alfatreze.TAU", "tau", ROM_N + tv_field)
    raises(lambda: tc.build(**kw), "a ROM stamped with another version than core.json", "would disagree")
    make(new, "alfatreze.TAU", "tau", ROM_N)
    check(all(p["rom_version"] is None for p in doc["packages"]), "rom_version is null for a ROM without the field")

    # 2k. release channels (RELEASE_SYSTEM_SPEC section 4): names, predecessors, replaces, retarget
    import tau_layout as tl
    check([tl.channel_of(v) for v in ("v0.7.0", "0.7.0-preview.2", "v0.7.0-rc.1", "0.6.0-alpha.5", "0.7.0-dev.385")]
          == ["stable", "preview", "preview", "preview", "dev"], "channel from the version: stable / preview (preview, rc, old alpha) / dev")
    check(tl.zip_name("alfatreze.TAU Preview Diagnostics", "0.7.0-preview.2", "2026-11-02") == "alfatreze.TAU_Preview_Diagnostics_0.7.0-preview.2_2026-11-02.zip",
          "zip names keep Analogue's pattern with spaces as _")
    chz = t / "chz"; chz.mkdir()
    dz = make(chz, "alfatreze.TAU Diagnostics", "tau", ROM_D)
    dd = tc.build(**dict(kw, zips=[zips[0], dz], previous=prev, rbf=None, bitstream_version="4D50331A"))
    pk = {p["core_id"]: p for p in dd["packages"]}
    check(pk["alfatreze.TAU Diagnostics"]["replaces"] == ["alfatreze.TAU_DIAGNOSTIC"] and pk["alfatreze.TAU"]["replaces"] == [],
          "TAU Diagnostics names the old TAU_DIAGNOSTIC as replaced")
    check(dd["requires_omega"]["persist_ids_changed"] == [12, 16, 18], "a renamed core is compared with its predecessor's previous zip")
    rt = tl.retarget(ROOT / "dist", t / "rt", "alfatreze.TAU", "alfatreze.TAU Preview", "preview")
    rcj = json.loads((rt / "Cores/alfatreze.TAU Preview/core.json").read_text())["core"]["metadata"]
    rdj = {sl["id"]: sl["parameters"] for sl in json.loads((rt / "Cores/alfatreze.TAU Preview/data.json").read_text())["data"]["data_slots"]}
    check(rcj["shortname"] == "TAU Preview" and rcj["platform_ids"] == ["tau_preview", "tau"]
          and (rt / "Assets/tau_preview/alfatreze.TAU Preview/tau.rom").read_bytes() == (ROOT / "dist/Assets/tau/alfatreze.TAU/tau.rom").read_bytes()
          and json.loads((rt / "Platforms/tau_preview.json").read_text())["platform"]["name"] == "TAU Preview"
          and rdj[5] == "0x1000000" and rdj[8] == "0x1000000" and rdj[1] == "0x10A",
          "retarget: dist as 'TAU Preview' on tau_preview + tau, build files moved, shared slots read TAU's media")

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
    (t / "ap_core.rbf.json").write_text(fitman(["TAU_RAM_192K", "TAU_HALCYON", "TAU_LPC"], tc.sha(RAW)))
    raises(lambda: tc.build(**kw), "bitstream rev 24 (192 KB only) against rev-26 ROMs", "4D503318")
    (t / "ap_core.rbf.json").write_text(fitman(["TAU_RAM_192K", "TAU_CLK66"], "00" * 32))
    raises(lambda: tc.build(**kw), "fit manifest of a different RBF", "different RBF")
    (t / "ap_core.rbf.json").unlink()
    raises(lambda: tc.build(**kw), "RBF without a fit manifest and no --bitstream-version", "--bitstream-version")
    check(tc.build(**dict(kw, rbf=None, bitstream_version="4D50331A"))["packages"][0]["bitstream_core_version"] == "4D50331A", "--bitstream-version is accepted instead")
    other = t / "other.rbf"; other.write_bytes(RAW[::-1])
    (t / "other.rbf.json").write_text(fitman(["TAU_RAM_192K", "TAU_CLK66", "TAU_HALCYON", "TAU_LPC"], tc.sha(other.read_bytes())))
    raises(lambda: tc.build(**dict(kw, rbf=other)), "RBF that is not the bitstream in the zips", "not the bitstream")
    raises(lambda: tc.build(**dict(kw, release="v0.6.0-preview.9", rbf=None, bitstream_version="4D50331A")), "release missing from the changelog", "heading")
    cl7 = t / "CL7.md"; cl7.write_text("## v0.7.0 — later\n- x\n")
    raises(lambda: tc.build(**dict(kw, release="v0.7.0", changelog=cl7, rbf=None, bitstream_version="4D50331A")), "release whose X.Y.Z is not core.json's", "core.json version")
    raises(lambda: tc.build(**dict(kw, previous=prev[:1], rbf=None, bitstream_version="4D50331A")), "missing previous zip for a core", "--previous")
    check(tc.build(**dict(kw, previous=None, rbf=None, bitstream_version="4D50331A"))["requires_omega"]["persist_ids_changed"] == [], "--no-previous gives an empty list")

print(f"tau-compat: {fails} failure(s)")
sys.exit(1 if fails else 0)
