#!/usr/bin/env python3
"""The meter package, `<id>.tmeter` (docs/features/meters/EXTERNAL_METERS_SPEC.md section 1): pack a shipped meter, verify a package against every rule of the spec, compose a v2 bin from packages.

  python3 tools/meter_package.py pack KEY [KEY ...] --out DIR     a shipped meter (meters/<key>/meter.json + fw2/ui/meter_pack_<key>.c) -> DIR/<id>.tmeter, deterministic (same sources, same bytes)
  python3 tools/meter_package.py verify FILE [FILE ...]           exit 1 and a reason in plain words for every rule a package breaks
  python3 tools/meter_package.py compose OUT.bin PKG [PKG ...]    a v2 bin (tools/meter_bin.py): Winamp Bars first as the fallback row, the others in the order given, 64 rows at most

A package is a zip: `meter.json` (schema 1, id, name, version, author, licence, homepage, abi, tau_min, params_rev, a SHA-256 pin for every other file, `signature` null), `meter.bin` (a TMPK v2 pack linked for the 64 KiB window),
README.md, LICENSE, and optionally `presets/` (the binary preset files the firmware reads), `preview.png` (for people only), `source/`. `source` (a link) is an optional manifest field: it is what marks a meter "not binary only".
The shipped release builds its `tau-packs.bin` from the seven packages made here (tools/player2/build_packs.py), so the shipped path and a third party's path are the same code.

Limits verify enforces (spec section 1, R-021/9): the zip at most 4 MiB compressed AND at most 4 MiB declared (a zip bomb compresses small), no path outside the package (no `..`, no absolute path, no backslash), no file beyond the table,
every file pinned, `meter.bin` at most the window including its pstate and linked for the window, id exactly 32 lower-case hex digits, name 1..23 printable ASCII. README.md and LICENSE are required (Omega shows them before an install).
The manifest's `name` is the name of the TOC row; for the shipped meters it is the name inside the pack's own descriptor."""
import argparse, hashlib, json, re, struct, sys, tempfile, zipfile, zlib
from collections import Counter
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb

LIMIT = 4 * 1024 * 1024                                        # the zip cap of the spec: compressed size and the sum of the declared sizes
WINAMP_BARS_ID = "7dddfe63121e904843088c075cc28ce9"            # the built-in fallback meter (meters/winamp_bars/meter.json): the firmware refuses a bin without its row
FIELDS = ("schema", "id", "name", "version", "author", "licence", "homepage", "abi", "tau_min", "params_rev", "files", "signature")
ALLOWED = re.compile(r"meter\.json|meter\.bin|README\.md|LICENSE|preview\.png|presets/index\.tpi|presets/p\d+\.tpr|source/[^/\\\0].*")
REQUIRED = ("meter.json", "meter.bin", "README.md", "LICENSE")
PACK_ORIGIN, PACK_CRC, PACK_PS_OFF, PACK_PS_SIZE = 12, 20, 40, 44          # offsets in the 48-byte TMPK header (tools/meter_bin.py pack_header)


class PackageError(ValueError):
    """a refusal in plain words (compose, pack)"""


# ---------------------------------------------------------------- reading and verifying
def _read(path):
    """-> (names, {name: bytes}, [reasons]): the zip's files when it is safe to read them (size caps, no stray path); reasons says what is wrong otherwise"""
    path = Path(path); why = []
    if not path.is_file(): return [], {}, ["%s does not exist" % path.name]
    size = path.stat().st_size
    if size > LIMIT: why.append("the package is %s bytes: the limit is 4 MiB (%s bytes)" % (format(size, ","), format(LIMIT, ",")))
    try: z = zipfile.ZipFile(path)
    except (zipfile.BadZipFile, OSError):
        return [], {}, why + ["%s is not a zip file" % path.name]
    with z:
        infos = [i for i in z.infolist() if not i.filename.endswith("/")]
        names = [i.filename for i in infos]
        declared = sum(i.file_size for i in infos)
        if declared > LIMIT: why.append("the files inside declare %s bytes in total: the limit is 4 MiB (%s bytes)" % (format(declared, ","), format(LIMIT, ",")))
        for n, c in Counter(names).items():
            if c > 1: why.append("%s appears twice in the package" % n)
        for n in names:
            parts = n.split("/")
            if n.startswith("/") or re.match(r"[A-Za-z]:", n): why.append("path %r is absolute: every file must be inside the package" % n)
            elif ".." in parts: why.append("path %r goes outside the package (no '..' allowed)" % n)
            elif "\\" in n or "\0" in n or "" in parts: why.append("path %r is not a plain relative path (no backslash, empty part or NUL)" % n)
        if why: return names, {}, why                                     # never read what the limits or the paths refused
        files = {}
        for i in infos:
            try: files[i.filename] = z.read(i)
            except (zipfile.BadZipFile, zlib.error, EOFError, RuntimeError) as e: why.append("%s cannot be read from the zip (%s)" % (i.filename, e))
    return names, files, why


def _check_manifest(man):
    if not isinstance(man, dict): return ["meter.json is not a JSON object"]
    why = []
    for k in FIELDS:
        if k not in man and k not in ("homepage", "tau_min"): why.append("meter.json has no %r" % k)
    if man.get("schema") != 1 and "schema" in man: why.append("meter.json schema %r is not supported (this tool reads schema 1)" % (man["schema"],))
    i = man.get("id")
    if "id" in man and not (isinstance(i, str) and mb.valid_id(i)): why.append("the id %r is not 32 lower-case hex digits" % (i,))
    n = man.get("name")
    if "name" in man and not (isinstance(n, str) and n.isascii() and 1 <= len(n) <= mb.MAX_NAME and all(0x20 <= ord(c) <= 0x7E for c in n)):
        why.append("the name %r must be 1 to 23 printable ASCII characters" % (n,))
    for k in ("author", "licence"):
        if k in man and not (isinstance(man[k], str) and man[k].strip()): why.append("%s must be a non-empty text" % k)
    for k in ("homepage", "source"):
        if k in man and not isinstance(man[k], str): why.append("%s must be a text (a link)" % k)
    if "version" in man and not (isinstance(man["version"], str) and re.fullmatch(r"\d{1,5}\.\d{1,5}\.\d{1,5}", man["version"])): why.append("the version %r is not major.minor.patch" % (man["version"],))
    if "abi" in man and not (isinstance(man["abi"], str) and re.fullmatch(r"\d{1,3}\.\d{1,3}", man["abi"])): why.append("abi %r is not major.minor" % (man["abi"],))
    if man.get("tau_min") is not None and not (isinstance(man["tau_min"], str) and re.fullmatch(r"\d{1,3}\.\d{1,3}(\.\d{1,5})?", man["tau_min"])): why.append("tau_min %r is not major.minor" % (man["tau_min"],))
    pr = man.get("params_rev")
    if "params_rev" in man and not (type(pr) is int and 0 <= pr <= 0xFFFF): why.append("params_rev %r must be a whole number from 0 to 65535" % (pr,))
    if "files" in man and not (isinstance(man["files"], dict) and all(isinstance(k, str) and isinstance(v, str) for k, v in man["files"].items())): why.append("files must map each file name to its SHA-256")
    if man.get("signature") is not None: why.append("the package has a signature, which this version cannot check: signatures are reserved and must be null")
    return why


def _check_pack(pack, man):
    why = []
    try: ph = mb.pack_header(pack)
    except mb.BinError as e: return ["meter.bin is not a valid TMPK pack (%s)" % e]
    if len(pack) != mb.PACK_HDR + ph["size"]: why.append("meter.bin has %d bytes after its image" % (len(pack) - mb.PACK_HDR - ph["size"]))
    if mb.crc32(pack[mb.PACK_HDR:]) != ph["crc"]: why.append("meter.bin: its image does not match the CRC in its header")
    if not mb.window_ok(ph["size"], ph["ps_off"], ph["ps_size"]): why.append("meter.bin does not fit the 64 KiB execution window together with its pstate (image %d bytes, pstate %d bytes at %d)" % (ph["size"], ph["ps_size"], ph["ps_off"]))
    if ph["origin"] != mb.WINDOW: why.append("meter.bin is linked for 0x%08X, not for the window at 0x%08X" % (ph["origin"], mb.WINDOW))
    abi = man.get("abi")
    if isinstance(abi, str) and re.fullmatch(r"\d{1,3}\.\d{1,3}", abi):
        a, b = (int(x) for x in abi.split("."))
        if (a << 8 | b) != ph["abi"]: why.append("the manifest says abi %s but meter.bin needs abi %d.%d" % (abi, ph["abi"] >> 8, ph["abi"] & 255))
    if not why:
        try:
            di = mb.desc_info(pack[mb.PACK_HDR:])
            if not mb.preview_ok(di["preview"]): why.append("meter.bin: its preview is not 56 x 32 pixels in at most 1 KiB")
        except mb.BinError as e: why.append("meter.bin: its descriptor is not valid (%s)" % e)
    return why


def load(path):
    """-> (manifest, files); raises PackageError with every reason (joined) when the package is refused"""
    why, man, files = _verify(path)
    if why: raise PackageError("%s: %s" % (Path(path).name, "; ".join(why)))
    return man, files


def _verify(path):
    names, files, why = _read(path)
    if why: return why, None, files
    man = None
    if "meter.json" in files:
        try: man = json.loads(files["meter.json"].decode("utf-8"))
        except (ValueError, UnicodeDecodeError) as e: why.append("meter.json is not valid JSON (%s)" % e)
    for n in REQUIRED:
        if n not in files: why.append("%s is missing from the package" % n)
    for n in sorted(files):
        if not ALLOWED.fullmatch(n): why.append("%s is not part of a meter package (allowed: meter.json, meter.bin, README.md, LICENSE, preview.png, presets/, source/)" % n)
    if man is not None:
        why += _check_manifest(man)
        pins = man.get("files") if isinstance(man, dict) and isinstance(man.get("files"), dict) else {}
        for n in sorted(files):
            if n == "meter.json": continue
            if n not in pins: why.append("%s is not pinned in meter.json files" % n)
            elif hashlib.sha256(files[n]).hexdigest() != pins[n]: why.append("%s does not match its SHA-256 pin in meter.json" % n)
        for n in sorted(pins):
            if n not in files: why.append("meter.json pins %s, which is not in the package" % n)
        if "meter.bin" in files: why += _check_pack(files["meter.bin"], man)
    return why, man, files


def verify(path):
    """-> [reason, ...] in plain words; an empty list = the package passes every rule"""
    return _verify(path)[0]


# ---------------------------------------------------------------- packing a shipped meter
def _zip(files):
    """a deterministic zip: meter.json first, the rest sorted, fixed time stamps and attributes"""
    order = ["meter.json"] + sorted(n for n in files if n != "meter.json")
    import io
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w", zipfile.ZIP_DEFLATED) as z:
        for n in order:
            zi = zipfile.ZipInfo(n, (2026, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED; zi.external_attr = 0o644 << 16
            z.writestr(zi, files[n])
    return buf.getvalue()


def _readme(m, name):
    return ("# %s\n\nOne of the meters that ship with TAU, the music player for the Analogue Pocket (%s).\n\n- id: %s\n- version: %s, by %s, licence %s\n- source: %s\n\n"
            "A meter runs with full access to the player. This one is built from the project's own source.\n" % (name.title(), m.get("homepage", ""), m["id"], m["version"], m["author"], m["licence"], m.get("source", m.get("homepage", ""))))


def pack(key, out_dir, hang=None, workdir=None):
    """meters/<key> -> out_dir/<id>.tmeter; hang = mode number builds the meter's hang TEST pack instead (a watchdog test package only). Returns the path."""
    import pack_meter as pm, mpreset, meter_manifest
    if key not in pm.SLOT: raise PackageError("no such shipped meter: %s" % key)
    m = json.loads((ROOT / "meters" / key / "meter.json").read_text())
    need = ("version", "author", "licence", "params_rev")
    if any(k not in m for k in need): raise PackageError("meters/%s/meter.json lacks %s" % (key, ", ".join(k for k in need if k not in m)))
    hid = int(m["id"][:8], 16)
    with tempfile.TemporaryDirectory(dir=workdir) as td:
        if hang is not None: blob, _ = pm.build(key, mb.WINDOW, Path(td) / (key + ".elf"), ["-DHANG_MODE=%d" % hang], source=key + "_hang", header_id=hid & 0xFFFF)
        else: blob, _ = pm.build(key, mb.WINDOW, Path(td) / (key + ".elf"), header_id=hid & 0xFFFF)
    name = mb.desc_info(blob[mb.PACK_HDR:])["name"]
    ph = mb.pack_header(blob)
    files = {"meter.bin": blob, "README.md": _readme(m, name).encode("ascii", "replace"), "LICENSE": (ROOT / "LICENSE").read_bytes()}
    for rel, data in mpreset.factory_files(meter_manifest.load(ROOT / "meters" / key / "meter.json")).items():
        files["presets/" + rel.split("/", 1)[1]] = data
    man = {"schema": 1, "id": m["id"], "name": name, "version": m["version"], "author": m["author"], "licence": m["licence"], "homepage": m.get("homepage", ""),
           "abi": "%d.%d" % (ph["abi"] >> 8, ph["abi"] & 255), "tau_min": m.get("tau_min", "0.0"), "params_rev": m["params_rev"]}
    if m.get("source"): man["source"] = m["source"]
    man["files"] = {n: hashlib.sha256(b).hexdigest() for n, b in sorted(files.items())}
    man["signature"] = None
    files["meter.json"] = (json.dumps(man, indent=2) + "\n").encode()
    out = Path(out_dir); out.mkdir(parents=True, exist_ok=True)
    p = out / (m["id"] + ".tmeter"); p.write_bytes(_zip(files))
    bad = verify(p)
    if bad: raise PackageError("the package just made for %s does not verify: %s" % (key, "; ".join(bad)))
    return p


# ---------------------------------------------------------------- composing a bin
def compose(out, paths):
    """packages -> a v2 bin at `out`. Winamp Bars goes first as the fallback row, the others keep the order given. Returns {"rows", "bytes", "warnings"}; raises PackageError in plain words, writing nothing."""
    pk = []
    for p in paths:
        why, man, files = _verify(p)
        if why: raise PackageError("%s is refused: %s" % (Path(p).name, "; ".join(why)))
        pk.append((Path(p), man, files))
    if len(pk) > mb.MAX_METERS: raise PackageError("%d packages: a bin holds at most %d meters (remove %d)" % (len(pk), mb.MAX_METERS, len(pk) - mb.MAX_METERS))
    seen, by_hash = {}, {}
    for p, man, _ in pk:
        if man["id"] in seen: raise PackageError("%s and %s are the same meter (id %s): list it once" % (seen[man["id"]].name, p.name, man["id"]))
        seen[man["id"]] = p
        h = man["id"][:8]
        if h in by_hash: raise PackageError("%s and %s have different ids but the same id hash %s: the firmware needs every hash to be unique, so one of them cannot be installed" % (by_hash[h].name, p.name, h))
        by_hash[h] = p
    wb = [x for x in pk if x[1]["id"] == WINAMP_BARS_ID]
    if not wb: raise PackageError("Winamp Bars (id %s) must be one of the packages: the player needs its row as the fallback when another meter is refused" % WINAMP_BARS_ID)
    ordered = wb + [x for x in pk if x[1]["id"] != WINAMP_BARS_ID]
    entries = []
    for p, man, files in ordered:
        tau = 0
        if man.get("tau_min"): a, b = (int(x) for x in man["tau_min"].split(".")[:2]); tau = a << 8 | b
        flags = mb.FLAG_VISIBLE | (mb.FLAG_FALLBACK if man["id"] == WINAMP_BARS_ID else 0) | (0 if any(n.startswith("source/") for n in files) or man.get("source") else mb.FLAG_BINARY_ONLY)
        entries.append(mb.Entry(id=man["id"], name=man["name"], version=mb._semver(man["version"]), pack=files["meter.bin"], tau_min=tau, flags=flags, params_rev=man["params_rev"]))
    try: data = mb.compose(entries)
    except ValueError as e: raise PackageError(str(e))
    warnings, groups = [], {}
    for e in entries: groups.setdefault(int(e.id[:8], 16) & 0xFFFF, []).append(e.name)
    for low, names in sorted(groups.items()):
        if len(names) > 1: warnings.append("%s share the low 16 bits of their id hash (%04x): they still run, but Tau starts them from their defaults and never saves their tuning" % (" and ".join(names), low))
    out = Path(out); out.parent.mkdir(parents=True, exist_ok=True); out.write_bytes(data)
    return {"rows": len(entries), "bytes": len(data), "warnings": warnings}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("pack"); a.add_argument("keys", nargs="+"); a.add_argument("--out", type=Path, required=True)
    v = sub.add_parser("verify"); v.add_argument("files", nargs="+", type=Path)
    c = sub.add_parser("compose"); c.add_argument("out", type=Path); c.add_argument("packages", nargs="+", type=Path)
    args = ap.parse_args(argv)
    try:
        if args.cmd == "pack":
            for k in args.keys: print(pack(k, args.out))
            return 0
        if args.cmd == "verify":
            rc = 0
            for f in args.files:
                why = verify(f)
                print("%s: %s" % (f.name, "ok" if not why else "REFUSED"))
                for w in why: print("  - " + w)
                rc |= bool(why)
            return rc
        r = compose(args.out, args.packages)
        print("%s: %d meter(s), %d bytes" % (args.out, r["rows"], r["bytes"]))
        for w in r["warnings"]: print("warning: " + w)
        return 0
    except PackageError as e:
        print("refused: %s" % e); return 1


if __name__ == "__main__":
    sys.exit(main())
