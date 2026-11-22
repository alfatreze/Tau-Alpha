#!/usr/bin/env python3
"""Reference implementation of the meter-settings save file `tau-meter-settings.bin` v1 (docs/features/meters/METER_SAVE_FILE_SPEC.md section 4).

It is written independently of the firmware's C code (fw2/cores/mset_file_core.h) and sim/test_p2_mset_file.py checks the two against each other byte for byte. A Mac-side tool (Tau Omega, a checker) uses
this module to READ a file and to create the zeroed seed; it must never rewrite a file that holds a valid generation (the one-writer rule, section 4.4): `apply_change` is for tests and for tools that only ever
append a new generation with seq + 1, exactly as the firmware does.

    python3 tools/meter_settings.py FILE            # print the active generation and its records
"""
import struct, sys, zlib

MAGIC = b"TMSF"
VERSION = 1
FILE_SIZE = 8192
BANK = 4096
HDR = 20
MAX_USED = BANK - HDR      # 4076
MAXN = 64


def _gen(b: bytes, bank: int):
    """-> ('ok', gen) | ('newer', None) | ('bad', None)"""
    g = b[bank * BANK:(bank + 1) * BANK]
    if g[:4] != MAGIC:
        return "bad", None
    magic, ver, count, seq, used, rsv, crc = struct.unpack_from("<4sHHIHHI", g, 0)
    if ver > VERSION:
        return "newer", None
    if ver != VERSION or rsv or used > MAX_USED:
        return "bad", None
    body = g[HDR:HDR + used]
    if zlib.crc32(g[:16] + body) & 0xFFFFFFFF != crc:
        return "bad", None
    recs, pos = [], 0
    while len(recs) < count:
        if pos + 8 > used:
            return "bad", None
        id_hash, rev, preset, n = struct.unpack_from("<IHBB", body, pos)
        if n > MAXN or pos + 8 + 3 * n > used:
            return "bad", None
        pairs = [struct.unpack_from("<BH", body, pos + 8 + 3 * k) for k in range(n)]
        recs.append({"id_hash": id_hash, "params_rev": rev, "preset": preset, "entries": pairs})
        pos += 8 + 3 * n
    if pos != used:
        return "bad", None
    return "ok", {"seq": seq, "records": recs, "used": used}


def parse(b: bytes):
    """-> {'status', 'active' (0, 1 or None), 'locked', 'seq', 'records', 'gens'}; status: ok | none | locked"""
    if len(b) != FILE_SIZE:
        return {"status": "size", "active": None, "locked": False, "seq": 0, "records": [], "gens": [None, None]}
    st = [_gen(b, 0), _gen(b, 1)]
    locked = any(s == "newer" for s, _ in st)
    ok = [g if s == "ok" else None for s, g in st]
    if ok[0] and ok[1]:
        active = 1 if ((ok[1]["seq"] - ok[0]["seq"] + 2**31) % 2**32) - 2**31 > 0 else 0
    else:
        active = 0 if ok[0] else (1 if ok[1] else None)
    g = ok[active] if active is not None else None
    return {"status": "locked" if locked else ("ok" if g else "none"), "active": active, "locked": locked,
            "seq": g["seq"] if g else 0, "records": g["records"] if g else [], "gens": ok}


def find(b: bytes, id_hash: int, params_rev: int):
    """The record for (id_hash, params_rev) or None (a record of another params_rev is not returned)."""
    for r in parse(b)["records"]:
        if r["id_hash"] == id_hash and r["params_rev"] == params_rev & 0xFFFF:
            return r
    return None


def _rec_bytes(r):
    out = struct.pack("<IHBB", r["id_hash"], r["params_rev"], r["preset"], len(r["entries"]))
    for i, v in r["entries"]:
        out += struct.pack("<BH", i, v)
    return out


def _build(seq: int, recs):
    body = b"".join(_rec_bytes(r) for r in recs)
    assert len(body) <= MAX_USED
    head = struct.pack("<4sHHIHH", MAGIC, VERSION, len(recs), seq, len(body), 0)
    crc = zlib.crc32(head + body) & 0xFFFFFFFF
    return (head + struct.pack("<I", crc) + body).ljust(BANK, b"\0")


def apply_change(b: bytes, id_hash: int, params_rev: int, preset: int, entries):
    """What the firmware does for one committed edit: remove the meter's old record, append the new one (none when `entries` is empty), drop the OLDEST records while the generation would exceed 4,076 bytes,
    and write the result as a new generation (seq + 1) into the OTHER bank. -> new file bytes, or None when a newer version is present (refused)."""
    p = parse(b)
    if p["locked"]:
        return None
    entries = list(entries)[:MAXN]
    recs = [r for r in p["records"] if r["id_hash"] != id_hash]
    new = {"id_hash": id_hash, "params_rev": params_rev & 0xFFFF, "preset": preset, "entries": entries}
    newsize = 8 + 3 * len(entries) if entries else 0
    total = sum(8 + 3 * len(r["entries"]) for r in recs)
    excess = max(0, total + newsize - MAX_USED)
    dropped = 0
    keep = []
    for r in recs:
        sz = 8 + 3 * len(r["entries"])
        if dropped < excess:
            dropped += sz
            continue
        keep.append(r)
    if entries:
        keep.append(new)
    dst = 0 if p["active"] is None else 1 - p["active"]
    seq = 1 if p["active"] is None else (p["seq"] + 1) & 0xFFFFFFFF
    out = bytearray(b)
    out[dst * BANK:(dst + 1) * BANK] = _build(seq, keep)
    return bytes(out)


def main(argv):
    if len(argv) != 2:
        print(__doc__); return 2
    b = open(argv[1], "rb").read()
    p = parse(b)
    print(f"status {p['status']}  active bank {p['active']}  seq {p['seq']}  records {len(p['records'])}")
    for r in p["records"]:
        print(f"  id {r['id_hash']:08x} rev {r['params_rev']} preset {r['preset']} entries {r['entries']}")
    return 0 if p["status"] in ("ok", "none") else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
