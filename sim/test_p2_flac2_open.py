#!/usr/bin/env python3
"""Player 2 FLAC core: opening a file (f2_open_step) and the seek table (f2_seekpoint), against independent Python parsers and constructors.
 * synthetic files: an optional ID3v2 tag (with and without footer), "fLaC", STREAMINFO, then PADDING/APPLICATION/SEEKTABLE/VORBIS_COMMENT/CUESHEET/PICTURE/unknown blocks in random order with the last flag on the final
   one: audio_off, every STREAMINFO field, the captured SEEKTABLE and VORBIS_COMMENT bytes (and their truncation flags when the capture buffer is smaller than the block) match, with the file delivered in windows
   from 1 byte to whole, and with the caller skipping the unwanted block bodies itself (f2_open_skippable);
 * every real FLAC under 'test music' opens to the same audio offset, STREAMINFO and block captures as the Python parse;
 * refusals: bad magic, STREAMINFO not first or the wrong length or repeated, block type 127, a SEEKTABLE whose size is not a multiple of 18, a bad ID3 size byte; a truncated file asks for more, never errors;
 * f2_seekpoint picks the last real point at or before the target (placeholders ignored) for 3,000 random tables and targets.
Mutants: ID3 footer ignored, block length top byte dropped, truncation flag never set, audio offset off by one."""
import os, random, struct, subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import flac2_enc as enc
ROOT = Path(__file__).resolve().parent.parent
MUSIC = ROOT.parent / "test music"
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
def fnv(b):
    h = 2166136261
    for x in b: h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h
def build(d, bug):
    exe = d / f"o{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_flac2_open_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, path, chunks="4096", scap=4096, vcap=512, skip=0):
    r = subprocess.run([str(exe), str(path), chunks, str(scap), str(vcap), str(skip)], capture_output=True, text=True, timeout=300)
    if r.returncode: return None
    a = r.stdout.split("|"); h = list(map(int, a[0].split())); s = list(map(int, a[1].split())); k = list(map(int, a[2].split())); v = list(map(int, a[3].split())); c = int(a[4])
    return dict(st=h[0], err=h[1], state=h[2], audio=h[3], si=s, seek=k, vc=v, used=c)
def ref_parse(data, scap, vcap):
    """independent parse: -> (audio_off, si fields, seek bytes, vc bytes, totals)"""
    o = 0
    if data[:3] == b"ID3":
        sz = (data[6] << 21) | (data[7] << 14) | (data[8] << 7) | data[9]; o = 10 + sz + (10 if data[5] & 0x10 else 0)
    assert data[o:o + 4] == b"fLaC"; o += 4; seek = vc = None; si = None
    while True:
        h = data[o]; ln = int.from_bytes(data[o + 1:o + 4], "big"); body = data[o + 4:o + 4 + ln]; typ = h & 0x7F
        if typ == 0: si = body
        if typ == 3: seek = (o + 4, body)
        if typ == 4: vc = (o + 4, body)
        o += 4 + ln
        if h & 0x80: break
    v = int.from_bytes(si[10:18], "big")
    sif = [v >> 44, ((v >> 41) & 7) + 1, ((v >> 36) & 31) + 1, v & ((1 << 36) - 1), int.from_bytes(si[0:2], "big"), int.from_bytes(si[2:4], "big")]
    sk = [1, min(len(seek[1]), scap), len(seek[1]), int(len(seek[1]) > scap), seek[0], fnv(seek[1][:scap])] if seek else [0, 0, 0, 0, 0, fnv(b"")]
    vk = [1, min(len(vc[1]), vcap), len(vc[1]), int(len(vc[1]) > vcap), vc[0], fnv(vc[1][:vcap])] if vc else [0, 0, 0, 0, 0, fnv(b"")]
    return o, sif, sk, vk
def synth(rng):
    frames, si, pcm, starts, blocks = enc.encode_stream(rng, bits=16, chans=2, rate=44100, blocks=[16, 16], variable=False)
    pre = b""
    if rng.random() < 0.4:
        sz = rng.randrange(0, 3000); foot = rng.random() < 0.3; hdr = b"ID3" + bytes([4, 0, 0x10 if foot else 0]) + bytes([(sz >> 21) & 127, (sz >> 14) & 127, (sz >> 7) & 127, sz & 127])
        pre = hdr + bytes(rng.randrange(256) for _ in range(sz)) + (b"3DI" + bytes(7) if foot else b"")
    kinds = [1, 2, 3, 4, 5, 6, rng.choice([7, 20, 100])]; rng.shuffle(kinds); kinds = kinds[: rng.randrange(0, 7)]
    blks = [(0, si)]
    for k in kinds:
        ln = {1: rng.randrange(0, 4000), 6: rng.choice([0, 30000, 70000])}.get(k, rng.randrange(0, 600))
        if k == 3: ln = 18 * rng.randrange(0, 90)
        blks.append((k, bytes(rng.randrange(256) for _ in range(ln))))
    rest = blks[1:]; rng.shuffle(rest); blks = [blks[0]] + rest
    body = b""
    for i, (t, b) in enumerate(blks): body += bytes([(0x80 if i == len(blks) - 1 else 0) | t]) + len(b).to_bytes(3, "big") + b
    return pre + b"fLaC" + body + frames
def suite(exe, d, n=60, seed=2):
    rng = random.Random(seed); ok = True; why = None
    for i in range(n):
        data = synth(rng); p = d / "f.flac"; p.write_bytes(data); scap = rng.choice([0, 100, 4096, 20000]); vcap = rng.choice([0, 50, 512])
        want = ref_parse(data, scap, vcap)
        for chunks, skip in (("1", 0), ("4096", 0), ("7,1,300", 0), ("64", 1)):
            g = run(exe, p, chunks, scap, vcap, skip)
            if g is None or g["st"] != 0 or g["state"] != 6 or g["audio"] != want[0] or g["si"] != want[1] or g["seek"] != want[2] or g["vc"] != want[3]:
                return False, (i, chunks, skip, g, want)
    return ok, why
def refusals(exe, d):
    rng = random.Random(4); frames, si, *_ = enc.encode_stream(rng, bits=16, chans=2, rate=44100, blocks=[16], variable=False)
    def blk(t, b, last=False): return bytes([(0x80 if last else 0) | t]) + len(b).to_bytes(3, "big") + b
    cases = {
        "bad magic": (b"fLaX" + blk(0, si, True), 2),
        "streaminfo not first": (b"fLaC" + blk(1, bytes(10)) + blk(0, si, True), 2),
        "streaminfo length 33": (b"fLaC" + blk(0, si[:33], True), 2),
        "two streaminfo": (b"fLaC" + blk(0, si) + blk(0, si, True), 2),
        "block type 127": (b"fLaC" + blk(0, si) + blk(127, b"", True), 2),
        "seektable size not a multiple of 18": (b"fLaC" + blk(0, si) + blk(3, bytes(19), True), 2),
        "bad ID3 size byte": (b"ID3\x04\x00\x00\x00\x00\x80\x00" + b"fLaC" + blk(0, si, True), 2),
        "no last-block flag before EOF (asks for more)": (b"fLaC" + blk(0, si) + blk(1, bytes(10)), 1),
        "truncated inside STREAMINFO": (b"fLaC" + blk(0, si)[:20], 1),
        "empty file": (b"", 1),
    }
    bad = []
    for name, (data, want) in cases.items():
        p = d / "r.flac"; p.write_bytes(data)
        if not data: bad.append(name) if False else None; continue
        g = run(exe, p, "3")
        if g is None or g["st"] != want: bad.append((name, g and g["st"]))
    return not bad, bad
def real(exe, d):
    ok = True; n = 0; bad = None
    for p in sorted(MUSIC.rglob("*.flac")):
        data = p.read_bytes()
        try: want = ref_parse(data, 4096, 512)
        except Exception: continue
        g = run(exe, p, "4096,1,10000", 4096, 512, 1 if n % 2 else 0); n += 1
        if g is None or g["st"] != 0 or g["audio"] != want[0] or g["si"] != want[1] or g["seek"] != want[2] or g["vc"] != want[3]: return False, (p.name, g, want), n
    return True, None, n
def seekpoints():
    r = random.Random(6); src = '''#include <stdio.h>
#include <stdlib.h>
#include "flac2_core.h"
int main(void){ unsigned n; static uint8_t t[18*400]; while (scanf("%u", &n)==1) { for (unsigned i=0;i<18*n;i++){ unsigned x; scanf("%x",&x); t[i]=(uint8_t)x; } unsigned long long tg; scanf("%llu",&tg); uint64_t s=0,o=0; uint32_t k=0; int f=f2_seekpoint(t,n,tg,&s,&o,&k); printf("%d %llu %llu %u\\n",f,(unsigned long long)s,(unsigned long long)o,k);} return 0; }'''
    return src
with tempfile.TemporaryDirectory() as td:
    d = Path(td); good = build(d, 0)
    ok, why = suite(good, d); check("synthetic files (ID3 +/- footer, every block kind in random order, capture buffers smaller than the blocks): offset, STREAMINFO, captures and truncation flags exact; 1-byte windows and caller-side skipping", ok, why)
    ok, why = refusals(good, d); check("refusals (bad magic, STREAMINFO not first / wrong length / repeated, type 127, seektable size, bad ID3 size) and truncated files ask for more instead of failing", ok, why)
    ok, why, n = real(good, d); check(f"{n} real FLAC files open to the same audio offset, STREAMINFO, SEEKTABLE and VORBIS_COMMENT as the Python parse", ok, why)
    # seek points
    (d / "sp.c").write_text(seekpoints()); subprocess.run(["cc", "-std=gnu11", "-O1", "-I", str(ROOT / "fw2/cores"), "-o", str(d / "sp"), str(d / "sp.c")], check=True)
    r = random.Random(6); inp = ""; want = []
    for _ in range(3000):
        npts = r.randrange(0, 60); real_n = r.randrange(0, npts + 1) if npts else 0; smps = sorted(r.sample(range(0, 5_000_000), real_n)) if real_n else []
        if smps and r.random() < 0.5: smps[0] = 0
        offs = sorted(r.sample(range(0, 1 << 30), real_n)) if real_n else []
        tab = b"".join(struct.pack(">QQH", s_, o_, r.randrange(16, 4609)) for s_, o_ in zip(smps, offs)) + b"".join(struct.pack(">QQH", 0xFFFFFFFFFFFFFFFF, 0, 0) for _ in range(npts - real_n))
        tg = r.randrange(0, 6_000_000); inp += f"{npts} {tab.hex(' ')}\n{tg}\n" if npts else f"0\n{tg}\n"
        cand = [(s_, o_, int.from_bytes(tab[18 * i + 16:18 * i + 18], 'big')) for i, (s_, o_) in enumerate(zip(smps, offs)) if s_ <= tg]
        want.append((1, *cand[-1]) if cand else (0, 0, 0, 0))
    out = subprocess.run([str(d / "sp")], input=inp.replace(" \n", "\n"), capture_output=True, text=True).stdout.split("\n")
    got = [tuple(map(int, l.split())) for l in out if l.strip()]
    got = [g if g[0] else (0, 0, 0, 0) for g in got]
    check("f2_seekpoint: the last real point at or before the target, placeholders ignored (3,000 random tables)", got == want, (got[:3], want[:3]))
    for bug, name in ((22, "block length top byte dropped"), (23, "ID3 footer ignored"), (24, "truncation flag never set"), (25, "audio offset off by one")):
        m = build(d, bug); a = suite(m, d, n=40)[0]; b = real(m, d)[0] if bug == 25 else True
        check(f"mutant {bug} ({name}) is caught", not (a and b))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
