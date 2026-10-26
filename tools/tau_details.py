#!/usr/bin/env python3
"""Per-track technical details for the Tau library index (track details page, docs/features/TRACK_DETAILS_SPEC.md, B-727).

Extracted on the host while the library index is built (standard library only), stored as an extra, optional section of the index (a 9th section entry in header bytes 112..119: an older
loader ignores it) and shown by the player's track details page. One fixed 32-byte record per track, in the order of the index's track records:

   0 u8 codec (1 MP3, 2 FLAC)   1 u8 channels   2 u8 bits per sample (FLAC; 0 for MP3)   3 u8 flags
   4 u32 sample rate (Hz)       8 u32 file size (bytes)
  12 u16 bitrate (kbit/s, average; 0 unknown)   14 u16 embedded cover size (KiB, 0 none, 65535 max)
  16 i16 ReplayGain track gain (centi-dB)   18 i16 album gain   20 u16 track peak (Q15, 32768 = 1.0)   22 u16 album peak
  24 u16 LAME encoder delay (samples)   26 u16 LAME end padding   28 u32 offset of the encoder/vendor string in the index string pool (0 = none)
flags: 0 VBR  1 LAME tag  2 tau-art sidecar present  3 embedded cover  4 RG track gain  5 RG album gain  6 RG track peak  7 RG album peak
The section starts with a 16-byte header: 'TDET', u16 version (1), u16 record size (32), u32 count, u32 reserved."""
import re
import struct
from pathlib import Path

MAGIC = 0x54454454          # 'TDET' read little-endian from b"TDET"
REC = 32
VERSION = 1
F_VBR, F_LAME, F_SIDECAR, F_COVER, F_RGTG, F_RGAG, F_RGTP, F_RGAP = (1 << k for k in range(8))
SIDECAR = "tau-art/cover_128.pal256.timg"        # the name fw/timg.inc looks for

_BR = {1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
       2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]}
_SR = {3: [44100, 48000, 32000], 2: [22050, 24000, 16000], 0: [11025, 12000, 8000]}


def parse_gain_cdb(s):
    m = re.match(r"\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+))", s or "")
    if not m:
        return None
    v = round(float(m.group(1)) * 100)
    return max(-3000, min(1800, v))


def parse_peak_q15(s):
    """Same rule as fw/replaygain.h rg_parse_peak_q15: four decimals, rounded UP, whole part at most 7."""
    m = re.match(r"\s*(\d*)(?:\.(\d*))?", s or "")
    if not m or not (m.group(1) or m.group(2)):
        return None
    whole, frac = int(m.group(1) or 0), m.group(2) or ""
    if whole > 7:
        whole, frac = 7, "9999"
    f4 = int((frac + "0000")[:4]) + (1 if any(c != "0" for c in frac[4:]) else 0)
    if f4 > 9999:
        whole, f4 = whole + 1, 0
    if whole > 7:
        whole, f4 = 7, 9999
    return ((whole * 10000 + f4) * 32768 + 9999) // 10000


def _synchsafe(b):
    return (b[0] << 21) | (b[1] << 14) | (b[2] << 7) | b[3]


def _mp3(path, size):
    d = dict(codec=1, ch=2, bits=0, flags=0, rate=0, size=size, kbps=0, cover_kib=0, enc="")
    rg = {}
    with open(path, "rb") as f:
        h = f.read(10)
        start = 0
        if h[:3] == b"ID3" and h[3] in (3, 4):
            end = 10 + _synchsafe(h[6:10])
            start = end
            pos = 10
            ver = h[3]
            while pos + 10 <= end:
                f.seek(pos)
                fh = f.read(10)
                if len(fh) < 10 or fh[0] == 0:
                    break
                fid = fh[:4].decode("latin-1")
                fsz = _synchsafe(fh[4:8]) if ver == 4 else struct.unpack(">I", fh[4:8])[0]
                if fsz <= 0 or pos + 10 + fsz > end:
                    break
                if fid == "APIC":
                    d["cover_kib"] = min(65535, (fsz + 1023) // 1024)
                elif fid == "TXXX" and fsz < 96:
                    body = f.read(fsz)
                    enc = body[0]
                    try:
                        txt = (body[1:].decode("utf-16") if enc in (1, 2) else body[1:].decode("latin-1" if enc == 0 else "utf-8", "replace"))
                    except UnicodeDecodeError:
                        txt = ""
                    desc, _, val = txt.partition("\x00")
                    rg[desc.strip().upper()] = val.strip("\x00 ")
                pos += 10 + fsz
        f.seek(start)
        buf = f.read(16384)
    for i in range(len(buf) - 4):
        if buf[i] == 0xFF and (buf[i + 1] & 0xE0) == 0xE0:
            b1, b2, b3 = buf[i + 1], buf[i + 2], buf[i + 3]
            ver, layer = (b1 >> 3) & 3, (b1 >> 1) & 3
            if ver == 1 or layer != 1 or (b2 >> 4) in (0, 15) or ((b2 >> 2) & 3) == 3:
                continue
            fam = 1 if ver == 3 else 2
            br = _BR[fam][b2 >> 4]
            sr = _SR[ver][(b2 >> 2) & 3]
            spf = 1152 if ver == 3 else 576
            mono = (b3 >> 6) == 3
            d["ch"], d["rate"], d["kbps"] = (1 if mono else 2), sr, br
            side = (17 if mono else 32) if ver == 3 else (9 if mono else 17)
            x = i + 4 + side
            if buf[x:x + 4] in (b"Xing", b"Info") and x + 12 <= len(buf):
                if buf[x:x + 4] == b"Xing":
                    d["flags"] |= F_VBR
                fl = struct.unpack(">I", buf[x + 4:x + 8])[0]
                e, frames, nbytes = x + 8, 0, 0
                if fl & 1:
                    frames = struct.unpack(">I", buf[e:e + 4])[0]; e += 4
                if fl & 2:
                    nbytes = struct.unpack(">I", buf[e:e + 4])[0]; e += 4
                if fl & 4:
                    e += 100
                if fl & 8:
                    e += 4
                if frames and sr:
                    secs = frames * spf / sr
                    d["kbps"] = int(round((nbytes or (size - start)) * 8 / secs / 1000))
                ext = buf[e:e + 24]
                if len(ext) >= 24 and all(0x20 <= c < 0x7F for c in ext[:4]):
                    d["flags"] |= F_LAME
                    d["enc"] = ext[:9].decode("ascii", "replace").strip("\x00 ")
                    d["delay"] = (ext[21] << 4) | (ext[22] >> 4)
                    d["pad"] = ((ext[22] & 15) << 8) | ext[23]
            break
    _rg_into(d, rg.get("REPLAYGAIN_TRACK_GAIN"), rg.get("REPLAYGAIN_ALBUM_GAIN"), rg.get("REPLAYGAIN_TRACK_PEAK"), rg.get("REPLAYGAIN_ALBUM_PEAK"))
    return d


def _rg_into(d, tg, ag, tp, ap):
    for key, fl, val, conv in (("tg", F_RGTG, tg, parse_gain_cdb), ("ag", F_RGAG, ag, parse_gain_cdb),
                               ("tp", F_RGTP, tp, parse_peak_q15), ("ap", F_RGAP, ap, parse_peak_q15)):
        v = conv(val) if val else None
        if v is not None:
            d[key] = v
            d["flags"] |= fl


def _flac(path, size):
    d = dict(codec=2, ch=2, bits=16, flags=0, rate=0, size=size, kbps=0, cover_kib=0, enc="")
    rg = {}
    secs = 0
    with open(path, "rb") as f:
        if f.read(4) != b"fLaC":
            return d
        pic = 0
        while True:
            h = f.read(4)
            if len(h) < 4:
                break
            last, typ, ln = h[0] >> 7, h[0] & 0x7F, int.from_bytes(h[1:4], "big")
            if typ == 0 and ln >= 18:
                b = f.read(ln)
                sr = (b[10] << 12) | (b[11] << 4) | (b[12] >> 4)
                d["ch"] = ((b[12] >> 1) & 7) + 1
                d["bits"] = (((b[12] & 1) << 4) | (b[13] >> 4)) + 1
                total = ((b[13] & 15) << 32) | struct.unpack(">I", b[14:18])[0]
                d["rate"] = sr
                secs = total / sr if sr else 0
            elif typ == 4 and ln < 1 << 20:
                b = f.read(ln)
                vl = struct.unpack("<I", b[:4])[0]
                d["enc"] = b[4:4 + vl].decode("utf-8", "replace").strip("\x00 ")
                p = 4 + vl
                n = struct.unpack("<I", b[p:p + 4])[0]
                p += 4
                for _ in range(n):
                    cl = struct.unpack("<I", b[p:p + 4])[0]
                    kv = b[p + 4:p + 4 + cl].decode("utf-8", "replace")
                    p += 4 + cl
                    if "=" in kv:
                        k, v = kv.split("=", 1)
                        rg[k.upper()] = v
            else:
                if typ == 6:
                    pic += ln
                f.seek(ln, 1)
            if last:
                break
    d["cover_kib"] = min(65535, (pic + 1023) // 1024) if pic else 0
    if secs:
        d["kbps"] = int(round(size * 8 / secs / 1000))
    _rg_into(d, rg.get("REPLAYGAIN_TRACK_GAIN"), rg.get("REPLAYGAIN_ALBUM_GAIN"), rg.get("REPLAYGAIN_TRACK_PEAK"), rg.get("REPLAYGAIN_ALBUM_PEAK"))
    return d


def extract(path):
    """-> dict of the fields above ('enc' is text; the sidecar flag is set by the caller: it needs the folder)."""
    path = Path(path)
    size = path.stat().st_size
    d = _flac(path, size) if path.suffix.lower() == ".flac" else _mp3(path, size)
    if d["cover_kib"]:
        d["flags"] |= F_COVER
    if (path.parent / SIDECAR).is_file():
        d["flags"] |= F_SIDECAR
    return d


def pack(d, pool):
    """One 32-byte record (peaks are u16 Q15: a peak of 2.0 or more is stored as 65535, issue 025; parse_peak_q15 is unchanged so the firmware rule stays identical); `pool.add(text)` returns the string offset (0 stays 'none': the pool's first string is the root path, so a real string never has offset 0)."""
    enc = pool.add(d["enc"]) if d.get("enc") else 0
    return struct.pack("<BBBBIIHHhhHHHHI", d["codec"], d["ch"], d["bits"], d["flags"], d["rate"], min(d["size"], 0xFFFFFFFF),
                       min(d["kbps"], 65535), d["cover_kib"], d.get("tg", 0), d.get("ag", 0), min(d.get("tp", 0), 65535), min(d.get("ap", 0), 65535),
                       d.get("delay", 0), d.get("pad", 0), enc)


FIELDS = "codec ch bits flags rate size kbps cover_kib tg ag tp ap delay pad enc_off".split()


def unpack(b):
    return dict(zip(FIELDS, struct.unpack("<BBBBIIHHhhHHHHI", b)))


def section(records):
    return struct.pack("<4sHHII", b"TDET", VERSION, REC, len(records) // REC, 0) + bytes(records)
