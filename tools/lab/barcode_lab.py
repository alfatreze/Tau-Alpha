#!/usr/bin/env python3
"""Barcode study lab (branch barcode-study): compare ways to carry a Tau report on the 400x360 Pocket screen.

Candidates (all host-side models, nothing runs on a Pocket):
  qr     current scheme: QR level L, byte mode, base64 text of the record (what fw/qrcode.h draws)
  qr3    three QR codes, one per R/G/B plane (any plain QR decoder reads each plane after a channel split)
  grid   Tau pixel grid: cell x cell pixels, `bits` bits per colour channel, row-major, fiducial frame + header

Run:  work/venv-qr/bin/python tools/lab/barcode_lab.py
"""
import base64, io, os, struct, sys, time, zlib
import numpy as np
from PIL import Image
import cv2

W, H = 400, 360


def to565(img):                               # what the Pocket screenshot contains: RGB565 expanded to 888
    a = np.asarray(img.convert("RGB"), dtype=np.uint16)
    r, g, b = a[..., 0] >> 3, a[..., 1] >> 2, a[..., 2] >> 3
    return np.stack([(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)], -1).astype(np.uint8)


# ---------------------------------------------------------------- grid format
# Layout: 4 px solid fiducial frame (alternating black/white 4 px blocks) around the cell area; header = first
# HDR_CELLS cells are always cell=4, 1 bit/channel (8 colours) so a decoder can read geometry before anything else.
FR = 4
MAGIC = 0x7A


def grid_geometry(cell):
    return (W - 2 * FR) // cell, (H - 2 * FR) // cell


def grid_encode(payload: bytes, cell=1, bits=5):
    """bits per channel 1..5 (6 for green in 565 would need a split; keep symmetric). Returns image and bytes used."""
    gw, gh = grid_geometry(cell)
    hdr = struct.pack(">BBBBI", MAGIC, cell, bits, 0, len(payload)) + struct.pack(">I", zlib.crc32(payload))
    data = hdr + payload
    bpc = 3 * bits                                   # bits per cell
    nb = len(data) * 8
    if nb > gw * gh * bpc:
        return None, 0
    bitarr = np.unpackbits(np.frombuffer(data, np.uint8))
    bitarr = np.concatenate([bitarr, np.zeros(gw * gh * bpc - nb, np.uint8)])
    v = bitarr.reshape(-1, 3, bits)
    levels = (v * (1 << np.arange(bits - 1, -1, -1))).sum(-1)          # 0..2^bits-1 per channel
    maxl = (1 << bits) - 1
    px = (levels * 255 // maxl).astype(np.uint8).reshape(gh, gw, 3)
    img = np.zeros((H, W, 3), np.uint8)
    body = np.repeat(np.repeat(px, cell, 0), cell, 1)
    img[FR:FR + gh * cell, FR:FR + gw * cell] = body
    for i in range(0, W, 8):                         # fiducial frame
        img[0:FR, i:i + 4] = 255; img[H - FR:H, i:i + 4] = 255
    for i in range(0, H, 8):
        img[i:i + 4, 0:FR] = 255; img[i:i + 4, W - FR:W] = 255
    return img, len(data)


def grid_decode(img, cell, bits):
    """Decoder is told cell/bits here (the real format would read them from a fixed-mode header)."""
    gw, gh = grid_geometry(cell)
    a = np.asarray(img)[FR:FR + gh * cell, FR:FR + gw * cell]
    c = cell // 2
    samp = a[c::cell, c::cell] if cell > 1 else a                      # centre sample of each cell
    maxl = (1 << bits) - 1
    lv = np.clip((samp.astype(np.int32) * maxl + 127) // 255, 0, maxl).astype(np.uint8)
    bb = ((lv[..., None] >> np.arange(bits - 1, -1, -1)) & 1).astype(np.uint8).reshape(-1)
    raw = np.packbits(bb).tobytes()
    if raw[0] != MAGIC:
        return None
    n = struct.unpack(">I", raw[4:8])[0]
    crc = struct.unpack(">I", raw[8:12])[0]
    pl = raw[12:12 + n]
    return pl if zlib.crc32(pl) == crc else None


# ---------------------------------------------------------------- QR
def qr_matrix(text: bytes, ecl="L"):
    import segno
    q = segno.make(text, error=ecl, micro=False, boost_error=False, mask=0, encoding="latin-1")
    return q


def qr_draw(q, m):
    mat = np.array(q.matrix, dtype=np.uint8)
    n = mat.shape[0]
    side = (n + 8) * m
    if side > min(W, H):
        return None
    img = np.full((H, W, 3), 255, np.uint8)
    body = np.where(np.repeat(np.repeat(mat, m, 0), m, 1) == 1, 0, 255).astype(np.uint8)
    img[4 * m:4 * m + n * m, 4 * m:4 * m + n * m] = body[..., None]
    return img


def qr_decode(img):
    dets = [cv2.QRCodeDetectorAruco(), cv2.QRCodeDetector()]
    for scale in (1, 2):
        im = img if scale == 1 else cv2.resize(img, None, fx=scale, fy=scale, interpolation=cv2.INTER_NEAREST)
        for d in dets:
            try:
                t, _, _ = d.detectAndDecode(im)
            except cv2.error:
                t = ""
            if t:
                return t.encode("latin-1")
    return None


def qr_cap_bytes(version, ecl="L"):
    import segno
    # largest byte-mode payload whose automatically chosen version is still <= `version` (segno ignores a too-small
    # explicit version and silently upgrades, so check q.version instead)
    lo, hi = 1, 3200
    while lo < hi:
        mid = (lo + hi + 1) // 2
        if segno.make(b"A" * mid, error=ecl, micro=False, encoding="latin-1").version <= version:
            lo = mid
        else:
            hi = mid - 1
    return lo


# ---------------------------------------------------------------- channels
def damage(img, kind):
    if kind == "png":
        return img
    if kind.startswith("jpeg"):
        q = int(kind[4:])
        b = io.BytesIO(); Image.fromarray(img).save(b, "JPEG", quality=q); b.seek(0)
        return np.asarray(Image.open(b).convert("RGB"))
    if kind == "half":                               # downscale 50% then back up (chat-app preview)
        s = cv2.resize(img, (W // 2, H // 2), interpolation=cv2.INTER_AREA)
        return cv2.resize(s, (W, H), interpolation=cv2.INTER_LINEAR)
    if kind == "cam":                                # crude camera: blur + gain/gamma + noise + jpeg 80
        rng = np.random.default_rng(1)
        f = cv2.GaussianBlur(img, (0, 0), 1.1).astype(np.float32) / 255
        f = f ** 1.15 * 0.93 + 0.03
        f = f + rng.normal(0, 0.02, f.shape)
        return damage((np.clip(f, 0, 1) * 255).astype(np.uint8), "jpeg80")
    raise ValueError(kind)


def rand_record(n, seed=7):
    # TLV-ish, partly structured like the real record (not incompressible noise), but kept as raw bytes
    rng = np.random.default_rng(seed)
    return bytes(rng.integers(0, 256, n, dtype=np.uint8))


def main():
    kinds = ["png", "jpeg95", "jpeg80", "half", "cam"]
    print("== capacity (400x360 screen, payload bytes of binary record) ==")
    for v in (12, 20, 38):
        raw = qr_cap_bytes(v)
        m = 4 if v <= 12 else 3 if v <= 20 else 2
        print(f" qr v{v:<2} L  mod {m}px  text cap {raw:5d} B -> binary (base64) {raw * 3 // 4:5d} B")
    for cell, bits in [(1, 5), (1, 3), (2, 5), (2, 3), (4, 3), (4, 2), (4, 1), (6, 1)]:
        gw, gh = grid_geometry(cell)
        cap = gw * gh * 3 * bits // 8 - 12
        print(f" grid cell {cell}px {bits}b/ch      cap {cap:6d} B")
    print(" qr3 (3 planes of v38): ~", 3 * qr_cap_bytes(38) * 3 // 4, "B")

    print("\n== decode survival + time (payload sized to ~80% of each mode's capacity) ==")
    hdr = f"{'mode':<22}{'payload':>8}" + "".join(f"{k:>9}" for k in kinds) + f"{'dec ms':>9}"
    print(hdr)
    # QR v38 baseline
    for v, m in [(12, 4), (20, 3), (38, 2)]:
        cap = qr_cap_bytes(v) * 3 // 4
        pl = rand_record(int(cap * 0.8))
        txt = base64.b64encode(pl)
        q = qr_matrix(txt)
        img = qr_draw(q, m)
        if img is None:
            print(f"qr v{v}: does not fit"); continue
        res = []; ms = None
        for k in kinds:
            d = damage(img, k)
            t0 = time.perf_counter(); out = qr_decode(d); dt = (time.perf_counter() - t0) * 1000
            if k == "png": ms = dt
            res.append("ok" if out == txt else "FAIL")
        print(f"{'qr v%d %dpx' % (q.version, m):<22}{len(pl):>8}" + "".join(f"{r:>9}" for r in res) + f"{ms:>9.0f}")
    # grid modes
    for cell, bits in [(1, 5), (1, 3), (2, 3), (4, 3), (4, 2), (4, 1)]:
        gw, gh = grid_geometry(cell)
        cap = gw * gh * 3 * bits // 8 - 12
        pl = rand_record(int(cap * 0.8))
        img, _ = grid_encode(pl, cell, bits)
        img = to565(Image.fromarray(img))             # the Pocket really emits RGB565 pixels
        res = []; ms = None
        for k in kinds:
            d = damage(img, k)
            t0 = time.perf_counter(); out = grid_decode(d, cell, bits); dt = (time.perf_counter() - t0) * 1000
            if k == "png": ms = dt
            res.append("ok" if out == pl else "FAIL")
        print(f"{'grid %dpx %db/ch' % (cell, bits):<22}{len(pl):>8}" + "".join(f"{r:>9}" for r in res) + f"{ms:>9.1f}")


if __name__ == "__main__":
    main()
