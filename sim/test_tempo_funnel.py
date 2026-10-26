#!/usr/bin/env python3
"""Host test for fw/tempo_core.h (Cymo C7 T2, B-557): the funnel between the decoder and the FIFO. Decoded frames of any size, mono or stereo, go through
the staging ring (word-wide, planar, wrapping), the 64-sample chunking with its odd-sample handling, the core v2 and the drain loop; the output must be exactly what the
independent integer twin (tools/lab/wsola_fixed_ref.py, whole input at once) produces for the same samples. Frame sizes include an MP3 frame, half of one, an odd one and
one sample at a time; the input is longer than the staging ring so it wraps. An abort (the pending-reload exit) must stop at once with the output so far intact. Seven broken
copies of the funnel must each fail."""
import math, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools/lab"))
import wsola_fixed_ref as ref

fails = 0


def check(name, cond, info=""):
    global fails
    print(("ok   " if cond else "FAIL ") + name + (f"  {info}" if info and not cond else ""))
    fails += 0 if cond else 1


def voice(n, fs, seed=1):
    sin_t = [int(round(math.sin(2 * math.pi * i / 4096) * 32767)) for i in range(4096)]
    out, ph, lcg = [], 0, seed
    for i in range(n):
        f = 130 + 28 * math.sin(2 * math.pi * 0.6 * i / fs)
        ph = (ph + int(f / fs * 4096 * 65536)) & 0xFFFFFFF
        acc = sum(sin_t[((ph >> 16) * h) & 4095] // (h + 1) for h in (1, 2, 3, 4, 5))
        env = 0.55 + 0.4 * math.sin(2 * math.pi * 1.3 * i / fs)
        lcg = (lcg * 1103515245 + 12345) & 0x7FFFFFFF
        out.append(max(-32768, min(32767, int(acc * env * 0.55) + ((lcg >> 8) & 255) - 128)))
    return out


XFLAGS = sys.argv[1:]          # extra compiler flags, e.g. -DTEMPO_SLICE=1 (the sliced output path, RAM diet): the same checks run against it


def build(src_root, exe):
    r = subprocess.run(["cc", "-O2", "-Wall", "-Wextra", "-Werror", *XFLAGS, "-o", str(exe), str(src_root / "sim/tempo_funnel_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); raise SystemExit("harness build failed")


def run(exe, chans, fs, spd, frame, tmp, abort_at=None):
    n = len(chans[0]); data = bytearray()
    for i in range(n):
        for c in chans:
            data += struct.pack("<h", c[i])
    Path(tmp / "in.raw").write_bytes(bytes(data))
    args = [str(exe), str(tmp / "in.raw"), str(tmp / "out.raw"), str(fs), str(len(chans)), str(spd), str(frame)] + ([str(abort_at)] if abort_at is not None else [])
    r = subprocess.run(args, capture_output=True, text=True)
    if r.returncode != 0 or len(r.stderr.split()) != 3:
        return None, 0, 0                      # a crash (an out-of-bounds write) is a failure of the broken copy
    pairs, frames, aborted = (int(v) for v in r.stderr.split())
    b = Path(tmp / "out.raw").read_bytes()
    v = struct.unpack("<%dh" % (len(b) // 2), b)
    k = len(chans)
    return [list(v[c::k]) for c in range(k)], pairs, aborted


with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    exe = td / "h"; build(ROOT, exe)
    mono = voice(44100 * 2, 44100)
    mono_b = voice(44100 * 2, 44100, seed=7)
    lo22 = voice(22050 * 2, 22050)
    cases = [("mono 1.5x, 1152-sample frames", [mono], 44100, 384, 1152), ("mono 1.5x, 576-sample frames", [mono], 44100, 384, 576),
             ("mono 1.5x, odd 1153-sample frames", [mono], 44100, 384, 1153), ("mono 2.0x, 1152-sample frames", [mono], 44100, 512, 1152),
             ("mono 1.1x, 1152-sample frames", [mono], 44100, 282, 1152), ("mono 1.5x, one sample per frame", [mono[:20000]], 44100, 384, 1),
             ("stereo 1.5x, 2304-sample frames (1152 pairs)", [mono, mono_b], 44100, 384, 2304), ("stereo 1.5x, 1152-sample frames (576 pairs)", [mono, mono_b], 44100, 384, 1152),
             ("stereo 1.5x, 2306-sample frames (1153 pairs, odd)", [mono, mono_b], 44100, 384, 2306), ("mono 22.05 kHz 1.5x, 576-sample frames", [lo22], 22050, 384, 576)]
    expected_cache = {}
    for name, chans, fs, spd, frame in cases:
        got, pairs, aborted = run(exe, chans, fs, spd, frame, td)
        n = len(chans[0])
        # an odd count of samples leaves the last one waiting for its partner: it is not staged
        key = (id(chans), fs, spd, n - (n & 1))
        if key not in expected_cache:
            expected_cache[key] = ref.run([c[: n - (n & 1)] for c in chans], fs, spd)[0]
        want = expected_cache[key]
        check(f"funnel == independent twin: {name} ({pairs} pairs)", got == want and aborted == 0)
    # abort: the pending-reload exit stops at once and what was output is intact
    exp = ref.run([mono], 44100, 384)[0]
    got, pairs, aborted = run(exe, [mono], 44100, 384, 1152, td, abort_at=700)
    check("an abort stops at the pair asked for, the output so far is the start of the stream", aborted == 1 and pairs == 700 and got[0] == exp[0][:700])
    # five broken copies
    src = (ROOT / "fw/tempo_core.h").read_text()
    muts = [("reader takes the low half for an odd sample", "if (idx & 1u) { *dst++ = (int16_t)(w >> 16); i++; n--; }", "if (idx & 1u) { *dst++ = (int16_t)w; i++; n--; }"),
            ("the odd sample is dropped instead of kept", "if (k & 1u) { t->odd_l = tl[k - 1u]; t->odd_r = tr[k - 1u]; t->have_odd = 1u; k--; }", "if (k & 1u) { k--; }"),
            ("ring written one word off", "const uint32_t base = t->written >> 1;", "const uint32_t base = (t->written >> 1) + 1u;"),
            ("ring words not wrapped (a chunk straddling the end runs past it)", "const uint32_t wi = (base + (i >> 1)) & (TEMPO_RING / 2u - 1u);", "const uint32_t wi = base + (i >> 1);"),
            ("right ring written with the left samples", "TEMPO_PS_R[wi] = (uint32_t)(uint16_t)tr[i] | ((uint32_t)(uint16_t)tr[i + 1u] << 16);", "TEMPO_PS_R[wi] = (uint32_t)(uint16_t)tl[i] | ((uint32_t)(uint16_t)tl[i + 1u] << 16);"),
            ("a stereo hop pushes the left channel twice", "TEMPO_PUSH(t->out_l[i], st ? t->out_r[i] : t->out_l[i])", "TEMPO_PUSH(t->out_l[i], st ? t->out_l[i] : t->out_l[i])"),
            ("the last pair of every hop is not pushed", "for (uint32_t i = 0; i < h; i++)\n            if (!TEMPO_PUSH", "for (uint32_t i = 0; i + 1u < h; i++)\n            if (!TEMPO_PUSH")]
    if "-DTEMPO_SLICE=1" in XFLAGS:
        muts += [("slices: the last pair of every slice is not pushed", "for (uint32_t i = 0; i < n; i++)\n                if (!TEMPO_PUSH", "for (uint32_t i = 0; i + 1u < n; i++)\n                if (!TEMPO_PUSH"),
                 ("slices: the slice start is not advanced", "for (uint32_t a = 0; a < h; a += TEMPO_SLICE_N)", "for (uint32_t a = 0; a < h; a += h)")]
    if "-DTEMPO_SLICE=1" in XFLAGS:
        muts = [m for m in muts if m[0] != "the last pair of every hop is not pushed"]     # that loop is the whole-hop path, compiled out here
    for name, a, b in muts:
        assert a in src, a
        md = td / "mut"; (md / "fw").mkdir(parents=True, exist_ok=True); (md / "sim").mkdir(exist_ok=True)
        (md / "fw/tempo_core.h").write_text(src.replace(a, b))   # every occurrence: the hop is pushed by two code paths (whole hop, slices)
        for fn in ("wsola_core.h", "wsola_tables.h"):
            (md / "fw" / fn).write_text((ROOT / "fw" / fn).read_text())
        (md / "sim/tempo_funnel_harness.c").write_text((ROOT / "sim/tempo_funnel_harness.c").read_text())
        mexe = td / "mutexe"; build(md, mexe)
        ch2 = [mono, mono_b]
        want2 = ref.run(ch2, 44100, 384)[0]
        got2, _, _ = run(mexe, ch2, 44100, 384, 2306, td)
        check(f"mutant caught: {name}", got2 is None or got2 != want2)

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
