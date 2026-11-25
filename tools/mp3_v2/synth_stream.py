#!/usr/bin/env python3
"""Tiny synthetic MP3 stream builder (own code, from the layout of the standard: ISO/IEC 11172-3 header, side information and Huffman table 1).
Used by the MP3 v2 tests (WP-01 oracle determinism, WP-03 scanner). It is NOT the full stream builder of WP-08: it covers what those two
tests need, every field of the header and the side information, an optional CRC field, tags (ID3v2 with footer/unsync/extended flags, ID3v1,
APEv2), a Xing/Info (+LAME delay/padding) frame and a VBRI frame.

Content: every granule/channel codes `pairs` pairs of Huffman table 1 (values 0 or 1, pseudo-random from a seeded LCG), all three regions use
table 1, linbits 0, no scale factor bits (scalefac_compress 0), no count1 area. Any block type or region split therefore decodes the same bits.
`main_data_begin` can be set to point before the stream start (a frame short of reservoir data)."""
import struct

BITRATES = {1: [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320],
            2: [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160]}   # layer III; MPEG-2 and 2.5 share the second row
RATES = {1: [44100, 48000, 32000], 2: [22050, 24000, 16000], 25: [11025, 12000, 8000]}
VERSION_BITS = {1: 3, 2: 2, 25: 0}
# Huffman table 1 (the standard's table, pairs (x, y) in {0,1}); sign bits follow for nonzero values
TABLE1 = {(0, 0): (1, 1), (0, 1): (1, 3), (1, 0): (1, 2), (1, 1): (0, 3)}   # (code, length)


class BitWriter:
    def __init__(self): self.bits = []
    def put(self, v, n):
        for i in range(n - 1, -1, -1): self.bits.append((v >> i) & 1)
    def nbits(self): return len(self.bits)
    def tobytes(self):
        b = self.bits + [0] * (-len(self.bits) % 8)
        return bytes(sum(b[i + j] << (7 - j) for j in range(8)) for i in range(0, len(b), 8))


def frame_bytes(version, br_idx, sr_idx, padding):
    br = BITRATES[1 if version == 1 else 2][br_idx] * 1000
    sr = RATES[version][sr_idx]
    return (144 if version == 1 else 72) * br // sr + padding


def side_info_size(version, channels):
    return {(1, 1): 17, (1, 2): 32, (2, 1): 9, (2, 2): 17, (25, 1): 9, (25, 2): 17}[(version, channels)]


def header(version=1, crc=False, br_idx=9, sr_idx=0, padding=0, mode=1, mode_ext=0, emphasis=0, layer_bits=1):
    """mode: 0 stereo, 1 joint stereo, 2 dual channel, 3 mono"""
    h = 0xFFE00000 | (VERSION_BITS[version] << 19) | (layer_bits << 17) | ((0 if crc else 1) << 16) | (br_idx << 12) | (sr_idx << 10) | (padding << 9) \
        | (mode << 6) | (mode_ext << 4) | emphasis
    return struct.pack(">I", h)


class LCG:
    def __init__(self, seed): self.s = seed & 0xFFFFFFFF
    def next(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s >> 16


def granule_bits(rng, pairs):
    w = BitWriter()
    for _ in range(pairs):
        x, y = rng.next() & 1, rng.next() & 1
        code, ln = TABLE1[(x, y)]
        w.put(code, ln)
        if x: w.put(rng.next() & 1, 1)
        if y: w.put(rng.next() & 1, 1)
    return w


def frame(version=1, mode=1, mode_ext=0, crc=False, br_idx=9, sr_idx=0, padding=0, main_data_begin=0, window=None, mixed=None, block_type=None,
          pairs=40, seed=1, scfsi=0, emphasis=0, fill=0, gain=190, total=None):
    """One Layer III frame. window/block_type/mixed: per-(granule, channel) lists (or a single value for all); block_type 0..3 (needs window=1).
    Returns bytes."""
    ch = 1 if mode == 3 else 2
    ngr = 2 if version == 1 else 1
    n = ngr * ch
    def per(v, default):
        if v is None: return [default] * n
        return list(v) if isinstance(v, (list, tuple)) else [v] * n
    ws = per(window, 0); bt = per(block_type, 0); mx = per(mixed, 0)
    rng = LCG(seed)
    chunks = [granule_bits(rng, pairs) for _ in range(n)]
    si = BitWriter()
    if version == 1:
        si.put(main_data_begin, 9); si.put(0, 5 if ch == 1 else 3)
        for c in range(ch): si.put(scfsi & 15, 4)
    else:
        si.put(main_data_begin, 8); si.put(0, 1 if ch == 1 else 2)
    for g in range(ngr):
        for c in range(ch):
            i = g * ch + c
            si.put(chunks[i].nbits(), 12); si.put(pairs, 9); si.put(gain, 8)
            si.put(0, 4 if version == 1 else 9)
            si.put(ws[i], 1)
            if ws[i]:
                si.put(bt[i], 2); si.put(mx[i], 1); si.put(1, 5); si.put(1, 5)
                for _ in range(3): si.put(0, 3)
            else:
                si.put(1, 5); si.put(1, 5); si.put(1, 5); si.put(0, 4); si.put(0, 3)
            if version == 1: si.put(0, 1)       # preflag (MPEG-1 only)
            si.put(0, 1); si.put(0, 1)           # scalefac_scale, count1table_select
    sib = si.tobytes()
    assert len(sib) == side_info_size(version, ch), (len(sib), version, ch)
    data = BitWriter()
    for c in chunks: data.bits += c.bits
    body = sib + data.tobytes()
    total = total if total is not None else frame_bytes(version, br_idx, sr_idx, padding)
    crcb = b"\x00\x00" if crc else b""
    pad = total - 4 - len(crcb) - len(body)
    assert pad >= 0, "too much main data for the frame"
    return header(version, crc, br_idx, sr_idx, padding, mode, mode_ext, emphasis) + crcb + body + bytes([fill]) * pad


def xing_frame(version=1, mode=1, br_idx=9, sr_idx=0, kind=b"Info", frames=100, nbytes=40000, toc=True, delay=576, padding=1000, lame=True, crc=False):
    ch = 1 if mode == 3 else 2
    total = frame_bytes(version, br_idx, sr_idx, 0)
    off = 4 + (2 if crc else 0) + side_info_size(version, ch)
    body = bytearray(total)
    body[0:4] = header(version, crc, br_idx, sr_idx, 0, mode, 0)
    flags = 1 | 2 | (4 if toc else 0)
    t = kind + struct.pack(">III", flags, frames, nbytes) + (bytes(range(0, 200, 2)) if toc else b"")
    if lame:
        t += b"LAME3.100" + bytes([0x24, 0x00]) + b"\x00" * 8 + bytes([0x00, 0x00]) + bytes([(delay >> 4) & 255, ((delay & 15) << 4) | ((padding >> 8) & 15), padding & 255]) + b"\x00" * 6
    body[off:off + len(t)] = t
    return bytes(body)


def vbri_frame(version=1, mode=1, br_idx=9, sr_idx=0, frames=100, nbytes=40000):
    total = frame_bytes(version, br_idx, sr_idx, 0)
    body = bytearray(total)
    body[0:4] = header(version, False, br_idx, sr_idx, 0, mode, 0)
    body[36:36 + 26] = b"VBRI" + struct.pack(">HHHII", 1, 0, 0, nbytes, frames) + struct.pack(">HHHH", 0, 1, 2, 4)   # version, delay, quality, bytes, frames, toc entries ...
    return bytes(body)


def synchsafe(n): return bytes([(n >> 21) & 127, (n >> 14) & 127, (n >> 7) & 127, n & 127])


def id3v2(size=200, footer=False, unsync=False, ext=False):
    flags = (0x80 if unsync else 0) | (0x40 if ext else 0) | (0x10 if footer else 0)
    body = b"\x00" * size   # padding only (a tag body of zeros is valid padding)
    out = b"ID3\x03\x00" + bytes([flags]) + synchsafe(size) + body
    if footer: out += b"3DI\x03\x00" + bytes([flags]) + synchsafe(size)
    return out


def id3v1(): return b"TAG" + b"\x00" * 125


def apev2(items_size=40):
    body = b"\x00" * items_size
    foot = b"APETAGEX" + struct.pack("<IIII", 2000, items_size + 32, 0, 0) + b"\x00" * 8     # footer only: flags 0 = no header present
    return body + foot


def stream(nframes=20, seed=1, **kw):
    """nframes identical-geometry frames with distinct data (seed advances), joined."""
    return b"".join(frame(seed=seed + i, **kw) for i in range(nframes))


if __name__ == "__main__":
    import sys
    sys.stdout.buffer.write(stream(int(sys.argv[1]) if len(sys.argv) > 1 else 20))
