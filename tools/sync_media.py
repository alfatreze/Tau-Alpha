#!/usr/bin/env python3
"""Copy music, playlists and images onto the Pocket SD card for one or more Tau cores.

Every numbered test build (TAU PSRAM NN) needs its own copy of the media, because a core reads
Assets/<platform>/common/. This does that in one step and is meant to grow into the media library sync:
it takes files or folders, converts images the player can use, copies to each destination core,
verifies every file by SHA-256 and can write a manifest.

  sync_media.py "Nausicaa OST" playlist.m3u --core alfatreze.TAU_PSRAM_03 --core alfatreze.TAU_PSRAM_04
  sync_media.py MUSIC_DIR --all-tau                      # every alfatreze.TAU* core on the card
  sync_media.py MUSIC_DIR --from-core alfatreze.TAU --core alfatreze.TAU_PSRAM_05   # clone one core's media
  add --library to build and verify the media library index (tau-library.tdb) in each destination after copying,
  add --dry-run to see the plan, --mirror to delete files in the destination folder that are not in the source,
  --manifest FILE to write what was done as JSON.

Rules:
  * Folders keep their name under common/ ("Nausicaa OST" -> common/Nausicaa OST); loose files go to common/.
  * Audio (.mp3 .flac) and playlists (.m3u .m3u8) are copied unchanged. MP3 cover art is checked with
    tools/library_check.py (what the core would do with it) and reported; tags are never rewritten.
  * Standalone images are made player-safe: the decoder reads baseline JPEG only, so PNG/other formats and
    progressive JPEGs are converted to baseline JPEG (macOS sips), long side limited to --max-image, and the result
    must fit the firmware size cap. Already-fine JPEGs are copied byte for byte.
  * The player reads cover art ONLY from inside the track (MP3 APIC / FLAC PICTURE), never from a cover.jpg beside it.
    --embed-cover therefore writes the folder's cover image (cover.jpg, folder.jpg, front.jpg or cover-*.jpg, or
    --cover FILE) into the COPIES of that folder's tracks (replacing any existing art); the source files are never
    changed. The cover is first made player-safe (baseline JPEG, <= --max-image px, under the 2 MiB firmware cap).
    Loose image files are not copied unless --copy-images (the player would not use them).
  * A folder of tracks with no .m3u/.m3u8 gets a playlist.m3u generated in the destination (bare filenames, natural
    track order, the convention of tools/make_album_playlists.py); --no-playlist turns that off.
  * Names are made ASCII-only on the card (Nausicaä -> Nausicaa; characters with no plain equivalent are removed), for folders and
    files, and every playlist line is rewritten to match. The player cannot open paths with accented characters
    (docs/issues/001; macOS also stores them in decomposed form, which makes them differ from the playlist).
    --keep-names turns this off. A collision after conversion stops the run.
  * Files that are not media are skipped (listed); ._* and .DS_Store are never copied.
Exit status 1 if any verification fails or a requested core is not found.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
try:
    import library_check as lib
except Exception:            # the checker is an aid; the sync works without it
    lib = None

AUDIO = {".mp3", ".flac"}
LISTS = {".m3u", ".m3u8"}
IMAGES = {".jpg", ".jpeg", ".png", ".bmp", ".gif", ".tif", ".tiff", ".heic", ".webp"}
SKIP_NAMES = {".DS_Store", "Thumbs.db"}
ART_MAX_BYTES = getattr(lib, "ART_MAX_BYTES", 2 * 1024 * 1024) if lib else 2 * 1024 * 1024


def sha(p, chunk=1 << 20):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def is_junk(p: Path):
    return p.name.startswith("._") or p.name in SKIP_NAMES


def core_platform(card: Path, core: str):
    j = card / "Cores" / core / "core.json"
    if not j.exists():
        return None
    return json.loads(j.read_text())["core"]["metadata"]["platform_ids"][0]


def find_cores(card: Path, args):
    names = list(args.core or [])
    if args.all_tau:
        names += sorted(p.name for p in (card / "Cores").iterdir()
                        if p.is_dir() and p.name.startswith("alfatreze.TAU"))
    out, seen = [], set()
    for n in names:
        if n in seen:
            continue
        seen.add(n)
        plat = core_platform(card, n)
        if plat is None:
            print(f"ERROR: core {n!r} not found on the card", file=sys.stderr)
            return None
        out.append((n, plat))
    return out


def gather(sources):
    """-> list of (source file Path, relative destination path under common/)."""
    items = []
    for s in sources:
        s = Path(s)
        if s.is_dir():
            for p in sorted(s.rglob("*")):
                if p.is_file() and not is_junk(p) and not any(x.startswith("._") for x in p.parts):
                    items.append((p, Path(s.name) / p.relative_to(s)))
        elif s.is_file() and not is_junk(s):
            items.append((s, Path(s.name)))
        else:
            print(f"warning: {s} not found, skipped", file=sys.stderr)
    return items


def jpeg_info(p):
    """(width, height, progressive) or None."""
    if lib is None:
        return None
    return lib.jpeg_dims(open(p, "rb").read(1 << 20))


def prepare_image(src: Path, rel: Path, tmp: Path, max_px, quality, force=False):
    """-> (file to copy, relative destination, note). Converts when the player could not use it."""
    ext = src.suffix.lower()
    info = jpeg_info(src) if ext in (".jpg", ".jpeg") else None
    ok_jpeg = info and not info[2] and max(info[0], info[1]) <= max_px and src.stat().st_size <= ART_MAX_BYTES
    if ok_jpeg and not force:
        return src, rel, "baseline JPEG, copied as is"
    out = tmp / (rel.stem + ".jpg")
    n = 0
    while out.exists():
        n += 1
        out = tmp / f"{rel.stem}_{n}.jpg"
    cmd = ["sips", "-s", "format", "jpeg", "-s", "formatOptions", str(quality), "-Z", str(max_px), str(src), "--out", str(out)]
    if not shutil.which("sips"):
        return None, rel, "needs conversion but sips is not available (macOS only)"
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or not out.exists():
        return None, rel, "conversion failed: " + (r.stderr or r.stdout).strip()[:120]
    q = quality
    while out.stat().st_size > ART_MAX_BYTES and q > 40:      # stay under the firmware cap
        q -= 10
        subprocess.run(cmd[:5] + [str(q)] + cmd[6:], capture_output=True)
    after = jpeg_info(out)
    if after and after[2]:
        return None, rel, "conversion still progressive"
    return out, rel.with_suffix(".jpg"), (f"converted to baseline JPEG q{q} (was {ext or 'no ext'}, "
                                          f"{src.stat().st_size:,} -> {out.stat().st_size:,} bytes)")


def ascii_part(name):
    import unicodedata
    d = unicodedata.normalize("NFKD", name)
    d = "".join(c for c in d if not unicodedata.combining(c))
    out = "".join(c for c in d if 0x20 <= ord(c) < 0x7F)      # accents -> plain letter, no equivalent -> removed
    for bad in '<>:"|?*\\':
        out = out.replace(bad, "_")
    out = out.strip()
    stem, dot, ext = out.rpartition(".")
    if not out or (dot and not stem):
        out = "track" + (("." + ext) if dot else "")             # a name that was entirely non-ASCII
    return out


def ascii_rel(rel: Path):
    return Path(*[ascii_part(x) for x in rel.parts])


def natural_key(name):
    import re
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", name.lower())]


def _syncsafe(n):
    return bytes([(n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F])


def _unsyncsafe(b):
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]


def embed_mp3(src: Path, jpeg: bytes, out: Path):
    """Write jpeg as the only APIC of a copy of src. Returns None or a reason it could not."""
    data = src.read_bytes()
    kept, audio, major = [], data, 3
    if data[:3] == b"ID3":
        major, flags = data[3], data[5]
        if major not in (3, 4):
            return f"ID3v2.{major} not supported"
        if flags & 0xD0:
            return "ID3 unsynchronisation / extended header / footer not supported"
        size = _unsyncsafe(data[6:10])
        body, audio = data[10:10 + size], data[10 + size:]
        p = 0
        while p + 10 <= len(body):
            fid = body[p:p + 4]
            if fid[0] == 0:
                break                                    # padding
            fs = _unsyncsafe(body[p + 4:p + 8]) if major == 4 else int.from_bytes(body[p + 4:p + 8], "big")
            if fs <= 0 or p + 10 + fs > len(body):
                return "malformed ID3 frame"
            if fid != b"APIC":
                kept.append(body[p:p + 10 + fs])
            p += 10 + fs
    pic = b"\x00" + b"image/jpeg\x00" + b"\x03" + b"\x00" + jpeg
    fsz = _syncsafe(len(pic)) if major == 4 else len(pic).to_bytes(4, "big")
    kept.append(b"APIC" + fsz + b"\x00\x00" + pic)
    tag = b"".join(kept)
    out.write_bytes(b"ID3" + bytes([major, 0, 0]) + _syncsafe(len(tag)) + tag + audio)
    return None


def embed_flac(src: Path, jpeg: bytes, wh, out: Path):
    data = src.read_bytes()
    if data[:4] != b"fLaC":
        return "not a FLAC file"
    p, blocks = 4, []
    while True:
        h = data[p]
        typ, ln = h & 0x7F, int.from_bytes(data[p + 1:p + 4], "big")
        blocks.append((typ, data[p + 4:p + 4 + ln]))
        p += 4 + ln
        if h & 0x80:
            break
    audio = data[p:]
    blocks = [b for b in blocks if b[0] not in (1, 6)]          # drop padding and old pictures
    mime = b"image/jpeg"
    pic = (3).to_bytes(4, "big") + len(mime).to_bytes(4, "big") + mime + (0).to_bytes(4, "big") \
        + wh[0].to_bytes(4, "big") + wh[1].to_bytes(4, "big") + (24).to_bytes(4, "big") + (0).to_bytes(4, "big") \
        + len(jpeg).to_bytes(4, "big") + jpeg
    blocks.append((6, pic))
    o = bytearray(b"fLaC")
    for i, (typ, body) in enumerate(blocks):
        o += bytes([(0x80 if i == len(blocks) - 1 else 0) | typ]) + len(body).to_bytes(3, "big") + body
    out.write_bytes(bytes(o) + audio)
    return None


COVER_NAMES = ("cover.jpg", "cover.jpeg", "folder.jpg", "front.jpg", "cover.png", "folder.png")


def find_cover(folder: Path):
    for n in COVER_NAMES:
        if (folder / n).is_file():
            return folder / n
    g = sorted(p for p in folder.glob("cover-*") if p.suffix.lower() in IMAGES and not is_junk(p))
    return g[0] if g else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sources", nargs="*", help="files or folders to copy")
    ap.add_argument("--card", default="/Volumes/Pock", help="card root (default /Volumes/Pock)")
    ap.add_argument("--core", action="append", help="destination core id, e.g. alfatreze.TAU_PSRAM_03 (repeatable)")
    ap.add_argument("--all-tau", action="store_true", help="every alfatreze.TAU* core on the card")
    ap.add_argument("--from-core", help="also use this core's Assets/<platform>/common media as a source")
    ap.add_argument("--mirror", action="store_true", help="delete files in the destination folders that are not in the source")
    ap.add_argument("--dry-run", action="store_true", help="show the plan, write nothing")
    ap.add_argument("--max-image", type=int, default=2500, help="longest cover side in pixels (default 2500; the firmware handles up to about 2560)")
    ap.add_argument("--quality", type=int, default=90, help="JPEG quality for converted images (default 90)")
    ap.add_argument("--embed-cover", action="store_true", help="write each folder's cover image into the copies of its tracks")
    ap.add_argument("--cover", help="use this image as the cover for every track (implies --embed-cover)")
    ap.add_argument("--copy-images", action="store_true", help="also copy loose image files (converted if needed)")
    ap.add_argument("--no-playlist", action="store_true", help="do not generate playlist.m3u for folders that have none")
    ap.add_argument("--keep-names", action="store_true", help="do not convert names to ASCII (the player fails on non-ASCII names)")
    ap.add_argument("--cover-quality", type=int, help="re-encode embedded covers at this JPEG quality (e.g. 75); default: keep the original bytes")
    ap.add_argument("--cover-max", type=int, help="shrink embedded covers to at most this many pixels on the long side (the screen shows 92 px)")
    ap.add_argument("--dest-suffix", default="", help="append this text to every copied top-level folder name (keep two variants side by side)")
    ap.add_argument("--manifest", help="write a JSON record of the run to this file")
    ap.add_argument("--library", action="store_true", help="after copying, build and verify tau-library.tdb in each destination (tools/tau_library.py)")
    args = ap.parse_args()

    card = Path(args.card)
    if not (card / "Cores").is_dir():
        sys.exit(f"{card} does not look like a Pocket card (no Cores folder); is it mounted?")
    cores = find_cores(card, args)
    if cores is None:
        return 1
    if not cores:
        sys.exit("no destination: give --core ID (repeatable) or --all-tau")

    sources = [Path(s) for s in args.sources]
    if args.from_core:
        plat = core_platform(card, args.from_core)
        if plat is None:
            sys.exit(f"--from-core {args.from_core!r} not found on the card")
        common = card / "Assets" / plat / "common"
        for p in sorted(common.iterdir()):
            if not is_junk(p) and p.name not in ("tau.rom", "tau-loading.bin"):
                sources.append(p)
    if not sources:
        sys.exit("nothing to copy: give files or folders, or --from-core")

    items = gather(sources)
    if args.dest_suffix:
        items = [(sp, Path(rl.parts[0] + args.dest_suffix, *rl.parts[1:]) if len(rl.parts) > 1 else rl) for sp, rl in items]
    top_map = {}
    if args.dest_suffix:
        for (sp, rl), (_, rl0) in zip(items, gather(sources)):
            if len(rl.parts) > 1: top_map[rl0.parts[0]] = rl.parts[0]
    skipped, plan = [], []
    tmp = Path(tempfile.mkdtemp(prefix="sync_media_"))
    try:
        # per source folder: has a playlist? which cover?
        folders = {}
        for src, rel in items:
            if len(rel.parts) < 2:
                continue
            f = folders.setdefault(rel.parent, {"dir": src.parent, "audio": [], "list": False})
            if src.suffix.lower() in AUDIO:
                f["audio"].append(src.name)
            elif src.suffix.lower() in LISTS and src.parent == f["dir"]:
                f["list"] = True
        cover_cache = {}

        def cover_for(rel_parent):
            if args.cover:
                key = "override"
            else:
                key = rel_parent
            if key not in cover_cache:
                c = Path(args.cover) if args.cover else find_cover(folders[rel_parent]["dir"]) if rel_parent in folders else None
                if c is None or not c.is_file():
                    cover_cache[key] = None
                else:
                    f2, _, note = prepare_image(c, Path(c.name), tmp, min(args.max_image, args.cover_max or args.max_image),
                                                args.cover_quality or args.quality, force=bool(args.cover_quality or args.cover_max))
                    if f2 is None:
                        print(f"warning: cover {c} unusable: {note}", file=sys.stderr)
                        cover_cache[key] = None
                    else:
                        wh = jpeg_info(f2)
                        cover_cache[key] = (c, f2.read_bytes(), wh, note)
            return cover_cache[key]

        embed = args.embed_cover or bool(args.cover)
        img_skipped = 0
        for src, rel in items:
            ext = src.suffix.lower()
            if ext in AUDIO or ext in LISTS:
                note, use = "", src
                if ext in AUDIO and embed:
                    cv = cover_for(rel.parent)
                    if cv is None:
                        note = "no cover image found; tracks unchanged"
                    else:
                        c, jpg, wh, cnote = cv
                        n = 0
                        out = tmp / f"emb_{len(plan)}{ext}"
                        why = embed_mp3(src, jpg, out) if ext == ".mp3" else embed_flac(src, jpg, wh, out)
                        if why:
                            note = f"cover NOT embedded ({why})"
                        else:
                            use = out
                            note = f"cover embedded: {c.name} {wh[0]}x{wh[1]}" if wh else f"cover embedded: {c.name}"
                if ext == ".mp3" and lib:
                    try:
                        v, why, _ = lib.verdict(str(use))
                        note = (note + "; " if note else "") + f"art: {v}" + ("" if v == "ok" else f" ({why})")
                    except Exception as e:
                        note = (note + "; " if note else "") + f"art check failed: {e}"
                elif ext == ".flac" and use is src:
                    note = (note + "; " if note else "") + "art: not checked"
                plan.append((use, rel, note, use is not src))
            elif ext in IMAGES:
                if not args.copy_images:
                    img_skipped += 1
                    continue
                f, r2, note = prepare_image(src, rel, tmp, args.max_image, args.quality)
                if f is None:
                    skipped.append((src, note))
                else:
                    plan.append((f, r2, note, f != src))
            else:
                skipped.append((src, "not a supported media type"))
        if not args.no_playlist:
            for rp, info in sorted(folders.items(), key=lambda kv: str(kv[0])):
                if info["audio"] and not info["list"]:
                    pl = tmp / f"pl_{len(plan)}.m3u"
                    pl.write_text("\n".join(sorted(info["audio"], key=natural_key)) + "\n", encoding="utf-8")
                    plan.append((pl, rp / "playlist.m3u", f"generated, {len(info['audio'])} tracks", True))

        if not args.keep_names:
            renamed, seen, newplan = [], {}, []
            for f, rel, note, conv in plan:
                r2 = ascii_rel(rel)
                if str(r2) in seen and seen[str(r2)] != str(rel):
                    sys.exit(f"name collision after ASCII conversion: {rel} and {seen[str(r2)]} -> {r2}")
                seen[str(r2)] = str(rel)
                if r2 != rel:
                    renamed.append((rel, r2))
                if rel.suffix.lower() in LISTS:            # playlist lines follow the file names
                    lines, changed = [], False
                    for ln in f.read_text(encoding="utf-8", errors="replace").splitlines():
                        if not ln.strip() or ln.lstrip().startswith("#"):
                            n = ln
                        else:
                            parts = ln.split("/")
                            if len(parts) > 1 and parts[0] in top_map:
                                parts[0] = top_map[parts[0]]
                            n = "/".join(ascii_part(x) for x in parts)
                        changed |= n != ln
                        lines.append(n)
                    if changed:
                        pl2 = tmp / f"plr_{len(newplan)}.m3u"
                        pl2.write_text("\n".join(lines) + "\n", encoding="utf-8")
                        f, conv = pl2, True
                        note = (note + "; " if note else "") + "playlist lines converted to ASCII"
                newplan.append((f, r2, note, conv))
            plan = newplan
            if renamed:
                folders_r = sorted({(a.parent, b.parent) for a, b in renamed if a.parent != b.parent})
                print(f"names converted to ASCII: {len(renamed)} file(s)" +
                      "".join(f"\n  {a}  ->  {b}" for a, b in folders_r[:6]))
        record, bad = [], 0
        for core, plat in cores:
            base = card / "Assets" / plat / "common"
            print(f"\n== {core}  ->  Assets/{plat}/common/")
            wanted = set()
            for f, rel, note, conv in plan:
                dest = base / rel
                wanted.add(dest.resolve())
                sh = sha(f)
                state = "new"
                if dest.exists():
                    state = "same" if (dest.stat().st_size == f.stat().st_size and sha(dest) == sh) else "update"
                line = f"  {state:6} {rel}" + (f"   [{note}]" if note else "")
                print(line)
                rec = {"core": core, "dest": str(rel), "sha256": sh, "bytes": f.stat().st_size,
                       "state": state, "converted": conv, "note": note}
                if state != "same" and not args.dry_run:
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(f, dest)
                    if sha(dest) != sh:
                        print(f"  VERIFY FAILED {rel}", file=sys.stderr)
                        bad += 1
                        rec["state"] = "verify-failed"
                record.append(rec)
            if args.mirror:
                # only inside the folders this run copies into; loose files in common/ are never touched
                for top in sorted({rel.parts[0] for _, rel, _, _ in plan if len(rel.parts) > 1}):
                    for p in sorted((base / top).rglob("*")) if (base / top).exists() else []:
                        if p.is_file() and not is_junk(p) and p.resolve() not in wanted:
                            print(f"  delete {p.relative_to(base)}")
                            if not args.dry_run:
                                p.unlink()
            if not args.dry_run:
                for p in list(base.rglob("._*")):    # macOS metadata we may have created
                    try:
                        p.unlink()
                    except FileNotFoundError:
                        pass
            if args.library and not args.dry_run:
                import tau_library
                print(f"  building library index for {core} ...")
                try:
                    tau_library.build_dir(base, playlists=True)
                except (SystemExit, tau_library.LibError) as e:
                    print(f"  LIBRARY INDEX FAILED: {e}", file=sys.stderr)
                    bad += 1
        if img_skipped:
            print(f"\n{img_skipped} loose image file(s) not copied (the player only reads art inside the track; "
                  f"use --embed-cover, or --copy-images)")
        if skipped:
            print("\nskipped:")
            for s, why in skipped:
                print(f"  {s}  ({why})")
        if args.manifest:
            Path(args.manifest).write_text(json.dumps({"cores": [c for c, _ in cores], "files": record,
                                                       "skipped": [[str(s), w] for s, w in skipped]}, indent=2) + "\n")
        n = sum(1 for r in record if r["state"] in ("new", "update"))
        print(f"\n{'plan' if args.dry_run else 'done'}: {len(plan)} file(s) x {len(cores)} core(s), "
              f"{n} to write/written, {sum(1 for r in record if r['state'] == 'same')} already identical"
              + (f", {bad} VERIFY FAILURES" if bad else ""))
        if not args.dry_run:
            os.sync() if hasattr(os, "sync") else None
        return 1 if bad else 0
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
