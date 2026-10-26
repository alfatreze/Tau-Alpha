#!/usr/bin/env python3
"""B-727: fw/track_details.h (the real C that formats the track details page) against an independent Python formatter over random records, plus the string list's ids."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
C = r'''
#include <stdio.h>
#include <stdlib.h>
static const char *tab[];
#define TDET_S(i) tab[i]
#include "track_details.h"
#define X(n, s) s,
static const char *tab[] = { TDET_STRINGS(X) };
#undef X
int main(void)
{
    char cmd[16];
    while (scanf("%15s", &cmd) == 1) {
        char b[160]; char *q = b; unsigned a, c, d, e, f; int g;
        if (!strcmp(cmd, "trackno")) { scanf("%u", &a); q = tdet_trackno(q, a); }
        else if (!strcmp(cmd, "length")) { scanf("%u", &a); q = tdet_length(q, a); }
        else if (!strcmp(cmd, "format")) { scanf("%u %u %u %d", &a, &c, &d, &g); q = tdet_format(q, a, c, d, g); }
        else if (!strcmp(cmd, "audio")) { scanf("%u %u", &a, &c); q = tdet_audio(q, a, c); }
        else if (!strcmp(cmd, "bitrate")) { scanf("%u %u %u", &a, &c, &d); q = tdet_bitrate(q, a, c, d); }
        else if (!strcmp(cmd, "size")) { scanf("%u", &a); q = tdet_size(q, a); }
        else if (!strcmp(cmd, "gapless")) { scanf("%u %u %u %u", &a, &c, &d, &e); q = tdet_gapless(q, a, c, d, e); }
        else if (!strcmp(cmd, "gain")) { int cd; scanf("%d %u", &cd, &a); q = tdet_gain(q, cd, a); }
        else if (!strcmp(cmd, "cover")) { scanf("%u %u", &a, &c); q = tdet_cover(q, a, c); }
        (void)f; (void)e;
        *q = 0; puts(b);
    }
    printf("COUNT %d\n", (int)TDS_COUNT);
    return 0;
}
'''
VBR, LAME, SIDE, COVER = 1, 2, 4, 8

def fix(v, scale, digits): return f"{v // scale}" + (f".{v % scale:0{digits}d}" if digits else "")
def py_gain(cdb, peak):
    s = ("-" if cdb < 0 else "+") + fix(abs(cdb), 100, 2) + " DB"
    if peak: s += "  PEAK " + fix((peak * 10000 + 16384) // 32768, 10000, 4)
    return s
def py(cmd, a):
    if cmd == "trackno":
        t = a[0]; s = ""
        if t >> 10: s += f"DISC {t >> 10}  "
        if t & 1023: s += f"TRACK {t & 1023}"
        return s
    if cmd == "length": return "" if not a[0] else f"{a[0] // 60}:{(a[0] % 60) // 10}{a[0] % 10}"
    if cmd == "format":
        codec, bits, flags, has = a
        s = "FLAC" if codec == 2 else "MP3"
        if has and codec == 2 and bits: s += f" {bits} BIT"
        if has and codec == 1: s += "  VBR" if flags & VBR else "  CBR"
        return s
    if cmd == "audio": return "" if not a[0] else fix(a[0] // 100, 10, 1) + " KHZ  " + ("MONO" if a[1] == 1 else "STEREO")
    if cmd == "bitrate":
        k, codec, flags = a
        return "" if not k else f"{k}" + (" KBPS" if codec == 1 and not flags & VBR else " KBPS AVG")
    if cmd == "size": return "" if not a[0] else fix((a[0] + 52428) // 104858, 10, 1) + " MB"
    if cmd == "gapless":
        codec, flags, delay, pad = a
        return "EXACT, NO TRIM" if codec == 2 else (f"DELAY {delay}  PAD {pad}" if flags & LAME else "")
    if cmd == "gain": return py_gain(*a)
    if cmd == "cover":
        flags, kib = a; s = ""
        if flags & SIDE: s += "SIDECAR 128 PX"
        if flags & SIDE and flags & COVER: s += " + "
        if flags & COVER: s += f"EMBEDDED {kib} KB"
        if not flags & (SIDE | COVER): s += "NONE"
        return s
R = random.Random(27)
cases = []
for _ in range(1500):
    cases.append(("trackno", [R.choice((0, 5, 1 << 10 | 7, 2 << 10 | 12, 63 << 10 | 1023, R.randint(0, 65535)))]))
    cases.append(("length", [R.choice((0, 59, 60, 61, 599, 3600, R.randint(0, 65535)))]))
    cases.append(("format", [R.choice((1, 2)), R.choice((0, 8, 16, 24)), R.randint(0, 255), R.randint(0, 1)]))
    cases.append(("audio", [R.choice((0, 22050, 32000, 44100, 48000, 88200, 96000, R.randint(0, 200000))), R.choice((1, 2))]))
    cases.append(("bitrate", [R.choice((0, 32, 128, 320, 987, 65535)), R.choice((1, 2)), R.randint(0, 255)]))
    cases.append(("size", [R.choice((0, 1, 52427, 52428, 1048576, 14848563, 2961218, 4294967295, R.randint(0, 2 ** 32 - 1)))]))
    cases.append(("gapless", [R.choice((1, 2)), R.randint(0, 255), R.choice((0, 576, 4095)), R.choice((0, 1668, 4095))]))
    cases.append(("gain", [R.choice((0, -431, 587, -3000, 1800, R.randint(-3000, 1800))), R.choice((0, 12891, 32395, 32768, 65535, R.randint(0, 65535)))]))
    cases.append(("cover", [R.randint(0, 255), R.choice((0, 36, 156, 65535, R.randint(0, 65535)))]))
fails = 0
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    (td / "t.c").write_text("#include <string.h>\n" + C)
    r = subprocess.run(["cc", "-O1", "-w", "-I", str(ROOT / "fw"), "-o", str(td / "t"), str(td / "t.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stderr); sys.exit(1)
    inp = "".join(f"{c} " + " ".join(str(x) for x in a) + "\n" for c, a in cases)
    out = subprocess.run([str(td / "t")], input=inp, capture_output=True, text=True).stdout.split("\n")
    got = out[:len(cases)]
    bad = [(c, a, g, py(c, a)) for (c, a), g in zip(cases, got) if g != py(c, a)]
    print(("ok   " if not bad else "FAIL ") + f"{len(cases)} random fields: the firmware formatter equals the independent formatter", bad[:3] if bad else "")
    fails += 1 if bad else 0
    cnt = int(out[len(cases)].split()[1])
    print("ok   " if cnt == 57 else "FAIL ", f"string list has {cnt} entries")
    fails += 0 if cnt == 57 else 1
    for c, a, want in (("gain", [-431, 32395], "-4.31 DB  PEAK 0.9886"), ("gain", [587, 12891], "+5.87 DB  PEAK 0.3934"), ("size", [14848563], "14.2 MB"), ("audio", [44100, 2], "44.1 KHZ  STEREO"),
                       ("cover", [12, 156], "SIDECAR 128 PX + EMBEDDED 156 KB"), ("gapless", [1, 2, 576, 1668], "DELAY 576  PAD 1668"), ("format", [1, 0, 3, 1], "MP3  VBR"), ("trackno", [2 << 10 | 5], "DISC 2  TRACK 5")):
        g = subprocess.run([str(td / "t")], input=f"{c} " + " ".join(map(str, a)) + "\n", capture_output=True, text=True).stdout.split("\n")[0]
        print(("ok   " if g == want else "FAIL ") + f"{c} {a} -> {want!r}", "" if g == want else g)
        fails += 0 if g == want else 1
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
