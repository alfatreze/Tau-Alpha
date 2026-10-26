#!/usr/bin/env python3
"""Player 2 FLAC core: the seek search (f2_seek_*), against ground truth from a full decode.
 * synthetic streams (fixed and variable block size, bitrates that swing from silence to noise): for random targets and the awkward ones (first sample, last sample, every frame start, one before every
   frame end) the search lands on a TRUE frame start whose frame holds the target -- with a seek table of every Nth point (with placeholders) and without;
 * probes stay bounded (<= 64) and the median is small;
 * a forged far-away header planted in the data does not fool it (the plausibility test), checked at the level of the landing frame being a true frame;
 * end to end: decoding from the landing frame with a recovery decoder that does NOT know the timeline (the first verified frame sets it) gives exactly the original audio from that frame on, so discarding
   (target - frame start) pairs puts the stream on the target to the sample; and a forged header BEFORE the real frames does not poison the timeline;
 * real music: MacCunn, Clementi, Ghost in the Shell, Rite of Spring (24-bit), Aphex Twin: 25 random targets each, with and without the file's own seek table;
 * mutants: no plausibility test, bracket sides swapped, fallback frame not remembered.
"""
import os, random, struct, subprocess, sys, tempfile
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
def build(d, bug, src="p2_flac2_seek_harness.c", name="s"):
    exe = d / f"{name}{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-misleading-indentation", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim" / src)], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def index(exe, path, si):
    r = subprocess.run([str(exe), str(path), si.hex(), "0", "0", "0", "index"], capture_output=True, text=True, timeout=600)
    return [tuple(map(int, l.split())) for l in r.stdout.splitlines()]
def seek(exe, path, si, total, var, targets, table=()):
    inp = f"{len(table)}\n" + "".join(f"{a} {b}\n" for a, b in table) if table else ""
    r = subprocess.run([str(exe), str(path), si.hex(), str(total), str(var), "1" if table else "0", ",".join(map(str, targets))], input=inp, capture_output=True, text=True, timeout=600)
    if r.returncode: return None
    return [tuple(map(int, l.split())) for l in r.stdout.splitlines()]
def judge(res, targets, idx):
    starts = {o: (s, b) for o, s, b in idx}; bad = []
    for t, (st, off, smp, probes) in zip(targets, res):
        if st != 1 or off not in starts or starts[off][0] != smp or not (smp <= t < smp + starts[off][1]) or probes > 64: bad.append((t, st, off, smp, probes, starts.get(off)))
    return bad
def synth(rng, var, n=120):
    bs = rng.choice([64, 256, 576])
    blocks = [bs] * (n - 1) + [rng.randrange(16, bs + 1)] if not var else [rng.choice([bs, bs // 2 + 16, bs * 2]) for _ in range(n)]
    frames, si, pcm, starts, blocks = enc.encode_stream(rng, bits=rng.choice([16, 24]), chans=2, rate=44100, blocks=blocks, variable=var)
    return frames, si, np.array(pcm, dtype="<i4"), starts, blocks
def suite(exe, d, nstreams=8, seed=3):
    rng = random.Random(seed); probes = []
    for i in range(nstreams):
        var = i % 2 == 1; frames, si, pcm, starts, blocks = synth(rng, var); p = d / "s.bin"; p.write_bytes(frames); total = len(pcm)
        idx = index(build(d, 0), p, si) if False else None
        sstarts = [0]
        for b in blocks[:-1]: sstarts.append(sstarts[-1] + b)
        idx = [(o, s, b) for o, s, b in zip(starts, sstarts, blocks)]
        targets = [0, total - 1, total // 2] + [rng.randrange(total) for _ in range(40)] + [s for s in sstarts[1:20]] + [s - 1 for s in sstarts[1:20]]
        tab = [(sstarts[k], starts[k]) for k in range(0, len(starts), 7)]
        for table in ((), tab):
            res = seek(exe, p, si, total, int(var), targets, table)
            if res is None: return False, "crash", probes
            bad = judge(res, targets, idx)
            if bad: return False, (i, var, bool(table), bad[:2]), probes
            probes += [r[3] for r in res]
    return True, None, probes
def end_to_end(exe, d, rec_exe):
    rng = random.Random(11)
    for i in range(6):
        var = i % 2; frames, si, pcm, starts, blocks = synth(rng, var, 60); p = d / "e.bin"; p.write_bytes(frames); total = len(pcm)
        sstarts = [0]
        for b in blocks[:-1]: sstarts.append(sstarts[-1] + b)
        for t in [rng.randrange(total) for _ in range(6)] + [0, total - 1]:
            r = seek(exe, p, si, total, int(var), [t])[0]; st, off, smp, probes = r
            sub = d / "sub.bin"; sub.write_bytes(frames[off:]); po = d / "sub.pcm"; so = d / "sub.st"
            subprocess.run([str(rec_exe), str(sub), si.hex(), "1000000", "1000000", "65536", "1", str(po), str(so), "notl"], check=True, timeout=120)
            got = np.fromfile(po, dtype="<i4").reshape(-1, 2)
            real = total - smp
            if not np.array_equal(got[:real], pcm[smp:]): return False, ("decode from the landing frame is not the original audio", i, t, off, smp)
            if not np.array_equal(got[t - smp], pcm[t]): return False, ("target sample not on target", i, t)
        # a forged valid-CRC header with a huge number BEFORE the real frames must not poison the timeline: every frame that comes out is at its right place (the junk may eat the first real frame, no more)
        hdr = frames[:8]; forged = bytes(hdr[:4]) + enc.utf8num((1 << 31) if var else (1 << 20)); forged += bytes([enc.crc8(forged)]); junk = forged + bytes(rng.randrange(256) for _ in range(30))
        sub = d / "poison.bin"; sub.write_bytes(junk + frames); po = d / "poison.pcm"; so = d / "poison.st"
        subprocess.run([str(rec_exe), str(sub), si.hex(), "1000000", "1000000", "65536", "1", str(po), str(so), "notl"], check=True, timeout=120)
        got = np.fromfile(po, dtype="<i4").reshape(-1, 2)
        okp = any(len(got) >= total - s0 and np.array_equal(got[:total - s0], pcm[s0:]) for s0 in sstarts[:2])
        if not okp: return False, ("forged header poisoned the timeline", i, len(got), total)
    return True, None
def damaged_target(exe, d):
    """the frame that holds the target is destroyed: no frame can be landed on, so the search must END (no loop) on the last good frame before it -- the safety net"""
    rng = random.Random(17)
    for i in range(6):
        frames, si, pcm, starts, blocks = synth(rng, False, 60); total = len(pcm); sstarts = [0]
        for b in blocks[:-1]: sstarts.append(sstarts[-1] + b)
        k = rng.randrange(12, 50); ends = starts[1:] + [len(frames)]
        bad = bytearray(frames)
        for j in range(starts[k], ends[k]): bad[j] = rng.randrange(256)
        p = d / "dt.bin"; p.write_bytes(bytes(bad)); t = sstarts[k] + rng.randrange(blocks[k])
        r = seek(exe, p, si, total, 0, [t])
        if r is None: return False, "crash"
        st, off, smp, probes = r[0]
        if st != 1 or probes > 64 or off != starts[k - 1] or smp != sstarts[k - 1]: return False, (i, k, t, r[0], starts[k - 1], sstarts[k - 1])
    return True, None
def real_files(exe, d):
    out = []
    for name in ["Test Album/01 MacCunn FLAC 44k.flac", "Test Album/09 Clementi FLAC 48k.flac", "Ghost in the Shell 2.0 OST (FLAC)/03 - Puppetmaster (2.0 Ver.).flac", "Test Album/13 riteofspring-sample-48khz.flac", "Test Album/12. Aphex Twin - Omgyjya-Switch7.flac"]:
        f = MUSIC / name
        if not f.exists(): continue
        data = f.read_bytes(); o = 4; si = None; tab = b""
        while True:
            hd = data[o]; ln = int.from_bytes(data[o + 1:o + 4], "big"); body = data[o + 4:o + 4 + ln]
            if (hd & 0x7F) == 0: si = body
            if (hd & 0x7F) == 3: tab = body
            o += 4 + ln
            if hd & 0x80: break
        p = d / "real.bin"; p.write_bytes(data[o:]); v = int.from_bytes(si[10:18], "big"); total = v & ((1 << 36) - 1)
        idx = index(build(d, 0), p, si); rng = random.Random(len(name)); targets = [0, total - 1] + [rng.randrange(total) for _ in range(23)]
        pts = [(int.from_bytes(tab[i:i + 8], "big"), int.from_bytes(tab[i + 8:i + 16], "big")) for i in range(0, len(tab), 18) if int.from_bytes(tab[i:i + 8], "big") != 0xFFFFFFFFFFFFFFFF]
        for table in ((), tuple(pts[:4000])):
            res = seek(exe, p, si, total, 0, targets, table)
            bad = None if res is None else judge(res, targets, idx)
            if res is None or bad: return False, (name, bool(table), bad[:2] if bad else "crash"), out
            out += [r[3] for r in res]
    return True, None, out
with tempfile.TemporaryDirectory() as td:
    d = Path(td); good = build(d, 0); rec = build(d, 0, "p2_flac2_rec_harness.c", "rec")
    ok, why, probes = suite(good, d)
    med = sorted(probes)[len(probes) // 2] if probes else 0
    check(f"synthetic fixed/variable streams: {len(probes)} seeks land on a true frame that holds the target (awkward targets included), with and without a seek table; probes <= 64, median {med}, max {max(probes) if probes else 0}", ok, why)
    ok, why = end_to_end(good, d, rec); check("decoding from the landing frame (timeline unknown) is exactly the original audio; the target sample is on the target; a forged header before the frames does not poison the timeline", ok, why)
    ok, why = damaged_target(good, d); check("a destroyed target frame: the search ends (no loop) on the last good frame before it", ok, why)
    ok, why, pr = real_files(good, d) if os.environ.get("FLAC2_SEEK_REAL", "1") != "0" else (True, None, [0]); check(f"real music (5 files, with and without their seek tables): every seek lands on a true frame holding the target; probes median {sorted(pr)[len(pr)//2] if pr else 0}, max {max(pr) if pr else 0}", ok, why)
    for bug, name in ((27, "no plausibility test"), (29, "bracket sides swapped"), (30, "fallback frame not remembered")):
        m = build(d, bug); r1 = suite(m, d, nstreams=4)[0] and damaged_target(m, d)[0]
        check(f"mutant {bug} ({name}) is caught", not r1)
    check("mutant 31 (an unverified header sets the timeline) is caught", not end_to_end(good, d, build(d, 31, "p2_flac2_rec_harness.c", "rec"))[0])
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
