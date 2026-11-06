#!/usr/bin/env python3
"""Meter presets as separate files (docs/features/METER_PRESETS_EXTERNAL.md, B-966): the writer, the reader and the factory generator for the two file kinds that fw2/cores/mpreset_core.h reads.

  <meter id>/index.tpi    the presets of one meter in list order (id, hidden flag, name); the folder is the meter's manifest id
  <meter id>/p<id>.tpr    one preset: a sparse list of (parameter key hash, value)

  python3 tools/player2/mpreset.py factory OUT_DIR [KEY ...]    writes the factory set of the meters (from meters/<key>/meter.json) under OUT_DIR/tau-presets/
  python3 tools/player2/mpreset.py show FILE                    prints a .tpi or .tpr file

The writer refuses what the firmware would have to repair: a key the meter does not have, a value outside the parameter's range, two parameters whose key hashes collide, a name over 15 characters, more than 127 presets or an id over 126."""
import json, struct, sys, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_desc as md
import meter_manifest as mm
IDX_MAGIC, PRE_MAGIC, VERSION = b"TPRI", b"TPRS", 1
NAME = 16

def key_hash(key):
    """FNV-1a over the key's bytes, the 32 bits folded to 16 (mp_key_hash in fw2/cores/mpreset_core.h)"""
    h = 2166136261
    for b in key.encode("ascii"): h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return (h ^ (h >> 16)) & 0xFFFF

def params_of(manifest):
    """[(key, hash, min, max, default)] in the manifest's order; refuses two keys with one hash"""
    rows = md.param_rows(manifest); out = []; seen = {}
    for r, p in zip(rows, manifest["params"]):
        h = key_hash(r["key"])
        if h in seen: raise ValueError("%s: the parameter keys %r and %r have the same hash %04x" % (manifest["key"], seen[h], r["key"], h))
        seen[h] = r["key"]
        dflt = p.get("default", r["min"])
        if p["type"] == "bool": dflt = int(bool(dflt))
        out.append((r["key"], h, r["min"], r["max"], int(dflt)))
    return out

def _wrap(magic, ident, meter_id, body):
    return magic + struct.pack("<BBH", VERSION, ident, meter_id) + struct.pack("<I", zlib.crc32(body) & 0xFFFFFFFF) + body

def build_index(meter_id, presets):
    """presets: [{"id", "name", "hidden"?}] in list order"""
    if len(presets) > 127: raise ValueError("more than 127 presets (the ids are 0..126: the choice is persisted in 7 bits)")
    ids = [p["id"] for p in presets]
    if len(set(ids)) != len(ids) or any(not 0 <= i <= 126 for i in ids): raise ValueError("preset ids must be distinct and 0..126")
    body = struct.pack("<H", len(presets))
    for p in presets:
        nm = p["name"].encode("ascii")
        if not 0 < len(nm) <= 15: raise ValueError("a name of 1 to 15 characters: %r" % p["name"])
        body += struct.pack("<BB", p["id"], 1 if p.get("hidden") else 0) + nm.ljust(NAME, b"\0")
    return _wrap(IDX_MAGIC, 0, meter_id, body)

def build_preset(meter_id, ident, name, values, params):
    """values: {key: value}, sparse; params: params_of(manifest)"""
    nm = name.encode("ascii")
    if len(nm) > 15: raise ValueError("a name of at most 15 characters: %r" % name)
    known = {k: (lo, hi) for k, _, lo, hi, _ in params}; hashes = {k: h for k, h, *_ in params}
    for k, v in values.items():
        if k not in known: raise ValueError("%r is not a parameter of this meter" % k)
        if not known[k][0] <= v <= known[k][1]: raise ValueError("%s = %s is outside %s" % (k, v, known[k]))
    body = struct.pack("<BB", len(values), len(nm)) + nm + b"".join(struct.pack("<HH", hashes[k], v) for k, v in values.items())
    return _wrap(PRE_MAGIC, ident, meter_id, body)

def _split(data, magic):
    if len(data) < 14 or data[:4] != magic: raise ValueError("not a %s file" % magic.decode())
    ver, ident, meter_id, crc = struct.unpack_from("<BBHI", data, 4)
    if ver != VERSION: raise ValueError("version %d" % ver)
    if zlib.crc32(data[12:]) & 0xFFFFFFFF != crc: raise ValueError("crc")
    return ident, meter_id, data[12:]

def parse_index(data):
    _, meter_id, body = _split(data, IDX_MAGIC)
    n = struct.unpack_from("<H", body)[0]
    if len(body) != 2 + n * (2 + NAME): raise ValueError("layout")
    out = []
    for i in range(n):
        e = body[2 + i * (2 + NAME):2 + (i + 1) * (2 + NAME)]
        out.append({"id": e[0], "hidden": bool(e[1] & 1), "name": e[2:].split(b"\0")[0].decode("ascii")})
    return meter_id, out

def parse_preset(data):
    ident, meter_id, body = _split(data, PRE_MAGIC)
    nk, nl = body[0], body[1]
    if nl > 15 or len(body) != 2 + nl + 4 * nk: raise ValueError("layout")
    name = body[2:2 + nl].decode("ascii")
    pairs = [struct.unpack_from("<HH", body, 2 + nl + 4 * k) for k in range(nk)]
    return meter_id, ident, name, pairs

def apply_preset(data, params):
    """what the firmware computes: defaults, then every key the meter has, clamped; returns the values in the manifest's order"""
    _, _, _, pairs = parse_preset(data)
    vals = [d for *_, d in params]
    for h, v in pairs:
        for i, (_, ph, lo, hi, _) in enumerate(params):
            if ph == h: vals[i] = min(max(v, lo), hi); break
    return vals

def factory_files(manifest):
    """{relative path: bytes} of a meter's factory set, from the presets of its manifest (every parameter written, so a change of a default later cannot change a factory preset)"""
    params = params_of(manifest); mid = manifest["index"]; key = manifest["key"]
    keys = [p[0] for p in params]
    files = {}; entries = []
    for i, pr in enumerate(manifest["presets"]):
        nm = pr["name"].upper()[:15]
        vals = {k: int(pr["values"][k]) for k in keys if k in pr["values"]}
        files["%d/p%d.tpr" % (mid, i)] = build_preset(mid, i, nm, vals, params)
        entries.append({"id": i, "name": nm})
    files["%d/index.tpi" % mid] = build_index(mid, entries)
    return files

def main(argv):
    if len(argv) >= 3 and argv[1] == "factory":
        out = Path(argv[2]) / "tau-presets"; keys = argv[3:]
        for mf in sorted((ROOT / "meters").glob("*/meter.json")):
            d = mm.load(mf)
            if "p2" not in d or (keys and d["key"] not in keys): continue
            for rel, data in factory_files(d).items():
                (out / rel).parent.mkdir(parents=True, exist_ok=True); (out / rel).write_bytes(data)
            print("%s: %d presets" % (d["key"], len(d["presets"])))
        return 0
    if len(argv) == 3 and argv[1] == "show":
        data = Path(argv[2]).read_bytes()
        print(parse_index(data) if data[:4] == IDX_MAGIC else parse_preset(data)); return 0
    print(__doc__); return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv))
