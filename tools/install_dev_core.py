#!/usr/bin/env python3
"""Install a packaged Tau core onto the Pocket SD card, following docs/CARD_INSTALL_PROCEDURE.md.

One command instead of a hand-typed shell sequence. Default is a DRY RUN that prints the plan and touches
nothing; add --yes to write. Every step that could lose data is preceded by a verified backup, and the
script stops on the first failed check (nothing is removed unless the new core copied and verified).

  # what I did by hand for 0.5.0-alpha.14: install, carry the media over, replace the old test core
  python3 tools/install_dev_core.py work/diagnostics/tau-0_5_0_a_14/pocket \\
      --carry-from alfatreze.TAU_0_5_0_A_13 --remove alfatreze.TAU_0_5_0_A_13 --yes

Steps (docs/CARD_INSTALL_PROCEDURE.md): 1 back up everything about to be removed or replaced, verified;
2 copy the new core, verify every file by SHA-256; 3 (--carry-from) copy the media and REBUILD the library
index for the new core's own path (B-136); 4 remove the named cores; 5 delete the five Pocket catalog caches
(B-143 -- a new core does not appear without this); 6 remove AppleDouble/.DS_Store junk in the touched paths;
7 eject. The release cores (alfatreze.TAU, alfatreze.TAU_DIAGNOSTIC) are never removed or replaced unless
--allow-release is given.

If the procedure changes, change THIS script and the doc together (owner rule, 2026-09-25).
"""
import argparse, datetime, filecmp, hashlib, json, os, shutil, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHES = ["core_viewby_platform.bin", "corelist_cache.bin", "cores_cache.bin",
          "platform_viewby_category.bin", "platforms_cache.bin"]
RELEASE_CORES = {"alfatreze.TAU", "alfatreze.TAU_DIAGNOSTIC"}
JUNK = ("._", ".DS_Store")


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def is_junk(name):
    return name.startswith("._") or name == ".DS_Store"


def tree(p):
    """{relative path: sha256} of every real file under p (junk ignored)."""
    out = {}
    for f in sorted(Path(p).rglob("*")):
        if f.is_file() and not is_junk(f.name):
            out[str(f.relative_to(p))] = sha(f)
    return out


def copy_tree(src, dst):
    shutil.copytree(src, dst, ignore=lambda d, names: [n for n in names if is_junk(n)], dirs_exist_ok=True)


def die(msg):
    sys.exit(f"STOP: {msg}")


def core_paths(card, core_id):
    """(core dir, assets dir, platform json, platform image) for a core id that exists on the card."""
    cdir = card / "Cores" / core_id
    if not cdir.is_dir():
        return None
    meta = json.loads((cdir / "core.json").read_text())["core"]["metadata"]
    plat = meta["platform_ids"][0]
    return cdir, card / "Assets" / plat, card / "Platforms" / f"{plat}.json", card / "Platforms/_images" / f"{plat}.bin"


def package_identity(pkg):
    cores = [d for d in (pkg / "Cores").iterdir() if d.is_dir()]
    if len(cores) != 1:
        die(f"{pkg}/Cores must contain exactly one core, found {[c.name for c in cores]}")
    meta = json.loads((cores[0] / "core.json").read_text())["core"]["metadata"]
    plat = meta["platform_ids"][0]
    return cores[0].name, plat, meta.get("version", "?")


def run(cmd, what):
    print(f"   $ {' '.join(str(c) for c in cmd)}")
    r = subprocess.run([str(c) for c in cmd], cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout[-1500:], r.stderr[-1500:])
        die(f"{what} failed (exit {r.returncode})")
    return r.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", type=Path, help="packaged core directory (has Cores/, Assets/, Platforms/)")
    ap.add_argument("--card", type=Path, default=Path("/Volumes/Pock"))
    ap.add_argument("--carry-from", metavar="CORE_ID", help="copy this core's media to the new core and rebuild the library index")
    ap.add_argument("--remove", action="append", default=[], metavar="CORE_ID", help="core to remove after a verified install (repeatable)")
    ap.add_argument("--replace", action="store_true", help="the new core already exists on the card: back it up and refresh its core files (its media stays)")
    ap.add_argument("--allow-release", action="store_true", help="permit touching alfatreze.TAU / alfatreze.TAU_DIAGNOSTIC")
    ap.add_argument("--backup-dir", type=Path, help="default: work/card-backups/<timestamp>")
    ap.add_argument("--no-eject", action="store_true")
    ap.add_argument("--yes", action="store_true", help="actually write (default is a dry run)")
    a = ap.parse_args()
    dry = not a.yes
    pkg, card = a.package.resolve(), a.card

    print(f"{'DRY RUN -- nothing will be written' if dry else 'INSTALL'}")
    if not (pkg / "Cores").is_dir():
        die(f"{pkg} is not a packaged core directory")
    if not card.is_dir() or not (card / "Cores").is_dir():
        die(f"card not mounted at {card}")
    new_id, new_plat, ver = package_identity(pkg)
    print(f"package: {new_id}  platform {new_plat}  version {ver}")
    run([sys.executable, "tools/check_tau_package.py", pkg], "package check")

    for c in a.remove + ([new_id] if a.replace else []):
        if c in RELEASE_CORES and not a.allow_release:
            die(f"{c} is a release core; refusing without --allow-release")
    exists = (card / "Cores" / new_id).exists()
    if exists and not a.replace:
        die(f"{new_id} is already on the card; use --replace to back it up and overwrite")
    if a.carry_from and not (card / "Cores" / a.carry_from).is_dir():
        die(f"--carry-from {a.carry_from} is not on the card")
    for c in a.remove:
        if not (card / "Cores" / c).is_dir():
            die(f"--remove {c} is not on the card")

    to_backup = list(dict.fromkeys(a.remove + ([new_id] if exists else [])))
    bdir = a.backup_dir or ROOT / "work/card-backups" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    print(f"backup:  {bdir}  <- {to_backup or 'nothing to back up'} + {len(CACHES)} catalog caches")
    print(f"install: {new_id}" + (f"  (replacing the copy on the card)" if exists else ""))
    if a.carry_from:
        print(f"media:   carry from {a.carry_from}, rebuild the library index for {new_id}")
    print(f"remove:  {a.remove or 'nothing'}")
    print("then:    delete catalog caches, clean junk files, " + ("(no eject)" if a.no_eject else "eject"))
    if dry:
        print("\n(dry run) re-run with --yes to write.")
        return

    # 1. backup + verify
    print("\n[1/7] backup")
    bdir.mkdir(parents=True, exist_ok=True)
    for c in to_backup:
        cdir, adir, pjson, pimg = core_paths(card, c)
        dst = bdir / c
        copy_tree(cdir, dst / "Cores" / c)
        if adir.is_dir(): copy_tree(adir, dst / "Assets" / adir.name)
        for f, sub in ((pjson, "Platforms"), (pimg, "Platforms/_images")):
            if f.exists():
                (dst / sub).mkdir(parents=True, exist_ok=True); shutil.copy2(f, dst / sub / f.name)
        ok = tree(cdir) == tree(dst / "Cores" / c) and (not adir.is_dir() or tree(adir) == tree(dst / "Assets" / adir.name))
        if not ok: die(f"backup of {c} does not match the card")
        print(f"   {c}: backed up and verified")
    (bdir / "System").mkdir(exist_ok=True)
    for f in CACHES:
        p = card / "System" / f
        if p.exists():
            shutil.copy2(p, bdir / "System" / f)
            if sha(p) != sha(bdir / "System" / f): die(f"backup of {f} does not match")
    print(f"   caches: {len(list((bdir / 'System').iterdir()))} backed up and verified")

    # 2. copy + verify
    print("\n[2/7] copy and verify")
    if exists:                                   # replace the core files; the media in Assets/<platform>/common stays
        shutil.rmtree(core_paths(card, new_id)[0])
    copy_tree(pkg / "Cores" / new_id, card / "Cores" / new_id)
    copy_tree(pkg / "Assets" / new_plat, card / "Assets" / new_plat)
    (card / "Platforms/_images").mkdir(parents=True, exist_ok=True)
    shutil.copy2(pkg / "Platforms" / f"{new_plat}.json", card / "Platforms" / f"{new_plat}.json")
    shutil.copy2(pkg / "Platforms/_images" / f"{new_plat}.bin", card / "Platforms/_images" / f"{new_plat}.bin")
    for rel in (f"Cores/{new_id}", f"Assets/{new_plat}"):
        want, got = tree(pkg / rel), tree(card / rel)
        bad = [k for k in want if want[k] != got.get(k)]           # every package file must be on the card, identical
        if rel.startswith("Cores/"): bad += [k for k in got if k not in want]   # a core dir must match exactly
        if bad:
            die(f"copy mismatch in {rel}: {bad[:5]}")
    for f in ("Platforms/" + f"{new_plat}.json", "Platforms/_images/" + f"{new_plat}.bin"):
        if sha(pkg / f) != sha(card / f): die(f"{f} differs after copy")
    for rel in (f"Cores/{new_id}/bitstream.rbf_r", f"Assets/{new_plat}/common/tau.rom", f"Assets/{new_plat}/common/tau-cold.bin"):
        if (pkg / rel).exists():
            print(f"   {sha(card / rel)[:16]}  {rel}  identical")

    # 3. media + library index
    if a.carry_from:
        print("\n[3/7] media and library index")
        out = run([sys.executable, "tools/sync_media.py", "--from-core", a.carry_from, "--core", new_id, "--library",
                   "--card", card], "media sync")
        print("   " + [l for l in out.splitlines() if l.startswith("done:")][-1])
        d = card / "Assets" / new_plat / "common"
        v = run([sys.executable, "tools/tau_library.py", "verify", d / "tau-library.tdb", "--root", d], "library verify").strip().splitlines()[-1]
        rep = run([sys.executable, "tools/tau_library.py", "report", d / "tau-library.tdb"], "library report")
        want_root = f"/Assets/{new_plat}/common/"
        if v != "OK" or want_root not in rep:
            die(f"library index not valid or wrong root (want {want_root}): {v}")
        print(f"   index {v}, root {want_root}")
    else:
        print("\n[3/7] media: skipped (no --carry-from)")

    # 4. remove
    print("\n[4/7] remove superseded cores")
    for c in a.remove:
        cdir, adir, pjson, pimg = core_paths(card, c)
        shutil.rmtree(cdir)
        if adir.is_dir(): shutil.rmtree(adir)
        for f in (pjson, pimg):
            if f.exists(): f.unlink()
        print(f"   removed {c}")
    if not a.remove: print("   none")

    # 5. catalog caches
    print("\n[5/7] delete catalog caches")
    n = 0
    for f in CACHES:
        p = card / "System" / f
        if p.exists(): p.unlink(); n += 1
    print(f"   {n} deleted (the Pocket rebuilds them on its next scan)")

    # 6. junk
    print("\n[6/7] clean junk files in the touched paths")
    n = 0
    for base in (card / "Cores" / new_id, card / "Assets" / new_plat):
        for f in list(base.rglob("*")):
            if f.is_file() and is_junk(f.name): f.unlink(); n += 1
    for f in (card / "Platforms" / f"._{new_plat}.json", card / "Platforms/_images" / f"._{new_plat}.bin"):
        if f.exists(): f.unlink(); n += 1
    print(f"   {n} removed")

    # 7. eject
    print("\n[7/7] " + ("eject skipped" if a.no_eject else "eject"))
    if not a.no_eject:
        os.sync()
        r = subprocess.run(["diskutil", "eject", str(card)], capture_output=True, text=True)
        print("   " + (r.stdout.strip().splitlines() or r.stderr.strip().splitlines() or ["(no output)"])[-1])

    cores = sorted(p.name for p in (card / "Cores").iterdir() if p.name.startswith("alfatreze.")) if card.exists() else []
    print(f"\nDONE: {new_id} installed. Backup: {bdir}" + (f"\nalfatreze cores now on the card: {cores}" if cores else ""))


if __name__ == "__main__":
    main()
