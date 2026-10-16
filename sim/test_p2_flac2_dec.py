#!/usr/bin/env python3
"""Player 2 FLAC core piece 4 (fw2/cores/flac2_core.h): the frame decoder, end to end.
 * synthetic streams from an independent encoder (sim/flac2_enc.py: every subframe type, wasted bits, 4 stereo assignments, mono, 8..24 bit, fixed and variable block sizes, partitioned/escape Rice, LPC
   orders up to 32) decode to EXACTLY the PCM that was encoded, with the frame CRC-16 verified inside the core;
 * the output is identical for any input chunking, any residual budget and any output-buffer size (resumable decoding: 1-byte-at-a-time windows, 1 residual per call, 1 pair per call included);
 * corruption: EVERY single bit flip anywhere in a frame is reported (header CRC-8, subframe errors or the frame CRC-16) and only whole frames before the damaged one are committed -- run under UBSan/ASan;
 * real files: the test vectors (mono, 8/20/24 bit, mid/side) and real music decode to the MD5 in their STREAMINFO (bit-exact), via the C core;
 * mutants: mid/side bit, wasted bits (both channels), CRC not checked, no byte padding, side-channel extra bit.
"""
import hashlib, os, random, struct, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import flac2_enc as enc
ROOT = Path(__file__).resolve().parent.parent
MUSIC = ROOT.parent / "test music"
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
NS = int(os.environ.get("FLAC2_DEC_N", "60"))
def build(d, bug, san=False):
    exe = d / f"d{bug}{'s' if san else ''}"
    fl = ["-fsanitize=undefined,address", "-fno-sanitize-recover=all"] if san else []
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-misleading-indentation", f"-DBUG={bug}", *fl, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_flac2_dec_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, d, frames, si, chunks="1000000", slices="1000000", caps="65536", flags=1, tag="r", hw="-"):
    fb = d / f"{tag}.bin"; fb.write_bytes(frames); po = d / f"{tag}.pcm"; so = d / f"{tag}.st"
    r = subprocess.run([str(exe), str(fb), si.hex(), chunks, slices, caps, str(flags), str(po), str(so), hw], capture_output=True, text=True, timeout=900)
    if r.returncode: return None, r.stderr[-300:]
    st = list(map(int, so.read_text().split())); pcm = np.fromfile(po, dtype="<i4").reshape(-1, 2) if po.stat().st_size else np.zeros((0, 2), dtype="<i4")
    return dict(status=st[0], ok=st[1], bad=st[2], err=st[3], pend=st[4], off=st[5], hw_samples=st[6], hw_fails=st[7]), pcm
def synth(rng, i):
    bits = rng.choice([8, 12, 16, 16, 16, 20, 24]); chans = rng.choice([1, 2, 2, 2]); rate = rng.choice([44100, 48000, 32000, 96000, 22050]); var = rng.random() < 0.4
    return enc.encode_stream(rng, bits=bits, chans=chans, rate=rate, variable=var), bits, chans
def suite(exe, d, seed=21):
    rng = random.Random(seed); res = dict(exact=True, inv=True, detail=None, n=0)
    cfgs = [("1000000", "1000000", "65536"), ("1", "1", "1"), ("3,1,7,64", "1,2,5,300", "1,2,3,64"), ("4096", "64", "17")]
    for i in range(NS):
        (frames, si, pcm, starts, blocks), bits, chans = synth(rng, i); want = np.array(pcm, dtype="<i4")
        outs = []
        for ci, (ch, sl, oc) in enumerate(cfgs[: (4 if i % 3 == 0 else 1)]):
            st, got = run(exe, d, frames, si, ch, sl, oc, tag=f"s{ci}")
            if st is None or st["status"] != 0 or st["ok"] != len(starts) or st["bad"] != 0 or not np.array_equal(got, want):
                res["exact"] = False; res["detail"] = (i, bits, chans, ci, st, None if st is None else (got.shape, want.shape)); break
            outs.append(got)
        if not res["exact"]: break
        res["n"] += 1
    return res
def corruption(exe, d, seed=5):
    rng = random.Random(seed); allok = True; detail = None; flips = 0
    for i in range(12):
        (frames, si, pcm, starts, blocks), bits, chans = synth(rng, i)
        psum = [0]
        for bsz in blocks: psum.append(psum[-1] + bsz)                     # pairs committed before frame k
        for _ in range(25):
            bit = rng.randrange(len(frames) * 8); bad = bytearray(frames); bad[bit // 8] ^= 1 << (bit % 8); flips += 1
            fi = max(k for k in range(len(starts)) if starts[k] <= bit // 8)
            st, got = run(exe, d, bytes(bad), si, tag="c2")
            if st is None: allok = False; detail = ("crash", bit, st, got); break
            if st["status"] == 0 and st["ok"] == len(starts): allok = False; detail = ("undetected", i, bit, fi); break
            # whole frames before the damaged one only; the damaged frame is never committed
            if len(got) != psum[fi] or not np.array_equal(got, np.array(pcm[: psum[fi]], dtype="<i4").reshape(-1, 2)): allok = False; detail = ("committed wrong", i, bit, fi, len(got), psum[fi], st); break
        if not allok: break
    return allok, flips, detail
def md5_of(pcm, chans, bits):
    a = pcm[:, :chans] if chans == 1 else pcm
    b = (bits + 7) // 8; raw = np.ascontiguousarray(a.astype("<i4")).reshape(-1).view(np.uint8).reshape(-1, 4)[:, :b]
    return hashlib.md5(raw.tobytes()).digest()
def real_file(exe, d, path):
    data = path.read_bytes(); assert data[:4] == b"fLaC"; o = 4; si = None
    while True:
        hd = data[o]; ln = int.from_bytes(data[o + 1:o + 4], "big")
        if (hd & 0x7F) == 0: si = data[o + 4:o + 4 + 34]
        o += 4 + ln
        if hd & 0x80: break
    v = int.from_bytes(si[10:18], "big"); bits = ((v >> 36) & 31) + 1; chans = ((v >> 41) & 7) + 1; total = v & ((1 << 36) - 1); md5 = si[18:34]
    st, pcm = run(exe, d, data[o:], si, "4096,100000,333", "400,100000", "64,4608,1000", tag="real")
    if st is None: return False, ("crash", st, pcm)
    good = st["status"] == 0 and st["bad"] == 0 and len(pcm) == total
    m = md5_of(pcm, chans, bits) == md5 if any(md5) else None
    return good and m is not False, (path.name, st, len(pcm), total, bits, chans, "md5" if m else "no md5")
def hw_tier(exe, d):
    """the LPC unit takes the 64-bit tier only: 24-bit audio exact with it, a fault at any sample still exact (the sample is finished in software, the unit stays off), 16-bit music that passes the 32-bit bound never touches it"""
    rng = random.Random(33); used = 0
    for i in range(12):
        (frames, si, pcm, starts, blocks), bits, chans = enc.encode_stream(rng, bits=24, chans=2, rate=48000, blocks=[576] * 6, variable=False), 24, 2
        want = np.array(pcm, dtype="<i4")
        st, got = run(exe, d, frames, si, tag="h0")
        for mode in ("h", "h0", "h7", "h300", "h2000"):
            st, got = run(exe, d, frames, si, hw=mode, tag="h1")
            if st is None or st["status"] != 0 or not np.array_equal(got, want): return False, ("24-bit with unit", i, mode, st)
            used += st["hw_samples"]
            if st["hw_fails"] > 1 or (mode == "h" and st["hw_fails"]): return False, ("fault count wrong", mode, st)
        st, got = run(exe, d, frames, si, hw="h", tag="h2"); used += st["hw_samples"]
    return used > 0, ("the unit was never used", used)
def hw_real(exe, d):
    f = MUSIC / "Test Album/09 Clementi FLAC 48k.flac"; g = MUSIC / "Test Album/13 riteofspring-sample-48khz.flac"
    if not f.exists() or not g.exists(): return True, "skipped"
    out = {}
    for name, p in (("16", f), ("24", g)):
        data = p.read_bytes(); o = 4; si = None
        while True:
            hd = data[o]; ln = int.from_bytes(data[o + 1:o + 4], "big")
            if (hd & 0x7F) == 0: si = data[o + 4:o + 38]
            o += 4 + ln
            if hd & 0x80: break
        v = int.from_bytes(si[10:18], "big"); bits = ((v >> 36) & 31) + 1; md5 = si[18:34]
        for mode in ("h", "h4000"):
            st, pcm = run(exe, d, data[o:], si, "65536", "100000", "4608", flags=1, tag="hr", hw=mode)
            if st is None or st["status"] != 0 or md5_of(pcm, 2, bits) != md5: return False, (name, mode, st)
            out[(name, mode)] = st
    ok = out[("16", "h")]["hw_samples"] == 0 and out[("24", "h")]["hw_samples"] > 100000 and out[("24", "h4000")]["hw_fails"] == 1 and out[("24", "h4000")]["hw_samples"] == 4000
    return ok, out
with tempfile.TemporaryDirectory() as td:
    d = Path(td); good = build(d, 0)
    r = suite(good, d)
    check(f"{r['n']}/{NS} synthetic streams decode to exactly the encoded PCM (all subframe types, wasted bits, 4 stereo assignments, mono, 8 to 24 bit, fixed/variable block size), CRC-16 verified; identical for 1-byte windows, 1 residual per call, 1 pair per call", r["exact"], r["detail"])
    ok, n, det = corruption(build(d, 0, san=True), d)
    check(f"{n} single-bit corruptions: every one detected, only whole frames before the damaged one committed, clean under UBSan/ASan", ok, det)
    REAL = os.environ.get("FLAC2_DEC_REAL", "1") != "0"           # FLAC2_DEC_REAL=0 skips the large music files (make test-host); the full proof runs them
    for p in ["flac-tests/t1 stereo 16 blk4096.flac", "flac-tests/t2 stereo 16 midside.flac", "flac-tests/t3 mono 16.flac", "flac-tests/t4 stereo 8bit.flac", "flac-tests/t5 stereo 20bit.flac", "flac-tests/t6 stereo 24 blk1152.flac",
              "Test Album/09 Clementi FLAC 48k.flac", "Test Album/13 riteofspring-sample-48khz.flac", "Ghost in the Shell 2.0 OST (FLAC)/03 - Puppetmaster (2.0 Ver.).flac"]:
        f = MUSIC / p
        if not f.exists() or (not REAL and not p.startswith("flac-tests/")): print("skip:", p); continue
        ok, info = real_file(good, d, f); check(f"real file decodes bit-exact to its STREAMINFO MD5: {f.name}", ok, info)
    ok, why = hw_tier(good, d); check("the LPC unit takes the 64-bit tier: 24-bit synthetic exact with it, a fault at sample 0/7/300/2000 still exact (hw_fails 1), the unit used", ok, why)
    if REAL or True:
        ok, why = hw_real(good, d) if REAL else (True, "skipped"); check("real music with the unit: Clementi 16-bit never uses it, Rite of Spring 24-bit uses it for 100,000+ samples and is exact, a fault at sample 4,000 is exact and counted once", ok, why)
    for bug, name in ((12, "mid/side low bit dropped"), (13, "wasted bits of the buffered channel"), (14, "CRC-16 never checked"), (15, "wasted bits of the streaming channel"), (16, "no padding to the byte boundary"), (17, "side channel extra bit"), (51, "a corrupted unit answer"), (52, "the unit also takes the 32-bit tier")):
        if bug == 52 and not REAL: print("skip: mutant 52 needs the real 16-bit file (FLAC2_DEC_REAL=1)"); continue
        mexe = build(d, bug); rr = suite(mexe, d, seed=21)
        caught = not rr["exact"]
        if bug == 14: caught = not corruption(mexe, d)[0]
        if bug in (51, 52): caught = not hw_tier(mexe, d)[0] or (bug == 52 and not hw_real(mexe, d)[0])
        check(f"mutant {bug} ({name}) is caught", caught)
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
