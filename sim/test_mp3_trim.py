#!/usr/bin/env python3
"""fw/mp3_trim.h (gapless trim, B-737). (1) Arithmetic against an independent model over random (frames, spf, delay, padding): the kept pairs of all frames concatenate to
exactly [S, S+P), nothing outside, no pair twice; refusal of absent/insane numbers. (2) When the owner's test music is present (not in the repo, so skipped otherwise): the real
Helix decoder (sim/mp3_decode_host.c) decodes four Test Album MP3s, the trim is applied with the firmware's own header, and the result must equal the FLAC of the same
recording in length and line up with it at lag 0 at BOTH ends (a sample off in either direction is worse)."""
import random, re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include "mp3_trim.h"
int main(void)
{
    unsigned s = 12345; long bad = 0, cases = 0;
    for (int t = 0; t < 4000; t++) {
        s = s * 1664525u + 1013904223u; uint32_t spf = (s >> 16) & 1 ? 1152u : 576u;
        s = s * 1664525u + 1013904223u; uint32_t frames = 1u + ((s >> 8) %% 3000u);
        s = s * 1664525u + 1013904223u; uint32_t delay = (s >> 8) %% 2300u;
        s = s * 1664525u + 1013904223u; uint32_t pad = (s >> 8) %% 4096u;
        mp3_trim_cfg_t c; int ok = mp3_trim_cfg(&c, frames, spf, delay, pad);
        int want_ok = (delay || pad) && pad >= 529u && frames * spf > delay + pad;
        if (ok != want_ok) { bad++; continue; }
        if (!ok) continue;
        /* reference: the decoded stream has frames+1 frames; keep pair p iff S <= p < S+P */
        uint32_t S = spf + delay + 529u, P = frames * spf - delay - pad, next = S, total = 0;
        for (uint32_t fi = 0; fi < frames + 1u; fi++) {
            uint32_t first, count; mp3_trim_range(&c, fi, &first, &count);
            if (count) {
                uint32_t abs0 = fi * spf + first;
                if (abs0 != next || first + count > spf) { bad++; break; }
                next += count; total += count;
            }
        }
        cases++;
        if (total != P || next != S + P) bad++;
    }
    /* refusals */
    mp3_trim_cfg_t c;
    if (mp3_trim_cfg(&c, 0, 1152, 576, 800)) bad++;
    if (mp3_trim_cfg(&c, 100, 1152, 0, 0)) bad++;
    if (mp3_trim_cfg(&c, 1, 1152, 1000, 1000)) bad++;
    if (mp3_trim_cfg(&c, 100, 1152, 576, 100)) bad++;
    printf("%%s: %%ld cases, %%ld failures\n", bad ? "FAIL" : "ok", cases, bad);
    return bad != 0;
}
'''
MUTANTS = [("no decoder delay", "c->S = spf + delay + 529u;", "c->S = spf + delay;"),
           ("padding ignored", "c->P = total - delay - padding;", "c->P = total - delay;"),
           ("Info frame not counted", "c->S = spf + delay + 529u;", "c->S = delay + 529u;"),
           ("end not clipped", "hi = f1 < e ? f1 : e;", "hi = f1;")]
def run_c(hdr_text, tmp):
    (tmp / "mp3_trim.h").write_text(hdr_text)
    (tmp / "t.c").write_text(HARNESS % ())
    r = subprocess.run(["cc", "-O1", "-Wall", "-I", str(tmp), "-o", str(tmp / "t"), str(tmp / "t.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(tmp / "t")], capture_output=True, text=True)
def real_files(fails):
    try:
        import numpy as np
    except ImportError:
        print("skip real-file alignment (numpy missing)"); return fails
    T = ROOT.parent / "test music" / "Test Album"
    if not T.is_dir(): print("skip real-file alignment (test music not present)"); return fails
    H = ROOT / "third_party/libhelix-mp3"
    with tempfile.TemporaryDirectory() as d:
        d = Path(d); src = d / "src"; src.mkdir()
        for f in list((H / "real").glob("*.c")) + list((H / "real").glob("*.h")): (src / f.name).write_bytes(f.read_bytes())
        (src / "assembly.h").write_bytes((ROOT / "sim/helix_host/assembly.h").read_bytes())
        for f in ("mp3dec.c", "mp3tabs.c"): (src / f).write_bytes((H / f).read_bytes())
        exe = d / "mp3host"
        r = subprocess.run(["cc", "-std=gnu11", "-O2", "-w", "-I", str(src), "-I", str(H / "pub"), "-o", str(exe), str(ROOT / "sim/mp3_decode_host.c")] + [str(p) for p in src.glob("*.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        def lame(p):
            b = p.read_bytes()[:200000]
            o = 10 + ((b[6] << 21) | (b[7] << 14) | (b[8] << 7) | b[9]) if b[:3] == b"ID3" else 0
            for i in range(o, o + 64):
                if b[i:i + 4] in (b"Xing", b"Info"):
                    fl = int.from_bytes(b[i + 4:i + 8], "big"); e = i + 8; fr = 0
                    if fl & 1: fr = int.from_bytes(b[e:e + 4], "big"); e += 4
                    if fl & 2: e += 4
                    if fl & 4: e += 100
                    if fl & 8: e += 4
                    return fr, (b[e + 21] << 4) | (b[e + 22] >> 4), ((b[e + 22] & 15) << 8) | b[e + 23]
        def readwav(p):
            b = p.read_bytes(); i = 12
            while i < len(b):
                sz = int.from_bytes(b[i + 4:i + 8], "little")
                if b[i:i + 4] == b"data": return np.frombuffer(b[i + 8:i + 8 + sz], dtype="<i2").reshape(-1, 2).astype(np.int64)
                i += 8 + sz + (sz & 1)
        pairs = [("02 MacCunn MP3 128 44k.mp3", "01 MacCunn FLAC 44k.flac"), ("03 MacCunn MP3 320 44k.mp3", "01 MacCunn FLAC 44k.flac"),
                 ("07 Clementi MP3 128 44k.mp3", "06 Clementi FLAC 44k.flac"), ("08 Clementi MP3 320 44k.mp3", "06 Clementi FLAC 44k.flac")]
        for mp3, fl in pairs:
            if not (T / mp3).exists() or not (T / fl).exists(): continue
            subprocess.run([str(exe), str(T / mp3)], stdout=open(d / "m.pcm", "wb"), stderr=subprocess.DEVNULL)
            m = np.fromfile(d / "m.pcm", dtype="<i2").reshape(-1, 2).astype(np.int64)
            if subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16", str(T / fl), str(d / "f.wav")], capture_output=True).returncode:
                print("skip real-file alignment (afconvert missing)"); return fails
            f = readwav(d / "f.wav")
            fr, de, pa = lame(T / mp3)
            S, P = 1152 + de + 529, fr * 1152 - de - pa
            tr = m[S:S + P]
            ok = len(tr) == len(f)
            print(("ok   " if ok else "FAIL ") + f"{mp3}: trimmed length {len(tr)} equals the FLAC's {len(f)}"); fails += not ok
            def snr(a, b): return 10 * np.log10((np.mean(b.astype(float) ** 2) + 1e-9) / (np.mean((a - b) ** 2.0) + 1e-9))
            n = 100000
            for name, sl in (("start", slice(0, n)), ("end", slice(len(f) - n, len(f)))):
                best = max((snr(m[S + lag:S + lag + P][sl, 0], f[sl, 0]), -lag) for lag in (-2, -1, 0, 1, 2))
                ok = best[1] == 0
                print(("ok   " if ok else "FAIL ") + f"{mp3}: {name} aligned at lag 0 (SNR {best[0]:.1f} dB)"); fails += not ok
    return fails
def main():
    fails = 0
    with tempfile.TemporaryDirectory() as d:
        d = Path(d); base = (ROOT / "fw/mp3_trim.h").read_text()
        r = run_c(base, d); sys.stdout.write(r.stdout); fails += r.returncode != 0
        for name, a, b in MUTANTS:
            assert a in base, name
            m = d / "m"; m.mkdir(exist_ok=True)
            caught = run_c(base.replace(a, b, 1), m).returncode != 0
            print(("ok   mutant caught: " if caught else "FAIL mutant NOT caught: ") + name); fails += not caught
    fails = real_files(fails)
    print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
if __name__ == "__main__": main()
