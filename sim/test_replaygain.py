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
        else if (!strcmp(cmd, "target")) { unsigned vol, f, cap; scanf("%%u %%u %%u", &vol, &f, &cap); printf("%%d\n", (int)rg_target((int32_t)vol, f, cap)); }
        else if (!strcmp(cmd, "peak")) { unsigned n; scanf("%%u", &n); char buf[64] = {0}; for (unsigned i = 0; i < n; i++) { unsigned c; scanf("%%2x", &c); buf[i] = (char)c; } uint32_t v = 0; int ok = rg_parse_peak_q15(buf, n, &v); printf("%%d %%u\n", ok, (unsigned)v); }
        else if (!strcmp(cmd, "cap")) { unsigned pk, ok; scanf("%%u %%u", &pk, &ok); printf("%%u\n", rg_cap_q15(pk, ok)); }
        else if (!strcmp(cmd, "pickpeak")) { unsigned mode, have, tp, ap; scanf("%%u %%u %%u %%u", &mode, &have, &tp, &ap); rg_t r; rg_clear(&r); r.have = (uint8_t)have; r.track_peak = tp; r.album_peak = ap; printf("%%u\n", rg_pick_peak(mode, &r)); }
        else if (!strcmp(cmd, "pick")) { unsigned mode, have; int t, a; scanf("%%u %%u %%d %%d", &mode, &have, &t, &a); rg_t r = { t, a, (uint8_t)have, 0, 0 }; printf("%%u\n", rg_pick_factor(mode, &r)); }
        else if (!strcmp(cmd, "id3")) {                   /* id3 <avail> <tag_len> <bytes> */
            unsigned avail, tl; scanf("%%u %%u", &avail, &tl); unsigned char *b = malloc(avail + 1); for (unsigned i = 0; i < avail; i++) { unsigned c; scanf("%%2x", &c); b[i] = (unsigned char)c; }
            rg_t r; rg_clear(&r); rg_id3_scan(&r, b, avail, tl); printf("%%u %%d %%d %%u %%u\n", r.have, (int)r.track_cdb, (int)r.album_cdb, (unsigned)r.track_peak, (unsigned)r.album_peak); free(b);
        } else if (!strcmp(cmd, "vorbis")) {              /* vorbis <len> <hex> */
            unsigned n; scanf("%%u", &n); char buf[200] = {0}; for (unsigned i = 0; i < n; i++) { unsigned c; scanf("%%2x", &c); buf[i] = (char)c; }
            rg_t r; rg_clear(&r); rg_vorbis_entry(&r, buf, n); printf("%%u %%d %%d %%u %%u\n", r.have, (int)r.track_cdb, (int)r.album_cdb, (unsigned)r.track_peak, (unsigned)r.album_peak);
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
        out = run("".join(f"target {v} {f} 32768\n" for v, f, _ in tcases))
        for (v, f, w), line in zip(tcases, out): check(f"target vol {v} x factor {f} = {w} (never above unity)", int(line) == w, line)
        # picking
        pc = [(0, 3, -600, -300, 32768), (1, 3, -600, -300, None), (2, 3, -600, -300, None), (2, 1, -600, 0, None), (1, 2, 0, -300, None), (1, 0, 0, 0, 32768), (2, 0, 0, 0, 32768)]
        out = run("".join(f"pick {m} {h} {t} {a}\n" for m, h, t, a, _ in pc))
        def fac(c): return int(run(f"factor {c}\n")[0])
        wants = [32768, fac(-600), fac(-300), fac(-600), fac(-300), 32768, 32768]
        for (m, h, t, a, _), w, line in zip(pc, wants, out): check(f"pick mode {m} have {h} track {t} album {a}", int(line) == w, line)
        # B-720: peaks, the boost cap and the target above unity
        pk = [("0.988553", 1, None), ("1", 1, 32768), ("1.000000", 1, 32768), ("0.5", 1, 16384), ("0", 1, 0), ("0.0001", 1, 4), ("2.5", 1, 81920), ("9.9", 1, 262141), (" 0.75 ", 1, 24576), ("abc", 0, 0), ("", 0, 0), (".5", 1, 16384), ("0.98765432", 1, None)]
        out = run("".join(f"peak {len(t)} {t.encode().hex()}\n" for t, _, _ in pk))
        for (t, ok, v), line in zip(pk, out):
            got = line.split()
            if v is None: tv = float(t) * 32768; check(f"peak {t!r}: within four decimals above the true value ({tv:.1f})", int(got[0]) == 1 and tv - 1e-9 <= int(got[1]) <= tv + 3.4, line)
            else: check(f"peak {t!r} -> Q15 {v if ok else '-'}", int(got[0]) == ok and (not ok or int(got[1]) == v), line)
        for t in ("0.988553", "0.3141", "0.999999", "0.0007"):
            got = int(run(f"peak {len(t)} {t.encode().hex()}\n")[0].split()[1]); check(f"peak {t} is never rounded down ({got} >= {float(t) * 32768:.2f})", got >= float(t) * 32768 - 1e-9)
        caps = [(0, 1, 32768), (30000, 0, 32768), (32768, 1, 30934), (16384, 1, 61865), (8000, 1, 65535), (30934, 1, 32768), (29000, 1, 34952), (1, 1, 65535)]
        out = run("".join(f"cap {p} {o}\n" for p, o, _ in caps))
        for (p, o, w), line in zip(caps, out): check(f"cap peak {p} boost_ok {o} = {w} (0.944 / peak, floor unity, ceiling +6 dB, no peak or no stage = unity)", int(line) == w or (w == 30934 and int(line) == 32768), line)
        out = run("".join(f"cap {p} 1\n" for p in range(1, 40000, 37)))
        check("a boost never lets peak x gain pass 0.944 of full scale (peak tag honoured over the whole range, ceiling 65535)", all(int(l) == 32768 or int(l) == 65535 or int(l) * p <= 1013608448 for p, l in zip(range(1, 40000, 37), out)))
        tc = [(32768, 40000, 65535, 40000 * 32768 // 32768 // 1), ]
        out = run("target 32768 65536 65535\ntarget 32768 65536 32768\ntarget 24000 65536 50000\ntarget 16384 90000 65535\ntarget 32768 33000 65535\ntarget 0 262144 65535\n")
        check("target above unity: 2.0 factor at full volume reaches the cap 65535 only when the cap allows (cap 65535 -> 65535 clamp, cap 32768 -> unity)", [int(x) for x in out[:2]] == [65535, 32768], out)
        check("target above unity: volume 24000 x factor 2.0 = 48000 under a 50000 cap", int(out[2]) == 48000, out[2])
        check("target above unity: capped by the peak-derived cap, never by more", int(out[3]) == 45000 and 1, out[3])
        check("target between unity and the cap is exact (factor 33000/32768 at unity volume)", int(out[4]) == 33000, out[4])
        check("target of a muted volume is 0 whatever the cap", int(out[5]) == 0, out[5])
        pp = [(0, 15, 100, 200, 0), (1, 15, 100, 200, 100), (2, 15, 100, 200, 200), (2, 1, 100, 200, 0), (2, 5, 100, 200, 100), (1, 2, 100, 200, 0), (1, 10, 100, 200, 200), (1, 6, 100, 200, 0)]
        out = run("".join(f"pickpeak {m} {h} {t} {a}\n" for m, h, t, a, _ in pp))
        for (m, h, t, a, w), line in zip(pp, out): check(f"pickpeak mode {m} have {h}: the peak that goes with the chosen gain ({w}; 0 = none)", int(line) == w, line)
        # mutants of the boost logic must change at least one answer of a fixed probe script
        probe = "".join(f"cap {pk} 1\ncap {pk} 0\n" for pk in (1, 100, 8000, 16384, 29000, 32768, 40000)) + "".join(f"peak {len(t)} {t.encode().hex()}\n" for t in ("0.988553", "0.5", "1", "9.9", "0.12345")) \
              + "".join(f"pickpeak {m} {h} 111 222\n" for m in (0, 1, 2) for h in (0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 15)) + "target 32768 65536 65535\ntarget 24000 65536 50000\ntarget 32768 65536 32768\n"
        base = run(probe)
        hdr = (ROOT / "fw/replaygain.h").read_text()
        for name, a, b in (("cap ignores the peak", "uint32_t lim = RG_PEAK_NUM / peak_q15;", "uint32_t lim = RG_BOOST_MAX;"),
                           ("headroom 0 dB instead of -0.5 dB", "#define RG_PEAK_NUM   1013608448u", "#define RG_PEAK_NUM   1073741824u"),
                           ("cap ignores the stage", "if (!boost_ok || peak_q15 == 0u) return RG_UNITY;", "if (peak_q15 == 0u) return RG_UNITY;"),
                           ("cap may fall below unity", "if (lim < RG_UNITY) lim = RG_UNITY;", ""),
                           ("cap above +6 dB", "return lim > RG_BOOST_MAX ? RG_BOOST_MAX : lim;", "return lim;"),
                           ("peak rounds down", "+ 9999u) / 10000u);", ") / 10000u);"),
                           ("dropped digits ignored", "frac += dropped;", ""),
                           ("album mode uses the track peak", "if (mode == RG_ALBUM && (r->have & 2u)) return (r->have & 8u) ? r->album_peak : 0u;", "if (mode == RG_ALBUM && (r->have & 2u)) return (r->have & 4u) ? r->track_peak : 0u;"),
                           ("peak used without its tag", "return (r->have & 4u) ? r->track_peak : 0u;", "return r->track_peak;"),
                           ("target ignores the cap", "return g > cap ? (int32_t)cap : (int32_t)g;", "return (int32_t)g;")):
            assert a in hdr, name
            msrc = Path(d) / "m.c"; mexe = Path(d) / "m"
            msrc.write_text(HARNESS % str(Path(d) / "mh.h")); (Path(d) / "mh.h").write_text(hdr.replace(a, b, 1))
            subprocess.run(["cc", "-O1", "-w", "-o", str(mexe), str(msrc)], check=True)
            got = subprocess.run([str(mexe)], input=probe, capture_output=True, text=True).stdout.split("\n")[:-1]
            check(f"mutant caught: {name}", got != base)
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
                    check(f"id3v2.{ver} enc {enc}{' lowercase key' if lower else ''}: track -6.48, album -3.10", line == "7 -648 -310 32113 0", line)
        b = tag_bytes(4, 3, apic_first=True)
        line = run(f"id3 {len(b)} {len(b)} {hexs(b)}\n")[0]
        check("id3v2.4 with a 20 KB picture first, whole tag loaded", line == "7 -648 -310 32113 0", line)
        line = run(f"id3 4096 {len(b)} {hexs(b[:4096])}\n")[0]
        check("only the first 4 KB of that tag loaded: the gain frames inside it are still found", line == "7 -648 -310 32113 0", line)
        outs = run("".join(f"id3 {n} {len(b)} {hexs(b[:n])}\n" for n in range(0, 240)))
        check("every truncation of the tag from 0 to 239 bytes is handled without a crash and never invents a value",
              len(outs) == 240 and all(int(o.split()[0]) < 16 and (o.split()[0] == "0" or o in ("1 -648 0 0 0", "2 0 -310 0 0", "3 -648 -310 0 0", "5 -648 0 32113 0", "7 -648 -310 32113 0", "6 0 -310 32113 0", "4 0 0 32113 0")) for o in outs), outs[:3])
        b = tag_bytes(4, 3, track="+2.5 dB", album="0.00 dB")
        line = run(f"id3 {len(b)} {len(b)} {hexs(b)}\n")[0]
        check("id3v2.4 +2.5 dB and 0.00 dB", line == "7 250 0 32113 0", line)
        # vorbis
        for e, want in (("REPLAYGAIN_TRACK_GAIN=-7.25 dB", "1 -725 0 0 0"), ("replaygain_album_gain=-1.00 dB", "2 0 -100 0 0"), ("REPLAYGAIN_TRACK_PEAK=0.9", "4 0 0 29492 0"), ("replaygain_album_peak=1.000000", "8 0 0 0 32768"), ("TITLE=x", "0 0 0 0 0")):
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
                               ([b"REPLAYGAIN_TRACK_PEAK=0.99", b"TITLE=Q"], "0 4 [] [] title=[Q]"),
                               ([b"REPLAYGAIN_ALBUM_PEAK=0.5", b"REPLAYGAIN_TRACK_PEAK=0.25"], "0 12 [] [] title=[]"),
                               ([], "0 0 [] [] title=[]")):
            check(f"flac_open keeps {[c.decode() for c in comments]}", frg(comments) == want, frg(comments))
    print("PASSED" if not fails else f"FAILED ({fails})")
    sys.exit(1 if fails else 0)
if __name__ == "__main__": main()
