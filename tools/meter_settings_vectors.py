#!/usr/bin/env python3
"""Golden vectors for the meter-settings save file `tau-meter-settings.bin` v1 (docs/features/meters/METER_SAVE_FILE_SPEC.md section 4), for Tau Omega's reader (message 028, O026/3).

    python3 tools/meter_settings_vectors.py [OUT_DIR]       default docs/research/meter_settings   (deterministic: no clock, no toolchain)

Every file is built with the Python reference `tools/meter_settings.py` (the same module a Mac tool reads with); `sim/test_p2_mset_vectors.py` proves the firmware's C code (`fw2/cores/mset_file_core.h`) answers every vector the
same way and regenerates the folder byte for byte. `expected.json` says, for each file: its size and SHA-256, what a reader answers (status, which bank wins, seq, the records) and for some files a list of lookups
(id_hash, params_rev) with the record found or none."""
import hashlib, json, random, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import meter_settings as ms
OUT = ROOT / "docs/research/meter_settings"
A, B, C = 0x7DDDFE63, 0x58128034, 0x7471CC21          # three shipped meters' id hashes (Winamp Bars, ...): realistic keys, no new ids
ZERO = bytes(ms.FILE_SIZE)


def edits(*steps, start=ZERO):
    """a file after these edits, each (id_hash, params_rev, preset, [(index, value), ...]) applied by the firmware's rule"""
    b = start
    for s in steps:
        b = ms.apply_change(b, *s)
    return b


def set_bank(b, bank, gen):
    o = bytearray(b); o[bank * ms.BANK:(bank + 1) * ms.BANK] = gen; return bytes(o)


def vectors():
    """name -> (bytes, what, lookups[(id_hash, params_rev)])"""
    v = {}
    e1 = (A, 1, 0, [(0, 4)])
    e2 = (B, 1, 0, [(0, 1), (18, 3)])
    e3 = (C, 2, 3, [(0, 1)])
    v["zero_seed.bin"] = (ZERO, "the zero-filled seed Omega or the installer creates: no generation, defaults", [(A, 1)])
    v["all_ff.bin"] = (b"\xff" * ms.FILE_SIZE, "8,192 bytes of 0xFF (the Pocket's own fill when a file is missing, bit 5): no generation", [(A, 1)])
    v["one_record.bin"] = (edits(e1), "a fresh file after one edit: bank A, seq 1, one record", [(A, 1), (B, 1)])
    v["two_generations_b_wins.bin"] = (edits(e1, e2), "two edits: bank A holds seq 1 (one record), bank B seq 2 (two records): the larger seq wins, so bank B", [(A, 1), (B, 1)])
    v["three_records.bin"] = (edits(e1, e2, e3), "three edits: bank A seq 3 wins over bank B seq 2; three records, oldest first; the third is params_rev 2 with preset 3", [(A, 1), (B, 1), (C, 2), (C, 1)])
    v["record_replaced.bin"] = (edits(e1, e2, (A, 1, 5, [(1, 99), (2, 100)])), "the first meter edited again: its record moves to the END (recency order) and replaces the old one", [(A, 1), (B, 1)])
    v["record_removed.bin"] = (edits(e1, e2, e3, (B, 1, 0, [])), "an edit with no entries removes the meter's record (B): two records left", [(A, 1), (B, 1), (C, 2)])
    rng = random.Random(1133)
    big = ZERO
    for k in range(40):
        ent = [(i, rng.randrange(65536)) for i in sorted(rng.sample(range(64), 40))]
        big = ms.apply_change(big, 0xA0000000 + k, 1, 0, ent)
    v["full_with_eviction.bin"] = (big, "40 meters with 40 changed parameters each (5,120 bytes of records, more than a generation holds): the oldest meters were evicted, the newest are intact; used <= 4,076", [(0xA0000000, 1), (0xA0000027, 1)])
    good = edits(e1, e2, e3)
    p = ms.parse(good); act = p["active"]
    d = bytearray(good); d[act * ms.BANK + 24] ^= 0x01
    v["damaged_newest.bin"] = (bytes(d), "three_records.bin with one bit flipped inside a record of the newest generation (bank A): its CRC fails, so the older generation (bank B, seq 2, two records) is used", [(A, 1), (B, 1), (C, 2)])
    d = bytearray(good); d[act * ms.BANK + 16] ^= 0x80
    v["damaged_header_crc.bin"] = (bytes(d), "three_records.bin with a bit of the newest generation's stored CRC flipped: same outcome, the older generation is used", [(C, 2)])
    d = bytearray(good); d[act * ms.BANK + 24] ^= 0x01; d[(1 - act) * ms.BANK + 24] ^= 0x01
    v["damaged_both.bin"] = (bytes(d), "one bit flipped inside a record of each generation: no valid generation, defaults", [(A, 1)])
    n = bytearray(good); n[act * ms.BANK + 4:act * ms.BANK + 6] = (2).to_bytes(2, "little")
    v["newer_version.bin"] = (bytes(n), "three_records.bin with version 2 in the newest generation's header (a file from a future Tau): the whole file is ignored (status locked): no record of either generation is used and nothing is written", [(A, 1)])
    v["wrong_size_8191.bin"] = (good[:-1], "three_records.bin cut by one byte: not an 8,192-byte file, so the Pocket does not load it (size_exact) and a reader says size", [])
    v["wrong_size_8193.bin"] = (good + b"\0", "three_records.bin with one byte appended: size", [])
    # seq across the wrap: serial arithmetic ((a - b) as a signed 32-bit number); a plain comparison picks the wrong bank in the first three
    g_old = ms._build(0xFFFFFFFF, [{"id_hash": A, "params_rev": 1, "preset": 0, "entries": [(0, 1)]}])
    g_new = ms._build(0x00000000, [{"id_hash": A, "params_rev": 1, "preset": 0, "entries": [(0, 2)]}])
    v["seq_wrap_b_wins.bin"] = (set_bank(set_bank(ZERO, 0, g_old), 1, g_new), "bank A seq 0xFFFFFFFF, bank B seq 0: serial arithmetic says 0 is NEWER, so bank B wins (a plain 'larger number wins' would pick bank A)", [(A, 1)])
    v["seq_wrap_a_wins.bin"] = (set_bank(set_bank(ZERO, 0, g_new), 1, g_old), "the same two generations the other way round: bank A seq 0, bank B seq 0xFFFFFFFF: bank A wins", [(A, 1)])
    h_old = ms._build(0x7FFFFFFF, [{"id_hash": B, "params_rev": 1, "preset": 0, "entries": [(0, 1)]}])
    h_new = ms._build(0x80000000, [{"id_hash": B, "params_rev": 1, "preset": 0, "entries": [(0, 2)]}])
    v["seq_half_b_wins.bin"] = (set_bank(set_bank(ZERO, 0, h_old), 1, h_new), "bank A seq 0x7FFFFFFF, bank B seq 0x80000000: the difference is +1, so bank B wins (a signed 32-bit compare of the seqs themselves would pick A)", [(B, 1)])
    v["seq_half_a_wins.bin"] = (set_bank(set_bank(ZERO, 0, h_new), 1, h_old), "bank A seq 0x80000000, bank B seq 0x7FFFFFFF: bank A wins", [(B, 1)])
    return v


def answer(b: bytes, lookups):
    """what a reader answers for these bytes: the reference's parse, as JSON-able data"""
    p = ms.parse(b)
    if p["locked"]:        # a version above 1 anywhere: the firmware ignores the WHOLE file (old pool serves, nothing written); a reader must not use the other generation either
        p = {**p, "active": None, "records": [], "seq": 0}
    d = {"status": p["status"], "active_bank": p["active"], "seq": p["seq"] if p["active"] is not None else None,
         "count": len(p["records"]), "records": [{"id_hash": "%08x" % r["id_hash"], "params_rev": r["params_rev"], "preset": r["preset"], "entries": [list(x) for x in r["entries"]]} for r in p["records"]]}
    if len(b) == ms.FILE_SIZE:
        d["generations"] = [{"valid": g is not None, "seq": g["seq"] if g else None} for g in p["gens"]]
    d["lookups"] = []
    for idh, rev in lookups:
        r = ms.find(b, idh, rev) if len(b) == ms.FILE_SIZE and not p["locked"] else None
        d["lookups"].append({"id_hash": "%08x" % idh, "params_rev": rev, "found": r is not None, **({"preset": r["preset"], "entries": [list(x) for x in r["entries"]]} if r else {})})
    return d


README = """# Golden vectors for the meter-settings save file `tau-meter-settings.bin` v1 (for Tau Omega: O026/3)

Made by `tools/meter_settings_vectors.py` (deterministic: no clock, no toolchain) with the Python reference `tools/meter_settings.py`; `sim/test_p2_mset_vectors.py` proves that the firmware's own C code (`fw2/cores/mset_file_core.h`) answers every file the same way, regenerates this folder byte for byte, and that a reader which compares `seq` as a plain number fails on the wrap files.

Format and rules: `docs/features/meters/METER_SAVE_FILE_SPEC.md` section 4. In short: two generations of 4,096 bytes; a generation is a 20-byte header (`TMSF`, version 1, count, seq, used, CRC-32 of header bytes 0..15 and the records) and `used` bytes of records (`id_hash u32, params_rev u16, preset u8, n u8, n x (index u8, value u16)`, little-endian, oldest first). A generation is valid when magic, version 1, `used <= 4076`, the records exactly filling `used` and the CRC all hold; the valid generation with the larger `seq` in **serial arithmetic** (`(a - b)` read as a signed 32-bit number is positive) is the active one; none valid = defaults; a version above 1 in EITHER generation = `locked`: the whole file is ignored (no records are used, not even those of the other generation) and nothing is written.

## What `expected.json` says for each file
`status` (`ok` = a valid generation exists, `none` = defaults, `locked` = a newer version is present, `size` = not an 8,192-byte file), `active_bank` (0 or 1, null when none), `seq`, `count`, `records` (in file order), `generations` (valid and seq of bank A and bank B), and `lookups`: for each (`id_hash`, `params_rev`) whether the **active** generation holds a record usable for it. A record of another `params_rev` is **not** found (it is ignored on read and dropped at that meter's next write). A file that is not 8,192 bytes is never loaded by the Pocket (`size_exact`), so for the two `wrong_size` files the answer is `size` and no lookups.

## Files
| file | bytes | SHA-256 | status | bank | seq | records | what |
|---|---|---|---|---|---|---|---|
@ROWS@

`locked` files: the firmware does not write them and neither should a tool (the one-writer rule, spec 4.4). Writes by Omega are limited to creating the zero seed.

## Regenerating
`python3 tools/meter_settings_vectors.py` rewrites everything, including `expected.json` and this README.
"""


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else OUT
    out.mkdir(parents=True, exist_ok=True)
    vs = vectors(); rows = []; files = []
    for name, (b, what, lookups) in vs.items():
        (out / name).write_bytes(b)
        a = answer(b, lookups)
        files.append({"file": name, "bytes": len(b), "sha256": hashlib.sha256(b).hexdigest(), **a, "what": what})
        rows.append("| `%s` | %d | `%s` | %s | %s | %s | %d | %s |" % (name, len(b), files[-1]["sha256"], a["status"], "-" if a["active_bank"] is None else "AB"[a["active_bank"]], "-" if a["seq"] is None else a["seq"], a["count"], what))
    exp = {"about": "expected answer of a reader for each file (tools/meter_settings.py is the reference; sim/test_p2_mset_vectors.py proves the firmware's C code agrees); records are in file order, oldest first; lookups use the active generation only; seq compares in serial arithmetic",
           "file_size": ms.FILE_SIZE, "files": files}
    (out / "expected.json").write_text(json.dumps(exp, indent=1) + "\n")
    (out / "README.md").write_text(README.replace("@ROWS@", "\n".join(rows)))
    print("%d vectors written to %s" % (len(files), out))


if __name__ == "__main__":
    main()
