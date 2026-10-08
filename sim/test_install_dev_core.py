#!/usr/bin/env python3
"""Host test for tools/install_dev_core.py: installs the real dist/ package onto a scratch 'card' and checks the
dry run, the install, --replace (media survives), the catalog-cache deletion and the release-core protection.
Never touches a real card."""
import json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOL = [sys.executable, str(ROOT / "tools/install_dev_core.py")]
CACHES = ["core_viewby_platform.bin", "corelist_cache.bin", "cores_cache.bin", "platform_viewby_category.bin", "platforms_cache.bin"]
fails = 0


def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
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

    (card / "Assets/tau/common/my-track.mp3").write_bytes(b"media")
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk2", "--replace", "--allow-release", "--no-eject", "--yes")
    check("--replace refreshes the core and keeps the media", rc == 0 and (card / "Assets/tau/common/my-track.mp3").exists())
    check("--replace backed up the old copy", (td / "bk2/alfatreze.TAU/Cores/alfatreze.TAU").is_dir())
    check("--replace backup leaves the duplicate media out (B-542)", not (td / "bk2/alfatreze.TAU/Assets/tau/common/my-track.mp3").exists())
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk2b", "--replace", "--allow-release", "--backup-media", "--no-eject", "--yes")
    check("--backup-media keeps the media in the backup", rc == 0 and (td / "bk2b/alfatreze.TAU/Assets/tau/common/my-track.mp3").read_bytes() == b"media")

    ta = td / "tau-assets.bin"; ta.write_bytes(b"TAUA-test")
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk3", "--replace", "--allow-release", "--assets", ta, "--no-eject", "--yes")
    check("--assets places tau-assets.bin in common/, verified", rc == 0 and (card / "Assets/tau/common/tau-assets.bin").read_bytes() == b"TAUA-test")

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

    # Release manifest (tau-compat.json schema 2): obsolete files removed, card checked; a non-matching --compat stops before writing.
    import zipfile
    sys.path.insert(0, str(ROOT / "tools"))
    import tau_compat as tc
    zp = td / "alfatreze.TAU_rel.zip"
    with zipfile.ZipFile(zp, "w") as z:
        for f in sorted(pkg.rglob("*")):
            if f.is_file() and f.relative_to(pkg).parts[0] in ("Cores", "Assets", "Platforms") and not f.name.startswith("._") and f.name != ".DS_Store":
                z.write(f, str(f.relative_to(pkg)))
    ver = json.loads((pkg / "Cores/alfatreze.TAU/core.json").read_text())["core"]["metadata"]["version"]
    cl = td / "CL.md"; cl.write_text(f"## v{ver}-alpha.99\n- test\n")
    acc = tc.rom_accepts((pkg / "Assets/tau/common/tau.rom").read_bytes())
    cpath = td / "tau-compat.json"
    cpath.write_text(tc.dumps(tc.build(release=f"v{ver}-alpha.99", zips=[zp], previous=None, bitstream_version=acc[0], changelog=cl)))
    (card / "Assets/tau/alfatreze.TAU").mkdir(parents=True, exist_ok=True)
    (card / "Assets/tau/alfatreze.TAU/TAU.json").write_text("{}")
    (card / "Assets/tau/common/tau-assets.bin").unlink()      # the earlier --assets case placed a fake "TAUA-test" file, not a TAUA v1 container
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk6", "--replace", "--allow-release", "--compat", cpath, "--no-eject", "--yes")
    check("with --compat: the obsolete TAU.json is removed and the card check passes",
          rc == 0 and "removed obsolete Assets/tau/alfatreze.TAU/TAU.json" in out and "card matches" in out
          and not (card / "Assets/tau/alfatreze.TAU/TAU.json").exists())
    check("the obsolete file is in the backup of the replaced core", (td / "bk6/alfatreze.TAU/Assets/tau/alfatreze.TAU/TAU.json").exists())
    bad = json.loads(cpath.read_text()); bad["packages"][0]["layout"] = [dict(e, sha256="0" * 64) if e["path"].endswith("tau.rom") else e for e in bad["packages"][0]["layout"]]
    (td / "bad.json").write_text(json.dumps(bad))
    (card / "System/corelist_cache.bin").write_text("x")
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk7", "--replace", "--allow-release", "--compat", td / "bad.json", "--no-eject", "--yes")
    check("a --compat that does not describe the package stops before writing", rc != 0 and "does not describe this package" in out and not (td / "bk7").exists())
    (card / "Assets/tau/common/tau-assets.bin").write_bytes(b"TAUA" + (9).to_bytes(2, "little") + bytes(6))
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk8", "--replace", "--allow-release", "--compat", cpath, "--no-eject", "--yes")
    check("a card file in a format the release cannot read fails the card check and keeps the caches",
          rc != 0 and "TAUA version 9" in out and (card / "System/corelist_cache.bin").exists())
    (card / "Assets/tau/common/tau-assets.bin").unlink()
    rc, out = run(pkg, "--card", card, "--backup-dir", td / "bk9", "--replace", "--allow-release", "--no-eject", "--yes")
    check("without a matching manifest the card check is skipped", rc == 0 and "card check skipped" in out)

    rc, out = run(pkg, "--card", td / "nocard")
    check("a missing card is a clean stop", rc != 0 and "not mounted" in out)

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
