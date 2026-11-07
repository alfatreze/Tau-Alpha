#!/usr/bin/env python3
"""The firmware reader of the name sidecar (fw2/cores/names_core.h, the real code, host build sim/p2_names_harness.c with ASan/UBSan) against the
reference (tools/name_sidecar.py): every field of a corpus-built table answers exactly what the display rule says at Latin-1 coverage, every
way a file can be wrong gives its load code (and never a crash), malformed UTF-8 never reaches the screen, and 400 random corruptions are survived."""
import random
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "sim"))
import name_sidecar as NS
import toolchain
import name_corpus as C

fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {str(info)[:300]}" if info and not cond else ""))


E = dict(NOFILE=20, HDR=21, BIND=22, COUNT=23, LAYOUT=24, SIZE=25, STR=26, TOOBIG=27)
BID = 0x1234ABCD

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    exe = td / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-function", "-fsanitize=address,undefined", "-fno-sanitize-recover=undefined",
                        "-o", str(exe), str(ROOT / "sim/p2_names_harness.c")], env=toolchain.host_env(), capture_output=True, text=True)
    if r.returncode or "warning" in r.stderr:
        print(r.stderr[-1500:]); sys.exit("harness build failed or warned")

    def run(blob, bid=BID, counts=None, cap=512, name="n.bin"):
        p = td / name
        if blob is not None:
            p.write_bytes(blob)
        elif p.exists():
            p.unlink()
        nt, na, nr = counts
        o = subprocess.run([str(exe), str(p), str(bid), str(nt), str(na), str(nr), str(cap)], capture_output=True, text=True)
        if o.returncode:
            return None, o.stderr[-400:] or o.stdout[-200:]
        lines = o.stdout.splitlines()
        return int(lines[0].split()[1]), lines[1:]

    # ---------------------------------------------------------------- the table: corpus tags plus hand-picked displays
    extra = ["Café", "naïve ½", "ÿþ", "Ελλάδα", "Party \U0001f389", "日本語", "Café 東",
             "soft­hyphen", "nb sp", "curly ’ quote", "A" * 127, "¡Hola!", "x×y ÷ z", "¤¥¦", "é decomposed"]
    texts = [t for c in C.CASES if c.get("tags") for t in c["tags"] if t] + extra
    tracks = []
    for i, t in enumerate(texts):
        tracks.append({"file": f"file {i}.mp3", "title": t, "artist": texts[(i + 3) % len(texts)] if i % 2 else None, "album": texts[(i + 7) % len(texts)] if i % 3 == 0 else None})
    albums = [(texts[i], texts[(i + 1) % len(texts)] if i % 2 else None) for i in range(6)]
    artists = [texts[i + 2] for i in range(5)] + [None]
    counts = (len(tracks), len(albums), len(artists))
    blob = NS.build_bin(BID, tracks, albums, artists)
    ref = NS.parse_bin(blob, BID, counts)

    code, lines = run(blob, counts=counts)
    check("a good file loads", code == 0, (code, lines))
    got = {}
    for ln in lines:
        w = ln.split()
        if w[0] == "ranges":
            got["ranges"] = w[1:]
        else:
            got[(w[0], int(w[1]), int(w[2]))] = (int(w[3]), w[4] if len(w) > 4 else "", w[5:] )
    exp, bad, n_drawn, n_fallback = {}, [], 0, 0
    def want(text):
        if not text: return None
        return text.encode("latin-1").hex() if all(NS.cov_latin1(ch) for ch in text) else "-"
    fields = [("T", i, f, ref["tracks"][i][k]) for i in range(len(tracks)) for f, k in enumerate(("file", "title", "artist", "album"))]
    fields += [("A", i, f, ref["albums"][i][f]) for i in range(len(albums)) for f in range(2)]
    fields += [("R", i, 0, ref["artists"][i]) for i in range(len(artists))]
    for k, i, f, text in fields:
        w = want(text)
        g = got.get((k, i, f))
        if w is None:
            if g is not None: bad.append((k, i, f, "present but should be absent"))
            continue
        if g is None: bad.append((k, i, f, "missing")); continue
        if w == "-":
            n_fallback += 1
            if g[0] != 0 or g[1] != "-": bad.append((k, i, f, text, g))
        else:
            n_drawn += 1
            if g[0] != len(w) // 2 or g[1] != w: bad.append((k, i, f, text, g))
    check(f"every field answers the display rule at Latin-1 coverage ({n_drawn} drawn, {n_fallback} left to the index text)", not bad, bad[:3])
    check("both outcomes are exercised", n_drawn > 50 and n_fallback > 20, (n_drawn, n_fallback))
    check("an undrawable field leaves the caller's buffer untouched", all("TOUCHED" not in " ".join(g[2]) for g in got.values() if isinstance(g, tuple)))
    check("outputs are NUL-terminated", all("NOTERM" not in " ".join(g[2]) for g in got.values() if isinstance(g, tuple)))
    check("ids and fields out of range give nothing", got.get("ranges") == ["0", "0", "0"], got.get("ranges"))
    spec = {t: want(t) for t in ("Café 東", "soft­hyphen", "nb sp", "curly ’ quote", "ÿþ")}
    check("a mixed string, a soft hyphen, a no-break space and a curly quote all fall back whole", [spec[t] for t in ("Café 東", "soft­hyphen", "nb sp", "curly ’ quote")] == ["-"] * 4 and spec["ÿþ"] is not None)

    # ---------------------------------------------------------------- the output cap
    code, lines = run(blob, counts=counts, cap=5)
    cut = [ln.split() for ln in lines if ln.startswith("T ") and ln.split()[3] != "0"]
    check("a small buffer is filled to cap-1 characters and still terminated", cut and all(int(w[3]) <= 4 and "NOTERM" not in w for w in cut), cut[:2])
    code, lines = run(blob, counts=counts, cap=1)
    check("a buffer of one byte is never written", code == 0 and all(ln.split()[3] == "0" for ln in lines if ln.startswith("T ")))

    # ---------------------------------------------------------------- load codes, one per way a file can be wrong
    flip = lambda b, i: b[:i] + bytes([b[i] ^ 0xFF]) + b[i + 1:]
    def fix_hcrc(b):
        b = bytearray(b); struct.pack_into("<I", b, 32, zlib.crc32(bytes(b[:32])) & 0xFFFFFFFF); return bytes(b)
    def put(b, off, fmt, v):
        b = bytearray(b); struct.pack_into(fmt, b, off, v); return bytes(b)
    o_trk = 48
    o_alb = (o_trk + 16 * counts[0] + 15) & ~15
    o_art = (o_alb + 8 * counts[1] + 15) & ~15
    o_pool, n_pool = struct.unpack_from("<II", blob, 24)
    cases = [
        ("no file", None, E["NOFILE"], counts, BID),
        ("an empty file", b"", E["NOFILE"], counts, BID),
        ("bad magic", flip(blob, 0), E["HDR"], counts, BID),
        ("a damaged header (crc)", flip(blob, 13), E["HDR"], counts, BID),
        ("a newer version", fix_hcrc(put(blob, 4, "<H", 9)), E["HDR"], counts, BID),
        ("a file shorter than its header", blob[:30], E["HDR"], counts, BID),
        ("a sidecar for another index (rebuilt library)", blob, E["BIND"], counts, BID ^ 1),
        ("wrong track count", blob, E["COUNT"], (counts[0] + 1, counts[1], counts[2]), BID),
        ("wrong album count", blob, E["COUNT"], (counts[0], counts[1] - 1, counts[2]), BID),
        ("wrong artist count", blob, E["COUNT"], (counts[0], counts[1], counts[2] + 1), BID),
        ("a bad pool offset", fix_hcrc(put(blob, 24, "<I", o_pool + 16)), E["LAYOUT"], counts, BID),
        ("an empty pool", fix_hcrc(put(blob, 28, "<I", 0)), E["LAYOUT"], counts, BID),
        ("a pool over the 1 MiB cap", fix_hcrc(put(blob, 28, "<I", 2 << 20)), E["TOOBIG"], counts, BID),
        ("a truncated file", blob[:-40], E["SIZE"], counts, BID),
        ("a file longer than its header says (the extra bytes are ignored: no read past the end)", blob + b"\x00" * 16, 0, counts, BID),
        ("an unterminated pool", blob[:-1] + b"x", E["STR"], counts, BID),
        ("a pool that does not start with NUL", blob[:o_pool] + b"x" + blob[o_pool + 1:], E["STR"], counts, BID),
        ("a track offset past the pool", put(blob, o_trk + 4, "<I", n_pool + 5), E["STR"], counts, BID),
        ("a track offset into the middle of a string", put(blob, o_trk + 4, "<I", 2), E["STR"], counts, BID),
        ("an album offset into the middle of a string", put(blob, o_alb, "<I", 2), E["STR"], counts, BID),
        ("an artist offset past the pool", put(blob, o_art, "<I", n_pool), E["STR"], counts, BID),
    ]
    for label, b, want_code, cnt, bid in cases:
        code, lines = run(b, bid=bid, counts=cnt, name="c.bin")
        check(f"load code: {label}", code == want_code, (code, lines))
    # Python and C agree on accept/reject for the same cases
    agree = []
    for label, b, want_code, cnt, bid in cases:
        if b is None or label.startswith("a file longer than its header"):      # the tool's reader is strict about the size; the firmware ignores trailing bytes (no read past the end)
            continue
        try:
            NS.parse_bin(b, bid, cnt); py_ok = True
        except NS.SidecarError:
            py_ok = False
        agree.append(py_ok == (want_code == 0))
        if not agree[-1]: print('  disagree:', label, py_ok)
    check("the reference reader and the firmware core reject the same files", all(agree))

    # ---------------------------------------------------------------- malformed UTF-8 never reaches the screen
    one = NS.build_bin(BID, [{"title": "abc", "file": "f"}], [], [])
    cnt1 = (1, 0, 0)
    po = struct.unpack_from("<I", one, 24)[0]
    toff = struct.unpack_from("<I", one, 48 + 4)[0]
    assert one[po + toff:po + toff + 4] == b"abc\x00"
    badseq = {
        "a stray continuation byte": b"a\x80b", "an overlong NUL (C0 80)": b"\xc0\x80b", "an overlong slash (E0 80 AF)": b"\xe0\x80\xafb",
        "a truncated 3-byte sequence": b"\xe6\x9d", "a UTF-16 surrogate (ED A0 80)": b"\xed\xa0\x80", "above U+10FFFF (F4 90 80 80)": b"\xf4\x90\x80\x80",
        "a lead byte F8": b"\xf8\x88\x80\x80\x80", "0xFF": b"\xffz",
    }
    for label, seq in badseq.items():
        b = bytearray(one)
        seq = (seq + b"\x00" * 3)[:3] if len(seq) < 3 else seq[:3]
        b[po + toff:po + toff + 3] = seq if len(seq) == 3 else seq + b"b" * (3 - len(seq))
        code, lines = run(bytes(b), counts=cnt1, name="u.bin")
        check(f"malformed UTF-8 is not drawn: {label}", code == 0 and any(ln.startswith("T 0 1 0 -") and "TOUCHED" not in ln for ln in lines), (code, lines))
    good = bytearray(one); good[po + toff:po + toff + 3] = "é".encode("utf-8") + b"x"
    code, lines = run(bytes(good), counts=cnt1, name="u.bin")
    check("a valid two-byte character is drawn as one Latin-1 byte", any(ln.startswith("T 0 1 2 e978") for ln in lines), lines)

    # ---------------------------------------------------------------- 400 random corruptions: never a crash, always a known load code
    rnd = random.Random(437)
    survived, codes = 0, set()
    for k in range(400):
        b = bytearray(blob)
        for _ in range(rnd.choice((1, 1, 2, 5))):
            b[rnd.randrange(len(b))] = rnd.randrange(256)
        if k % 10 == 0:
            b = b[:rnd.randrange(len(b))]
        code, lines = run(bytes(b), counts=counts, name="f.bin")
        if code is not None and (code == 0 or code in E.values()):
            survived += 1; codes.add(code)
        else:
            print("  crash or unknown code on mutation", k, code, str(lines)[:200])
    check("400 random corruptions: no sanitizer fault, only known load codes", survived == 400, survived)
    check("the corruptions reach several different load codes", len(codes) >= 4, codes)

if fails:
    print("FAILED:", fails)
    sys.exit(1)
print("PASSED")
