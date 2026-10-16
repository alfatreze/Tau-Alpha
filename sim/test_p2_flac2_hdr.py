#!/usr/bin/env python3
"""Player 2 FLAC core piece 3 (fw2/cores/flac2_core.h): CRC-8, CRC-16 (nibble tables), STREAMINFO and the frame header, against an INDEPENDENT RFC 9639 writer and bitwise CRCs in this file.
 * CRC-8 / CRC-16 equal the bitwise definitions on random data, CRC-16 identical however the data is split (state carried between pieces) and for the empty input;
 * every valid header combination decodes to what was written: all 15 block-size codes (fixed, 8-bit and 16-bit explicit), every rate code including the three explicit forms, all 11 channel assignments,
   every sample-size code, fixed and variable block size with coded numbers of 1..7 bytes (up to 36 bits);
 * a PREFIX of a valid header returns NEED, never ERR, and the full header returns the same hdr_len however many extra bytes follow;
 * every single bit flip of a valid header is rejected (CRC-8 detects all single-bit errors; flips of the sync, reserved and code bits are caught earlier);
 * reserved codes, a STREAMINFO mismatch (rate, channel count, bit depth), a block size over the maximum and bad UTF-8 continuation bytes are refused with the right reason;
 * mutants: CRC-16 table entry, 7-byte numbers refused.
"""
import os, random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
def crc8(d):
    c = 0
    for b in d:
        c ^= b
        for _ in range(8): c = ((c << 1) ^ 7) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c
def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8): c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c
def utf8num(v):
    if v < 0x80: return bytes([v])
    for n, (lim, lead) in enumerate([(1 << 11, 0xC0), (1 << 16, 0xE0), (1 << 21, 0xF0), (1 << 26, 0xF8), (1 << 31, 0xFC), (1 << 36, 0xFE)], start=2):
        if v < lim:
            out = []
            for _ in range(n - 1): out.append(0x80 | (v & 0x3F)); v >>= 6
            return bytes([lead | v]) + bytes(reversed(out))
    raise ValueError
def streaminfo(min_bs, max_bs, rate, ch, bps, total):
    b = min_bs.to_bytes(2, "big") + max_bs.to_bytes(2, "big") + b"\0\0\0" + b"\0\0\0"
    v = (rate << 44) | ((ch - 1) << 41) | ((bps - 1) << 36) | total
    return b + v.to_bytes(8, "big") + bytes(16)
BS = {1: 192, 2: 576, 3: 1152, 4: 2304, 5: 4608, 8: 256, 9: 512, 10: 1024, 11: 2048, 12: 4096, 13: 8192, 14: 16384, 15: 32768}
RATE = {1: 88200, 2: 176400, 3: 192000, 4: 8000, 5: 16000, 6: 22050, 7: 24000, 8: 32000, 9: 44100, 10: 48000, 11: 96000}
BPS = {1: 8, 2: 12, 4: 16, 5: 20, 6: 24, 7: 32}
def write_header(variable, bsc, rc, ac, sc, num, bsx=None, rx=None):
    h = bytes([0xFF, 0xF8 | variable, (bsc << 4) | rc, (ac << 4) | (sc << 1)]) + utf8num(num)
    if bsc == 6: h += bytes([bsx - 1])
    if bsc == 7: h += (bsx - 1).to_bytes(2, "big")
    if rc == 12: h += bytes([rx // 1000])
    if rc == 13: h += rx.to_bytes(2, "big")
    if rc == 14: h += (rx // 10).to_bytes(2, "big")
    return h + bytes([crc8(h)])
def build(d, bug):
    exe = d / f"h{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_flac2_hdr_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, lines):
    out = subprocess.run([str(exe)], input="".join(l + "\n" for l in lines), capture_output=True, text=True, timeout=600).stdout.splitlines()
    return [tuple(map(int, o.split())) for o in out]
H = lambda si, hdr, av=None: f"H {si.hex()} {hdr.hex()} {len(hdr) if av is None else av}"
def suite(exe):
    rng = random.Random(5); ok = {}
    # CRCs
    cases = [bytes(rng.randrange(256) for _ in range(rng.randrange(0, 200))) for _ in range(300)]
    r8 = run(exe, [f"C8 {c.hex() or '-'}" for c in cases]); ok["crc8"] = [x[0] for x in r8] == [crc8(c) for c in cases]
    sp = [rng.choice([1, 2, 3, 7, 1000]) for _ in cases]; init = [rng.choice([0, 0, 0x1234, 0xFFFF]) for _ in cases]
    r16 = run(exe, [f"C16 {i} {c.hex() or '-'} {s}" for c, s, i in zip(cases, sp, init)]); ok["crc16"] = [x[0] for x in r16] == [crc16(c, i) for c, i in zip(cases, init)]
    # STREAMINFO
    si_list = [(rng.choice([16, 192, 1152, 4096]), 0, rng.choice([8000, 44100, 48000, 96000, 192000, 655350]), rng.randrange(1, 9), rng.randrange(4, 33), rng.randrange(0, 1 << 36)) for _ in range(200)]
    si_list = [(a, max(a, rng.choice([a, 4608, 32768])), c, d_, e, f) for a, _, c, d_, e, f in si_list]
    rs = run(exe, [f"SI {streaminfo(*s).hex()}" for s in si_list])
    ok["streaminfo"] = all(r == (0, s[2], s[3], s[4], s[5], s[0], s[1]) for r, s in zip(rs, si_list))
    bad_si = [streaminfo(8, 4096, 44100, 2, 16, 0), streaminfo(4096, 2048, 44100, 2, 16, 0), streaminfo(1152, 1152, 0, 2, 16, 0)]
    ok["streaminfo_refusals"] = [r[0] for r in run(exe, [f"SI {b.hex()}" for b in bad_si])] == [2, 2, 2]
    # headers
    base = streaminfo(16, 32768, 44100, 2, 16, 1000000); good = []; lines = []
    for variable in (0, 1):
        for bsc in list(BS) + [6, 7]:
            for _ in range(3):
                rc = rng.choice([0, 9, 12, 13, 14]); ac = rng.choice([1, 8, 9, 10]); sc = rng.choice([0, 4]); nbits = rng.choice([7, 11, 16, 21, 26, 31, 36 if variable else 31])
                num = rng.randrange(1 << (nbits - 1), 1 << nbits) if nbits > 7 else rng.randrange(0, 128)
                bsx = rng.randrange(16, 32769) if bsc in (6, 7) else None
                if bsc == 6: bsx = rng.randrange(16, 257)
                rate = {0: 44100, 9: 44100, 12: 44000, 13: 44100, 14: 44100}[rc]
                si = streaminfo(16, 32768, rate, 2, 16, 1000000)
                hdr = write_header(variable, bsc, rc, ac, sc, num, bsx, rate)
                want_bs = bsx if bsc in (6, 7) else BS[bsc]
                good.append((si, hdr, (variable, want_bs, num, ac))); lines.append(H(si, hdr))
    rg = run(exe, lines); okh = True; badh = None
    for r, (si, hdr, (var, bs, num, ac)) in zip(rg, good):
        want = (0, 0, len(hdr), bs, None, num, var, ac, 2, 16)
        if (r[0], r[1], r[2], r[3], r[5], r[6], r[7], r[8], r[9]) != (want[0], want[1], want[2], want[3], want[5], want[6], want[7], want[8], want[9]): okh = False; badh = (r, want, hdr.hex()); break
    ok["headers"] = okh; ok["headers_bad"] = badh
    # all rate and size codes in a header with explicit STREAMINFO match
    allrc = []
    for rcode, rate in RATE.items():
        si = streaminfo(16, 4608, rate, 2, 16, 0); allrc.append((si, write_header(0, 5, rcode, 1, 4, 3), rate))
    for scode, b in BPS.items():
        si = streaminfo(16, 4608, 44100, 2, b, 0); allrc.append((si, write_header(0, 5, 9, 1, scode, 3), 44100))
    for ac in range(0, 11):
        si = streaminfo(16, 4608, 44100, ac + 1 if ac < 8 else 2, 16, 0); allrc.append((si, write_header(0, 5, 9, ac, 4, 3), 44100))
    ra = run(exe, [H(si, h) for si, h, _ in allrc]); ok["codes"] = all(r[0] == 0 and r[4] == rate for r, (_, _, rate) in zip(ra, allrc))
    # prefixes
    pl = []
    for si, hdr, _ in good[:40]:
        for n in range(0, len(hdr)): pl.append(H(si, hdr, n))
    rp = run(exe, pl); ok["prefix"] = all(r[0] == 1 for r in rp)
    # trailing bytes do not change the result
    ok["trailing"] = all(run(exe, [H(si, hdr + bytes(rng.randrange(256) for _ in range(9)))])[0][2] == len(hdr) for si, hdr, _ in good[:30])
    # single bit flips
    fl = []
    for si, hdr, _ in good[:50]:
        for bit in range(len(hdr) * 8): x = bytearray(hdr); x[bit // 8] ^= 1 << (bit % 8); fl.append(H(si, bytes(x)))
    rf = run(exe, fl); ok["bitflips"] = all(r[0] != 0 for r in rf); ok["bitflips_n"] = len(rf)
    # refusals with reasons: F2E_SYNC 1, RESERVED 2, CRC8 3, MISMATCH 4, BLOCKSIZE 5, NUMBER 6
    si = streaminfo(16, 4608, 44100, 2, 16, 0); ref = []
    def fix(hb):
        hb = bytearray(hb); hb[-1] = crc8(bytes(hb[:-1])); return bytes(hb)
    g = write_header(0, 5, 9, 1, 4, 3)
    ref.append((bytes([0xFE]) + g[1:], 1)); ref.append((bytes([0xFF, 0xF0]) + g[2:], 1))
    ref.append((fix(bytes([0xFF, 0xF8, 0x09 | 0x00, g[3]]) + g[4:]), 2))          # block size code 0 reserved
    ref.append((fix(bytes([g[0], g[1], 0x5F, g[3]]) + g[4:]), 2))                  # rate code 15
    ref.append((fix(bytes([g[0], g[1], g[2], 0xB8]) + g[4:]), 2))                  # channel assignment 11
    ref.append((fix(bytes([g[0], g[1], g[2], 0x16]) + g[4:]), 2))                  # sample size code 3
    ref.append((fix(bytes([g[0], g[1], g[2], g[3] | 1]) + g[4:]), 2))              # reserved bit
    ref.append((g[:-1] + bytes([g[-1] ^ 0x55]), 3))
    ref.append((write_header(0, 5, 10, 1, 4, 3), 4))                                # rate 48000 against 44100
    ref.append((write_header(0, 5, 9, 0, 4, 3), 4))                                 # one channel against two
    ref.append((write_header(0, 5, 9, 1, 6, 3), 4))                                 # 24-bit against 16
    ref.append((write_header(0, 15, 9, 1, 4, 3), 5))                                # 32768 over max 4608
    ref.append((fix(bytes([g[0], g[1], g[2], g[3], 0x80]) + g[5:]), 6))             # a continuation byte first
    ref.append((fix(bytes([g[0], g[1], g[2], g[3], 0xFF]) + g[5:]), 6))             # 0xFF
    ref.append((fix(bytes([g[0], g[1], g[2], g[3], 0xC2, 0x20]) + g[5:]), 6))       # bad continuation
    rr = run(exe, [H(si, h) for h, _ in ref]); ok["refusals"] = [(r[0], r[1]) for r in rr] == [(2, e) for _, e in ref]; ok["refusals_got"] = [(r[0], r[1]) for r in rr]
    return ok
with tempfile.TemporaryDirectory() as td:
    d = Path(td); ok = suite(build(d, 0))
    check("CRC-8 equals the bitwise definition on 300 random buffers", ok["crc8"])
    check("CRC-16 equals the bitwise definition, however the data is split, any initial value, empty input", ok["crc16"])
    check("STREAMINFO fields decode to what was written (200 random), and block sizes under 16, max under min, a zero rate are refused", ok["streaminfo"] and ok["streaminfo_refusals"])
    check("every header combination decodes to what was written (fixed and variable, 15 block-size codes incl. 8/16-bit explicit, 5 rate forms, 4 assignments, numbers of 1..7 bytes)", ok["headers"], ok["headers_bad"])
    check("every rate code, sample-size code and channel assignment decodes against a matching STREAMINFO", ok["codes"])
    check("a prefix of a valid header returns NEED, never ERR", ok["prefix"])
    check("bytes after the header do not change the result", ok["trailing"])
    check(f"every single bit flip of a valid header is rejected ({ok['bitflips_n']} flips)", ok["bitflips"])
    check("reserved codes, bad CRC-8, STREAMINFO mismatches, block size over the maximum and bad coded numbers are refused with the right reason", ok["refusals"], ok["refusals_got"])
    for bug, name in ((10, "one CRC-16 table entry wrong"), (11, "7-byte coded numbers refused")):
        o = suite(build(d, bug)); caught = not all(v for k, v in o.items() if isinstance(v, bool))
        check(f"mutant {bug} ({name}) is caught", caught)
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
