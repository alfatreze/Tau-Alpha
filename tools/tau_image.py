#!/usr/bin/env python3
"""Tau image formats: encoders, decoders, a small container, and a load-time model.

One place to add and compare cover/thumbnail formats. Nothing here is read by the firmware yet (the library
thumbnail file is deferred, docs/MEDIA_LIBRARY_0.4_SPEC.md section 3); the formats are an offline study
(docs/IMAGE_FORMATS.md) that tools/sync_media.py can write next to the music with --art-variants, so real
covers on a real card can be tried as soon as a reader exists.

Requires Pillow and numpy (imported lazily; sync_media works without them unless art variants are asked for).

Container "TIM1", 16-byte header, little endian:
    0  4  magic 'TIM1'
    4  1  fmt   1 rgb565 | 2 palette | 3 bc1 | 4 jpeg
    5  1  bpp   16 | 8, 6 or 4 (palette) | 4 (bc1, informational) | 0 (jpeg)
    6  2  width
    8  2  height
   10  2  ncolors (palette entries; 0 otherwise)
   12  4  payload length in bytes (CLUT + pixels, excluding this header)
Payload:
    rgb565   w*h little-endian words, row major
    palette  ncolors little-endian RGB565 entries (the CLUT), then indices row major, MSB first, packed at bpp
             (8: one byte; 4: two per byte; 6: four per three bytes)
    bc1      ceil(w/4)*ceil(h/4) 8-byte blocks, row major (standard S3TC/DXT1: two RGB565 endpoints, 2-bit indices)
    jpeg     a baseline JPEG (4:2:0), the same decoder the player already has

Adding a codec: write encode(rgb ndarray HxWx3 uint8) -> bytes payload-with-header via pack(), a decoder in
DECODERS, and an entry in VARIANTS. tools/lab/img_format_lab.py then compares it with everything else.
"""
import io
import struct

MAGIC = b"TIM1"
F_RGB565, F_PAL, F_BC1, F_JPEG = 1, 2, 3, 4
FMT_NAMES = {F_RGB565: "rgb565", F_PAL: "palette", F_BC1: "bc1", F_JPEG: "jpeg"}


def _np():
    import numpy
    return numpy


def _pil():
    from PIL import Image
    return Image


# -- helpers -----------------------------------------------------------------------------------------------

def to565(a):
    """uint8 HxWx3 -> uint16 HxW RGB565."""
    np = _np()
    a = a.astype(np.uint16)
    return ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)


def from565(w):
    """uint16 array -> uint8 ...x3 with the usual bit replication."""
    np = _np()
    r, g, b = (w >> 11) & 31, (w >> 5) & 63, w & 31
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)


def quantise565(a):
    return from565(to565(a))


def psnr(a, b):
    np = _np()
    m = ((a.astype(float) - b.astype(float)) ** 2).mean()
    return 99.0 if m == 0 else float(10 * np.log10(255.0 ** 2 / m))


def fit_long_side(img, size):
    """PIL image -> uint8 HxWx3 scaled proportionally so the LONG side is `size` px (Lanczos). No crop, no padding:
    a non-square cover stays non-square (e.g. 1024x1540 -> 85x128). The container stores width and height."""
    Image = _pil()
    img = img.convert("RGB")
    w, h = img.size
    k = size / max(w, h)
    return _np().asarray(img.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS))


fit_square = fit_long_side      # old name, kept for callers written before non-square covers were kept as they are


def pack(fmt, bpp, w, h, ncolors, payload):
    return MAGIC + struct.pack("<BBHHHI", fmt, bpp, w, h, ncolors, len(payload)) + payload


def unpack_header(data):
    if data[:4] != MAGIC:
        raise ValueError("not a TIM1 file")
    fmt, bpp, w, h, nc, ln = struct.unpack("<BBHHHI", data[4:16])
    if len(data) != 16 + ln:
        raise ValueError(f"length mismatch: header says {ln}, file has {len(data) - 16}")
    return fmt, bpp, w, h, nc, data[16:]


# -- bit packing for palette indices -----------------------------------------------------------------------

def pack_indices(idx, bpp):
    np = _np()
    flat = idx.reshape(-1).astype(np.uint8)
    if bpp == 8:
        return flat.tobytes()
    if bpp == 4:
        if len(flat) & 1:
            flat = np.append(flat, 0)
        return ((flat[0::2] << 4) | flat[1::2]).astype(np.uint8).tobytes()
    if bpp == 6:
        pad = (-len(flat)) % 4
        if pad:
            flat = np.append(flat, np.zeros(pad, np.uint8))
        q = flat.reshape(-1, 4).astype(np.uint32)
        v = (q[:, 0] << 18) | (q[:, 1] << 12) | (q[:, 2] << 6) | q[:, 3]
        return np.stack([(v >> 16) & 255, (v >> 8) & 255, v & 255], 1).astype(np.uint8).tobytes()
    raise ValueError(bpp)


def unpack_indices(buf, bpp, count):
    np = _np()
    b = np.frombuffer(buf, np.uint8)
    if bpp == 8:
        return b[:count].copy()
    if bpp == 4:
        out = np.empty(len(b) * 2, np.uint8)
        out[0::2], out[1::2] = b >> 4, b & 15
        return out[:count]
    if bpp == 6:
        q = b.reshape(-1, 3).astype(np.uint32)
        v = (q[:, 0] << 16) | (q[:, 1] << 8) | q[:, 2]
        out = np.stack([(v >> 18) & 63, (v >> 12) & 63, (v >> 6) & 63, v & 63], 1).astype(np.uint8).reshape(-1)
        return out[:count]
    raise ValueError(bpp)


# -- encoders ----------------------------------------------------------------------------------------------

def enc_rgb565(rgb):
    h, w, _ = rgb.shape
    return pack(F_RGB565, 16, w, h, 0, to565(rgb).astype("<u2").tobytes())


def enc_palette(rgb, colours):
    """Median-cut + Floyd-Steinberg (Pillow), CLUT quantised to RGB565."""
    np = _np()
    Image = _pil()
    h, w, _ = rgb.shape
    bpp = {256: 8, 64: 6, 16: 4}[colours]
    p = Image.fromarray(rgb).quantize(colors=colours, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    pal = np.array(p.getpalette()[:colours * 3], np.uint8).reshape(-1, 3)
    if len(pal) < colours:
        pal = np.vstack([pal, np.zeros((colours - len(pal), 3), np.uint8)])
    clut = to565(pal[None])[0].astype("<u2")
    idx = np.asarray(p, np.uint8)
    return pack(F_PAL, bpp, w, h, colours, clut.tobytes() + pack_indices(idx, bpp))


def _bc1_palette(c0, c1):
    """Four-colour BC1 palette from two RGB565 words (c0 > c1), integer maths as the decoders use."""
    np = _np()
    a = from565(np.array([c0], np.uint16))[0].astype(int)
    b = from565(np.array([c1], np.uint16))[0].astype(int)
    return np.array([a, b, (2 * a + b) // 3, (a + 2 * b) // 3])


def enc_bc1(rgb):
    """Principal-axis endpoints with one least-squares refinement. Crude on purpose (a real encoder such as
    stb_dxt or squish would do better); the format and decoder are what matter for the study."""
    np = _np()
    h, w, _ = rgb.shape
    H, W = (h + 3) // 4 * 4, (w + 3) // 4 * 4
    a = np.pad(rgb, ((0, H - h), (0, W - w), (0, 0)), mode="edge")
    out = bytearray()
    for by in range(0, H, 4):
        for bx in range(0, W, 4):
            blk = a[by:by + 4, bx:bx + 4].reshape(-1, 3).astype(float)
            mu = blk.mean(0)
            c = blk - mu
            if c.std() > 0:
                ax = np.linalg.svd(c, full_matrices=False)[2][0]
                t = c @ ax
                e0, e1 = mu + ax * t.max(), mu + ax * t.min()
            else:
                e0 = e1 = mu
            best = None
            for _ in range(2):                                     # pick indices, refit endpoints, once
                w0 = int(to565(np.clip(e0, 0, 255).astype(np.uint8)[None, None])[0, 0])
                w1 = int(to565(np.clip(e1, 0, 255).astype(np.uint8)[None, None])[0, 0])
                if w0 < w1:
                    w0, w1 = w1, w0
                pal = _bc1_palette(w0, w1) if w0 != w1 else np.tile(_bc1_palette(w0, w0)[:1], (4, 1))
                d = ((blk[:, None] - pal[None]) ** 2).sum(-1)
                idx = d.argmin(1) if w0 != w1 else np.zeros(16, int)
                err = d[np.arange(16), idx].sum()
                if best is None or err < best[0]:
                    best = (err, w0, w1, idx)
                # least squares: endpoint means weighted by interpolation position
                wt = np.array([1.0, 0.0, 2 / 3, 1 / 3])[idx]
                s00, s01, s11 = (wt * wt).sum(), (wt * (1 - wt)).sum(), ((1 - wt) ** 2).sum()
                det = s00 * s11 - s01 * s01
                if abs(det) < 1e-6:
                    break
                r0, r1 = (wt[:, None] * blk).sum(0), ((1 - wt)[:, None] * blk).sum(0)
                e0, e1 = (s11 * r0 - s01 * r1) / det, (s00 * r1 - s01 * r0) / det
            _, w0, w1, idx = best
            bits = 0
            for i, v in enumerate(idx):
                bits |= int(v) << (2 * i)
            out += struct.pack("<HHI", w0, w1, bits)
    return pack(F_BC1, 4, w, h, 0, bytes(out))


def enc_jpeg(rgb, quality):
    Image = _pil()
    h, w, _ = rgb.shape
    f = io.BytesIO()
    Image.fromarray(rgb).save(f, "JPEG", quality=quality, subsampling=2, optimize=True, progressive=False)
    return pack(F_JPEG, 0, w, h, 0, f.getvalue())


# -- decoders ----------------------------------------------------------------------------------------------

def dec_rgb565(bpp, w, h, nc, pl):
    np = _np()
    return from565(np.frombuffer(pl, "<u2")[:w * h].reshape(h, w))


def dec_palette(bpp, w, h, nc, pl):
    np = _np()
    clut = np.frombuffer(pl[:nc * 2], "<u2")
    idx = unpack_indices(pl[nc * 2:], bpp, w * h)
    return from565(clut[idx].reshape(h, w))


def dec_bc1(bpp, w, h, nc, pl):
    np = _np()
    bw, bh = (w + 3) // 4, (h + 3) // 4
    out = np.zeros((bh * 4, bw * 4, 3), np.uint8)
    for i in range(bw * bh):
        w0, w1, bits = struct.unpack_from("<HHI", pl, i * 8)
        if w0 > w1:
            pal = _bc1_palette(w0, w1)
        else:                                                      # 3-colour mode, index 3 = black
            a = from565(np.array([w0], np.uint16))[0].astype(int)
            b = from565(np.array([w1], np.uint16))[0].astype(int)
            pal = np.array([a, b, (a + b) // 2, [0, 0, 0]])
        by, bx = divmod(i, bw)
        for p in range(16):
            out[by * 4 + p // 4, bx * 4 + p % 4] = pal[(bits >> (2 * p)) & 3]
    return out[:h, :w]


def dec_jpeg(bpp, w, h, nc, pl):
    return _np().asarray(_pil().open(io.BytesIO(pl)).convert("RGB"))


DECODERS = {F_RGB565: dec_rgb565, F_PAL: dec_palette, F_BC1: dec_bc1, F_JPEG: dec_jpeg}


def decode(data):
    """TIM1 bytes -> uint8 HxWx3 (what the screen would show, before any RGB565 the display itself applies)."""
    fmt, bpp, w, h, nc, pl = unpack_header(data)
    return DECODERS[fmt](bpp, w, h, nc, pl)


# -- variants and the automatic choice ---------------------------------------------------------------------

VARIANTS = {
    "rgb565": enc_rgb565,
    "pal256": lambda a: enc_palette(a, 256),
    "pal64": lambda a: enc_palette(a, 64),
    "pal16": lambda a: enc_palette(a, 16),
    "bc1": enc_bc1,
    "jpg60": lambda a: enc_jpeg(a, 60),
    "jpg75": lambda a: enc_jpeg(a, 75),
    "jpg85": lambda a: enc_jpeg(a, 85),
}
DEFAULT_VARIANTS = "pal256"                    # the chosen default (owner, 2026-09-26): see docs/IMAGE_FORMATS.md
DEFAULT_SIZE = 128                              # main cover size, long side in px
AUTO_CANDIDATES = ("pal64", "pal256", "bc1")   # what 'auto' picks between (jpeg is the fallback, never chosen here)
AUTO_MARGIN_DB = 1.5


def encode(rgb, variant):
    if variant not in VARIANTS:
        raise ValueError(f"unknown variant {variant!r}; known: {', '.join(VARIANTS)}")
    return VARIANTS[variant](rgb)


def score(rgb, data):
    return psnr(quantise565(rgb), decode(data))


def encode_auto(rgb):
    """Smallest candidate within AUTO_MARGIN_DB of the best PSNR. -> (bytes, variant name, psnr)."""
    res = []
    for v in AUTO_CANDIDATES:
        d = encode(rgb, v)
        res.append((v, d, score(rgb, d)))
    top = max(r[2] for r in res)
    v, d, s = min((r for r in res if r[2] >= top - AUTO_MARGIN_DB), key=lambda r: len(r[1]))
    return d, v, s


# -- load-time model ---------------------------------------------------------------------------------------
# ESTIMATES, not measurements. Anchors: uncached SDRAM window ~48-50 cycles per access at 60 MHz, so about
# 0.8 us per 32-bit word (A-094); CPU 60 MHz, rv32im, no FPU; embedded-cover decodes measured on the Pocket
# at 2.6 s (small cover), 5.3 s (455 px), 15.8 s (1400 px) (A-120, B-027). The SD read rate is an ASSUMPTION
# (0.5-2 MB/s plus a fixed open/seek) because no clean device measurement exists. Replace these constants
# when a reader exists and the numbers can be measured.

SD_MBPS = (0.5, 2.0)
SD_FIXED_MS = 5.0
WORD_US = 0.8
CBLIT_US_PER_PX = (0.5, 1.5)
BC1_CYCLES_PER_BLOCK = (250, 400)
JPEG_MS_PER_MCU = (2.0, 6.0)      # from the two measured decodes above (6.3 and 2.0 ms per 16x16 MCU)
CPU_MHZ = 60.0


def estimate_ms(data):
    """-> (low, high) milliseconds to read the file and put the picture on screen."""
    fmt, bpp, w, h, nc, pl = unpack_header(data)
    n = len(data)
    rd = (SD_FIXED_MS + n / (SD_MBPS[1] * 1e3), SD_FIXED_MS + n / (SD_MBPS[0] * 1e3))
    words = w * h / 2
    if fmt == F_RGB565:
        t = (words * WORD_US / 1e3,) * 2
    elif fmt == F_PAL:
        cp = (len(pl) / 4) * WORD_US / 1e3                          # copy index plane to SDRAM
        t = (cp + w * h * CBLIT_US_PER_PX[0] / 1e3, cp + w * h * CBLIT_US_PER_PX[1] / 1e3)
    elif fmt == F_BC1:
        blocks = ((w + 3) // 4) * ((h + 3) // 4)
        wr = words * WORD_US / 1e3
        t = tuple(blocks * c / CPU_MHZ / 1e3 + wr for c in BC1_CYCLES_PER_BLOCK)
    else:
        mcus = ((w + 15) // 16) * ((h + 15) // 16)
        t = (mcus * JPEG_MS_PER_MCU[0], mcus * JPEG_MS_PER_MCU[1])
    return rd[0] + t[0], rd[1] + t[1]


def describe(rgb, data):
    fmt, bpp, w, h, nc, pl = unpack_header(data)
    lo, hi = estimate_ms(data)
    return {"format": FMT_NAMES[fmt], "bpp": bpp, "w": w, "h": h, "bytes": len(data),
            "psnr": score(rgb, data), "ms_lo": lo, "ms_hi": hi}


def _flac_picture(d):
    """Cover image bytes from a FLAC file's PICTURE metadata blocks (front cover preferred), or None."""
    if d[:4] != b"fLaC":
        return None
    i, best = 4, None
    while i + 4 <= len(d):
        hdr = d[i]; n = int.from_bytes(d[i + 1:i + 4], "big"); i += 4
        if (hdr & 0x7F) == 6 and i + n <= len(d):
            b = d[i:i + n]
            try:
                ptype = int.from_bytes(b[0:4], "big")
                o = 4; ml = int.from_bytes(b[o:o + 4], "big"); o += 4 + ml
                dl = int.from_bytes(b[o:o + 4], "big"); o += 4 + dl + 16
                dn = int.from_bytes(b[o:o + 4], "big"); o += 4
                img = b[o:o + dn]
                if img and (best is None or ptype == 3):
                    best = img
                    if ptype == 3:
                        return best
            except (IndexError, ValueError):
                pass
        i += n
        if hdr & 0x80:
            break
    return best


def extract_embedded(path):
    """First embedded cover of an MP3 (ID3v2 APIC) or a FLAC (PICTURE block) as raw image bytes, or None."""
    try:
        d = open(path, "rb").read(8 << 20)
    except OSError:
        return None
    if d[:4] == b"fLaC":
        return _flac_picture(d)
    if d[:3] != b"ID3":
        return None
    i = d.find(b"APIC", 10)
    if i < 0:
        return None
    for sig in (b"\xff\xd8\xff", b"\x89PNG\r\n\x1a\n"):
        j = d.find(sig, i)
        if j > 0:
            return d[j:]                       # trailing bytes are ignored by the image decoder
    return None
