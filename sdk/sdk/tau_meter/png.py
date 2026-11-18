"""A PNG writer and a tiny reader for our own files (RGB 8 bit), pure Python (zlib). Used for the preview image and the thumbnail tool."""
import struct, zlib


def write_png(path, width, height, rgb):
    """rgb: bytes/bytearray of width*height*3"""
    raw = b"".join(b"\0" + bytes(rgb[y * width * 3:(y + 1) * width * 3]) for y in range(height))
    def chunk(t, data):
        c = struct.pack(">I", len(data)) + t + data
        return c + struct.pack(">I", zlib.crc32(t + data) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_png(path):
    """-> (width, height, rgb bytes) for 8-bit RGB or RGBA non-interlaced PNGs (what write_png and common tools make)"""
    d = open(path, "rb").read()
    if d[:8] != b"\x89PNG\r\n\x1a\n": raise ValueError("not a PNG file")
    pos, idat, ihdr = 8, b"", None
    while pos < len(d):
        n, t = struct.unpack_from(">I4s", d, pos); body = d[pos + 8:pos + 8 + n]; pos += 12 + n
        if t == b"IHDR": ihdr = struct.unpack(">IIBBBBB", body)
        elif t == b"IDAT": idat += body
    w, h, depth, ctype, _, _, inter = ihdr
    if depth != 8 or ctype not in (2, 6) or inter: raise ValueError("only 8-bit RGB or RGBA PNG files without interlacing are read")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(idat); stride = w * bpp; out = bytearray(); prev = bytearray(stride); o = 0
    for _ in range(h):
        f = raw[o]; line = bytearray(raw[o + 1:o + 1 + stride]); o += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 255
            elif f == 2: line[i] = (line[i] + b) & 255
            elif f == 3: line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line; prev = line
    if bpp == 4:
        out = bytearray(b for i in range(0, len(out), 4) for b in out[i:i + 3])
    return w, h, bytes(out)
