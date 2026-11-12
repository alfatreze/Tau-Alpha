#!/usr/bin/env python3
"""Builds the golden vectors of docs/research/meter_bin/ (Omega needs them: SPEC section 2, plan P2.1): the packages the bins came from, a v2 bin, a v1 bin, and damaged bins with the refusal each must get.

  python3 tools/meter_bin_vectors.py [OUTDIR]        (default docs/research/meter_bin; needs the RISC-V toolchain: the seven packs are built from the CURRENT source, once)

The ids of the seven shipped meters are NOT decided yet (plan P2.0): the ids here are SYNTHETIC (last 8 digits 5e7e71c0, the first 24 from the SHA-256 of the meter's key) and say so in every meter.json. The packs are linked for the
window (origin 0x24900000) with the low 16 bits of the id hash as their header id; the v1 bundle is the packs as the firmware ships them today (linked for their slots). The damaged bins are derived from v2_seven.bin by
`damaged()`, which the test re-runs, so they are reproducible byte for byte without the toolchain."""
import hashlib, json, struct, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb

NAMES = {"winamp_bars": "Winamp Bars", "winamp_scope": "Winamp Scope", "chladni": "Chladni", "layered_wave": "Layered Wave", "vu_master": "VU Master", "waterfall_grid": "Waterfall", "sound_map": "Sound Map"}
SUFFIX = "5e7e71c0"        # the LAST 8 digits of every synthetic id (the first 8 are the id_hash and must differ between meters)
OUT = ROOT / "docs/research/meter_bin"


def synth_id(key): return hashlib.sha256(("tau-synthetic-id:" + key).encode()).hexdigest()[:24] + SUFFIX
def sha(b): return hashlib.sha256(b).hexdigest()


def build_packs():
    """({key: window pack}, v1 bundle bytes of the slot-linked packs)"""
    import pack_meter as pm, build_packs as bp, pack_bundle as pb
    win, slots = {}, []
    with tempfile.TemporaryDirectory() as td:
        for key in sorted(pm.SLOT, key=lambda k: pm.SLOT[k]):
            hid = int(synth_id(key)[4:8], 16)               # the low 16 bits of the id hash
            win[key], _ = pm.build(key, pm.SLOT_BASE + bp.WINDOW_SLOT * pm.SLOT_SIZE, Path(td) / (key + "_w.elf"), header_id=hid)
        for k, b in bp.build_all(Path(td)): slots.append(b)
    return win, pb.bundle(slots)


def write_packages(win, out):
    dirs = []
    for key, pack in win.items():
        d = out / "packages" / key; d.mkdir(parents=True, exist_ok=True)
        (d / "meter.bin").write_bytes(pack)
        ph = mb.pack_header(pack)
        man = {"schema": 1, "id": synth_id(key), "name": NAMES[key], "version": "0.7.0", "author": "Tau Alpha", "licence": "MIT", "abi": "%d.%d" % (ph["abi"] >> 8, ph["abi"] & 255), "params_rev": 1,
               "files": {"meter.bin": sha(pack)}, "signature": None, "synthetic_id": "SYNTHETIC test id (docs/research/meter_bin/README.md): the shipped meters' real ids are decided in plan P2.0"}
        (d / "meter.json").write_text(json.dumps(man, indent=1) + "\n"); dirs.append(d)
    return dirs


def _patch(b, off, data): x = bytearray(b); x[off:off + len(data)] = data; return bytes(x)
def _refix(b):
    x = bytearray(b); struct.pack_into("<I", x, 28, len(x)); struct.pack_into("<I", x, len(x) - 4, mb.crc32(bytes(x[32:-4]))); return bytes(x)


def damaged(v2, v1, slot_pack):
    """name -> (bytes, description, the code expected): every damage of a sound v2_seven.bin or v1_seven.bin; pure, no toolchain"""
    toc = lambda i, o: 32 + 256 * i + o
    pk1 = struct.unpack_from("<I", v2, toc(1, 92))[0]
    out = {}
    def add(n, b, why, code): out[n] = (b, why, code)
    add("bad_magic.bin", b"NOPE" + v2[4:], "the magic is not TPKB", mb.E_MAGIC)
    add("bad_version.bin", _patch(v2, 4, struct.pack("<H", 3)), "version 3 (this reader knows 1 and 2)", mb.E_VERSION)
    add("truncated.bin", v2[:-1], "one byte cut from the end", mb.E_SIZE)
    add("trailing_byte.bin", v2 + b"\0", "one byte after the end (total size says otherwise)", mb.E_SIZE)
    add("bad_crc.bin", _patch(v2, len(v2) - 1, bytes([v2[-1] ^ 1])), "the end CRC-32 is wrong", mb.E_CRC)
    add("flipped_pack_byte.bin", _patch(v2, pk1 + 100, bytes([v2[pk1 + 100] ^ 1])), "one byte of a pack's image flipped, the end CRC no longer matches", mb.E_CRC)
    add("count_65.bin", _patch(v2, 6, struct.pack("<H", 65)), "the header says 65 meters (the limit is 64)", mb.E_COUNT)
    add("layout_toc_moved.bin", _refix(_patch(v2, 8, struct.pack("<I", 36))), "toc_off is not 32 (CRC and total fixed up)", mb.E_LAYOUT)
    add("duplicate_hash.bin", _refix(_patch(_patch(v2, toc(1, 0), v2[toc(0, 0):toc(0, 0) + 4]), toc(1, 4), v2[toc(0, 4):toc(0, 4) + 8])), "row 1 has the id_hash and first 8 digits of row 0", mb.E_DUP)
    add("bad_id_uppercase.bin", _refix(_patch(v2, toc(2, 10), b"A")), "an upper-case hex digit in row 2's id", mb.E_ID)
    add("bad_hash.bin", _refix(_patch(v2, toc(1, 0), b"\1\2\3\4")), "row 1's id_hash is not its id's first 8 digits", mb.E_HASH)
    add("bad_name_empty.bin", _refix(_patch(v2, toc(0, 44), b"\0")), "row 0's name is empty", mb.E_NAME)
    add("bad_name_24.bin", _refix(_patch(v2, toc(0, 44), b"X" * 24)), "row 0's name has 24 characters and no NUL", mb.E_NAME)
    add("reserved_byte.bin", _refix(_patch(v2, toc(1, 200), b"\1")), "a reserved byte of row 1 is not zero", mb.E_RESERVED)
    add("params_49.bin", _refix(_patch(v2, toc(1, 82), struct.pack("<H", 49))), "row 1 claims 49 parameters (the limit is 48)", mb.E_PARAMS)
    add("pack_out_of_range.bin", _refix(_patch(v2, toc(1, 96), struct.pack("<I", 1 << 20))), "row 1's pack_size runs past the end of the file", mb.E_PACK)
    add("pack_crc_mismatch.bin", _refix(_patch(v2, toc(1, 100), b"\1\1\1\1")), "row 1's pack_crc is not the pack header's", mb.E_PACK)
    add("pack_window.bin", _refix(_patch(_patch(v2, toc(1, 104), struct.pack("<I", 70000)), pk1 + 44, struct.pack("<I", 70000))), "row 1's pstate (70,000 bytes, TOC and pack header agree) does not fit the 64 KiB window", mb.E_WINDOW)
    add("pack_origin_slot.bin", mb.compose([mb.Entry(id=synth_id("origin_test"), name="Slot linked", version=(0, 7, 0), pack=slot_pack)], strict=False), "a pack linked for a slot, not for the window", mb.E_ORIGIN)
    pv = struct.unpack_from("<I", v2, toc(1, 112))[0]
    add("preview_not_56x32.bin", _refix(_patch(v2, pv + 64, bytes([v2[pv + 64] ^ 1]))), "row 1's run-length stream no longer covers 56 x 32 pixels", mb.E_PREVIEW)
    # v1
    n0 = struct.unpack_from("<I", v1, 8 + 8)[0]; second = 8 + 48 + n0
    n1 = struct.unpack_from("<I", v1, second + 8)[0]
    add("v1_zeroed_descriptor.bin", v1[:second + 48] + bytes(n1) + v1[second + 48 + n1:], "a v1 bundle whose second pack's image (descriptor too) is zero", mb.E_DESC)
    add("v1_duplicate_meter.bin", v1[:6] + struct.pack("<H", 2) + v1[8:8 + 48 + n0] + v1[8:8 + 48 + n0], "a v1 bundle with the first pack twice (one meter id twice)", mb.E_DUP)
    add("v1_truncated.bin", v1[:-3], "a v1 bundle cut short", mb.E_SIZE)
    return out


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else OUT
    out.mkdir(parents=True, exist_ok=True)
    win, v1 = build_packs()
    dirs = write_packages(win, out)
    fb = synth_id("winamp_bars")
    seven = mb.compose_from_dirs(dirs, fallback=fb)
    # valid variants
    es = [mb.load_package(d) for d in dirs]
    for e in es:
        if e.id == fb: e.flags |= mb.FLAG_FALLBACK
    rev = [mb.Entry(**vars(e)) for e in reversed(es)]
    for e in rev:
        if e.name == "VU Master": e.flags &= ~mb.FLAG_VISIBLE
    fut = [mb.Entry(**vars(es[0])), mb.Entry(**vars(es[1])), mb.Entry(**vars(es[2]))]
    fut[1].pack = _patch(fut[1].pack, 4, struct.pack("<H", 0x0103)); fut[2].tau_min = 0x0009
    vs = {"v2_seven.bin": (seven, "the seven shipped meters in display order, Winamp Bars flagged as the fallback", mb.E_OK),
          "v2_reordered_hidden.bin": (mb.compose(rev), "the same seven in reverse order, VU Master hidden (visible flag clear): the TOC is the order", mb.E_OK),
          "v2_unavailable_rows.bin": (mb.compose(fut), "three meters: Winamp Bars, one needing ABI 1.3, one needing TAU 0.9 (a firmware at ABI 1.2, TAU 0.8 lists the last two as unavailable)", mb.E_OK),
          "v2_empty.bin": (mb.compose([]), "a sound bin with no meters (the player keeps Winamp Bars)", mb.E_OK),
          "v1_seven.bin": (v1, "the v1 bundle (tau-packs.bin as the firmware ships it today, packs linked for their slots) of the same seven meters", mb.E_OK)}
    slot_pack = [b for b in (v1[8 + 48 + struct.unpack_from("<I", v1, 16)[0]:],)][0]
    slot_pack = slot_pack[:48 + struct.unpack_from("<I", slot_pack, 8)[0]]
    dm = damaged(seven, v1, slot_pack)
    vectors = []
    for name, (b, why, code) in list(vs.items()) + list(dm.items()):
        (out / name).write_bytes(b)
        r = mb.report(b, 0x0102, 0x0008).splitlines()[0].split()
        assert int(r[1]) == code, (name, r, code)
        vectors.append({"file": name, "bytes": len(b), "sha256": sha(b), "code": code, "code_name": mb.CODE_NAMES[code], "count": int(r[2]) if code == 0 else None, "bad_row": int(r[3]) if code else None, "what": why})
    exp = {"about": "expected result of reading each file with a firmware at ABI 1.2 (0x0102) and TAU 0.8 (0x0008); codes are the table in fw2/cores/meterbin_core.h and tools/meter_bin.py", "fw_abi": "1.2", "fw_tau": "0.8",
           "v2_main": "v2_seven.bin", "package_order": [d.name for d in dirs], "fallback_id": fb, "synthetic_ids": True, "vectors": vectors}
    (out / "expected.json").write_text(json.dumps(exp, indent=1) + "\n")
    print("%d vectors, %d packages written to %s" % (len(vectors), len(dirs), out))


if __name__ == "__main__":
    main()
