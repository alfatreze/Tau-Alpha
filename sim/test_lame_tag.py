#!/usr/bin/env python3
"""B-624: fw/lame_tag.h reads the LAME/Info tag's encoder delay and end padding. Compared with mutagen (an independent implementation) on every MP3 that is available locally
(the Test Album), plus synthetic edge cases (no tag, short buffer, non-printable string, the 12-bit maxima). Real-file comparison is skipped when the music folder is absent."""
import glob, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1
with tempfile.TemporaryDirectory() as d:
    exe = Path(d) / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-I", str(ROOT / "fw"), "-o", str(exe), str(ROOT / "sim/lame_tag_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); sys.exit(1)
    def run(path):
        return subprocess.run([str(exe), str(path)], capture_output=True, text=True).stdout.strip()
    # synthetic: an MPEG-1 L3 frame header + side info zeros + Info tag with flags 0 (no optional fields) + LAME extension
    def make(delay, pad, enc=b"LAME4.0 \x00", flags=0, trunc=None):
        hdr = bytes([0xFF, 0xFB, 0x90, 0x00]) + bytes(32)             # 4-byte header + 32 bytes side info (stereo)
        tag = b"Info" + flags.to_bytes(4, "big")
        ext = enc + bytes(12) + bytes([(delay >> 4) & 0xFF, ((delay & 15) << 4) | (pad >> 8), pad & 0xFF])
        data = hdr[:4] + bytes(32) + tag + ext + bytes(100)
        return data[:trunc] if trunc else data
    # NB the harness scans from the first sync, then lame_scan looks within 64 bytes of the frame data start for Xing/Info
    for dl, pd in ((576, 1105), (0, 0), (4095, 4095), (1105, 0)):
        p = Path(d) / "s.mp3"; p.write_bytes(make(dl, pd))
        check("synthetic delay %d padding %d" % (dl, pd), run(p) == "%d %d" % (dl, pd), "(got '%s')" % run(p))
    p = Path(d) / "s.mp3"; p.write_bytes(bytes([0xFF, 0xFB, 0x90, 0x00]) + bytes(300))
    check("no Xing/Info tag: none", run(p) == "none")
    p.write_bytes(make(576, 1105, trunc=60)); check("truncated extension: none", run(p) == "none")
    p.write_bytes(make(576, 1105, enc=b"\x01\x02\x03\x04rest\x00")); check("non-printable encoder string: none", run(p) == "none")
    files = sorted(glob.glob(str(ROOT.parent / "test music" / "Test Album" / "*.mp3")))
    import shutil, struct, wave
    if files and shutil.which("afconvert"):
        # Independent reference: Apple's decoder honours the LAME gapless tag, so its decoded length must equal frames * 1152 - delay - padding
        n = bad = 0
        for f in files:
            raw = Path(f).read_bytes()
            off = 0
            if raw[:3] == b"ID3":
                off = 10 + ((raw[6] & 0x7F) << 21 | (raw[7] & 0x7F) << 14 | (raw[8] & 0x7F) << 7 | (raw[9] & 0x7F))
            seg = raw[off:off + 4000]
            i = min([k for k in (seg.find(b"Xing"), seg.find(b"Info")) if 0 < k < 64] or [-1])
            if i < 0:
                continue
            flags = struct.unpack(">I", seg[i + 4:i + 8])[0]
            if not flags & 1:
                continue
            frames = struct.unpack(">I", seg[i + 8:i + 12])[0]
            got = run(f)
            if got == "none":
                bad += 1; continue
            dl, pd = map(int, got.split())
            wav = Path(d) / "x.wav"
            subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16", f, str(wav)], check=True, capture_output=True)
            with wave.open(str(wav)) as w:
                decoded = w.getnframes()
            n += 1
            ok = decoded == frames * 1152 - dl - pd
            bad += 0 if ok else 1
            if not ok:
                print("   %s: frames %d delay %d padding %d -> %d, afconvert decoded %d" % (Path(f).name[:30], frames, dl, pd, frames * 1152 - dl - pd, decoded))
        check("real files: frames*1152 - delay - padding equals the length Apple's gapless decoder produces on %d MP3s" % n, n >= 3 and bad == 0, "(%d mismatches)" % bad)
    else:
        print("skip real-file comparison (no test music folder or no afconvert)")
sys.exit(1 if fails else 0)
