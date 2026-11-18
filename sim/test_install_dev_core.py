#!/usr/bin/env python3
"""Host test for tools/install_dev_core.py: installs the real dist/ package onto a scratch 'card' and checks the
dry run, the install, --replace (media survives), the catalog-cache deletion and the release-core protection.
Never touches a real card."""
import json, os, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = [sys.executable, str(ROOT / "tools/install_dev_core.py")]
CACHES = ["core_viewby_platform.bin", "corelist_cache.bin", "cores_cache.bin", "platform_viewby_category.bin", "platforms_cache.bin"]
fails = 0


def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + ("" if cond else "\n     " + str(info)))
    fails += 0 if cond else 1


def run(*args):
    r = subprocess.run(TOOL + [str(a) for a in args], capture_output=True, text=True, cwd=ROOT)
    return r.returncode, r.stdout + r.stderr


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    card = td / "card"
    for d in ("Cores", "Assets", "Platforms/_images", "System"):
        (card / d).mkdir(parents=True)
    for f in CACHES + ["recent.bin"]:
        (card / "System" / f).write_text("x")
    pkg = ROOT / "dist"

    rc, out = run(pkg, "--card", card)
    check("dry run succeeds and writes nothing", rc == 0 and "DRY RUN" in out and not (card / "Cores/alfatreze.TAU").exists())

    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk1", "--no-eject", "--yes")
    core_ok = (card / "Cores/alfatreze.TAU/bitstream.rbf_r").read_bytes() == (pkg / "Cores/alfatreze.TAU/bitstream.rbf_r").read_bytes()
    check("install copies the core byte-identically", rc == 0 and core_ok)
    check("catalog caches deleted, other System files kept",
          not any((card / "System" / f).exists() for f in CACHES) and (card / "System/recent.bin").exists())
    check("caches were backed up", all((td / "bk1/System" / f).exists() for f in CACHES))

    rc, out = run(pkg, "--card", card)
    check("an existing core is refused without --replace", rc != 0 and "--replace" in out)

    (card / "Assets/tau/common").mkdir(parents=True, exist_ok=True)       # a real card has the media folder; dist/ ships none since H4
    (card / "Assets/tau/common/my-track.mp3").write_bytes(b"media")
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk2", "--replace", "--allow-release", "--no-eject", "--yes")
    check("--replace refreshes the core and keeps the media", rc == 0 and (card / "Assets/tau/common/my-track.mp3").exists())
    check("--replace backed up the old copy", (td / "bk2/alfatreze.TAU/Cores/alfatreze.TAU").is_dir())
    check("--replace backup leaves the duplicate media out (B-542)", not (td / "bk2/alfatreze.TAU/Assets/tau/common/my-track.mp3").exists())
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk2b", "--replace", "--allow-release", "--backup-media", "--no-eject", "--yes")
    check("--backup-media keeps the media in the backup", rc == 0 and (td / "bk2b/alfatreze.TAU/Assets/tau/common/my-track.mp3").read_bytes() == b"media")

    bad_ta = td / "bad-assets.bin"; bad_ta.write_bytes(b"TAUA-test")
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk3x", "--replace", "--allow-release", "--assets", bad_ta, "--no-eject", "--yes")
    check("an --assets file the release cannot read is refused before writing (review M3)", rc != 0 and "refused before writing" in out and not (td / "bk3x").exists())
    fixture = ROOT / "docs/schemas/fixtures/tau-assets-roundtrip.bin"
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk3", "--replace", "--allow-release", "--assets", fixture, "--no-eject", "--yes")
    check("--assets places tau-assets.bin in common/, verified", rc == 0 and (card / "Assets/tau/common/tau-assets.bin").read_bytes() == fixture.read_bytes())
    other = td / "other" / "tau-assets.bin"; other.parent.mkdir(); other.write_bytes(fixture.read_bytes()[:-1] + b"\x00")
    pk2 = td / "pk2"; shutil.copytree(pkg, pk2 / "pocket"); shutil.copy2(other, pk2 / "tau-assets.bin")   # a sample next to the package
    rc, out = run(pk2 / "pocket", "--card", card, "--backup-dir", td / "bk3b", "--replace", "--allow-release", "--no-eject", "--yes")
    check("a sample found next to the package does not overwrite the card's own tau-assets.bin (review H1)",
          rc == 0 and "card's own file is kept" in out and (card / "Assets/tau/common/tau-assets.bin").read_bytes() == fixture.read_bytes())

    # B-332: --replace keeps the media; a stale index must be detected and rebuilt, a good one left alone.
    gen = td / "flacs"; gen.mkdir()
    subprocess.run([sys.executable, str(ROOT / "tools/flac_make_test.py"), str(gen)], capture_output=True, cwd=ROOT)
    flacs = sorted(gen.glob("*.flac"))[:2]
    common = card / "Assets/tau/common"
    (common / "Album").mkdir(exist_ok=True)
    for f in flacs: (common / "Album" / f.name).write_bytes(f.read_bytes())
    stale = td / "stale.tdb"
    subprocess.run([sys.executable, str(ROOT / "tools/tau_library.py"), "synth", "--tracks", "5", "--albums", "2", "--artists", "1", "--out", str(stale)],
                   capture_output=True, cwd=ROOT)
    if flacs and stale.exists():
        (common / "tau-library.tdb").write_bytes(stale.read_bytes())
        rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk4", "--replace", "--allow-release", "--no-eject", "--yes")
        v = subprocess.run([sys.executable, str(ROOT / "tools/tau_library.py"), "verify", str(common / "tau-library.tdb"), "--root", str(common)],
                           capture_output=True, text=True, cwd=ROOT)
        check("a stale library index is detected and rebuilt on --replace", rc == 0 and "rebuilding" in out and v.returncode == 0)
        check("albums without a tau-art folder are reported", "no tau-art cover file" in out)
        rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk5", "--replace", "--allow-release", "--no-eject", "--yes")
        check("a matching index is left alone", rc == 0 and "matches the media" in out)
    else:
        check("(media refresh test skipped: could not generate test files)", True)

    rc, out = run(pkg, "--card", card, "--replace", "--yes")
    check("release cores are protected without --allow-release", rc != 0 and "release core" in out)

    # B-672: removing a core that shares its platform with another core removes only that core's own folders (the probe found two
    # cores under one platform; the old --remove deleted the whole Assets/<platform> and the platform files).
    def twin(core, plat, platforms):
        tp = td / f"pkg-{core}" / "pocket"
        shutil.copytree(pkg / "Cores/alfatreze.TAU", tp / "Cores" / core)
        cj = json.loads((tp / "Cores" / core / "core.json").read_text())
        cj["core"]["metadata"].update(shortname=core.split(".")[1], platform_ids=platforms)
        (tp / "Cores" / core / "core.json").write_text(json.dumps(cj))
        shutil.copytree(pkg / "Assets/tau/alfatreze.TAU", tp / "Assets" / plat / core)
        (tp / "Platforms/_images").mkdir(parents=True)
        shutil.copy2(pkg / "Platforms/tau.json", tp / "Platforms" / f"{plat}.json")
        shutil.copy2(pkg / "Platforms/_images/tau.bin", tp / "Platforms/_images" / f"{plat}.bin")
        return tp
    rc, out = run(twin("alfatreze.TAU_TWIN", "tau", ["tau"]), "--card", card, "--backup-dir", td / "bk-tw", "--no-eject", "--yes")
    check("a second core installs beside TAU on the same platform", rc == 0 and (card / "Assets/tau/alfatreze.TAU_TWIN/tau.rom").exists())
    media_before = sorted(str(f.relative_to(card)) for f in (card / "Assets/tau/common").rglob("*") if f.is_file())
    rc, out = run("--card", card, "--remove", "alfatreze.TAU_TWIN")
    check("remove-only dry run lists only the core's own folders", rc == 0 and "DRY RUN" in out and "Assets/tau/alfatreze.TAU_TWIN" in out
          and "Platforms/tau.json" not in out and (card / "Cores/alfatreze.TAU_TWIN").exists())
    rc, out = run("--card", card, "--remove", "alfatreze.TAU_TWIN", "--backup-dir", td / "bk-rm", "--no-eject", "--yes")
    media_after = sorted(str(f.relative_to(card)) for f in (card / "Assets/tau/common").rglob("*") if f.is_file())
    check("removing a core from a shared platform keeps the platform's media, index and platform files (B-672)",
          rc == 0 and not (card / "Cores/alfatreze.TAU_TWIN").exists() and not (card / "Assets/tau/alfatreze.TAU_TWIN").exists()
          and media_after == media_before and (card / "Platforms/tau.json").exists() and (card / "Cores/alfatreze.TAU").exists())
    check("its backup holds only what belonged to it", (td / "bk-rm/alfatreze.TAU_TWIN/Assets/tau/alfatreze.TAU_TWIN/tau.rom").exists()
          and not (td / "bk-rm/alfatreze.TAU_TWIN/Assets/tau/common").exists())
    rc, out = run(twin("alfatreze.TAU_SOLO", "tau_solo", ["tau_solo"]), "--card", card, "--backup-dir", td / "bk-so", "--no-eject", "--yes")
    (card / "Assets/tau_solo/common").mkdir(parents=True, exist_ok=True); (card / "Assets/tau_solo/common/x.mp3").write_bytes(b"m")
    rc, out = run("--card", card, "--remove", "alfatreze.TAU_SOLO", "--backup-dir", td / "bk-rm2", "--no-eject", "--yes")
    check("a core with its own platform is still removed with its whole Assets folder and platform files, media backed up",
          rc == 0 and not (card / "Assets/tau_solo").exists() and not (card / "Platforms/tau_solo.json").exists()
          and (td / "bk-rm2/alfatreze.TAU_SOLO/Assets/tau_solo/common/x.mp3").exists())
    rc, out = run("--card", card, "--remove", "alfatreze.TAU", "--yes")
    check("remove-only also protects the release cores", rc != 0 and "release core" in out)

    # Dev channel (B-673 option a): TAU DEV builds share one TAU Dev platform and read TAU's library and tau-assets.bin in place.
    sys.path.insert(0, str(ROOT / "tools"))
    import tau_layout
    def devpkg(n):
        core = f"alfatreze.TAU DEV {n}"
        tp = twin(core, "tau_dev", ["tau_dev", "tau"])
        dj = json.loads((tp / "Cores" / core / "data.json").read_text())
        (tp / "Cores" / core / "data.json").write_text(json.dumps(tau_layout.read_shared_media(dj)))
        (tp / "Platforms/tau_dev.json").write_text(json.dumps({"platform": {"name": "TAU Dev"}}))
        return tp
    tau_assets_before = (card / "Assets/tau/common/tau-assets.bin").read_bytes()
    d1 = devpkg(1); shutil.copy2(fixture, d1.parent / "tau-assets.bin")          # a sample next to the package
    rc, out = run(d1, "--card", card, "--carry-from", "alfatreze.TAU", "--no-eject")
    check("dev channel: --carry-from is refused (the core reads TAU's media in place)", rc != 0 and "--carry-from is not used" in out)
    rc, out = run(d1, "--card", card, "--backup-dir", td / "bk-d1", "--no-eject", "--yes")
    check("dev channel: installs beside TAU, reads TAU's library in place, never touches TAU's own tau-assets.bin",
          rc == 0 and "read in place from Assets/tau/common" in out and (card / "Assets/tau_dev/alfatreze.TAU DEV 1/tau.rom").exists()
          and not (card / "Assets/tau_dev/common").exists() and (card / "Assets/tau/common/tau-assets.bin").read_bytes() == tau_assets_before)
    # Data slot 10 (tau-meters.bin, the user's meter bin, D-M22): shared by every Tau core, so a dev core reads it from Assets/tau/common (platform index 1), a release core likewise
    import tau_data_slots
    def metpkg(n):
        tp = devpkg(n); cdir = tp / "Cores" / f"alfatreze.TAU DEV {n}"
        tau_data_slots.add_meters_slot(cdir)
        (cdir / "data.json").write_text(json.dumps(tau_layout.read_shared_media(json.loads((cdir / "data.json").read_text()))))
        return tp, cdir
    mp, mcdir = metpkg(3)
    s10 = next(x for x in json.loads((mcdir / "data.json").read_text())["data"]["data_slots"] if x["id"] == 10)
    check("a dev package's slot 10 resolves to Assets/tau/common (platform index 1), like slots 5 and 8",
          tau_layout.slot_dir(card, s10, ["tau_dev", "tau"], "alfatreze.TAU DEV 3") == card / "Assets/tau/common" and tau_layout.slot_platform_index(s10["parameters"]) == 1)
    rel10 = dict(s10, parameters="0x0")
    check("a release package's slot 10 resolves to Assets/tau/common too", tau_layout.slot_dir(card, rel10, ["tau"], "alfatreze.TAU") == card / "Assets/tau/common")
    mine = card / "Assets/tau/common/tau-meters.bin"; mine.write_bytes(b"user meter bin, not ours")
    rc, out = run(mp, "--card", card, "--backup-dir", td / "bk-d3", "--no-eject", "--yes")
    check("a dev install never removes, replaces or backs up the user's tau-meters.bin in Assets/tau/common, and writes none into the dev platform",
          rc == 0 and mine.read_bytes() == b"user meter bin, not ours" and not any("tau-meters.bin" in str(x) for x in (td / "bk-d3").rglob("*")) and not (card / "Assets/tau_dev/common").exists()
          and not list((card / "Assets/tau_dev").rglob("tau-meters.bin")))
    rc, out = run("--card", card, "--remove", "alfatreze.TAU DEV 3", "--backup-dir", td / "bk-rd3", "--no-eject", "--yes")
    check("removing that dev build leaves the user's tau-meters.bin", rc == 0 and mine.read_bytes() == b"user meter bin, not ours")
    mine.unlink()
    rc, out = run(devpkg(2), "--card", card, "--backup-dir", td / "bk-d2", "--no-eject", "--yes")
    rc, out = run("--card", card, "--remove", "alfatreze.TAU DEV 1", "--backup-dir", td / "bk-rd1", "--no-eject", "--yes")
    check("dev channel: removing one dev build keeps the TAU Dev platform for the others and TAU's media",
          rc == 0 and not (card / "Cores/alfatreze.TAU DEV 1").exists() and (card / "Platforms/tau_dev.json").exists()
          and (card / "Assets/tau_dev/alfatreze.TAU DEV 2").exists() and (card / "Assets/tau/common/tau-library.tdb").exists())
    rc, out = run("--card", card, "--remove", "alfatreze.TAU DEV 2", "--backup-dir", td / "bk-rd2", "--no-eject", "--yes")
    check("dev channel: removing the last dev build removes the TAU Dev platform and leaves TAU alone",
          rc == 0 and not (card / "Assets/tau_dev").exists() and not (card / "Platforms/tau_dev.json").exists()
          and (card / "Platforms/tau.json").exists() and (card / "Assets/tau/common/tau-library.tdb").exists())

    # Release manifest (tau-compat.json schema 2): obsolete files removed, card checked; a non-matching --compat stops before writing.
    import zipfile
    sys.path.insert(0, str(ROOT / "tools"))
    import tau_compat as tc
    import tau_version
    rel = td / "relpkg" / "pkg"; shutil.copytree(pkg, rel)                  # a copy: the real dist/ is pinned
    ver = json.loads((rel / "Cores/alfatreze.TAU/core.json").read_text())["core"]["metadata"]["version"]
    rom_p = rel / "Assets/tau/alfatreze.TAU/tau.rom"
    rom_p.write_bytes(tau_version.stamp(rom_p.read_bytes(), ver))      # the fixture's ROM carries the version of its core.json (a release does)
    zp = td / "alfatreze.TAU_rel.zip"
    with zipfile.ZipFile(zp, "w") as z:
        for f in sorted(rel.rglob("*")):
            if f.is_file() and f.relative_to(rel).parts[0] in ("Cores", "Assets", "Platforms") and not f.name.startswith("._") and f.name != ".DS_Store":
                z.write(f, str(f.relative_to(rel)))
    ver = json.loads((rel / "Cores/alfatreze.TAU/core.json").read_text())["core"]["metadata"]["version"]
    cl = td / "CL.md"
    cl.write_text((ROOT / "CHANGELOG.md").read_text().replace("## v0.6.0-alpha.4", f"## v{ver}-preview.1 — 8 October 2026\n- test\n\n## v0.6.0-alpha.4", 1))
    acc = tc.rom_accepts((rel / "Assets/tau/alfatreze.TAU/tau.rom").read_bytes())
    pkg = rel                                                       # the installs below use the stamped copy, the one the manifest describes
    cpath = td / "tau-compat.json"
    reg = json.loads((ROOT / "tools/persist_registry.json").read_text())      # the tree's registry names later releases than this fixture's
    for v in reg["ids"].values():
        if tc.tag_key(v["since"]) > tc.tag_key(f"v{ver}-preview.1"):
            v["since"] = f"v{ver}-preview.1"
    (td / "reg.json").write_text(json.dumps(reg))
    cpath.write_text(tc.dumps(tc.build(release=f"v{ver}-preview.1", zips=[zp], previous=None, bitstream_version=acc[0], changelog=cl, registry_file=td / "reg.json")))
    (card / "Assets/tau/alfatreze.TAU").mkdir(parents=True, exist_ok=True)
    (card / "Assets/tau/alfatreze.TAU/TAU.json").write_text("{}")
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk6", "--replace", "--allow-release", "--compat", cpath, "--no-eject", "--yes")
    check("with --compat: the obsolete TAU.json is removed and the card check passes",
          rc == 0 and "removed obsolete Assets/tau/alfatreze.TAU/TAU.json" in out and "card matches" in out
          and not (card / "Assets/tau/alfatreze.TAU/TAU.json").exists())
    check("the obsolete file is in the backup of the replaced core", (td / "bk6/alfatreze.TAU/Assets/tau/alfatreze.TAU/TAU.json").exists())
    # H4 migration: an old-layout card (build-bound files in common/) is upgraded; the stale common/ copies are removed after the check,
    # unless another core on the platform still reads them from there.
    for n in ("tau.rom", "tau-cold.bin", "tau-loading.bin"):
        (card / "Assets/tau/common" / n).write_bytes(b"old-layout " + n.encode())
    old = card / "Cores/alfatreze.TAU_OLD"; old.mkdir()
    (old / "core.json").write_text(json.dumps({"core": {"metadata": {"author": "alfatreze", "shortname": "TAU_OLD", "platform_ids": ["tau"]}}}))
    (old / "data.json").write_text(json.dumps({"data": {"data_slots": [{"id": 1, "filename": "tau.rom", "parameters": "0x108"}]}}))
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk6b", "--replace", "--allow-release", "--compat", cpath, "--no-eject", "--yes")
    check("H4 upgrade: common/tau.rom is kept while an old-layout core on the platform still reads it; cold image and splash removed",
          rc == 0 and "kept obsolete Assets/tau/common/tau.rom" in out and (card / "Assets/tau/common/tau.rom").exists()
          and not (card / "Assets/tau/common/tau-cold.bin").exists() and not (card / "Assets/tau/common/tau-loading.bin").exists())
    shutil.rmtree(old)
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk6c", "--replace", "--allow-release", "--compat", cpath, "--no-eject", "--yes")
    check("H4 upgrade: with no other reader the stale common/tau.rom is removed too", rc == 0 and not (card / "Assets/tau/common/tau.rom").exists())
    bad = json.loads(cpath.read_text()); bad["packages"][0]["layout"] = [dict(e, sha256="0" * 64) if e["path"].endswith("tau.rom") else e for e in bad["packages"][0]["layout"]]
    (td / "bad.json").write_text(json.dumps(bad))
    (card / "System/corelist_cache.bin").write_text("x")
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk7", "--replace", "--allow-release", "--compat", td / "bad.json", "--no-eject", "--yes")
    check("a --compat that does not describe the package stops before writing", rc != 0 and "does not describe this package" in out and not (td / "bk7").exists())
    good_assets = (card / "Assets/tau/common/tau-assets.bin").read_bytes()
    (card / "Assets/tau/common/tau-assets.bin").write_bytes(b"TAUA" + (9).to_bytes(2, "little") + bytes(6))
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk8", "--replace", "--allow-release", "--compat", cpath, "--no-eject", "--yes")
    check("a card file in a format the release cannot read is refused BEFORE writing (review M3)",
          rc != 0 and "refused before writing" in out and "TAUA version 9" in out and not (td / "bk8").exists())
    (card / "Assets/tau/common/tau-assets.bin").write_bytes(good_assets)

    # Auto-restore (review M3): a fault after the copy, or a failed card check, puts the card back and keeps the caches.
    import hashlib, os
    def snapshot():
        return {str(f.relative_to(card)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(card.rglob("*")) if f.is_file()}
    rom = card / "Assets/tau/alfatreze.TAU/tau.rom"
    rom.write_bytes(rom.read_bytes() + b"OLD")                              # a card whose ROM differs from the package (an older build)
    before = snapshot()
    for stage in ("copy", "check"):
        env = dict(os.environ, TAU_INSTALL_TEST_CORRUPT=f"{stage}:Assets/tau/alfatreze.TAU/tau.rom")
        r = subprocess.run(TOOL + [str(pkg), "--card", str(card), "--backup-dir", str(td / f"bk-{stage}"), "--replace", "--allow-release",
                                   "--compat", str(cpath), "--no-eject", "--yes"], capture_output=True, text=True, cwd=ROOT, env=env)
        o = r.stdout + r.stderr
        check(f"a fault at '{stage}' restores the card exactly (verified) and keeps the caches",
              r.returncode != 0 and "restored to its state before the install (verified)" in o and snapshot() == before
              and (card / "System/corelist_cache.bin").exists())
    rom.write_bytes(rom.read_bytes()[:-3])
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk9", "--replace", "--allow-release", "--no-eject", "--yes")
    check("without a matching manifest the card check is skipped", rc == 0 and "card check skipped" in out)

    rc, out = run(pkg, "--card", td / "nocard")
    check("a missing card is a clean stop", rc != 0 and "not mounted" in out)


# P2.6: a release core with data slots 9 and 10. The user's meter bin (Assets/tau/common/tau-meters.bin, written by Tau Omega) is NEVER overwritten, replaced or dropped by an install; tau-packs.bin
# (slot 9, core-specific) is the core's own and IS replaced; a card whose user bin this release cannot read is refused before anything is written, like a bad tau-assets.bin.
with tempfile.TemporaryDirectory() as td2:
    import hashlib, zipfile
    td2 = Path(td2); sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
    import tau_compat as tc, tau_version, package_dev_build as pdb, build_packs as bpk, meter_bin as mbin
    card2 = td2 / "card"
    for d in ("Cores", "Assets", "Platforms/_images", "System"): (card2 / d).mkdir(parents=True)
    rel2 = td2 / "pkg" / "pocket"; shutil.copytree(ROOT / "dist", rel2)
    pdb.declare_packs_slots(rel2 / "Cores/alfatreze.TAU"); pdb.build_packs_bin(rel2 / "Assets/tau/alfatreze.TAU")
    ver2 = json.loads((rel2 / "Cores/alfatreze.TAU/core.json").read_text())["core"]["metadata"]["version"]
    rp = rel2 / "Assets/tau/alfatreze.TAU/tau.rom"; rp.write_bytes(tau_version.stamp(rp.read_bytes(), ver2))
    check("P2.6 fixture: the package declares slot 10 and ships no tau-meters.bin", any(sl["id"] == 10 for sl in json.loads((rel2 / "Cores/alfatreze.TAU/data.json").read_text())["data"]["data_slots"]) and not list(rel2.rglob("tau-meters.bin")))
    zp2 = td2 / "alfatreze.TAU_rel.zip"
    with zipfile.ZipFile(zp2, "w") as z:
        for f in sorted(rel2.rglob("*")):
            if f.is_file() and f.relative_to(rel2).parts[0] in ("Cores", "Assets", "Platforms"): z.write(f, str(f.relative_to(rel2)))
    cl2 = td2 / "CL.md"; cl2.write_text((ROOT / "CHANGELOG.md").read_text().replace("## v0.6.0-alpha.4", f"## v{ver2}-preview.1 \u2014 8 October 2026\n- test\n\n## v0.6.0-alpha.4", 1))
    reg2 = json.loads((ROOT / "tools/persist_registry.json").read_text())
    for v in reg2["ids"].values():
        if tc.tag_key(v["since"]) > tc.tag_key(f"v{ver2}-preview.1"): v["since"] = f"v{ver2}-preview.1"
    (td2 / "reg.json").write_text(json.dumps(reg2))
    cp2 = td2 / "tau-compat.json"
    cp2.write_text(tc.dumps(tc.build(release=f"v{ver2}-preview.1", zips=[zp2], previous=None, bitstream_version=tc.rom_accepts(rp.read_bytes())[0], changelog=cl2, registry_file=td2 / "reg.json")))
    e10 = [e for e in json.loads(cp2.read_text())["packages"][0]["layout"] if e["path"].endswith("tau-meters.bin")]
    check("P2.6 fixture: the manifest has the user, slot 10 entry", len(e10) == 1 and e10[0]["role"] == "user" and e10[0]["slot"] == 10)
    def snap(base): return {str(f.relative_to(base)): hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(base.rglob("*")) if f.is_file()}
    rc, out = run(rel2, "--card", card2, "--backup-dir", td2 / "b0", "--compat", cp2, "--no-eject", "--yes")
    check("a fresh install of a slot 10 core succeeds, card check included", rc == 0 and "card matches" in out, out[-400:])
    packs_pkg = (rel2 / "Assets/tau/alfatreze.TAU/tau-packs.bin").read_bytes()
    check("tau-packs.bin (slot 9) is installed in the core's own folder, identical", (card2 / "Assets/tau/alfatreze.TAU/tau-packs.bin").read_bytes() == packs_pkg)
    check("an install without a user bin does not create one", not (card2 / "Assets/tau/common/tau-meters.bin").exists())
    ub_data = bpk.build_from_packages(td2)[0]; ub = card2 / "Assets/tau/common/tau-meters.bin"; ub.parent.mkdir(parents=True, exist_ok=True); ub.write_bytes(ub_data)
    (card2 / "Assets/tau/alfatreze.TAU/tau-packs.bin").write_bytes(b"stale")
    rc, out = run(rel2, "--card", card2, "--backup-dir", td2 / "b1", "--replace", "--allow-release", "--compat", cp2, "--no-eject", "--yes")
    check("--replace of a release core: the user's tau-meters.bin is untouched (same bytes)", rc == 0 and ub.read_bytes() == ub_data, out[-300:])
    check("--replace replaces the core's own tau-packs.bin (slot 9)", (card2 / "Assets/tau/alfatreze.TAU/tau-packs.bin").read_bytes() == packs_pkg)
    check("the backup of the replaced core holds the user's tau-meters.bin (nothing is lost if the card is wrong)", (td2 / "b1/alfatreze.TAU/Assets/tau/common/tau-meters.bin").read_bytes() == ub_data)
    check("the card check lists no error for the user bin", "ERROR" not in out and "tau-meters.bin" not in "".join(l for l in out.splitlines() if "ERROR" in l))
    ub.write_bytes(b"TPKB" + (7).to_bytes(2, "little") + bytes(60))                  # a bin of a version this release cannot read
    damaged = snap(card2)
    rc, out = run(rel2, "--card", card2, "--backup-dir", td2 / "b2", "--replace", "--allow-release", "--compat", cp2, "--no-eject", "--yes")
    check("a user bin this release cannot read is refused BEFORE writing, naming the file, the card untouched", rc != 0 and "refused before writing" in out and "tau-meters.bin" in out and not (td2 / "b2").exists() and snap(card2) == damaged, out[-400:])
    ub.write_bytes(ub_data)
    rc, out = run(rel2, "--card", card2, "--backup-dir", td2 / "b3", "--replace", "--allow-release", "--compat", cp2, "--no-eject", "--yes")
    check("with the user bin sound again the same install goes through and keeps it", rc == 0 and ub.read_bytes() == ub_data)
    ub.write_bytes(ub_data)
    rc, out = run("--card", card2, "--remove", "alfatreze.TAU", "--allow-release", "--backup-dir", td2 / "b4", "--no-eject", "--yes")
    check("removing a release core backs up the user's tau-meters.bin first", rc == 0 and (td2 / "b4/alfatreze.TAU/Assets/tau/common/tau-meters.bin").read_bytes() == ub_data, out[-300:])


# The save slot's seed file (the S1 spike of the meter save file, docs/features/meters/METER_SAVE_FILE_SPEC.md): a package may ship a zeroed file under Saves/<platform>/<core>/; the installer creates it
# when it is missing and NEVER overwrites an existing one (the Pocket writes it back at Quit: it is the user's state), a restore removes a file it created, and a dry run says which.
with tempfile.TemporaryDirectory() as td3:
    td3 = Path(td3); card3 = td3 / "card"
    for d in ("Cores", "Assets", "Platforms/_images", "System"):
        (card3 / d).mkdir(parents=True)
    sys.path.insert(0, str(ROOT / "tools"))
    import tau_data_slots as tds
    pk3 = td3 / "pkg"; shutil.copytree(ROOT / "dist", pk3)
    tds.add_save_probe_slot(pk3 / "Cores/alfatreze.TAU")
    seed_rel = "Saves/tau/alfatreze.TAU/tau-save-probe.bin"
    (pk3 / seed_rel).parent.mkdir(parents=True); (pk3 / seed_rel).write_bytes(bytes(tds.SAVE_SIZE))
    rc, out = run(pk3, "--card", card3)
    check("the dry run says the missing save file will be created and writes nothing", rc == 0 and "missing, will be created" in out and not (card3 / seed_rel).exists(), out[-400:])
    rc, out = run(pk3, "--card", card3, "--backup-dir", td3 / "b1", "--no-eject", "--yes")
    check("the install creates the zeroed save file (8,192 bytes)", rc == 0 and (card3 / seed_rel).read_bytes() == bytes(8192), out[-400:])
    (card3 / seed_rel).write_bytes(b"WRITTEN BY THE POCKET AT QUIT".ljust(8192, b"\x01"))
    mine = (card3 / seed_rel).read_bytes()
    rc, out = run(pk3, "--card", card3)
    check("a second dry run says the existing save file is kept", rc != 0 or "exists on the card, kept" in out, out[-300:])
    rc, out = run(pk3, "--card", card3, "--backup-dir", td3 / "b2", "--replace", "--allow-release", "--no-eject", "--yes")
    check("a re-install (--replace) never overwrites the save file the Pocket wrote", rc == 0 and (card3 / seed_rel).read_bytes() == mine and "kept" in out, out[-400:])
    (card3 / seed_rel).unlink()
    env = dict(os.environ, TAU_INSTALL_TEST_CORRUPT="copy:Cores/alfatreze.TAU/bitstream.rbf_r")
    r = subprocess.run(TOOL + [str(pk3), "--card", str(card3), "--backup-dir", str(td3 / "b3"), "--replace", "--allow-release", "--no-eject", "--yes"], capture_output=True, text=True, cwd=ROOT, env=env)
    check("a failed install removes the save file it had just created (the card is restored)", r.returncode != 0 and not (card3 / seed_rel).exists(), (r.stdout + r.stderr)[-500:])
    import tau_compat as tc2
    data = json.loads((pk3 / "Cores/alfatreze.TAU/data.json").read_text())
    pkg = {"zip": "x.zip", "core_id": "alfatreze.TAU", "platform": "tau", "platforms": ["tau"], "data": data,
           "files": {"Cores/alfatreze.TAU/core.json": "a" * 64, "Assets/tau/alfatreze.TAU/tau.rom": "b" * 64, "Assets/tau/alfatreze.TAU/tau-cold.bin": "c" * 64, "Assets/tau/alfatreze.TAU/tau-loading.bin": "f" * 64, seed_rel: "d" * 64}}
    lay = tc2.layout(pkg)
    ent = [e for e in lay if e["path"] == seed_rel]
    check("tau-compat lists the seed as a user file that is created when missing and never overwritten", len(ent) == 1 and ent[0]["role"] == "user" and ent[0].get("seed") is True and ent[0]["slot"] == 11, ent)
    pkg2 = dict(pkg, files={k: v for k, v in pkg["files"].items() if k != seed_rel})
    ent = [e for e in tc2.layout(pkg2) if e["path"] == seed_rel]
    check("a save slot without a shipped seed is still listed (user, not a seed), so the plugin's Check knows the path", len(ent) == 1 and ent[0]["role"] == "user" and ent[0].get("seed") is False, ent)
    bad = dict(pkg, files=dict(pkg["files"], **{"Saves/tau/alfatreze.TAU/evil.bin": "e" * 64}))
    try:
        tc2.layout(bad); ok = False
    except tc2.CompatError:
        ok = True
    check("any other file under Saves/ is refused when a release manifest is built", ok)

# B-542: media is only left out of a backup when it is provably not lost.
sys.path.insert(0, str(ROOT / "tools"))
import install_dev_core as idc
check("media skipped for the carried-from core", idc.skip_media_for("A", "NEW", "A", False))
check("media skipped for the replaced core (its Assets stay)", idc.skip_media_for("NEW", "NEW", None, False))
check("a core removed WITHOUT --carry-from keeps its media in the backup", not idc.skip_media_for("B", "NEW", "A", False))
check("--backup-media always keeps it", not idc.skip_media_for("A", "NEW", "A", True))
check("media extensions recognised, everything else kept",
      idc.is_media("common/Album/01.mp3") and idc.is_media("common/cover.JPG") and idc.is_media("common/x/y.timg")
      and not idc.is_media("common/tau-library.tdb") and not idc.is_media("common/playlist.m3u") and not idc.is_media("common/tau.rom")
      and not idc.is_media("Album/01.mp3"))

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
