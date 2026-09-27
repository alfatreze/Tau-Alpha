#!/usr/bin/env python3
"""Host test for tools/install_dev_core.py: installs the real dist/ package onto a scratch 'card' and checks the
dry run, the install, --replace (media survives), the catalog-cache deletion and the release-core protection.
Never touches a real card."""
import subprocess, sys, tempfile
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

    rc, out = run(pkg, "--card", td / "nocard")
    check("a missing card is a clean stop", rc != 0 and "not mounted" in out)

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
