#!/usr/bin/env python3
"""The meter bin: compose a v2 bin (`tau-meters.bin`, `tau-packs.bin`) from meter packages, read and validate a v1 or v2 bin. The independent Python reference of fw2/cores/meterbin_core.h, written from
docs/features/meters/EXTERNAL_METERS_SPEC.md sections 2, 3 and 7 and docs/features/TAU_PACKS_FORMAT.md (v1), the way tools/tau_packs_info.py stands beside the firmware's pack reader. No code is taken from any other project.

  python3 tools/meter_bin.py compose OUT.bin PKG [PKG ...] [--fallback ID] [--hide ID]     PKG = a directory holding meter.json + meter.bin (+ source/), in display order
  python3 tools/meter_bin.py info BIN [--fw-abi 1.2] [--tau 0.8]                           JSON on stdout (exit 1 when the bin is refused: the code and its name are in the JSON)
  python3 tools/meter_bin.py check BIN [--fw-abi 1.2] [--tau 0.8]                          the firmware's report text (what the C reader prints in the host harness); exit 1 when refused

Layout (little endian): header 32 B | TOC (count x 256 B, display order) | previews (palette 64 B + the 56 x 32 run-length stream each, padded to 4) | packs (TMPK, padded to 4) | u32 CRC-32 of bytes 32.. before it.
A v1 bundle ('TPKB', version 1: header 8 B, packs back to back, no outer CRC) is mapped to the same rows. Refusal codes are the table below (the future `MTOC ERR n` Info row)."""
import hashlib, json, re, struct, sys, zlib
from dataclasses import dataclass
from pathlib import Path

HDR, TOC, MAX_METERS, MAX_BIN = 32, 256, 64, 4456448
WINDOW, WINDOW_SIZE = 0x24970000, 0x10000      # slot 7 of the 8 pack slots = the window P0 ran on a Pocket (B-1045). The spec's text says 0x24900000 (slot 0's address) in the same sentence: see the P2.1 report; ONE constant here and in meterbin_core.h
MAX_PREVIEW, PAL, PIXELS, MAX_PARAMS, MAX_NAME = 1024, 64, 56 * 32, 48, 23
PACK_HDR, V1_HASH_BASE = 48, 0x56310000          # v1 rows: id_hash = V1_HASH_BASE | the pack's meter id (a v1 pack has no 128-bit id)
FLAG_VISIBLE, FLAG_FALLBACK, FLAG_BINARY_ONLY, FLAG_V1 = 1, 2, 4, 8       # bit 3 (v1 row, id synthesized) is an addition of this reader; the spec names bits 0..2 only
ABI = 0x0102                                    # the host table this tree builds (fw2/cores/meter_pack_core.h MTR_PACK_ABI)
# refusal codes: the whole bin is refused (the Info row `MTOC ERR n`, then the player runs on Winamp Bars)
E_OK, E_SIZE, E_MAGIC, E_VERSION, E_CRC, E_COUNT, E_LAYOUT, E_ID, E_HASH, E_DUP, E_NAME, E_PACK, E_WINDOW, E_PREVIEW, E_RESERVED, E_ORIGIN, E_PARAMS, E_READ, E_DESC = range(19)
CODE_NAMES = ["OK", "SIZE", "MAGIC", "VERSION", "CRC", "COUNT", "LAYOUT", "ID", "HASH", "DUP", "NAME", "PACK", "WINDOW", "PREVIEW", "RESERVED", "ORIGIN", "PARAMS", "READ", "DESC"]
# availability of a row of a sound bin (the list greys it; the bin is not refused)
U_OK, U_ABI, U_TAU = 0, 1, 2


class BinError(ValueError):
    def __init__(self, code, row=-1, msg=""):
        super().__init__("%s%s%s" % (CODE_NAMES[code], (" (row %d)" % row) if row >= 0 else "", (": " + msg) if msg else ""))
        self.code, self.row = code, row


@dataclass
class Entry:
    id: str
    name: str
    version: tuple
    pack: bytes
    tau_min: int = 0
    flags: int = FLAG_VISIBLE
    params_rev: int = 0


def crc32(b): return zlib.crc32(b) & 0xFFFFFFFF
def u16(b, o): return struct.unpack_from("<H", b, o)[0]
def u32(b, o): return struct.unpack_from("<I", b, o)[0]
def pad4(n): return (n + 3) & ~3


def valid_id(s): return len(s) == 32 and re.fullmatch(r"[0-9a-f]{32}", s) is not None
def valid_name(raw: bytes):
    """a TOC name field: 1..23 printable ASCII, NUL terminated, NUL padded"""
    n = raw.find(b"\0")
    if n < 0: return False
    return 1 <= n <= MAX_NAME and all(0x20 <= c <= 0x7E for c in raw[:n]) and not any(raw[n:])


def preview_ok(blk):
    """palette 64 B + a run-length stream of exactly 56 x 32 pixels; 65..1024 bytes"""
    if not PAL < len(blk) <= MAX_PREVIEW: return False
    return sum((c & 31) + 1 for c in blk[PAL:]) == PIXELS


def window_ok(size, ps_off, ps_size):
    return 0 < size <= WINDOW_SIZE and ps_off >= size and ps_off <= WINDOW_SIZE and ps_size <= WINDOW_SIZE - ps_off


def pack_header(pack):
    """-> dict of the 48-byte TMPK header; raises BinError(PACK)"""
    if len(pack) < PACK_HDR or pack[:4] != b"TMPK": raise BinError(E_PACK, msg="header")
    abi, mid, size, origin, entry, crc, d_off, d_size, s_org, bss, ps_off, ps_size = struct.unpack_from("<HHIIIIIIIIII", pack, 4)
    if size == 0 or PACK_HDR + size > len(pack): raise BinError(E_PACK, msg="size")
    return dict(abi=abi, mid=mid, size=size, origin=origin, crc=crc, bss=bss, ps_off=ps_off, ps_size=ps_size)


def desc_info(image):
    """The descriptor the pack's image ends with (tools/player2/meter_desc.py), only what a row needs: name, parameters, commands, cycles, and the preview block (palette + run-length stream, contiguous)."""
    n = len(image)
    if n < 48: raise BinError(E_DESC, msg="no room for a descriptor")
    tot, mg = struct.unpack_from("<II", image, n - 8)
    if mg != 0x31435344 or tot < 48 or tot > n or tot > 4096 or tot & 3: raise BinError(E_DESC, msg="trailer")
    d = image[n - tot:]
    magic, ver, total, np_, _, cmds, _, maxc = struct.unpack_from("<IHHBBHII", d, 0)
    if magic != 0x4353444D or ver != 3 or total != tot: raise BinError(E_DESC, msg="header")
    if np_ > MAX_PARAMS: raise BinError(E_PARAMS)
    name_o, _, _, pal_o, rle_o, rle_n = struct.unpack_from("<HHHHHH", d, 20)
    end = tot - 8
    if name_o >= end or pal_o < 40 or pal_o + PAL > end or rle_o + rle_n > end: raise BinError(E_DESC, msg="offsets")
    z = d.find(b"\0", name_o, end)
    if z < 0 or not valid_name(d[name_o:z + 1]): raise BinError(E_NAME)
    if rle_o != pal_o + PAL: raise BinError(E_PREVIEW, msg="palette and stream not contiguous")
    return dict(name=d[name_o:z].decode("ascii"), nparams=np_, cmds=cmds, max_cycles=maxc, preview=bytes(d[pal_o:pal_o + PAL + rle_n]), tot=tot, pal_o=pal_o)


# ---------------------------------------------------------------- compose
def _semver(s):
    m = re.fullmatch(r"(\d{1,5})\.(\d{1,5})\.(\d{1,5})", s)
    if not m or any(int(x) > 65535 for x in m.groups()): raise ValueError("version %r is not major.minor.patch" % s)
    return tuple(int(x) for x in m.groups())


def compose(entries, strict=True):
    """-> the bytes of a v2 bin; ValueError for what a reader would refuse. strict=False only skips the two checks about where a pack is linked (origin, window), for building test vectors."""
    if len(entries) > MAX_METERS: raise ValueError("%d meters: the limit is %d" % (len(entries), MAX_METERS))
    rows, previews, packs, hashes = [], [], [], set()
    for e in entries:
        if not valid_id(e.id): raise ValueError("id %r is not 32 lower-case hex digits" % e.id)
        h = int(e.id[:8], 16)
        if h in hashes: raise ValueError("two ids share the id hash %08x (%s...)" % (h, e.id[:8]))
        hashes.add(h)
        if not valid_name(e.name.encode("latin1", "replace") + b"\0"): raise ValueError("name %r: 1..23 printable ASCII characters" % e.name)
        if e.flags & ~7: raise ValueError("flags %#x: only bits 0..2 exist" % e.flags)
        if e.flags & FLAG_FALLBACK and not e.flags & FLAG_VISIBLE: raise ValueError("the built-in fallback meter cannot be hidden")
        if not 0 <= e.tau_min <= 0xFFFF or not 0 <= e.params_rev <= 0xFFFF: raise ValueError("tau_min / params_rev out of range")
        try: ph = pack_header(e.pack)
        except BinError as x: raise ValueError("pack of %s: %s" % (e.name, x))
        if len(e.pack) != PACK_HDR + ph["size"]: raise ValueError("pack of %s: %d bytes after the image" % (e.name, len(e.pack) - PACK_HDR - ph["size"]))
        if crc32(e.pack[PACK_HDR:]) != ph["crc"]: raise ValueError("pack of %s: image CRC does not match its header" % e.name)
        if strict and ph["origin"] != WINDOW: raise ValueError("pack of %s is linked for 0x%08X, not for the window 0x%08X" % (e.name, ph["origin"], WINDOW))
        if strict and not window_ok(ph["size"], ph["ps_off"], ph["ps_size"]): raise ValueError("pack of %s does not fit the 64 KiB window with its pstate" % e.name)
        try: di = desc_info(e.pack[PACK_HDR:])
        except BinError as x: raise ValueError("pack of %s: %s" % (e.name, x))
        if not preview_ok(di["preview"]): raise ValueError("pack of %s: its preview is not 56 x 32 pixels in at most 1 KiB" % e.name)
        rows.append((e, ph, di)); previews.append(di["preview"]); packs.append(e.pack)
    prev_off = HDR + TOC * len(rows)
    poff, p = [], prev_off
    for b in previews: poff.append(p); p += pad4(len(b))
    packs_off, q, koff = p, p, []
    for b in packs: koff.append(q); q += pad4(len(b))
    total = q + 4
    if total > MAX_BIN: raise ValueError("the bin would be %d bytes: the limit is %d (remove a meter)" % (total, MAX_BIN))
    out = bytearray(total)
    struct.pack_into("<4sHHIIIIII", out, 0, b"TPKB", 2, len(rows), HDR, TOC * len(rows), prev_off, packs_off - prev_off, packs_off, total)
    for i, ((e, ph, di), po, ko) in enumerate(zip(rows, poff, koff)):
        o = HDR + TOC * i
        out[o + 4:o + 4 + 32] = e.id.encode(); out[o + 44:o + 44 + len(e.name)] = e.name.encode("ascii")
        struct.pack_into("<I", out, o, int(e.id[:8], 16))
        struct.pack_into("<HHH", out, o + 68, *e.version); struct.pack_into("<HHHHHH", out, o + 74, ph["abi"], e.tau_min, e.flags, e.params_rev, di["nparams"], di["cmds"])
        struct.pack_into("<IIIIII", out, o + 88, di["max_cycles"], ko, len(e.pack), ph["crc"], ph["ps_size"], ph["bss"]); struct.pack_into("<II", out, o + 112, po, len(di["preview"]))
        out[po:po + len(di["preview"])] = di["preview"]; out[ko:ko + len(e.pack)] = e.pack
    struct.pack_into("<I", out, total - 4, crc32(bytes(out[HDR:total - 4])))
    return bytes(out)


def load_package(d):
    """a package directory (meter.json + meter.bin [+ source/]) -> Entry. The pins in meter.json `files` are checked; `abi` and `tau_min` strings must agree with the pack."""
    d = Path(d); m = json.loads((d / "meter.json").read_text()); pack = (d / "meter.bin").read_bytes()
    pin = m.get("files", {}).get("meter.bin")
    if pin and hashlib.sha256(pack).hexdigest() != pin: raise ValueError("%s: meter.bin does not match its pin" % d)
    ph = pack_header(pack)
    ma, mi = (int(x) for x in m["abi"].split("."))
    if (ma << 8 | mi) != ph["abi"]: raise ValueError("%s: manifest abi %s is not the pack's %d.%d" % (d, m["abi"], ph["abi"] >> 8, ph["abi"] & 255))
    tau = 0
    if m.get("tau_min"): a, b = (int(x) for x in m["tau_min"].split(".")[:2]); tau = a << 8 | b
    flags = FLAG_VISIBLE | (0 if (d / "source").exists() or m.get("source") else FLAG_BINARY_ONLY)
    return Entry(id=m["id"], name=m["name"], version=_semver(m["version"]), pack=pack, tau_min=tau, flags=flags, params_rev=int(m.get("params_rev", 0)))


def compose_from_dirs(dirs, fallback=None, hide=(), strict=True):
    es = [load_package(d) for d in dirs]
    for e in es:
        if e.id == fallback: e.flags |= FLAG_FALLBACK
        if e.id in hide: e.flags &= ~FLAG_VISIBLE
    return compose(es, strict)


# ---------------------------------------------------------------- parse
def _row_v2(b, i, hdr):
    """one 256-byte TOC row -> dict; raises BinError with the first failing check (the order is part of the contract: the C reader makes the same checks in the same order)"""
    o = HDR + TOC * i; r = b[o:o + TOC]
    idr = r[4:44]
    if not valid_id(idr[:32].decode("latin1")) or any(idr[32:]): raise BinError(E_ID, i)
    if u32(r, 0) != int(idr[:8].decode(), 16): raise BinError(E_HASH, i)
    if not valid_name(r[44:68]): raise BinError(E_NAME, i)
    flags = u16(r, 78)
    if flags & ~7 or u16(r, 86) or any(r[120:]): raise BinError(E_RESERVED, i)
    nparams = u16(r, 82)
    if nparams > MAX_PARAMS: raise BinError(E_PARAMS, i)
    pack_off, pack_size, pack_crc, pstate, bss, prev_off, prev_len = struct.unpack_from("<IIIIIII", r, 92)
    packs_off, end = hdr["packs_off"], hdr["total"] - 4
    if pack_size < PACK_HDR or pack_off & 3 or pack_off < packs_off or pack_off > end or pack_size > end - pack_off: raise BinError(E_PACK, i, "range")
    ph = b[pack_off:pack_off + PACK_HDR]
    if ph[:4] != b"TMPK": raise BinError(E_PACK, i, "magic")
    pabi, _, psize, porigin, _, pcrc, _, _, _, pbss, pps_off, pps_size = struct.unpack_from("<HHIIIIIIIIII", ph, 4)
    if psize + PACK_HDR != pack_size or pabi != u16(r, 74) or pcrc != pack_crc or pps_size != pstate or pbss != bss: raise BinError(E_PACK, i, "TOC and pack header disagree")
    if not window_ok(psize, pps_off, pps_size): raise BinError(E_WINDOW, i)
    if porigin != WINDOW: raise BinError(E_ORIGIN, i)
    if prev_off < hdr["prev_off"] or prev_off > hdr["packs_off"] or prev_len > hdr["packs_off"] - prev_off or not preview_ok(b[prev_off:prev_off + prev_len]): raise BinError(E_PREVIEW, i)
    return dict(id_hash=u32(r, 0), id=idr[:32].decode(), name=r[44:44 + r[44:68].index(0)].decode(), ver=(u16(r, 68), u16(r, 70), u16(r, 72)), abi=u16(r, 74), tau_min=u16(r, 76), flags=flags, params_rev=u16(r, 80),
                nparams=nparams, cmds=u16(r, 84), max_cycles=u32(r, 88), pack_off=pack_off, pack_size=pack_size, pack_crc=pack_crc, pstate=pstate, bss=bss, preview_off=prev_off, preview_len=prev_len)


def _parse_v2(b):
    if len(b) < HDR + 4 or len(b) > MAX_BIN: raise BinError(E_SIZE)
    count = u16(b, 6)
    if count > MAX_METERS: raise BinError(E_COUNT)
    toc_off, toc_len, prev_off, prev_len, packs_off, total = struct.unpack_from("<IIIIII", b, 8)
    if total != len(b): raise BinError(E_SIZE, msg="total size %d, file %d" % (total, len(b)))
    if crc32(b[HDR:total - 4]) != u32(b, total - 4): raise BinError(E_CRC)
    if toc_off != HDR or toc_len != TOC * count or prev_off != HDR + toc_len or packs_off != prev_off + prev_len or packs_off & 3 or packs_off + 4 > total: raise BinError(E_LAYOUT)
    hdr = dict(prev_off=prev_off, packs_off=packs_off, total=total)
    rows = [_row_v2(b, i, hdr) for i in range(count)]
    _dups(rows)
    return dict(version=2, count=count, bytes=len(b), rows=rows)


def _dups(rows):
    seen = set()
    for i, r in enumerate(rows):
        if r["id_hash"] in seen: raise BinError(E_DUP, i)
        seen.add(r["id_hash"])


def _parse_v1(b):
    if len(b) > MAX_BIN: raise BinError(E_SIZE)
    count = u16(b, 6)
    if count > MAX_METERS: raise BinError(E_COUNT)
    off, offs = 8, []
    for i in range(count):
        if off + PACK_HDR > len(b): raise BinError(E_SIZE, i)
        if b[off:off + 4] != b"TMPK": raise BinError(E_PACK, i)
        size = u32(b, off + 8)
        if size == 0 or off + PACK_HDR + size > len(b): raise BinError(E_SIZE, i)
        offs.append(off); off += PACK_HDR + size
    if off != len(b): raise BinError(E_SIZE, msg="%d bytes after the last pack" % (len(b) - off))
    rows = []
    for i, off in enumerate(offs):
        ph = pack_header(b[off:off + PACK_HDR + u32(b, off + 8)])
        try: di = desc_info(b[off + PACK_HDR:off + PACK_HDR + ph["size"]])
        except BinError as x: raise BinError(x.code, i, str(x))
        if not window_ok(ph["size"], ph["ps_off"], ph["ps_size"]): raise BinError(E_WINDOW, i)
        if not preview_ok(di["preview"]): raise BinError(E_PREVIEW, i)
        rows.append(dict(id_hash=V1_HASH_BASE | ph["mid"], id="", name=di["name"], ver=(0, 0, 0), abi=ph["abi"], tau_min=0, flags=FLAG_VISIBLE | FLAG_V1 | (FLAG_FALLBACK if ph["mid"] == 0 else 0), params_rev=0,
                         nparams=di["nparams"], cmds=di["cmds"], max_cycles=di["max_cycles"], pack_off=off, pack_size=PACK_HDR + ph["size"], pack_crc=ph["crc"], pstate=ph["ps_size"], bss=ph["bss"],
                         preview_off=off + PACK_HDR + ph["size"] - di["tot"] + di["pal_o"], preview_len=len(di["preview"])))
    _dups(rows)
    return dict(version=1, count=count, bytes=len(b), rows=rows)


def parse(b):
    """-> {"version", "count", "bytes", "rows": [...]}; raises BinError (code, row) for a bin the firmware refuses whole"""
    if len(b) < 8: raise BinError(E_SIZE)
    if b[:4] != b"TPKB": raise BinError(E_MAGIC)
    v = u16(b, 4)
    if v == 2: return _parse_v2(b)
    if v == 1: return _parse_v1(b)
    raise BinError(E_VERSION)


def avail(row, fw_abi=ABI, fw_tau=0):
    """0 available, 1 needs another ABI (another major, or a minor above the firmware's), 2 needs a newer TAU: the list greys such a row"""
    if (row["abi"] >> 8) != (fw_abi >> 8) or (row["abi"] & 255) > (fw_abi & 255): return U_ABI
    return U_TAU if row["tau_min"] > fw_tau else U_OK


def report(b, fw_abi=ABI, fw_tau=0):
    """The text the C reader's host harness prints for the same bytes (sim/p2_meterbin_harness.c): `open CODE COUNT BADROW`, a `row` line per row, an `avail` line per row."""
    try: r = parse(b)
    except BinError as e: return "open %d 0 %d\n" % (e.code, e.row)
    out = ["open 0 %d -1" % r["count"]]
    for i, w in enumerate(r["rows"]):
        nums = [i, w["id_hash"]] + [w["id"] or "-"] + list(w["ver"]) + [w[k] for k in ("abi", "tau_min", "flags", "params_rev", "nparams", "cmds", "max_cycles", "pack_off", "pack_size", "pack_crc", "pstate", "bss", "preview_off", "preview_len")]
        out.append("row " + " ".join(str(x) for x in nums) + " " + w["name"])
    for i, w in enumerate(r["rows"]): out.append("avail %d %d" % (i, avail(w, fw_abi, fw_tau)))
    return "\n".join(out) + "\n"


def _ver(s): a, b = (int(x) for x in s.split(".")[:2]); return a << 8 | b


def main(argv):
    if len(argv) < 3 or argv[1] not in ("compose", "info", "check"): print(__doc__); return 2
    cmd, a = argv[1], argv[2:]
    def opt(n, default=None):
        if n in a: i = a.index(n); v = a[i + 1]; del a[i:i + 2]; return v
        return default
    if cmd == "compose":
        fb, hide = opt("--fallback"), []
        while "--hide" in a: hide.append(opt("--hide"))
        if len(a) < 2: print(__doc__); return 2
        try: data = compose_from_dirs(a[1:], fallback=fb, hide=hide)
        except ValueError as e: print("refused: %s" % e); return 1
        Path(a[0]).write_bytes(data); print("%s: %d B, %d meter(s)" % (a[0], len(data), u16(data, 6))); return 0
    fw_abi, fw_tau = _ver(opt("--fw-abi", "1.2")), _ver(opt("--tau", "0.8"))
    data = Path(a[0]).read_bytes()
    if cmd == "check":
        t = report(data, fw_abi, fw_tau); print(t, end=""); return 0 if t.startswith("open 0 ") else 1
    try:
        r = parse(data); r["rows"] = [dict(w, available=["yes", "needs another ABI", "needs a newer TAU"][avail(w, fw_abi, fw_tau)]) for w in r["rows"]]
        print(json.dumps(r, indent=1)); return 0
    except BinError as e:
        print(json.dumps({"error": str(e), "code": e.code, "code_name": CODE_NAMES[e.code], "row": e.row})); return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
