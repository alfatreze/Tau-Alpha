#!/usr/bin/env python3
"""fw/replaygain.h (B-599): parsing of the tag values, the dB-to-factor maths, mode picking, the attenuate-only fold into the volume target, and the ID3v2 / Vorbis
readers against REAL tags written by mutagen (v2.3 and v2.4, Latin-1, UTF-8 and UTF-16 TXXX, with and without a large APIC in front)."""
import math, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "%s"
int main(int argc, char **argv)
{
    char cmd[16];
    while (scanf("%%15s", cmd) == 1) {
        if (!strcmp(cmd, "parse")) {                      /* parse <len> <bytes as hex> */
            unsigned n; scanf("%%u", &n); char buf[256] = {0}; for (unsigned i = 0; i < n; i++) { unsigned c; scanf("%%2x", &c); buf[i] = (char)c; }
            int32_t v = 0; int ok = rg_parse_cdb(buf, n, &v); printf("%%d %%d\n", ok, (int)v);
        } else if (!strcmp(cmd, "factor")) { int cdb; scanf("%%d", &cdb); printf("%%u\n", rg_factor_q15(cdb)); }
        else if (!strcmp(cmd, "target")) { unsigned vol, f; scanf("%%u %%u", &vol, &f); printf("%%d\n", (int)rg_target((int32_t)vol, f)); }
        else if (!strcmp(cmd, "pick")) { unsigned mode, have; int t, a; scanf("%%u %%u %%d %%d", &mode, &have, &t, &a); rg_t r = { t, a, (uint8_t)have }; printf("%%u\n", rg_pick_factor(mode, &r)); }
        else if (!strcmp(cmd, "id3")) {                   /* id3 <avail> <tag_len> <bytes> */
            unsigned avail, tl; scanf("%%u %%u", &avail, &tl); unsigned char *b = malloc(avail + 1); for (unsigned i = 0; i < avail; i++) { unsigned c; scanf("%%2x", &c); b[i] = (unsigned char)c; }
            rg_t r; rg_clear(&r); rg_id3_scan(&r, b, avail, tl); printf("%%u %%d %%d\n", r.have, (int)r.track_cdb, (int)r.album_cdb); free(b);
        } else if (!strcmp(cmd, "vorbis")) {              /* vorbis <len> <hex> */
            unsigned n; scanf("%%u", &n); char buf[200] = {0}; for (unsigned i = 0; i < n; i++) { unsigned c; scanf("%%2x", &c); buf[i] = (char)c; }
            rg_t r; rg_clear(&r); rg_vorbis_entry(&r, buf, n); printf("%%u %%d %%d\n", r.have, (int)r.track_cdb, (int)r.album_cdb);
        }
    }
    return 0;
}
'''
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(detail))); fails += 0 if ok else 1
def hexs(b): return b.hex()
def main():
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; exe = Path(d) / "t"
        src.write_text(HARNESS % str(ROOT / "fw/replaygain.h"))
        subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-o", str(exe), str(src)], check=True)
        def run(script):
            return subprocess.run([str(exe)], input=script, capture_output=True, text=True).stdout.split("\n")[:-1]
        # parsing
        cases = [("-6.48 dB", 1, -648), ("+3.2 dB", 1, 320), ("0.00 dB", 1, 0), (" -0.5", 1, -50), ("-12 dB", 1, -1200), ("1.005 dB", 1, 100), ("-45.00 dB", 1, -3000),
                 ("+99.9 dB", 1, 1800), ("abc", 0, 0), ("", 0, 0), ("-", 0, 0), ("-.5", 1, -50)]
        out = run("".join(f"parse {len(t)} {t.encode().hex()}\n" for t, _, _ in cases))
        for (t, ok, v), line in zip(cases, out):
            check(f"parse {t!r}", line == f"{ok} {v}" if ok else line.startswith("0 "), line)
        # factor: against 10^(cdb/2000) over the whole range
        cdbs = list(range(-3000, 1801, 7)) + [0, -1, 1, 1800, -3000]
        out = run("".join(f"factor {c}\n" for c in cdbs))
        worst = 0.0
        for c, line in zip(cdbs, out):
            want = 32768 * 10 ** (c / 2000.0); got = int(line)
            if want > 2: worst = max(worst, abs(20 * math.log10(max(got, 1) / want)))
        check(f"factor within 0.01 dB of 10^(cdb/2000) over -30..+18 dB (worst {worst:.4f} dB)", worst < 0.01)
        check("factor 0 dB is exactly unity", int(out[cdbs.index(0)]) == 32768)
        # target: attenuate only
        tcases = [(32768, 32768, 32768), (32768, 70000, 32768), (16384, 70000, 32768), (16384, 16384, 8192), (0, 70000, 0), (30000, 40000, 32768), (10000, 20000, 6103)]
        out = run("".join(f"target {v} {f}\n" for v, f, _ in tcases))
        for (v, f, w), line in zip(tcases, out): check(f"target vol {v} x factor {f} = {w} (never above unity)", int(line) == w, line)
        # picking
        pc = [(0, 3, -600, -300, 32768), (1, 3, -600, -300, None), (2, 3, -600, -300, None), (2, 1, -600, 0, None), (1, 2, 0, -300, None), (1, 0, 0, 0, 32768), (2, 0, 0, 0, 32768)]
        out = run("".join(f"pick {m} {h} {t} {a}\n" for m, h, t, a, _ in pc))
        def fac(c): return int(run(f"factor {c}\n")[0])
        wants = [32768, fac(-600), fac(-300), fac(-600), fac(-300), 32768, 32768]
        for (m, h, t, a, _), w, line in zip(pc, wants, out): check(f"pick mode {m} have {h} track {t} album {a}", int(line) == w, line)
        # real tags
        try:
            import mutagen
            from mutagen.id3 import ID3, TXXX, TIT2, APIC
        except ImportError:
            print("SKIP real-tag checks: mutagen not installed"); return
        def tag_bytes(ver, enc, apic_first=False, track="-6.48 dB", album="-3.10 dB", lower=False):
            t = ID3()
            if apic_first: t.add(APIC(encoding=0, mime="image/jpeg", type=3, desc="", data=b"\xff\xd8" + b"\0" * 20000))
            t.add(TIT2(encoding=enc, text="A title"))
            t.add(TXXX(encoding=enc, desc="replaygain_track_gain" if lower else "REPLAYGAIN_TRACK_GAIN", text=track))
            t.add(TXXX(encoding=enc, desc="REPLAYGAIN_ALBUM_GAIN", text=album))
            t.add(TXXX(encoding=enc, desc="REPLAYGAIN_TRACK_PEAK", text="0.98"))
            p = Path(d) / "x.id3"
            t.save(str(p), v2_version=ver)
            return p.read_bytes()
        for ver in (3, 4):
            for enc in (0, 1, 3):
                if ver == 3 and enc == 3: continue
                for lower in (False, True):
                    b = tag_bytes(ver, enc, lower=lower)
                    line = run(f"id3 {len(b)} {len(b)} {hexs(b)}\n")[0]
                    check(f"id3v2.{ver} enc {enc}{' lowercase key' if lower else ''}: track -6.48, album -3.10", line == "3 -648 -310", line)
        b = tag_bytes(4, 3, apic_first=True)
        line = run(f"id3 {len(b)} {len(b)} {hexs(b)}\n")[0]
        check("id3v2.4 with a 20 KB picture first, whole tag loaded", line == "3 -648 -310", line)
        line = run(f"id3 4096 {len(b)} {hexs(b[:4096])}\n")[0]
        check("only the first 4 KB of that tag loaded: the gain frames inside it are still found", line == "3 -648 -310", line)
        outs = run("".join(f"id3 {n} {len(b)} {hexs(b[:n])}\n" for n in range(0, 240)))
        check("every truncation of the tag from 0 to 239 bytes is handled without a crash and never invents a value",
              len(outs) == 240 and all(o.split()[0] in ("0", "1", "2", "3") and (o.split()[0] == "0" or o in ("1 -648 0", "2 0 -310", "3 -648 -310")) for o in outs), outs[:3])
        b = tag_bytes(4, 3, track="+2.5 dB", album="0.00 dB")
        line = run(f"id3 {len(b)} {len(b)} {hexs(b)}\n")[0]
        check("id3v2.4 +2.5 dB and 0.00 dB", line == "3 250 0", line)
        # vorbis
        for e, want in (("REPLAYGAIN_TRACK_GAIN=-7.25 dB", "1 -725 0"), ("replaygain_album_gain=-1.00 dB", "2 0 -100"), ("REPLAYGAIN_TRACK_PEAK=0.9", "0 0 0"), ("TITLE=x", "0 0 0")):
            line = run(f"vorbis {len(e)} {e.encode().hex()}\n")[0]
            check(f"vorbis {e}", line == want, line)

        # the real fw/flac.c: a hand-built FLAC header with Vorbis comments, through flac_open()
        def flac_header(comments):
            si = struct.pack(">HH", 4096, 4096) + (0).to_bytes(3, "big") + (0).to_bytes(3, "big")
            si += ((44100 << 44) | (1 << 41) | (15 << 36) | 441000).to_bytes(8, "big") + bytes(16)
            vend = b"x"; vc = struct.pack("<I", len(vend)) + vend + struct.pack("<I", len(comments))
            for c in comments: vc += struct.pack("<I", len(c)) + c
            return b"fLaC" + bytes([0]) + len(si).to_bytes(3, "big") + si + bytes([0x84]) + len(vc).to_bytes(3, "big") + vc
        fx = Path(d) / "frg"
        r = subprocess.run(["cc", "-O1", "-w", "-I", str(ROOT / "fw"), "-o", str(fx), str(ROOT / "sim/flac_rg_harness.c"), str(ROOT / "fw/flac.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
        def frg(comments):
            return subprocess.run([str(fx)], input=flac_header(comments), capture_output=True).stdout.decode().strip()
        for comments, want in (([b"TITLE=T", b"REPLAYGAIN_TRACK_GAIN=-7.25 dB", b"REPLAYGAIN_ALBUM_GAIN=-1.00 dB"], "0 3 [-7.25 dB] [-1.00 dB] title=[T]"),
                               ([b"replaygain_album_gain=+2.5 dB"], "0 2 [] [+2.5 dB] title=[]"),
                               ([b"REPLAYGAIN_TRACK_GAIN=-3.00 dB", b"ARTIST=a"], "0 1 [-3.00 dB] [] title=[]"),
                               ([b"REPLAYGAIN_TRACK_PEAK=0.99", b"TITLE=Q"], "0 0 [] [] title=[Q]"),
                               ([], "0 0 [] [] title=[]")):
            check(f"flac_open keeps {[c.decode() for c in comments]}", frg(comments) == want, frg(comments))
    print("PASSED" if not fails else f"FAILED ({fails})")
    sys.exit(1 if fails else 0)
if __name__ == "__main__": main()
