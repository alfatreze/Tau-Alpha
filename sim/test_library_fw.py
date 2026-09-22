#!/usr/bin/env python3
"""Runs the real firmware library core (fw/library_core.h, built by the vendored RISC-V toolchain) under tools/rv32sim.py
against indexes made by tools/tau_library.py, and compares every result with the Python reference reader:
E-codes over a corruption matrix, and hashes of all paths, titles, albums, artists, order tables, jump tables,
shuffle permutations (exact xorshift32 / Fisher-Yates) and album queues.
TAU_BIG=1 also runs the 7,180-track synthetic library (about 10 s)."""
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sim"))
import tau_library as L
import test_library_index as T

GCC = ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc"
fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {info}" if info and not cond else ""))


def build(out, hmem):
    cmd = [str(GCC), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles",
           "-Wall", "-Wextra", "-Wno-unused-function", f"-DHMEM={hmem}", "-Wl,--no-warn-rwx-segments",
           "-T", str(ROOT / "tools/host/link.ld"), str(ROOT / "tools/host/start.S"),
           str(ROOT / "tools/host/library_harness.c"), "-o", str(out)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or "warning" in r.stderr:
        print(r.stderr)
        raise SystemExit("harness build failed or warned")


def run(elf, blob, tmp):
    f = Path(tmp) / "idx.bin"
    f.write_bytes(blob)
    r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), str(elf), str(f)], capture_output=True, text=True)
    return [ln for ln in r.stdout.splitlines() if not ln.startswith("[")]


def fnv(bs):
    h = 2166136261
    for b in bs:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def le16(v):
    return bytes([v & 255, (v >> 8) & 255])


def expected(data):
    ix = L.parse(data)
    n = ix.n
    exp = {}
    exp["LOAD"] = f"LOAD OK {n['tracks']} {n['albums']} {n['artists']} {n['playlists']} 0x{ix.build_id:08X}"
    exp["HEADER"] = "HEADER 0x%08X" % fnv(data[:128])
    exp["PATHS"] = "PATHS 0x%08X" % fnv(b"".join(L.track_path(ix, i).encode() + b"\n" for i in range(n["tracks"])))
    tb = b""
    for t in ix.tracks:
        tb += ix.str(t[0]).encode() + b"\n" + le16(t[2]) + le16(t[3]) + le16(t[4]) + bytes([t[5]])
    exp["TRACKS"] = "TRACKS 0x%08X" % fnv(tb)
    ab = b""
    for a in ix.albums:
        ab += ix.str(a[0]).encode() + b"\n" + ix.str(a[1]).encode() + b"\n" + le16(a[2]) + le16(a[3]) + le16(a[4]) + le16(a[5])
    exp["ALBUMS"] = "ALBUMS 0x%08X" % fnv(ab)
    rb = b""
    for a in ix.artists:
        rb += ix.str(a[0]).encode() + b"\n" + le16(a[1]) + le16(a[2])
    exp["ARTISTS"] = "ARTISTS 0x%08X" % fnv(rb)
    ao, _ = ix.sec["album_by_title"]
    to, _ = ix.sec["track_by_title"]
    ob = data[ao:ao + 2 * n["albums"]] + data[to:to + 2 * n["tracks"]]
    exp["ORDER"] = "ORDER 0x%08X" % fnv(ob)
    lo, _ = ix.sec["letters"]
    exp["LETTERS"] = "LETTERS 0x%08X" % fnv(data[lo:lo + 162])
    lb = b""
    for nm, first, cnt in ix.lists:
        lb += ix.str(nm).encode() + b"\n" + le16(cnt) + b"".join(le16(t) for t in ix.items[first:first + cnt])
    exp["LISTS"] = "LISTS 0x%08X" % fnv(lb)
    if ix.lists:
        f0, fl = ix.lists[0], ix.lists[-1]
        qb = b"".join(le16(t) for t in ix.items[f0[1]:f0[1] + f0[2]]) + b"".join(le16(t) for t in ix.items[fl[1]:fl[1] + fl[2]])
        exp["QLIST"] = f"QLIST {f0[2]} {fl[2]} hash 0x%08X" % fnv(qb)
    jb = b""
    for v, (cnt, name_at) in enumerate(((n["artists"], lambda p: ix.str(ix.artists[p][0])),
                                        (n["albums"], lambda p: ix.str(ix.albums[struct.unpack_from("<H", data, ix.sec["album_by_title"][0] + 2 * p)[0]][0])),
                                        (n["tracks"], lambda p: ix.str(ix.tracks[struct.unpack_from("<H", data, ix.sec["track_by_title"][0] + 2 * p)[0]][0])))):
        cls = [L.sort_key(name_at(p))[0] for p in range(cnt)]          # independent of the jump table
        for p in range(0, cnt, 7 if cnt > 300 else 1):
            nxt = next((q for q in range(p + 1, cnt) if cls[q] > cls[p]), p)
            start = next(q for q in range(cnt) if cls[q] == cls[p])
            if start < p:
                prv = start
            else:
                prev_cls = [c for c in set(cls) if c < cls[p]]
                prv = next(q for q in range(cnt) if cls[q] == max(prev_cls)) if prev_cls else p
            jb += le16(prv) + le16(nxt)
    exp["JUMPS"] = "JUMPS 0x%08X" % fnv(jb)
    for seed in (1, 12345):
        q = list(range(n["tracks"]))
        s = seed
        for i in range(n["tracks"], 1, -1):
            x = s or 0x9E3779B9
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= x >> 17
            x ^= (x << 5) & 0xFFFFFFFF
            s = x
            j = x % i
            q[i - 1], q[j] = q[j], q[i - 1]
        exp[f"SHUF {seed}"] = f"SHUF {seed} dup 0 hash 0x%08X" % fnv(b"".join(le16(v) for v in q))
    if n["albums"]:
        a0, al = ix.albums[0], ix.albums[-1]
        qb = b"".join(le16(a0[4] + i) for i in range(a0[5])) + b"".join(le16(al[4] + i) for i in range(al[5]))
        exp["QALB"] = f"QALB {a0[5]} {al[5]} hash 0x%08X" % fnv(qb)
    return exp


def compare(name, data, elf, tmp):
    out = run(elf, data, tmp)
    exp = expected(data)
    got = {}
    for ln in out:
        for k in ("LOAD", "HEADER", "PATHS", "TRACKS", "ALBUMS", "ARTISTS", "ORDER", "LETTERS", "JUMPS", "LISTS", "QLIST", "SHUF 1 ", "SHUF 12345", "QALB"):
            if ln.startswith(k):
                got[k.strip() if not k.startswith("SHUF") else k.strip()] = ln
    okall = True
    for k, v in exp.items():
        g = got.get(k)
        if g != v:
            okall = False
            print(f"   mismatch {k}: firmware {g!r} vs reference {v!r}")
    check(name, okall and not any(ln.startswith("PATHFAIL") and ln != "PATHFAIL 0" for ln in out))


def code_of(data):
    try:
        L.parse(data)
        return 0
    except L.LibError as e:
        return e.code


def main():
    if not GCC.exists():
        print("SKIP: vendored RISC-V toolchain not found; firmware library core not run")
        return 0
    with tempfile.TemporaryDirectory() as td:
        small, big = Path(td) / "small.elf", Path(td) / "big.elf"
        build(small, 1 << 16)
        build(big, 3 << 19)
        root = Path(td) / "common"
        T.tree(root)
        entries, pls, _ = L.scan(root, playlists=True)
        d1 = L.build_index(entries, pls, [])
        compare("small tree: every readback matches the reference (paths, titles, albums, artists, order, letters, shuffle, queues)",
                d1, small, td)
        ix = L.parse(d1)
        tr_off = ix.sec["tracks"][0]

        def flip(off, data=d1):
            b = bytearray(data)
            b[off] ^= 0xFF
            return bytes(b)

        def seal_hdr(b):
            b = bytearray(b)
            struct.pack_into("<I", b, 124, zlib.crc32(bytes(b[:124])) & 0xFFFFFFFF)
            return bytes(b)

        def seal_all(b):
            b = bytearray(b)
            c = zlib.crc32(bytes(b[128:])) & 0xFFFFFFFF
            struct.pack_into("<I", b, 24, c)
            struct.pack_into("<I", b, 16, c)
            return seal_hdr(bytes(b))
        swapped = bytearray(d1)
        cases = [
            ("bad magic", flip(0)), ("header CRC", flip(33)), ("version above reader", seal_hdr(T._set16(d1, 6, 3))),
            ("truncated", d1[:-16]), ("appended", d1 + b"\x00" * 16), ("short file", d1[:64]),
            ("body flip", flip(tr_off + 3)), ("string flip", flip(ix.sec["strings"][0] + 5)),
            ("count above cap", seal_hdr(T._set16(d1, 34, 2049))),
            ("section out of range", seal_hdr(T._set32(d1, 48 + 16, len(d1) + 16))),
            ("section misaligned", seal_hdr(T._set32(d1, 48 + 16, tr_off + 4))),
            ("track string offset", seal_all(T._set32(d1, tr_off, 0x7FFFFF))),
            ("track album id", seal_all(T._set16(d1, tr_off + 12, 999))),
            ("playlist section length odd", seal_all(T._set32(d1, 48 + 8 * 7 + 4, ix.sec["playlists"][1] + 1))),
            ("playlist range past its items", seal_all(T._set16(d1, ix.sec["playlists"][0] + 6, 999))),
            ("playlist item is not a track", seal_all(T._set16(d1, ix.sec["playlists"][0] + 8 * ix.n["playlists"], 999))),
        ]
        for name, blob in cases:
            want = code_of(blob)
            out = run(small, blob, td)
            check(f"corruption '{name}': firmware E-code equals reference (E{want})", out and out[0] == f"LOAD E{want}", out[:1])
        out = run(small, b"", td)
        check("empty slot -> E10", out and out[0] == "LOAD E10", out[:1])
        mixes = [dict(name="Mix A", rel_ids=list(range(0, 400, 7))), dict(name="Mix B", rel_ids=[5, 3, 5, 399])]
        syn = L.build_index(L.synth(400, 40, 20), mixes)
        big_idx = L.build_index(L.synth(400, 40, 20), mixes)
        check("synthetic 400-track index verifies", L.verify(big_idx) == [])
        compare("400-track synthetic: readbacks match", syn, big, td)
        toobig = L.build_index(L.synth(16384, 2048, 1024))
        out = run(small, toobig, td)
        check("index larger than the memory given -> E14 (cannot be held)", out and out[0] == "LOAD E14", out[:1])
        if os.environ.get("TAU_BIG"):
            compare("7,180-track synthetic library: readbacks match", L.build_index(L.synth(7180, 800, 300)), big, td)
    print("FAILED: " + ", ".join(fails) if fails else "all firmware library core tests passed")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
