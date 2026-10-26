#!/usr/bin/env python3
"""B-727: tools/tau_details.py (the per-track technical details stored in the library index) against hand-built MP3 and FLAC files whose every field is known, the packed record
round trip, the ReplayGain peak/gain parsers against the firmware's own (fw/replaygain.h compiled and run), and the index builder carrying the records."""
import struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_details as D
import tau_library as L

fails = 0
def ok(c, m, info=""):
    global fails
    print(("ok   " if c else "FAIL ") + m + ("" if c else "  " + str(info))); fails += 0 if c else 1

def syncsafe(n): return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])

def mp3_file(path, vbr=True, lame=True, apic_kib=0, rg=None, mono=False, rate_idx=0, br_idx=9):
    frames_total = 6693
    hdr = bytes([0xFF, 0xFB, (br_idx << 4) | (rate_idx << 2), 0xC0 if mono else 0x00])       # MPEG1 layer III, no CRC
    side = 17 if mono else 32
    x = b"Xing" if vbr else b"Info"
    body = bytearray(hdr) + bytes(side) + x + struct.pack(">I", 3) + struct.pack(">I", frames_total) + struct.pack(">I", 2961218 - 800)
    if lame:
        ext = bytearray(24); ext[:9] = b"LAME4.0 \x00"; ext[21] = 576 >> 4; ext[22] = ((576 & 15) << 4) | (1668 >> 8); ext[23] = 1668 & 255
        body += ext
    body += bytes(500)
    frames = b""
    tag = b""
    def frame(fid, data): return fid + syncsafe(len(data)) + b"\x00\x00" + data
    if apic_kib:
        tag += frame(b"APIC", b"\x00image/jpeg\x00\x03\x00" + bytes(apic_kib * 1024))
    for k, v in (rg or {}).items():
        tag += frame(b"TXXX", b"\x00" + k.encode() + b"\x00" + v.encode())
    pre = b"ID3\x04\x00\x00" + syncsafe(len(tag)) + tag if tag else b""
    Path(path).write_bytes(pre + bytes(body) + bytes(2961218 - len(pre) - len(body)))

def flac_file(path, rate=44100, ch=2, bits=16, total=7708092, vendor="reference libFLAC 1.2.1 20070917", rg=None, pic_kib=0):
    si = struct.pack(">HH", 4096, 4096) + (0).to_bytes(3, "big") + (0).to_bytes(3, "big")
    si += ((rate << 44) | ((ch - 1) << 41) | ((bits - 1) << 36) | total).to_bytes(8, "big") + bytes(16)
    comments = [f"{k}={v}".encode() for k, v in (rg or {}).items()]
    vc = struct.pack("<I", len(vendor)) + vendor.encode() + struct.pack("<I", len(comments)) + b"".join(struct.pack("<I", len(c)) + c for c in comments)
    out = b"fLaC" + bytes([0]) + len(si).to_bytes(3, "big") + si
    blocks = [(4, vc)] + ([(6, bytes(pic_kib * 1024))] if pic_kib else [])
    for i, (t, b) in enumerate(blocks):
        out += bytes([(0x80 if i == len(blocks) - 1 else 0) | t]) + len(b).to_bytes(3, "big") + b
    Path(path).write_bytes(out + bytes(14848563 - len(out)))

with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    mp3_file(td / "a.mp3", apic_kib=156, rg={"REPLAYGAIN_TRACK_GAIN": "-4.31 dB", "REPLAYGAIN_TRACK_PEAK": "0.970734", "REPLAYGAIN_ALBUM_GAIN": "+5.87 dB", "REPLAYGAIN_ALBUM_PEAK": "0.393311"})
    d = D.extract(td / "a.mp3")
    ok((d["codec"], d["ch"], d["rate"], d["kbps"], d["delay"], d["pad"], d["enc"]) == (1, 2, 44100, d["kbps"], 576, 1668, "LAME4.0"), "MP3: codec, channels, rate, LAME delay/padding, encoder", d)
    ok(d["flags"] & D.F_VBR and d["flags"] & D.F_LAME and d["flags"] & D.F_COVER, "MP3: VBR (Xing), LAME tag and embedded cover flags", d["flags"])
    ok(d["cover_kib"] in (156, 157), "MP3: embedded cover size from the APIC frame", d["cover_kib"])
    ok((d["tg"], d["ag"], d["tp"], d["ap"]) == (-431, 587, D.parse_peak_q15("0.970734"), D.parse_peak_q15("0.393311")) and d["flags"] & 0xF0 == 0xF0, "MP3: ReplayGain gains in centi-dB and peaks in Q15, four flags", d)
    ok(d["kbps"] == int(round((2961218 - 800) * 8 / (6693 * 1152 / 44100) / 1000)), "MP3 VBR average bitrate from the Xing byte and frame counts", d["kbps"])
    mp3_file(td / "b.mp3", vbr=False, lame=False)
    d = D.extract(td / "b.mp3")
    ok(not d["flags"] & (D.F_VBR | D.F_LAME) and d["enc"] == "" and "delay" not in d, "MP3 CBR (Info) without LAME: no VBR, no LAME, no encoder", d)
    mp3_file(td / "c.mp3", mono=True, rate_idx=1)
    d = D.extract(td / "c.mp3")
    ok((d["ch"], d["rate"]) == (1, 48000), "MP3 mono 48 kHz", d)
    flac_file(td / "a.flac", rg={"replaygain_track_gain": "+5.87 dB", "REPLAYGAIN_TRACK_PEAK": "0.393311"}, pic_kib=155)
    d = D.extract(td / "a.flac")
    ok((d["codec"], d["ch"], d["bits"], d["rate"]) == (2, 2, 16, 44100) and d["enc"].startswith("reference libFLAC"), "FLAC: STREAMINFO channels, bits, rate and the vendor string", d)
    ok(d["cover_kib"] == 155 and d["flags"] & D.F_COVER, "FLAC: PICTURE block size", d)
    ok(d["tg"] == 587 and d["tp"] == D.parse_peak_q15("0.393311") and d["flags"] & D.F_RGTG and d["flags"] & D.F_RGTP and not d["flags"] & D.F_RGAG, "FLAC: ReplayGain (lowercase key) with only the track tags present", d)
    ok(d["kbps"] == int(round(14848563 * 8 / (7708092 / 44100) / 1000)), "FLAC average bitrate from file size and duration", d["kbps"])
    flac_file(td / "h.flac", rate=96000, ch=2, bits=24, total=96000 * 100)
    d = D.extract(td / "h.flac")
    ok((d["bits"], d["rate"]) == (24, 96000), "FLAC 24 bit 96 kHz", d)
    (td / "tau-art").mkdir()
    (td / "tau-art" / "cover_128.pal256.timg").write_bytes(b"x")
    ok(D.extract(td / "a.flac")["flags"] & D.F_SIDECAR, "a tau-art sidecar next to the file sets the sidecar flag")
    # record round trip and the index builder
    class Pool:
        def __init__(s): s.t = {"": 0}; s.n = 1
        def add(s, x):
            if x not in s.t: s.t[x] = s.n; s.n += len(x) + 1
            return s.t[x]
    p = Pool(); rec = D.pack(D.extract(td / "a.mp3"), p)
    ok(len(rec) == 32, "a record is 32 bytes")
    big = dict(D.extract(td / "a.mp3"), tp=D.parse_peak_q15("2.0"), ap=D.parse_peak_q15("7.9999"))
    try:
        ub = D.unpack(D.pack(big, Pool()))
        ok(ub["tp"] == 65535 and ub["ap"] == 65535, "issue 025: a ReplayGain peak of 2.0 or more is clamped to 65535 (1.9999) in the record", ub)
    except Exception as ex:
        ok(False, "issue 025: a ReplayGain peak of 2.0 or more is clamped to 65535, not a crash", ex)
    u = D.unpack(rec); e = D.extract(td / "a.mp3")
    ok(all(u[k] == e.get(k, 0) for k in ("codec", "ch", "bits", "flags", "rate", "size", "kbps", "cover_kib", "tg", "ag", "tp", "ap", "delay", "pad")) and u["enc_off"] == p.t["LAME4.0"], "pack/unpack round trip", u)
    entries, _, w = L.scan(td)
    idx = L.build_index(entries, (), w)
    ix = L.parse(idx)
    ok(ix.details and len(ix.details) == len(entries) == 5 and L.verify(idx) == [], "the index carries one details record per track and verifies")
    names = {ix.str(t[1]): i for i, t in enumerate(ix.tracks)}
    ok(ix.details[names["a.flac"]]["flags"] & D.F_SIDECAR and ix.str(ix.details[names["a.mp3"]]["enc_off"]) == "LAME4.0", "records follow the index's track order (album order), strings resolve", names)
    # gain / peak parsers: the same answers as the firmware's (fw/replaygain.h compiled and run)
    c = td / "rg.c"
    c.write_text('#include <stdio.h>\n#include <string.h>\n#include "replaygain.h"\nint main(void){char l[64];while(fgets(l,64,stdin)){size_t n=strlen(l);if(n&&l[n-1]==10)l[--n]=0;char k=l[0];uint32_t pv=0;int32_t gv=0;int ok=0;if(k==\'P\')ok=rg_parse_peak_q15(l+1,n-1,&pv);else ok=rg_parse_cdb(l+1,n-1,&gv);printf("%d %lld\\n",ok,k==\'P\'?(long long)pv:(long long)gv);}return 0;}\n')
    subprocess.run(["cc", "-O1", "-w", "-I", str(ROOT / "fw"), "-o", str(td / "rg"), str(c)], check=True)
    peaks = ["0.988553", "1", "1.000000", "0.5", "0", "0.0001", "2.5", "9.9", " 0.75 ", ".5", "0.98765432", "0.999999", "0.00005", "7.99999", "abc"]
    gains = ["-6.48 dB", "+3.2 dB", "0.00 dB", " -0.5", "-12 dB", "1.005 dB", "-45.00 dB", "+99.9 dB", "-.5", "abc"]
    r = subprocess.run([str(td / "rg")], input="".join(f"P{x}\n" for x in peaks) + "".join(f"G{x}\n" for x in gains), capture_output=True, text=True).stdout.split("\n")[:-1]
    py = [("1 %d" % v) if (v := D.parse_peak_q15(x)) is not None else "0 0" for x in peaks] + [("1 %d" % v) if (v := D.parse_gain_cdb(x)) is not None else "0 0" for x in gains]
    bad = [(x, a, b) for x, a, b in zip(peaks + gains, r, py) if a != b]
    ok(not bad, "peak and gain parsing equals the firmware's for %d inputs" % len(py), bad[:4])
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
