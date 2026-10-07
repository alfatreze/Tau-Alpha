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
7 eject. A backup leaves out the media (audio/images) of the core whose media is carried over or which is replaced in place -- it is a
duplicate that used to make every backup about 0.8 GB (B-542); --backup-media keeps it. A core removed WITHOUT being carried from keeps a full backup. The release cores (alfatreze.TAU, alfatreze.TAU_DIAGNOSTIC) are never removed or replaced unless
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
# Audio and image files under Assets/<platform>/common/: the bulk of a core's size (about 0.8 GB a core) and always a copy of media that lives somewhere else.
MEDIA_EXT = {".mp3", ".flac", ".wav", ".ogg", ".m4a", ".jpg", ".jpeg", ".png", ".timg"}


def sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def rmtree_tolerant(path):
    """shutil.rmtree that survives a file vanishing under it: macOS removes a ._ AppleDouble file together with its data file, so the
    listing shutil took a moment earlier can name a file that is already gone (B-336: it aborted a real removal half way)."""
    def onerr(func, p, exc):
        if isinstance(exc[1], FileNotFoundError):
            return
        raise exc[1]
    shutil.rmtree(path, onerror=onerr)
    if Path(path).exists():                       # a second pass picks up anything the first one skipped
        shutil.rmtree(path, onerror=onerr)


def is_junk(name):
    return name.startswith("._") or name == ".DS_Store"


def is_media(rel):
    """True for an audio/image file under common/ (rel is the path relative to Assets/<platform>)."""
    r = Path(rel)
    return "common" in r.parts and r.suffix.lower() in MEDIA_EXT


def skip_media_for(core, new_id, carry_from, backup_media):
    """Whether to leave a core's media out of its backup (B-542: it was 99% of 34 GB of backups, every one a duplicate). Only when the media is provably not
    lost: the core being removed is the one whose media is carried to the new core (copied and SHA-256 verified before anything is removed), or the core
    being replaced (its Assets folder is not touched by --replace). Any other core being removed keeps a full backup, media included."""
    return (not backup_media) and (core == carry_from or core == new_id)


def tree(p, skip_media=False):
    """{relative path: sha256} of every real file under p (junk ignored; media too when skip_media)."""
    out = {}
    for f in sorted(Path(p).rglob("*")):
        if f.is_file() and not is_junk(f.name):
            rel = f.relative_to(p)
            if skip_media and is_media(rel):
                continue
            out[str(rel)] = sha(f)
    return out


def copy_tree(src, dst, skip_media=False):
    def ignore(d, names):
        out = []
        for n in names:
            if is_junk(n):
                out.append(n)
            elif skip_media and (Path(d) / n).is_file() and is_media(Path(d).relative_to(src) / n):
                out.append(n)
        return out
    shutil.copytree(src, dst, ignore=ignore, dirs_exist_ok=True)
    if skip_media:                      # leave no empty media folders behind
        for d in sorted((q for q in Path(dst).rglob("*") if q.is_dir()), key=lambda q: len(q.parts), reverse=True):
            if not any(d.iterdir()):
                d.rmdir()


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
    ap.add_argument("--assets", type=Path, metavar="FILE", help="tau-assets.bin (themes/meter presets) to install; default: <package>/../tau-assets.bin, else the --carry-from core's copy")
    ap.add_argument("--remove", action="append", default=[], metavar="CORE_ID", help="core to remove after a verified install (repeatable)")
    ap.add_argument("--replace", action="store_true", help="the new core already exists on the card: back it up and refresh its core files (its media stays)")
    ap.add_argument("--allow-release", action="store_true", help="permit touching alfatreze.TAU / alfatreze.TAU_DIAGNOSTIC")
    ap.add_argument("--backup-dir", type=Path, help="default: work/card-backups/<timestamp>")
    ap.add_argument("--backup-media", action="store_true", help="include media in the backup of the carried-from or replaced core (default: skipped, it is a duplicate -- B-542)")
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
    pair_cmd = [sys.executable, "tools/check_fw_bitstream_pair.py", pkg]
    if (Path(pkg) / "bitstream-manifest.json").is_file():
        pair_cmd += ["--bitstream-manifest", str(Path(pkg) / "bitstream-manifest.json")]     # B-653: the features too
    run(pair_cmd, "firmware/bitstream pairing (B-581, B-653)")

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
    print(f"backup:  {bdir}  <- {to_backup or 'nothing to back up'} + {len(CACHES)} catalog caches"
          + ("" if a.backup_media else "  (media skipped for the carried-from/replaced core)"))
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
        skip = skip_media_for(c, new_id, a.carry_from, a.backup_media)
        copy_tree(cdir, dst / "Cores" / c)
        if adir.is_dir(): copy_tree(adir, dst / "Assets" / adir.name, skip_media=skip)
        for f, sub in ((pjson, "Platforms"), (pimg, "Platforms/_images")):
            if f.exists():
                (dst / sub).mkdir(parents=True, exist_ok=True); shutil.copy2(f, dst / sub / f.name)
        ok = tree(cdir) == tree(dst / "Cores" / c) and (not adir.is_dir() or tree(adir, skip) == tree(dst / "Assets" / adir.name))
        if not ok: die(f"backup of {c} does not match the card")
        print(f"   {c}: backed up and verified" + (" (media not backed up: carried to the new core / left in place)" if skip else ""))
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
        rmtree_tolerant(core_paths(card, new_id)[0])
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

    # 3a. Stale media check (B-332): --replace keeps the media, so a release install onto a card whose index no longer matches its files
    # (moved or renamed folders) left the library showing every album twice and refusing to open tracks. Without --carry-from (which already
    # rebuilds the index) verify the existing index against the files and rebuild it if anything is missing; warn about albums with no cover file.
    common = card / "Assets" / new_plat / "common"
    if not a.carry_from and common.is_dir():
        tdb = common / "tau-library.tdb"
        auds = [p for p in common.rglob("*") if p.suffix.lower() in (".mp3", ".flac") and not is_junk(p.name)]
        if auds:
            r = subprocess.run([sys.executable, "tools/tau_library.py", "verify", str(tdb), "--root", str(common)], capture_output=True, text=True, cwd=ROOT) if tdb.exists() else None
            if r is None or r.returncode != 0 or (r.stdout.strip().splitlines() or [""])[-1] != "OK":
                print(f"\n[3a] library index {'missing' if r is None else 'does not match the media'}: rebuilding for {new_id}")
                run([sys.executable, "tools/tau_library.py", "build", "--core", new_id, "--card", card, "--playlists"], "library rebuild")
                v = run([sys.executable, "tools/tau_library.py", "verify", tdb, "--root", common], "library verify").strip().splitlines()[-1]
                if v != "OK": die(f"rebuilt library index is not valid: {v}")
                print(f"   index rebuilt and verified: {v}")
            else:
                print("\n[3a] library index matches the media")
            albums = sorted({p.parent for p in auds})
            bare = [d.name for d in albums if not (d / "tau-art").is_dir()]
            if bare:
                print(f"   note: {len(bare)} album folder(s) have no tau-art cover file (slow embedded-JPEG covers): {', '.join(bare[:4])}"
                      " -- sync_media.py --art-variants writes them")

    # 3b. tau-assets.bin (data slot 8: extra themes and meter presets). It is not media, so sync_media skips it, and the packager keeps its
    # sample next to the package instead of inside it -- alpha.35 was installed without it (B-329). Always place it when one is known.
    ab = a.assets
    if ab is None and (pkg.parent / "tau-assets.bin").is_file():
        ab = pkg.parent / "tau-assets.bin"
    if ab is None and a.carry_from:
        cand = core_paths(card, a.carry_from)[1] / "common" / "tau-assets.bin"
        if cand.is_file(): ab = cand
    if ab is not None:
        dst = card / "Assets" / new_plat / "common" / "tau-assets.bin"
        shutil.copy2(ab, dst)
        if sha(ab) != sha(dst): die("tau-assets.bin differs after copy")
        print(f"\n[3b] tau-assets.bin installed ({sha(dst)[:16]}, from {ab})")
    else:
        print("\n[3b] tau-assets.bin: none found (Info shows THEME FILE / METER FILE NONE)")

    # 4. remove
    print("\n[4/7] remove superseded cores")
    for c in a.remove:
        cdir, adir, pjson, pimg = core_paths(card, c)
        rmtree_tolerant(cdir)
        if adir.is_dir(): rmtree_tolerant(adir)
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
