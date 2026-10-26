#!/usr/bin/env python3
"""Player 2 FLAC core: recovery (f2_rec_step in fw2/cores/flac2_core.h; spec 5.6). Streams from the independent encoder are damaged in the ways an SD card and a bridge damage them, and the strict contract is:
  * the output is ALWAYS exactly the right length (silence fills what was lost, so put == timeline);
  * every frame in the output is either bit-exact audio or silence -- NEVER wrong audio;
  * every damaged frame is silent; at most the stated number of undamaged neighbours are lost with it;
  * the decoder finds the next frame by itself (bit flips, overwritten runs, deleted runs, inserted garbage, truncation), ignores a replayed old frame (the timeline is behind it), survives scattered
    damage over a long stream and gives up after 8 consecutive failures instead of looping;
  * identical results for 1-byte windows, one residual per call, one pair per call.
Mutants: lost audio never owed, no timeline check, failure counter never cleared."""
import os, random, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import flac2_enc as enc
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
N = int(os.environ.get("FLAC2_REC_N", "40"))
def build(d, bug, san=False):
    exe = d / f"r{bug}{'s' if san else ''}"
    fl = ["-fsanitize=undefined,address", "-fno-sanitize-recover=all"] if san else []
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-misleading-indentation", f"-DBUG={bug}", *fl, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_flac2_rec_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, d, data, si, chunks="1000000", slices="1000000", caps="65536", tag="x"):
    fb = d / f"{tag}.bin"; fb.write_bytes(data); po = d / f"{tag}.pcm"; so = d / f"{tag}.st"
    r = subprocess.run([str(exe), str(fb), si.hex(), chunks, slices, caps, "1", str(po), str(so)], capture_output=True, text=True, timeout=300)
    if r.returncode: return None, r.stderr[-200:]
    st = list(map(int, so.read_text().split()))
    pcm = np.fromfile(po, dtype="<i4").reshape(-1, 2) if po.stat().st_size else np.zeros((0, 2), dtype="<i4")
    return dict(status=st[0], ok=st[1], bad=st[2], resyncs=st[3], silent=st[4], ferr=st[5], giveup=st[6], committed=st[7]), pcm
def make(rng, nframes=None, bits=None, small=False):
    nf = nframes or rng.randrange(6, 12); var = rng.random() < 0.4
    bs0 = rng.choice([16, 17, 40, 64]) if small else rng.choice([192, 576, 1152, 2304])
    blocks = [bs0] * nf if not var else [rng.choice([bs0, bs0 + 5, bs0 * 2]) for _ in range(nf)]
    b = bits or rng.choice([16, 16, 24, 8]); chans = rng.choice([1, 2, 2])
    frames, si, pcm, starts, blocks = enc.encode_stream(rng, bits=b, chans=chans, rate=rng.choice([44100, 48000]), blocks=blocks, variable=var)
    return frames, si, np.array(pcm, dtype="<i4"), starts, blocks
def verify(got, pcm, blocks, damaged, allowed_collateral):
    """-> (ok, why). every frame region: original or silence; damaged frames silent; collateral bounded; total exact."""
    if len(got) != len(pcm): return False, ("length", len(got), len(pcm))
    pos = 0; zeros = []
    for k, bsz in enumerate(blocks):
        a, b = got[pos:pos + bsz], pcm[pos:pos + bsz]; pos += bsz
        if np.array_equal(a, b): 
            if k in damaged and np.any(b): return False, ("damaged frame survived", k)
            continue
        if not np.all(a == 0): return False, ("WRONG AUDIO in frame", k)
        zeros.append(k)
    if not damaged <= set(zeros): return False, ("damaged frame not silent", sorted(damaged), zeros)
    if len(set(zeros) - damaged) > allowed_collateral: return False, ("too many neighbours lost", sorted(damaged), zeros)
    return True, None
def frames_hit(starts, total_len, a, b):
    ends = starts[1:] + [total_len]
    return {k for k in range(len(starts)) if a < ends[k] and b > starts[k]}
def damage(rng, kind, frames, starts):
    """-> (damaged bytes, set damaged frame indexes (original numbering), allowed collateral, extra expected)"""
    n = len(frames)
    if kind == "flip":
        bit = rng.randrange(n * 8); x = bytearray(frames); x[bit // 8] ^= 1 << (bit % 8); return bytes(x), frames_hit(starts, n, bit // 8, bit // 8 + 1), 0
    if kind == "burst":
        m = rng.randrange(1, 120); a = rng.randrange(0, n - m); x = bytearray(frames)
        for i in range(a, a + m): x[i] = rng.randrange(256)
        return bytes(x), frames_hit(starts, n, a, a + m), 1
    if kind == "delete":
        m = rng.randrange(1, 90); a = rng.randrange(1, n - m); return frames[:a] + frames[a + m:], frames_hit(starts, n, a, a + 1), 2
    if kind == "insert":                                                           # garbage in a gap between frames: nothing is lost
        k = rng.randrange(1, len(starts)); g = bytes(rng.randrange(256) for _ in range(rng.randrange(1, 150))); return frames[:starts[k]] + g + frames[starts[k]:], set(), 0
    if kind == "truncate":
        cut = rng.randrange(1, n); return frames[:cut], {k for k in range(len(starts)) if (starts[k + 1] if k + 1 < len(starts) else n) > cut}, 0
    if kind == "forged":                                                           # a valid-looking header (good CRC-8) claiming a start far in the future, followed by junk
        k = rng.randrange(1, len(starts)); hdr = bytearray(frames[starts[0]:starts[0] + 12]); var = hdr[1] & 1
        old_len = len(enc.utf8num(0)); newnum = enc.utf8num((1 << 31) if var else (1 << 20))
        body = bytes(hdr[:4]) + newnum + bytes(hdr[4 + old_len:]); 
        # the header ends with its CRC-8 right after the optional size/rate bytes: rebuild from the original header length
        hl = 4 + old_len; extra = 0
        bsc = hdr[2] >> 4; rcc = hdr[2] & 15; extra += 1 if bsc == 6 else 2 if bsc == 7 else 0; extra += 1 if rcc == 12 else 2 if rcc in (13, 14) else 0
        forged = bytes(hdr[:4]) + newnum + bytes(hdr[hl:hl + extra]); forged += bytes([enc.crc8(forged)])
        return frames[:starts[k]] + forged + bytes(rng.randrange(256) for _ in range(20)) + frames[starts[k]:], set(), 0
    if kind == "replay":                                                           # a whole old frame played again where the next one belongs
        k = rng.randrange(2, len(starts)); j = rng.randrange(0, k - 1); ends = starts[1:] + [n]
        return frames[:starts[k]] + frames[starts[j]:ends[j]] + frames[starts[k]:], set(), 0
def suite(exe, d, seed=3, count=None):
    rng = random.Random(seed); ok = True; why = None; ran = 0
    for i in range(count or N):
        frames, si, pcm, starts, blocks = make(rng)
        st, got = run(exe, d, frames, si, tag="clean")
        if st is None or st["status"] != 0 or st["bad"] != 0 or not np.array_equal(got, pcm): return False, ("clean", i, st), ran
        kind = rng.choice(["flip", "flip", "burst", "delete", "insert", "truncate", "replay", "forged", "forged"])
        bad, dm, coll = damage(rng, kind, frames, starts)
        st, got = run(exe, d, bad, si, tag="dmg")
        if st is None: return False, ("crash", kind, i), ran
        v, w = verify(got, pcm, blocks, dm, coll)
        if not v: return False, (kind, i, w), ran
        if i % 4 == 0:                                                              # the same result for hostile windows
            st2, got2 = run(exe, d, bad, si, "1", "1", "1", tag="dmg2")
            if st2 is None or not np.array_equal(got, got2): return False, ("window dependence", kind, i), ran
        ran += 1
    return ok, why, ran
def scattered(exe, d):
    rng = random.Random(8); frames, si, pcm, starts, blocks = enc.encode_stream(rng, bits=16, chans=2, rate=48000, blocks=[16] * 40, variable=False)
    x = bytearray(frames); bad = set(range(2, 40, 4))                               # 10 separate damaged frames, never adjacent
    for k in bad: x[starts[k] + 6] ^= 0x40
    st, got = run(exe, d, bytes(x), si, tag="sc"); pcm = np.array(pcm, dtype="<i4")
    v, w = verify(got, pcm, blocks, bad, 0)
    return v and st["giveup"] == 0, (w, st)
def consecutive(exe, d):
    rng = random.Random(9); frames, si, pcm, starts, blocks = enc.encode_stream(rng, bits=16, chans=2, rate=48000, blocks=[16] * 30, variable=False)
    x = bytearray(frames)
    ends = starts[1:] + [len(frames)]
    for k in range(5, 20): x[ends[k] - 1] ^= 0x40                                    # 15 consecutive frames with a bad CRC-16 (headers intact: each is a separate failure)
    st, got = run(exe, d, bytes(x), si, tag="co"); pcm = np.array(pcm, dtype="<i4")
    # give up after 8 failures: nothing wrong is played, the output is still the right length
    pos = 0
    for k, bsz in enumerate(blocks):
        a, b = got[pos:pos + bsz], pcm[pos:pos + bsz]; pos += bsz
        if not (np.array_equal(a, b) or np.all(a == 0)): return False, ("wrong audio", k, st)
    return st["giveup"] == 1 and len(got) == len(pcm), st
with tempfile.TemporaryDirectory() as td:
    d = Path(td); good = build(d, 0)
    ok, why, ran = suite(good, d)
    check(f"{ran} damaged streams (bit flips, overwritten runs, deleted runs, inserted garbage, truncation, a replayed old frame, a forged far-future header): right length, bit-exact or silent, damaged frames silent, bounded losses, window-independent", ok, why)
    ok, why = scattered(good, d); check("10 scattered damaged frames in a 40-frame stream: only those frames lost, the stream never gives up", ok, why)
    ok, why = consecutive(good, d); check("15 consecutive damaged frames: gives up after 8, plays nothing wrong, output still the right length", ok, why)
    ok, why, _ = suite(build(d, 0, san=True), d, seed=7, count=max(10, N // 3)); check("recovery is clean under UBSan/ASan", ok, why)
    for bug, name in ((18, "a damaged frame's duration never owed"), (19, "no timeline check on a found frame"), (21, "failure counter never cleared")):
        mexe = build(d, bug); r1 = suite(mexe, d, seed=3)[0]; r2 = scattered(mexe, d)[0]
        check(f"mutant {bug} ({name}) is caught", not (r1 and r2))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
