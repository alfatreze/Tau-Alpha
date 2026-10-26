#!/usr/bin/env python3
"""Player 2 MP3 frame driver (fw2/cores/mp3_frames_core.h): the REAL Helix decoder driven through the REAL stream reader (card model with latency, jitter, odd ring sizes)
must produce exactly the PCM of a whole-file Helix decode with the gapless trim applied (the expected window comes from an independent Python parse of the Xing/LAME
tag): ID3v2 skipped by a seek, every frame framed across ring wraps, trim exact to the pair, the P3 expectation (pairs_total) equals the pairs delivered, junk between
frames is skipped, a truncated file ends cleanly. Needs the owner's test music (not in the repo): skipped when absent. Mutants: no tag skip, no trim, wrong resync."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (("  " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
T = ROOT.parent / "test music"
files = [T / "gapless_tone/Gapless Tone Test/01 MP3 tone A.mp3", T / "Test Album/02 MacCunn MP3 128 44k.mp3", T / "Test Album/05 MacCunn MP3 320 48k.mp3", T / "Test Album/11 Speech MP3 128 44k (Five Boons of Life, Twain, LibriVox).mp3"]
files = [f for f in files if f.exists()]
if not files:
    print("skip: test music not present"); sys.exit(0)
H = ROOT / "third_party/libhelix-mp3"

def lame(b):
    o = 10 + ((b[6] << 21) | (b[7] << 14) | (b[8] << 7) | b[9]) if b[:3] == b"ID3" else 0
    for i in range(o, o + 80):
        if b[i:i + 4] in (b"Xing", b"Info"):
            fl = int.from_bytes(b[i + 4:i + 8], "big"); e = i + 8; fr = 0
            if fl & 1: fr = int.from_bytes(b[e:e + 4], "big"); e += 4
            if fl & 2: e += 4
            if fl & 4: e += 100
            if fl & 8: e += 4
            x = b[e + 21:e + 24]
            return fr, (x[0] << 4) | (x[1] >> 4), ((x[1] & 15) << 8) | x[2]
    return 0, 0, 0

def build_all(d, bug):
    src = d / f"src{bug}"; src.mkdir()
    for f in list((H / "real").glob("*.c")) + list((H / "real").glob("*.h")): (src / f.name).write_bytes(f.read_bytes())
    (src / "assembly.h").write_bytes((ROOT / "sim/helix_host/assembly.h").read_bytes())
    for f in ("mp3dec.c", "mp3tabs.c"): (src / f).write_bytes((H / f).read_bytes())
    exe = d / f"p2mp3_{bug}"
    r = subprocess.run(["cc", "-std=gnu11", "-O2", "-w", f"-DBUG={bug}", "-I", str(src), "-I", str(H / "pub"), "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_mp3_harness.c")] + [str(p) for p in src.glob("*.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe

def reference(d):
    src = d / "refsrc"; src.mkdir()
    for f in list((H / "real").glob("*.c")) + list((H / "real").glob("*.h")): (src / f.name).write_bytes(f.read_bytes())
    (src / "assembly.h").write_bytes((ROOT / "sim/helix_host/assembly.h").read_bytes())
    for f in ("mp3dec.c", "mp3tabs.c"): (src / f).write_bytes((H / f).read_bytes())
    exe = d / "ref"
    r = subprocess.run(["cc", "-std=gnu11", "-O2", "-w", "-I", str(src), "-I", str(H / "pub"), "-o", str(exe), str(ROOT / "sim/mp3_decode_host.c")] + [str(p) for p in src.glob("*.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe

def run(exe, path, out, cap=24576, seed=1, lat=5000, jit=2000, seek=0, nout=0, tail=0):
    r = subprocess.run([str(exe), str(path), str(out), str(cap), str(seed), str(lat), str(jit), str(seek), str(nout), str(tail)], capture_output=True, text=True)
    return {k: int(v) for k, v in (l.split("=") for l in r.stdout.split())}

def expected(ref, path, d, data=None):
    p = path
    if data is not None: p = d / "mod.mp3"; p.write_bytes(data)
    b = p.read_bytes()
    subprocess.run([str(ref), str(p)], stdout=open(d / "ref.pcm", "wb"), stderr=subprocess.DEVNULL)
    pcm = (d / "ref.pcm").read_bytes()
    fr, de, pa = lame(b)
    return pcm, fr, de, pa

def scenarios(exe, ref, d):
    res = {}
    for f in files:
        pcm, fr, de, pa = expected(ref, f, d)
        r = run(exe, f, d / "o.pcm"); got = (d / "o.pcm").read_bytes()
        ch, spf = r["ch"], r["spf"]
        S, P = spf + de + 529, fr * spf - de - pa
        want = pcm[S * ch * 2:(S + P) * ch * 2] if (fr and (de or pa) and pa >= 529) else pcm
        res[f.name] = (got == want and len(want) > 0, r, len(got), len(want))
    # odd ring sizes and different timing, one file
    f = files[0]; pcm, fr, de, pa = expected(ref, f, d)
    r0 = run(exe, f, d / "o.pcm")
    S, P = r0["spf"] + de + 529, fr * r0["spf"] - de - pa; want = pcm[S * r0["ch"] * 2:(S + P) * r0["ch"] * 2]
    ok = len(want) > 0
    for cap, seed, lat, jit in ((24576, 2, 3000, 6000), (12288 + 100, 3, 9000, 9000), (8192 + 7, 4, 1000, 100), (30000, 5, 20000, 1)):
        run(exe, f, d / "o2.pcm", cap, seed, lat, jit); ok &= (d / "o2.pcm").read_bytes() == want
    res["odd rings and timings"] = (ok, r0, 0, 0)
    # junk between frames: the reference resyncs the same way
    b = bytearray(f.read_bytes()); mid = len(b) // 2
    b[mid:mid] = bytes((i * 37 + 11) & 0xFF for i in range(777))
    (d / "junk.mp3").write_bytes(bytes(b))
    pcmj, _, _, _ = expected(ref, d / "junk.mp3", d)
    rj = run(exe, d / "junk.mp3", d / "oj.pcm"); gotj = (d / "oj.pcm").read_bytes()
    res["junk"] = (rj["resyncs"] >= 1 and rj["status"] == 1 and len(gotj) > 0.9 * len(want), rj, len(gotj), len(want))
    # a fake frame header in the middle (valid-looking 128 kbps 44.1 kHz header, but only 100 bytes of junk after it): the real frames around it must all survive, so the output equals the clean decode
    f2 = [x for x in files if "MacCunn MP3 128" in x.name]
    if f2:
        b = f2[0].read_bytes(); o = 10 + ((b[6] << 21) | (b[7] << 14) | (b[8] << 7) | b[9]); n = 0
        while n < 2000 and o + 4 < len(b):
            assert b[o] == 0xFF; o += 144 * 128000 // 44100 + ((b[o + 2] >> 1) & 1); n += 1
        (d / "fake.mp3").write_bytes(b[:o] + b"\xFF\xFB\x90\x00" + bytes((i * 91 + 5) & 0x7F for i in range(96)) + b[o:])
        pcmc, fr, de, pa = expected(ref, f2[0], d); r2 = run(exe, f2[0], d / "oc.pcm"); S, P = r2["spf"] + de + 529, fr * r2["spf"] - de - pa
        run(exe, d / "fake.mp3", d / "of.pcm")
        res["fake header survives"] = ((d / "of.pcm").read_bytes() == (d / "oc.pcm").read_bytes() and len((d / "oc.pcm").read_bytes()) > 0, r2, 0, 0)
    # truncated file: ends cleanly (EOF, no error state), delivers the frames that are whole
    (d / "trunc.mp3").write_bytes(f.read_bytes()[: len(f.read_bytes()) // 3 + 5])
    rt = run(exe, d / "trunc.mp3", d / "ot.pcm")
    res["truncated"] = (rt["status"] == 1 and rt["pairs_out"] > 0, rt, 0, 0)
    return res


def seek_tests(exe, ref, d):
    """Seek to arbitrary source pairs: the output must equal the whole-file decode from that pair (bit-exact), because the restart is 3 frames early and the warm-up is dropped."""
    import random
    out = {}
    for f in files:
        pcm, fr, de, pa = expected(ref, f, d)
        r0 = run(exe, f, d / "o.pcm"); ch, spf = r0["ch"], r0["spf"]
        trimmed = bool(fr and (de or pa) and pa >= 529)
        S, P = (spf + de + 529, fr * spf - de - pa) if trimmed else (0, len(pcm) // (2 * ch))
        want = pcm[S * ch * 2:(S + P) * ch * 2] if trimmed else pcm
        rnd = random.Random(7); N = 12000
        targets = [1, 700, spf, spf * 3 + 5, spf * 50 + 600, 3000, 4000, 5000, 7000, 9000, 20000, 60000, 150000, P // 2, P // 3 + 11, (P * 9) // 10, max(P - N - 3, 1)] + [rnd.randrange(1, max(P - N, 2)) for _ in range(4)]
        res = []
        for T in targets:
            rr = run(exe, f, d / "s.pcm", seek=T, nout=N); got = (d / "s.pcm").read_bytes()
            shift = None
            for k in (0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5):          # the landing frame is estimated (TOC/CBR): a whole number of frames off at most
                a = T + k * spf
                if a >= 0 and len(got) > 0 and got == want[a * ch * 2:(a + N) * ch * 2][:len(got)]: shift = k; break
            res.append((T, rr["plan_ok"], rr["status"], shift is not None and len(got) >= N * ch * 2 * 0.9, shift))
        out[f.name] = res
    return out

def skip_tests(exe, ref, d):
    """Skip silence: lead = a seek to `lead` pairs, tail = the track ends `tail` pairs early. The output must equal the whole-file decode from the landed lead to P - tail (the tail is exact; the lead lands within the seek's frames)."""
    out = {}
    for f in files:
        pcm, fr, de, pa = expected(ref, f, d)
        r0 = run(exe, f, d / "o.pcm"); ch, spf = r0["ch"], r0["spf"]
        if not (fr and (de or pa) and pa >= 529): continue                        # the tail needs the length: tagged files only
        S, P = spf + de + 529, fr * spf - de - pa; want = pcm[S * ch * 2:(S + P) * ch * 2]
        res = []
        for lead, tail in ((0, 4000), (0, 90000), (5000, 3000), (120000, 80000), (300000, 0)):
            if lead + tail + 20000 >= P: continue
            rr = run(exe, f, d / "k.pcm", seek=lead, tail=tail); got = (d / "k.pcm").read_bytes()
            shift = None
            for k in (0, 1, -1, 2, -2, 3, -3):
                a = lead + k * spf if lead else 0
                e = (P - tail + k * spf) if lead else (P - tail)               # a landing that is k frames off moves the whole window, both ends, by k frames (the label drives the end too)
                if a >= 0 and got == want[a * ch * 2:e * ch * 2] and len(got) > 0: shift = k; break
            if shift is None and b"Xing" in f.read_bytes()[:2000] and lead:          # a true VBR file lands by its 8-bit TOC: the window moves by the landing error and the label-driven end moves with it, so the end is only within a few frames
                for k in (0, 1, -1, 2, -2, 3, -3):
                    a = lead + k * spf
                    if a >= 0 and len(got) > 0 and got == want[a * ch * 2:a * ch * 2 + len(got)] and abs(len(got) // (ch * 2) - (P - tail - lead)) <= 4 * spf: shift = k; break
            res.append((lead, tail, shift is not None, shift, rr["pairs_total"] == P - tail))
        out[f.name] = res
    return out

with tempfile.TemporaryDirectory() as td:
    d = Path(td); ref = reference(d)
    exe0 = build_all(d, 0)
    good = scenarios(exe0, ref, d)
    for k, (ok, r, g, w) in good.items():
        check(f"{k}: trimmed PCM equals the whole-file decode" if k.endswith("mp3") else k, ok, (g, w))
    for k, (ok, r, g, w) in good.items():
        if k.endswith("mp3"): check(f"{k}: pairs_total (the P3 expectation) == pairs delivered", r["pairs_total"] in (0, r["pairs_out"]) and (r["pairs_total"] or not r["xing"]), (r["pairs_total"], r["pairs_out"]))
    sk = seek_tests(exe0, ref, d)
    for k, res in sk.items():
        bad = [x for x in res if not (x[1] and x[3])]
        check(f"{k}: {len(res)} seeks play correct, continuous audio from the target (landing within 3 frames; shifts {sorted(set(x[4] for x in res if x[4] is not None))})", not bad and all(abs(x[4]) <= 3 for x in res), bad)
    sk2 = skip_tests(exe0, ref, d)
    for k, res in sk2.items():
        check(f"{k}: skip silence (lead {[x[0] for x in res]}, tail {[x[1] for x in res]}) plays exactly the window, P3 expectation follows", bool(res) and all(x[2] and x[4] for x in res), res)
    r = good[files[0].name][1]; check("ID3v2 tag skipped by a seek", r["tag"] > 0 and r["errors"] <= 2, r)
    for bug, name in ((1, "tag not skipped"), (2, "trim ignored"), (3, "frame window too small"), (4, "seek without warm-up"), (5, "seek landing not dropped"), (6, "tail forgotten"), (7, "sync word trusted without the next header")):
        eb = build_all(d, bug)
        caught = any(not v[0] for v in scenarios(eb, ref, d).values())
        if not caught: caught = any(not (x[1] and x[3]) or abs(x[4] or 0) > 3 for res in seek_tests(eb, ref, d).values() for x in res)
        if not caught: caught = any(not (x[2] and x[4]) for res in skip_tests(eb, ref, d).values() for x in res)
        check(f"mutant {bug} ({name}) is caught", caught)
print("FAILED" if fails else "all ok", fails)
sys.exit(1 if fails else 0)
