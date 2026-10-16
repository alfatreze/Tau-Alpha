#!/usr/bin/env python3
"""Player 2 FLAC track driver (fw2/cores/flac2_track_core.h) through the REAL stream reader and a card model (latency, jitter, odd ring sizes), against ground truth.
 * whole files: ID3 + metadata incl. a big picture (the stream is repositioned past it) + seek table, played through rings of 1,500 to 24,576 bytes (every unit that straddles the wrap goes through the bridge):
   the committed audio is exactly the encoded PCM, put == total, the track ends;
 * seeks (planned in the middle of playback, forwards, backwards, to the start, the very end, with and without the file's seek table): every stretch starts exactly on its target and equals the original audio;
 * damage (flips, runs, deletions, garbage, a replayed frame, a forged header): the right total length, bit-exact or silent, damaged frames silent -- through the driver, stream and ring;
 * a file cut off mid-frame ends with the missing tail as silence; a cut inside the metadata, an empty file and a non-FLAC file FAIL cleanly; a card that stops answering FAILS with the audio so far intact;
 * real music decodes to its STREAMINFO MD5 through all of it, and a seek inside a real file lands on the sample.
 Mutants: one pair too many kept after a seek, no bridge, no silent tail, failed frames counted as delivered."""
import hashlib, os, random, struct, subprocess, sys, tempfile
from pathlib import Path
import numpy as np
sys.path.insert(0, str(Path(__file__).resolve().parent))
import flac2_enc as enc
ROOT = Path(__file__).resolve().parent.parent
MUSIC = ROOT.parent / "test music"
QUICK = os.environ.get("FLAC2_TRACK_QUICK", "0") == "1"                       # make test-host: fewer cases, no large music files
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
def build(d, bug, san=False):
    exe = d / f"t{bug}{'s' if san else ''}"
    fl = ["-fsanitize=undefined,address", "-fno-sanitize-recover=all"] if san else []
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-misleading-indentation", f"-DBUG={bug}", *fl, "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_flac2_track_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, d, data, cap=24576, seed=1, lat=1500, jit=3000, plan="", tag="t"):
    fp = d / f"{tag}.flac"; fp.write_bytes(data); po = d / f"{tag}.pcm"; io = d / f"{tag}.info"
    r = subprocess.run([str(exe), str(fp), str(po), str(io), str(cap), str(seed), str(lat), str(jit), "1", plan], capture_output=True, text=True, timeout=900)
    if r.returncode: return None
    segs = []; res = None
    for l in io.read_text().splitlines():
        if l.startswith("S "): _, b, c = l.split(); segs.append((int(b), int(c)))
        if l.startswith("R "):
            a = l[2:].split("|"); h = list(map(int, a[0].split())); m = list(map(int, a[1].split())); w = list(map(int, a[2].split()))
            res = dict(status=h[0], why=h[1], phase=h[2], ok=m[0], ferr=m[1], resyncs=m[2], silent=m[3], wraps=w[0], repos=w[1], probes=w[2], opened=int(a[3]))
    pcm = np.fromfile(po, dtype="<i4").reshape(-1, 2) if po.stat().st_size else np.zeros((0, 2), dtype="<i4")
    return res, segs, pcm
def make_file(rng, frames, si, starts, sstarts, pic=70000, table=True, id3=True):
    pre = b""
    if id3 and rng.random() < 0.5:
        sz = rng.randrange(0, 2500); pre = b"ID3" + bytes([4, 0, 0]) + bytes([(sz >> 21) & 127, (sz >> 14) & 127, (sz >> 7) & 127, sz & 127]) + bytes(rng.randrange(256) for _ in range(sz))
    blks = [(0, si), (1, bytes(rng.randrange(256) for _ in range(rng.randrange(0, 1500))))]
    if table:
        tb = b"".join(struct.pack(">QQH", sstarts[k], starts[k], 16) for k in range(0, len(starts), 5)) + struct.pack(">QQH", 0xFFFFFFFFFFFFFFFF, 0, 0); blks.append((3, tb))
    blks.append((4, bytes(rng.randrange(256) for _ in range(rng.randrange(20, 700)))))
    if pic: blks.append((6, bytes(rng.randrange(256) for _ in range(pic))))
    body = b""
    for i, (t, b) in enumerate(blks): body += bytes([(0x80 if i == len(blks) - 1 else 0) | t]) + len(b).to_bytes(3, "big") + b
    return pre + b"fLaC" + body, len(pre) + 4 + len(body)
def synth(rng, var, n=70, bits=None):
    bs = rng.choice([64, 192, 576]); blocks = [bs] * (n - 1) + [rng.randrange(16, bs + 1)] if not var else [rng.choice([bs, bs // 2 + 16, bs * 2]) for _ in range(n)]
    frames, si, pcm, starts, blocks = enc.encode_stream(rng, bits=bits or rng.choice([16, 16, 24]), chans=rng.choice([1, 2, 2]), rate=48000, blocks=blocks, variable=var)
    ss = [0]
    for b in blocks[:-1]: ss.append(ss[-1] + b)
    return frames, si, np.array(pcm, dtype="<i4"), starts, blocks, ss
def stretch_ok(segs, pcm, out, targets):
    """-> (ok, why): stretches in order, each starting on its target (the first at 0), each equal to the original audio from there"""
    pos = 0; want = [0] + list(targets)
    if len(segs) != len(want): return False, ("stretch count", segs, want)
    for (b, c), tg in zip(segs, want):
        if b != tg: return False, ("stretch does not start on its target", b, tg)
        if not np.array_equal(out[pos:pos + c], pcm[b:b + c]): return False, ("stretch differs from the original", b, c)
        pos += c
    return pos == len(out), ("lengths", pos, len(out))
def whole(exe, d):
    rng = random.Random(1)
    for i in range(5 if QUICK else 10):
        var = i % 2; frames, si, pcm, starts, blocks, ss = synth(rng, var); data, off = make_file(rng, frames, si, starts, ss, pic=rng.choice([0, 3000, 70000])); total = len(pcm)
        cap = [8192, 9001, 12289, 16000, 24576][i % 5]
        res, segs, out = run(exe, d, data + frames, cap=cap, seed=i, lat=rng.choice([0, 800, 4000]), jit=rng.choice([0, 6000]))
        res, segs, out = run(exe, d, data + b"", cap=cap) if False else (res, segs, out)
        full = data[:off] + frames
        res, segs, out = run(exe, d, full, cap=cap, seed=i, lat=rng.choice([0, 800, 4000]), jit=rng.choice([0, 6000]))
        if res is None or res["status"] != 0 or res["ferr"] != 0 or not np.array_equal(out, pcm) or segs != [(0, total)]: return False, (i, cap, res, segs, out.shape, pcm.shape)
    return True, None
def seeks(exe, d):
    rng = random.Random(2)
    for i in range(3 if QUICK else 8):
        var = i % 2; frames, si, pcm, starts, blocks, ss = synth(rng, var, n=80); total = len(pcm)
        for table in (True, False):
            data, off = make_file(rng, frames, si, starts, ss, pic=rng.choice([0, 20000]), table=table); full = data + frames
            tg = [rng.randrange(total) for _ in range(3)] + [0, total - 1, total // 2]; at = []; prev = 0
            for tt in tg: at.append(rng.randrange(1, max(2, min(400, total - prev)))); prev = tt            # each stretch has more pairs left than it plays before the next seek
            plan = ",".join(f"seek@{a}:{t}" for a, t in zip(at, tg))
            res, segs, out = run(exe, d, full, cap=rng.choice([8192, 12289, 24576]), seed=i, plan=plan + ",res@" + str(rng.choice([16, 100, 512])))
            if res is None or res["status"] != 0: return False, ("run", i, table, res)
            ok, why = stretch_ok(segs, pcm, out, tg)
            if not ok: return False, (i, table, why)
            if segs[-1][1] != total - segs[-1][0]: return False, ("final stretch does not reach the end", segs[-1], total)
    return True, None
def verify(got, pcm, blocks, damaged, allowed):
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
    if len(set(zeros) - damaged) > allowed: return False, ("too many neighbours lost", sorted(damaged), zeros)
    return True, None
def hit(starts, n, a, b):
    ends = starts[1:] + [n]; return {k for k in range(len(starts)) if a < ends[k] and b > starts[k]}
def damaged(exe, d, count=None, seed=3):
    count = count or (8 if QUICK else 24)
    rng = random.Random(seed)
    for i in range(count):
        frames, si, pcm, starts, blocks, ss = synth(rng, i % 2, n=40); meta, off = make_file(rng, frames, si, starts, ss, pic=rng.choice([0, 9000])); n = len(frames)
        kind = rng.choice(["flip", "burst", "delete", "insert", "replay", "forged"]); coll = 0
        if kind == "flip": bit = rng.randrange(n * 8); x = bytearray(frames); x[bit // 8] ^= 1 << (bit % 8); bad = bytes(x); dm = hit(starts, n, bit // 8, bit // 8 + 1)
        elif kind == "burst":
            m = rng.randrange(1, 100); a = rng.randrange(0, n - m); x = bytearray(frames)
            for j in range(a, a + m): x[j] = rng.randrange(256)
            bad = bytes(x); dm = hit(starts, n, a, a + m); coll = 1
        elif kind == "delete": m = rng.randrange(1, 70); a = rng.randrange(1, n - m); bad = frames[:a] + frames[a + m:]; dm = hit(starts, n, a, a + 1); coll = 2
        elif kind == "insert": k = rng.randrange(1, len(starts)); bad = frames[:starts[k]] + bytes(rng.randrange(256) for _ in range(rng.randrange(1, 120))) + frames[starts[k]:]; dm = set()
        elif kind == "replay": k = rng.randrange(2, len(starts)); j = rng.randrange(0, k - 1); ends = starts[1:] + [n]; bad = frames[:starts[k]] + frames[starts[j]:ends[j]] + frames[starts[k]:]; dm = set()
        else:
            k = rng.randrange(1, len(starts)); hdr = bytearray(frames[starts[0]:starts[0] + 12]); var = hdr[1] & 1; fg = bytes(hdr[:4]) + enc.utf8num((1 << 31) if var else (1 << 20)); hl = 4 + len(enc.utf8num(0)); extra = 0
            bsc = hdr[2] >> 4; rcc = hdr[2] & 15; extra += 1 if bsc == 6 else 2 if bsc == 7 else 0; extra += 1 if rcc == 12 else 2 if rcc in (13, 14) else 0
            fg += bytes(hdr[hl:hl + extra]); fg += bytes([enc.crc8(fg)]); bad = frames[:starts[k]] + fg + bytes(rng.randrange(256) for _ in range(20)) + frames[starts[k]:]; dm = set()
        res, segs, out = run(exe, d, meta[:off] + bad, cap=rng.choice([8192, 12289, 24576]), seed=i, plan="res@" + str(rng.choice([16, 100, 512])))
        if res is None: return False, ("crash/timeout", kind, i)
        v, w = verify(out, pcm, blocks, dm, coll)
        if not v: return False, (kind, i, w, res)
    return True, None
def edges(exe, d):
    rng = random.Random(4); frames, si, pcm, starts, blocks, ss = synth(rng, 0, n=20); meta, off = make_file(rng, frames, si, starts, ss, pic=2000); total = len(pcm)
    cut = starts[12] + (starts[13] - starts[12]) // 2
    res, segs, out = run(exe, d, meta[:off] + frames[:cut], tag="cut")
    ok1 = res is not None and res["status"] == 0 and len(out) == total and np.array_equal(out[:ss[12]], pcm[:ss[12]]) and np.all(out[ss[12]:] == 0)
    res2, _, _ = run(exe, d, meta[:30], tag="cutmeta"); ok2 = res2 is not None and res2["status"] == 1 and res2["why"] == 1
    res3, _, _ = run(exe, d, b"", tag="empty"); ok3 = res3 is not None and res3["status"] == 1
    res4, _, _ = run(exe, d, bytes(rng.randrange(256) for _ in range(5000)), tag="junk"); ok4 = res4 is not None and res4["status"] == 1 and res4["why"] == 1
    full = meta[:off] + frames; res5, segs5, out5 = run(exe, d, full, plan=f"fail@{off + starts[10]}", tag="iofail")
    ok5 = res5 is not None and res5["status"] == 1 and res5["why"] == 2 and len(out5) >= ss[8] and np.array_equal(out5[:ss[8]], pcm[:ss[8]])
    return (ok1, ok2, ok3, ok4, ok5), (res, res2, res3, res4, res5)
def real(exe, d):
    out = []
    if QUICK: return True, "skipped"
    for name in ["flac-tests/t3 mono 16.flac", "Test Album/09 Clementi FLAC 48k.flac", "Test Album/13 riteofspring-sample-48khz.flac", "Ghost in the Shell 2.0 OST (FLAC)/03 - Puppetmaster (2.0 Ver.).flac"]:
        f = MUSIC / name
        if not f.exists(): continue
        data = f.read_bytes(); o = 4; si = None
        while True:
            hd = data[o]; ln = int.from_bytes(data[o + 1:o + 4], "big")
            if (hd & 0x7F) == 0: si = data[o + 4:o + 38]
            o += 4 + ln
            if hd & 0x80: break
        v = int.from_bytes(si[10:18], "big"); bits = ((v >> 36) & 31) + 1; chans = ((v >> 41) & 7) + 1; total = v & ((1 << 36) - 1); md5 = si[18:34]
        res, segs, pcm = run(exe, d, data, cap=24576, lat=1500, jit=3000, tag="real")
        if res is None or res["status"] != 0 or len(pcm) != total: return False, (name, res, len(pcm), total)
        a = pcm[:, :chans] if chans == 1 else pcm; b = (bits + 7) // 8
        if any(md5) and hashlib.md5(np.ascontiguousarray(a.astype("<i4")).reshape(-1).view(np.uint8).reshape(-1, 4)[:, :b].tobytes()).digest() != md5: return False, (name, "MD5")
        # a seek inside the real file lands on the sample
        t = total // 3 + 12345; res2, segs2, out2 = run(exe, d, data, cap=24576, plan=f"seek@{total // 5}:{t}", tag="realseek")
        if res2 is None or res2["status"] != 0: return False, (name, "seek run", res2)
        ok, why = stretch_ok(segs2, pcm, out2, [t])
        if not ok: return False, (name, "seek", why)
        out.append((name, res["repos"], res["wraps"], res2["probes"]))
    return True, out
with tempfile.TemporaryDirectory() as td:
    d = Path(td); good = build(d, 0)
    ok, why = whole(good, d); check("whole files through ring sizes 8,192..24,576 (the stream reader issues a read only when a whole 4,096-byte chunk is free, so a ring must hold several chunks) and residual budgets of 16 to 512 per call with latency: metadata (ID3, picture skipped by repositioning, seek table, comments), bridge over every wrap, audio exact, put == total", ok, why)
    ok, why = seeks(good, d); check("seeks forwards/backwards/to 0/to the end, with and without the seek table: every stretch starts exactly on its target and is the original audio", ok, why)
    ok, why = damaged(good, d); check("damaged files (flip, run, deletion, garbage, replayed frame, forged header) through driver, stream and ring: exact length, bit-exact or silent, bounded losses", ok, why)
    oks, info = edges(good, d)
    check("a file cut off mid-frame ends with the missing tail as silence (exact length, audio before intact)", oks[0], info[0]); check("a cut inside the metadata fails cleanly (not a FLAC)", oks[1], info[1])
    check("an empty file fails cleanly", oks[2], info[2]); check("a non-FLAC file fails cleanly", oks[3], info[3]); check("a card that stops answering fails with the audio so far intact", oks[4], info[4])
    ok, why = real(good, d); check("real music: STREAMINFO MD5 through driver/stream/ring (mono, 16 and 24 bit) and a seek that lands on the sample" + (f" {why}" if ok else ""), ok, why if not ok else None)
    ok_san, why = damaged(build(d, 0, san=True), d, count=10, seed=9); check("the driver is clean under UBSan/ASan on damaged files", ok_san, why)
    for bug, name in ((40, "one pair too many kept after a seek"), (41, "no bridge over the wrap point"), (42, "no silent tail"), (43, "failed frames counted as delivered")):
        m = build(d, bug); caught = False
        if bug == 40: caught = not seeks(m, d)[0]
        if bug == 41: caught = not whole(m, d)[0]
        if bug == 42: caught = not edges(m, d)[0][0]
        if bug == 43:
            rng = random.Random(8)
            while True:
                frames, si, pcm, starts, blocks, ss = synth(rng, 0, n=30)
                if ((si[12] >> 1) & 7) == 1: break                                               # a stereo stream: its pairs stream out call by call, so a failed frame has staged pairs
            meta, off = make_file(rng, frames, si, starts, ss, pic=0); x = bytearray(frames); x[starts[11] - 1] ^= 0x10                      # the frame's CRC-16: every pair of it is staged before the failure
            res, segs, out = run(m, d, meta[:off] + bytes(x), plan="res@16"); caught = res is None or sum(c for _, c in segs) != len(out)
        check(f"mutant {bug} ({name}) is caught", caught)
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
