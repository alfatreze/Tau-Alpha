#!/usr/bin/env python3
"""Builds the golden vectors of docs/research/meter_bin/ (Omega needs them: SPEC section 2, plan P2.1): the packages the bins came from, a v2 bin, a v1 bin (for the Python tool only: the firmware reads v2), and damaged bins with the
refusal each must get, both at boot (open_toc: header, TOC, previews, no pack) and in the full check (the whole-file CRC and every pack header).

  python3 tools/meter_bin_vectors.py [OUTDIR]        (default docs/research/meter_bin; needs the RISC-V toolchain: the seven packs are built from the CURRENT source, once)

The ids are the seven meters' PERMANENT ids (meters/<key>/meter.json, B-1047) and their names the manifests'. The packs are linked for the window (origin 0x24970000) with the low 16 bits of the id hash as their header id; the v1 bundle is the
packs as the firmware ships them today (linked for their slots). The two meters of v2_tuning_collision.bin have SYNTHETIC ids (last 8 digits 5e7e71c0). The damaged bins are derived from v2_seven.bin by `damaged()`, which the test re-runs,
so they are reproducible byte for byte without the toolchain."""
import hashlib, json, struct, sys, tempfile, zlib
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/player2"))
import meter_bin as mb

KEYS = ["winamp_bars", "winamp_scope", "chladni", "layered_wave", "vu_master", "waterfall_grid", "sound_map"]
SUFFIX = "5e7e71c0"        # the LAST 8 digits of every synthetic id (the first 8 are the id_hash and must differ between meters)
OUT = ROOT / "docs/research/meter_bin"


def manifest(key): return json.loads((ROOT / "meters" / key / "meter.json").read_text())
def real_id(key): return manifest(key)["id"]
def synth_id(key): return hashlib.sha256(("tau-synthetic-id:" + key).encode()).hexdigest()[:24] + SUFFIX
def sha(b): return hashlib.sha256(b).hexdigest()


def build_packs():
    """({key: window pack}, v1 bundle bytes of the slot-linked packs)"""
    import pack_meter as pm, build_packs as bp, pack_bundle as pb
    win, slots = {}, []
    with tempfile.TemporaryDirectory() as td:
        for key in sorted(pm.SLOT, key=lambda k: pm.SLOT[k]):
            hid = int(real_id(key)[4:8], 16)               # the low 16 bits of the id hash
            win[key], _ = pm.build(key, pm.SLOT_BASE + bp.WINDOW_SLOT * pm.SLOT_SIZE, Path(td) / (key + "_w.elf"), header_id=hid)
        for k, b in bp.build_all(Path(td)): slots.append(b)
    return win, pb.bundle(slots)


def write_packages(win, out):
    dirs = []
    for key, pack in win.items():
        d = out / "packages" / key; d.mkdir(parents=True, exist_ok=True)
        (d / "meter.bin").write_bytes(pack)
        ph = mb.pack_header(pack)
        man = {"schema": 1, "id": real_id(key), "name": manifest(key)["name"], "version": "0.7.0", "author": "Tau Alpha", "licence": "MIT", "abi": "%d.%d" % (ph["abi"] >> 8, ph["abi"] & 255), "params_rev": 1,
               "files": {"meter.bin": sha(pack)}, "signature": None}
        (d / "meter.json").write_text(json.dumps(man, indent=1) + "\n"); dirs.append(d)
    return dirs


def _patch(b, off, data): x = bytearray(b); x[off:off + len(data)] = data; return bytes(x)
def _refix(b):
    """total size, toc_crc and the whole-file CRC recomputed (so a damage inside reaches the checks after them)"""
    x = bytearray(b); struct.pack_into("<I", x, 28, len(x)); po = struct.unpack_from("<I", x, 24)[0]
    if po <= len(x): struct.pack_into("<I", x, 32, mb.crc32(bytes(x[40:po])))
    struct.pack_into("<I", x, len(x) - 4, mb.crc32(bytes(x[40:-4]))); return bytes(x)


def damaged(v2, v1, slot_pack, slot_pack_w):
    """name -> (bytes, description, the code expected): every damage of a sound v2_seven.bin or v1_seven.bin; pure, no toolchain"""
    toc = lambda i, o: 40 + 256 * i + o
    pk1 = struct.unpack_from("<I", v2, toc(1, 92))[0]
    out = {}
    def add(n, b, why, code): out[n] = (b, why, code)
    add("bad_magic.bin", b"NOPE" + v2[4:], "the magic is not TPKB", mb.E_MAGIC)
    add("bad_version.bin", _patch(v2, 4, struct.pack("<H", 3)), "version 3 (this reader knows 1 and 2)", mb.E_VERSION)
    add("truncated.bin", v2[:-1], "one byte cut from the end", mb.E_SIZE)
    add("trailing_byte.bin", v2 + b"\0", "one byte after the end (total size says otherwise)", mb.E_SIZE)
    add("bad_toc_crc.bin", _patch(v2, 32, bytes([v2[32] ^ 1])), "toc_crc (the CRC of the TOC and the previews) is wrong", mb.E_CRC)
    add("flipped_toc_byte.bin", _patch(v2, toc(3, 150), b"\1"), "one byte of row 3 changed, toc_crc no longer matches", mb.E_CRC)
    add("bad_file_crc.bin", _patch(v2, len(v2) - 1, bytes([v2[-1] ^ 1])), "the whole-file CRC at the end is wrong (the firmware never reads it: boot code 0)", mb.E_FILECRC)
    add("flipped_pack_byte.bin", _patch(v2, pk1 + 100, bytes([v2[pk1 + 100] ^ 1])), "one byte of a pack's image flipped: the whole-file CRC no longer matches (boot code 0; the loader's pack_crc check catches it at selection)", mb.E_FILECRC)
    add("header_reserved.bin", _refix(_patch(v2, 36, b"\1")), "the reserved u32 at offset 36 is not zero", mb.E_RESERVED)
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
    add("pack_crc_mismatch.bin", _refix(_patch(v2, toc(1, 100), b"\1\1\1\1")), "row 1's pack_crc is not the pack header's (boot code 0: found when the meter is selected)", mb.E_PACK)
    add("pack_window.bin", _refix(_patch(_patch(v2, toc(1, 104), struct.pack("<I", 70000)), pk1 + 44, struct.pack("<I", 70000))), "row 1's pstate (70,000 bytes, TOC and pack header agree) does not fit the 64 KiB window", mb.E_WINDOW)
    add("pack_origin_slot.bin", mb.compose([mb.Entry(id=synth_id("origin_test"), name="Slot linked", version=(0, 7, 0), pack=slot_pack)], strict=False), "a pack linked for a slot, not for the window (boot code 0: found when the meter is selected)", mb.E_ORIGIN)
    pv = struct.unpack_from("<I", v2, toc(1, 112))[0]
    add("preview_not_56x32.bin", _refix(_patch(v2, pv + 64, bytes([v2[pv + 64] ^ 1]))), "row 1's run-length stream no longer covers 56 x 32 pixels", mb.E_PREVIEW)
    cu = [mb.Entry(id="aaaa1234" + synth_id("collide_a")[8:], name="Collide A", version=(0, 7, 0), pack=slot_pack_w), mb.Entry(id="bbbb1234" + synth_id("collide_b")[8:], name="Collide B", version=(0, 7, 0), pack=slot_pack_w),
          mb.Entry(id="cccc0001" + synth_id("collide_c")[8:], name="Alone", version=(0, 7, 0), pack=slot_pack_w)]
    out["v2_tuning_collision.bin"] = (mb.compose(cu), "a SOUND bin whose rows 0 and 1 share the low 16 bits of id_hash (1234): the firmware refuses tuning for those two rows (flags 1 1 0), nothing else changes", mb.E_OK)
    # v1
    n0 = struct.unpack_from("<I", v1, 8 + 8)[0]; second = 8 + 48 + n0
    n1 = struct.unpack_from("<I", v1, second + 8)[0]
    add("v1_zeroed_descriptor.bin", v1[:second + 48] + bytes(n1) + v1[second + 48 + n1:], "a v1 bundle whose second pack's image (descriptor too) is zero", mb.E_DESC)
    add("v1_duplicate_meter.bin", v1[:6] + struct.pack("<H", 2) + v1[8:8 + 48 + n0] + v1[8:8 + 48 + n0], "a v1 bundle with the first pack twice (one meter id twice)", mb.E_DUP)
    add("v1_truncated.bin", v1[:-3], "a v1 bundle cut short", mb.E_SIZE)
    return out


README = """# Golden vectors for the meter bin v2 (plan P2.1, for Tau Omega: O021/1)

Made by `tools/meter_bin_vectors.py` (which also writes this file), read by the independent Python reference `tools/meter_bin.py` and by the firmware's reader `fw2/cores/meterbin_core.h`; `sim/test_p2_meterbin.py` proves both give the results in `expected.json` (and each other's, on about 800 random bins). Format: `docs/features/meters/EXTERNAL_METERS_SPEC.md` sections 2, 3, 7. **HOST-ONLY: nothing here has run on a Pocket.**

## What is real and what is not
* The seven packs are the CURRENT firmware's packs (ABI 1.2), built once from the current source, linked for the window `0x24970000`, with the low 16 bits of the id hash as their header `meter id`. Their ids and names are the PERMANENT ones of `meters/<key>/meter.json` (B-1047).
* **The only synthetic ids** are the three rows of `v2_tuning_collision.bin` (last 8 digits `5e7e71c0`), built from the Winamp Bars pack.
* The v1 files (`v1_*.bin`) are for the **Python tool only** (inspecting old bundles): the firmware reads v2 alone and its reader answers `3 VERSION` to them. `v1_seven.bin` is `tau-packs.bin` as the firmware ships it today (packs linked for slots 0..6).

## Reading a bin (what the vectors pin)
Header **40 B** `{'TPKB', u16 2, u16 count, u32 toc_off = 40, toc_len, pv_off, pv_len, packs_off, total, toc_crc, 0}`, TOC rows of 256 B in display order, preview blocks (64-byte palette + run-length stream, padded to 4), packs (`TMPK`, padded to 4), u32 CRC-32 of bytes 40.. at the very end. `toc_crc` = CRC-32 of bytes `toc_off .. packs_off` (the TOC and the previews). `id_hash` = the id's first 8 hex digits read as a number, stored little endian. `pack_off`/`pack_size` = the whole `TMPK` (header included), `pack_crc` = the pack header's image CRC, `preview_off`/`preview_len` = absolute file offsets.
**Two checks, two codes.** The firmware at BOOT reads bytes `0 .. packs_off` only (header, TOC, previews) and checks `toc_crc` and the rows: `boot_code` in `expected.json`. The FULL check (tools, Omega after a write) adds the whole-file CRC and every pack's header against its row: `code`. A pack problem the boot path cannot see is found when the meter is SELECTED (the pack header against its row, then the image against `pack_crc`).
A refused bin gets ONE code (the first failing check) plus the row it was about:
`1 SIZE  2 MAGIC  3 VERSION  4 CRC (toc_crc)  5 COUNT  6 LAYOUT  7 ID  8 HASH  9 DUP  10 NAME  11 PACK  12 WINDOW  13 PREVIEW  14 RESERVED  15 ORIGIN  16 PARAMS  17 READ  19 FILECRC` (18 was the v1 descriptor, gone).
A sound bin may still have UNAVAILABLE rows: ABI major other than the firmware's or minor above it, or `tau_min` above the TAU version (`v2_unavailable_rows.bin` with a firmware at ABI 1.2, TAU 0.8: rows 1 and 2). And **tuning collisions**: rows sharing the low 16 bits of `id_hash` (`v2_tuning_collision.bin`: rows 0 and 1) get no tuning in the firmware.

## Files
`expected.json` lists every file with its size, SHA-256, `code`, `boot_code`, row count and which readers read it. Not filed: the 4,456,449-byte oversize bin (code 1 SIZE; 4,456,448 is the largest accepted): the test generates both.

| file | bytes | SHA-256 | full check | boot | what |
|---|---|---|---|---|---|
@ROWS@

### The packages the v2 bin was composed from (`meter.json` + `meter.bin`, in display order)
| package | id | SHA-256 of `meter.bin` |
|---|---|---|
@PKGS@

`python3 tools/meter_bin.py compose OUT.bin packages/<each, in this order> --fallback <the winamp_bars id>` reproduces `v2_seven.bin` byte for byte (the test checks it). The packages have only `meter.json` and `meter.bin`: `README.md` and `LICENSE` of the full `.tmeter` come with `tools/meter_package.py` (P2.6). The previews in the bins are the ones the packs' descriptors carry (the package has no authoritative `preview.png`). Binary-only (flag bit 2) is set when a package has no `source/` folder or `source` link: all seven here.

## Regenerating
`python3 tools/meter_bin_vectors.py` rebuilds the packs from the current source (needs the RISC-V toolchain) and rewrites everything including the SHA-256s; the damaged files are also reproducible without the toolchain from `v2_seven.bin` and `v1_seven.bin` (the test does it).
"""


def write_readme(out, exp):
    rows = "\n".join("| `%s` | %d | `%s` | %s | %s | %s |" % (v["file"], v["bytes"], v["sha256"], ("OK, %d rows" % v["count"]) if v["code"] == 0 else "%d %s%s" % (v["code"], v["code_name"], (" (row %d)" % v["bad_row"]) if v["bad_row"] >= 0 else ""),
                      "-" if v["boot_code"] is None else ("OK" if v["boot_code"] == 0 else "%d %s" % (v["boot_code"], v["boot_code_name"])), v["what"]) for v in exp["vectors"])
    pk = "\n".join("| `packages/%s/` | `%s` | `%s` |" % (n, json.loads((out / "packages" / n / "meter.json").read_text())["id"], sha((out / "packages" / n / "meter.bin").read_bytes())) for n in exp["package_order"])
    (out / "README.md").write_text(README.replace("@ROWS@", rows).replace("@PKGS@", pk))


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
        if e.id == real_id("vu_master"): e.flags &= ~mb.FLAG_VISIBLE
    fut = [mb.Entry(**vars(es[0])), mb.Entry(**vars(es[1])), mb.Entry(**vars(es[2]))]
    fut[1].pack = _patch(fut[1].pack, 4, struct.pack("<H", 0x0103)); fut[2].tau_min = 0x0009
    vs = {"v2_seven.bin": (seven, "the seven shipped meters in display order, Winamp Bars flagged as the fallback", mb.E_OK),
          "v2_reordered_hidden.bin": (mb.compose(rev), "the same seven in reverse order, VU Master hidden (visible flag clear): the TOC is the order", mb.E_OK),
          "v2_unavailable_rows.bin": (mb.compose(fut), "three meters: Winamp Bars, one needing ABI 1.3, one needing TAU 0.9 (a firmware at ABI 1.2, TAU 0.8 lists the last two as unavailable)", mb.E_OK),
          "v2_empty.bin": (mb.compose([]), "a sound bin with no meters (the player keeps Winamp Bars)", mb.E_OK),
          "v1_seven.bin": (v1, "the v1 bundle (tau-packs.bin as the firmware ships it today, packs linked for their slots) of the same seven meters", mb.E_OK)}
    slot_pack = [b for b in (v1[8 + 48 + struct.unpack_from("<I", v1, 16)[0]:],)][0]
    slot_pack = slot_pack[:48 + struct.unpack_from("<I", slot_pack, 8)[0]]
    dm = damaged(seven, v1, slot_pack, win["winamp_bars"])
    vectors = []
    for name, (b, why, code) in list(vs.items()) + list(dm.items()):
        (out / name).write_bytes(b)
        v1only = name.startswith("v1_")
        r = mb.report(b, 0x0102, 0x0008).splitlines()[0].split()
        assert int(r[1]) == code, (name, r, code)
        boot = int(mb.report(b, 0x0102, 0x0008, mode="toc").split()[1]) if not v1only else None
        vectors.append({"file": name, "bytes": len(b), "sha256": sha(b), "code": code, "code_name": mb.CODE_NAMES[code], "boot_code": boot, "boot_code_name": mb.CODE_NAMES[boot] if boot is not None else None,
                        "count": int(r[2]) if code == 0 else None, "bad_row": int(r[3]) if code else None, "readers": "python only (the firmware reads v2 alone)" if v1only else "python and C", "what": why})
    exp = {"about": "expected result of reading each file with a firmware at ABI 1.2 (0x0102) and TAU 0.8 (0x0008); `code` = the FULL check (whole-file CRC, every pack header: tools), `boot_code` = what the firmware finds at boot (header, TOC, previews only); codes are the table in fw2/cores/meterbin_core.h and tools/meter_bin.py", "fw_abi": "1.2", "fw_tau": "0.8",
           "v2_main": "v2_seven.bin", "package_order": [d.name for d in dirs], "fallback_id": fb, "synthetic_ids": "only the two Collide rows of v2_tuning_collision.bin", "vectors": vectors}
    (out / "expected.json").write_text(json.dumps(exp, indent=1) + "\n")
    write_readme(out, exp)
    print("%d vectors, %d packages written to %s" % (len(vectors), len(dirs), out))


if __name__ == "__main__":
    main()
