#!/usr/bin/env python3
"""tau-assets.bin: the one optional data file a Tau core reads for user-supplied assets (docs/THEME_FILE_FORMAT.md, decision D-M01).
Today it carries the THEM section (extra themes); METR (meter config) and ICON/FONT sections are planned. Read by fw/assets.inc
through data slot 8. Any problem with the file means the built-in themes only; the firmware never trusts it beyond CRCs and clamps.

    python3 tools/tau_assets.py pack THEME.json [THEME.json ...] -o tau-assets.bin      # validate + write
    python3 tools/tau_assets.py dump tau-assets.bin                                     # verify + print (JSON)
    python3 tools/tau_assets.py install tau-assets.bin --core alfatreze.TAU_0_5_0_A_31 [--card /Volumes/Pock]

Theme JSON is the same shape as themes/*.json (name, dark{...}, light{...}); extra themes are appended after the built-in ones.
"""
import argparse, json, re, shutil, struct, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_themes as gt  # noqa: E402

FILE_MAX_THEMES = 4          # fw/theme.h TH_FILE_MAX
NAME_LEN = 16
ASSETS_MAGIC, THEM_MAGIC = b"TAUA", b"TTHM"
VERSION = 1


def role_indices():
    """Enum index of every role key, read from fw/theme.h so the file layout can never drift from the firmware."""
    txt = (ROOT / "fw" / "theme.h").read_text()
    body = txt[txt.index("enum {"):txt.index("TR_COUNT")]
    names = re.findall(r"\bTR_[A-Z0-9_]+\b", re.sub(r"/\*.*?\*/", "", body, flags=re.S))
    idx = {n: i for i, n in enumerate(names)}
    count = len(names)
    return {k: idx[c] for k, c in gt.ROLES}, count


def crc(b):
    return zlib.crc32(b) & 0xFFFFFFFF


def pack_themes(themes):
    if not 1 <= len(themes) <= FILE_MAX_THEMES:
        raise ValueError(f"1..{FILE_MAX_THEMES} themes per file")
    ridx, rcount = role_indices()
    body = b""
    names = set()
    for t in themes:
        name = t["name"]
        if not re.fullmatch(r"[A-Z0-9 _-]{1,15}", name):
            raise ValueError(f"theme name {name!r}: 1..15 characters of A-Z 0-9 space _ - (the firmware font is uppercase)")
        if name in names or any(name == b["name"] for b in _builtin()):
            raise ValueError(f"theme name {name!r} is already used")
        names.add(name)
        for pol in gt.POLS:
            missing = [k for k, _ in gt.ROLES if k not in t.get(pol, {})] + (["bg_luma"] if "bg_luma" not in t.get(pol, {}) else [])
            if missing:
                raise ValueError(f"{name} {pol}: missing {missing}")
            if not 20 <= int(t[pol]["bg_luma"]) <= 235:
                raise ValueError(f"{name} {pol}: bg_luma must be 20..235")
        bad = gt.check_theme(t)
        if bad:
            raise ValueError("contrast rules failed:\n  " + "\n  ".join(bad))
        ent = name.encode().ljust(NAME_LEN, b"\0") + bytes([t["dark"]["bg_luma"], t["light"]["bg_luma"], 0, 0])
        for pol in gt.POLS:
            roles = [0] * rcount
            for k, _ in gt.ROLES:
                roles[ridx[k]] = gt.snap(t[pol][k])
            ent += struct.pack("<%dH" % rcount, *roles)
        body += ent
    head = THEM_MAGIC + struct.pack("<HBB", VERSION, rcount, len(themes))
    return head + struct.pack("<I", crc(body)) + body


def _builtin():
    return gt.load()


def pack_container(sections):
    """sections: [(tag bytes, data bytes)]"""
    n = len(sections)
    off = 12 + 16 * n
    table, blobs = b"", b""
    for tag, data in sections:
        table += tag + struct.pack("<III", off + len(blobs), len(data), crc(data))
        blobs += data
    return ASSETS_MAGIC + struct.pack("<HHI", VERSION, n, crc(table)) + table + blobs


def parse(blob):
    """Independent reference reader (the firmware's fw/assets_core.h must agree with it). Returns dict or raises ValueError."""
    if len(blob) < 12 or blob[:4] != ASSETS_MAGIC:
        raise ValueError("bad container magic")
    ver, n, tcrc = struct.unpack("<HHI", blob[4:12])
    if ver != VERSION:
        raise ValueError(f"unsupported container version {ver}")
    if n > 8 or len(blob) < 12 + 16 * n:
        raise ValueError("bad section table")
    table = blob[12:12 + 16 * n]
    if crc(table) != tcrc:
        raise ValueError("section table CRC mismatch")
    out = {"sections": {}}
    for i in range(n):
        tag, off, ln, c = struct.unpack("<4sIII", table[i * 16:i * 16 + 16])
        if off + ln > len(blob):
            raise ValueError(f"section {tag!r} runs past the end")
        data = blob[off:off + ln]
        if crc(data) != c:
            raise ValueError(f"section {tag!r} CRC mismatch")
        out["sections"][tag.decode("latin1")] = data
    if "THEM" in out["sections"]:
        out["themes"] = parse_themes(out["sections"]["THEM"])
    return out


def parse_themes(d):
    if len(d) < 12 or d[:4] != THEM_MAGIC:
        raise ValueError("bad THEM magic")
    ver, rc, tc = struct.unpack("<HBB", d[4:8])
    if ver != VERSION or not 1 <= tc:
        raise ValueError("bad THEM header")
    if crc(d[12:]) != struct.unpack("<I", d[8:12])[0]:
        raise ValueError("THEM CRC mismatch")
    per = NAME_LEN + 4 + 4 * rc
    if len(d) != 12 + per * tc:
        raise ValueError("THEM size mismatch")
    themes = []
    for i in range(tc):
        e = d[12 + i * per:12 + (i + 1) * per]
        name = e[:NAME_LEN].split(b"\0")[0].decode("latin1")
        luma = [e[16], e[17]]
        dark = list(struct.unpack("<%dH" % rc, e[20:20 + 2 * rc]))
        light = list(struct.unpack("<%dH" % rc, e[20 + 2 * rc:20 + 4 * rc]))
        themes.append({"name": name, "bg_luma": luma, "role_count": rc, "dark": dark, "light": light})
    return themes


def cmd_pack(a):
    themes = [json.loads(Path(p).read_text()) for p in a.themes]
    blob = pack_container([(b"THEM", pack_themes(themes))])
    Path(a.output).write_bytes(blob)
    print(f"wrote {a.output}: {len(blob)} bytes, {len(themes)} theme(s): " + ", ".join(t["name"] for t in themes))


def cmd_dump(a):
    r = parse(Path(a.file).read_bytes())
    print(json.dumps({"sections": {k: len(v) for k, v in r["sections"].items()},
                      "themes": [{**t, "dark": ["0x%04X" % x for x in t["dark"]], "light": ["0x%04X" % x for x in t["light"]]}
                                 for t in r.get("themes", [])]}, indent=1))


def cmd_install(a):
    parse(Path(a.file).read_bytes())          # refuse to install a broken file
    slug = a.core.split(".", 1)[1].lower()
    dst = Path(a.card) / "Assets" / slug / "common" / "tau-assets.bin"
    if not dst.parent.is_dir():
        sys.exit(f"{dst.parent} does not exist: install the core first")
    shutil.copyfile(a.file, dst)
    if dst.read_bytes() != Path(a.file).read_bytes():
        sys.exit("verify failed after copy")
    print(f"installed and verified {dst}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("pack"); p.add_argument("themes", nargs="+"); p.add_argument("-o", "--output", required=True); p.set_defaults(fn=cmd_pack)
    d = sub.add_parser("dump"); d.add_argument("file"); d.set_defaults(fn=cmd_dump)
    i = sub.add_parser("install"); i.add_argument("file"); i.add_argument("--core", required=True); i.add_argument("--card", default="/Volumes/Pock"); i.set_defaults(fn=cmd_install)
    a = ap.parse_args()
    try:
        a.fn(a)
    except ValueError as e:
        sys.exit(f"ERROR: {e}")


if __name__ == "__main__":
    main()
