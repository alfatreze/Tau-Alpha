"""An independent FLAC encoder for tests (written from RFC 9639; shares no code with fw/flac.c, tools/flac_verify.py or fw2/cores/flac2_core.h). Not a good compressor: it picks subframe types, predictor orders,
coefficients, wasted bits, partition orders, Rice parameters and escapes AT RANDOM within the legal range, so the decoder meets every construct. encode_stream() returns the frame bytes, the STREAMINFO body and the
exact PCM the decoder must produce."""
import hashlib, random
class W:
    def __init__(self): self.b = []
    def put(self, v, n):
        if n: self.b += [(v >> i) & 1 for i in range(n - 1, -1, -1)]
    def n(self): return len(self.b)
    def align(self): self.b += [0] * (-len(self.b) % 8)
    def bytes(self):
        assert len(self.b) % 8 == 0
        return bytes(int("".join(map(str, self.b[i:i + 8])), 2) for i in range(0, len(self.b), 8))
def crc8(d):
    c = 0
    for b in d:
        c ^= b
        for _ in range(8): c = ((c << 1) ^ 7) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c
def crc16(d):
    c = 0
    for b in d:
        c ^= b << 8
        for _ in range(8): c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c
def utf8num(v):
    if v < 0x80: return bytes([v])
    for n, (lim, lead) in enumerate([(1 << 11, 0xC0), (1 << 16, 0xE0), (1 << 21, 0xF0), (1 << 26, 0xF8), (1 << 31, 0xFC), (1 << 36, 0xFE)], start=2):
        if v < lim:
            out = []
            for _ in range(n - 1): out.append(0x80 | (v & 0x3F)); v >>= 6
            return bytes([lead | v]) + bytes(reversed(out))
    raise ValueError
BSC = {192: 1, 576: 2, 1152: 3, 2304: 4, 4608: 5, 256: 8, 512: 9, 1024: 10, 2048: 11, 4096: 12, 8192: 13, 16384: 14, 32768: 15}
RC = {88200: 1, 176400: 2, 192000: 3, 8000: 4, 16000: 5, 22050: 6, 24000: 7, 32000: 8, 44100: 9, 48000: 10, 96000: 11}
SC = {8: 1, 12: 2, 16: 4, 20: 5, 24: 6}
def zz(r): return (r << 1) if r >= 0 else ((-r) << 1) - 1
def sb(w, v, n): w.put(v & ((1 << n) - 1), n)
def enc_residual(w, res, bs, order, rng):
    pos = [p for p in range(0, 9) if bs % (1 << p) == 0 and bs >> p >= max(order, 1) and (bs >> p) - 0 >= order]
    po = rng.choice(pos if rng.random() < 0.7 else pos[:2]); parts = 1 << po; per = bs >> po
    chunks = []; i = 0
    for p in range(parts):
        c = per - (order if p == 0 else 0); chunks.append(res[i:i + c]); i += c
    kbest = []; need1 = False
    for ch in chunks:
        zs = [zz(r) for r in ch] or [0]; best = None
        for k in range(0, 31):
            if max(zs) >> k > 3000: continue
            cost = sum((z >> k) + 1 + k for z in zs)
            if best is None or cost < best[0]: best = (cost, k)
        kbest.append(best[1]); need1 |= best[1] > 14
    method = 1 if need1 or rng.random() < 0.3 else 0
    pbits, esc = (5, 31) if method else (4, 15)
    w.put(method, 2); w.put(po, 4)
    for ch, k in zip(chunks, kbest):
        if ch and rng.random() < 0.06 and all(-(1 << 30) <= r < (1 << 30) for r in ch):
            raw = max((abs(r).bit_length() + 1) for r in ch) if any(ch) else 0
            if raw <= 31:
                w.put(esc, pbits); w.put(raw, 5)
                for r in ch: sb(w, r, raw)
                continue
        k = min(k, esc - 1)
        w.put(k, pbits)
        for r in ch:
            z = zz(r); w.put(0, z >> k); w.put(1, 1); w.put(z & ((1 << k) - 1), k)
def enc_subframe(w, x, bits, rng):
    n = len(x); wasted = 0
    if any(x) and rng.random() < 0.85:
        wasted = min(((v & -v).bit_length() - 1) for v in x if v) if any(x) else 0
        wasted = min(wasted, bits - 1)
    xs = [v >> wasted for v in x]; b = bits - wasted
    def hdr(t):
        w.put(0, 1); w.put(t, 6)
        if wasted: w.put(1, 1); w.put(0, wasted - 1); w.put(1, 1)
        else: w.put(0, 1)
    if all(v == xs[0] for v in xs) and rng.random() < 0.8: hdr(0); sb(w, xs[0], b); return
    r = rng.random()
    if r < 0.12: hdr(1); [sb(w, v, b) for v in xs]; return
    if r < 0.45:
        order = rng.randrange(0, min(5, n)); FIX = {0: [], 1: [1], 2: [2, -1], 3: [3, -3, 1], 4: [4, -6, 4, -1]}[order]
        res = [xs[i] - sum(c * xs[i - 1 - j] for j, c in enumerate(FIX)) for i in range(order, n)]
        if any(abs(v) >= 1 << 30 for v in res): hdr(1); [sb(w, v, b) for v in xs]; return
        hdr(8 + order); [sb(w, v, b) for v in xs[:order]]; enc_residual(w, res, n, order, rng); return
    order = rng.randrange(1, min(32, n - 1) + 1); prec = rng.randrange(5, 16); shift = rng.randrange(0, 16); cm = (1 << (prec - 1)) - 1
    if rng.random() < 0.6:                                              # small, smooth-ish coefficients (like a real predictor)
        coef = [rng.randrange(-cm - 1, cm + 1) // max(1, order) for _ in range(order)]; coef[0] = cm // 2
    else: coef = [rng.randrange(-cm - 1, cm + 1) for _ in range(order)]
    res = [xs[i] - (sum(coef[j] * xs[i - 1 - j] for j in range(order)) >> shift) for i in range(order, n)]
    if any(abs(v) >= 1 << 30 for v in res): hdr(1); [sb(w, v, b) for v in xs]; return
    hdr(31 + order); [sb(w, v, b) for v in xs[:order]]; w.put(prec - 1, 4); sb(w, shift, 5); [sb(w, c, prec) for c in coef]; enc_residual(w, res, n, order, rng)
def gen_signal(rng, bits, n, amp=1.0):
    lo, hi = -(1 << (bits - 1)), (1 << (bits - 1)) - 1; k = rng.randrange(6); import math
    cl = lambda v: max(lo, min(hi, int(v)))
    if k == 0: x = [rng.randrange(lo, hi + 1) for _ in range(n)]
    elif k == 1: x = [cl(hi * amp * math.sin(i * rng.uniform(0.01, 0.9) + 1)) for i in range(n)]
    elif k == 2: x = [cl(rng.gauss(0, hi * amp / 8)) for _ in range(n)]
    elif k == 3: v = 0; x = []; [x.append(cl(v := v + rng.gauss(0, hi * amp / 64))) for _ in range(n)]
    elif k == 4: x = [lo if (i // 5) % 2 else hi for i in range(n)]
    else: x = [rng.choice([0, 0, 0, 1, -1]) for _ in range(n)]
    if rng.random() < 0.3:
        sh = rng.randrange(1, min(5, bits)); x = [v >> sh << sh for v in x]
    return x
def encode_stream(rng, bits=16, chans=2, rate=44100, blocks=None, variable=False, assigns=(1, 8, 9, 10)):
    """blocks: list of block sizes (each >= 16). Returns (frames bytes, streaminfo body, pcm list of (L, R), per-frame start offsets, block sizes)."""
    blocks = blocks or [rng.choice([16, 17, 192, 576, 1152, 4096, 4608]) for _ in range(rng.randrange(1, 5))]
    out = b""; pcm = []; starts = []; sample = 0
    for fi, bs in enumerate(blocks):
        starts.append(len(out))
        if chans == 2:
            ass = rng.choice(assigns); L = gen_signal(rng, bits, bs); R = gen_signal(rng, bits, bs)
            if rng.random() < 0.3: R = [max(-(1 << (bits - 1)), min((1 << (bits - 1)) - 1, v + rng.randrange(-3, 4))) for v in L]
            S = [l - r for l, r in zip(L, R)]; M = [(l + r) >> 1 for l, r in zip(L, R)]
            ch = {1: (L, R, bits, bits), 8: (L, S, bits, bits + 1), 9: (S, R, bits + 1, bits), 10: (M, S, bits, bits + 1)}[ass]
            pcm += list(zip(L, R))
        else:
            ass = 0; L = gen_signal(rng, bits, bs); ch = (L, None, bits, 0); pcm += [(v, v) for v in L]
        num = sample if variable else fi
        bsc = BSC.get(bs, 6 if bs <= 256 else 7); rcc = RC.get(rate, 0) if rng.random() < 0.8 else 0
        if rate not in RC and rcc == 0 and rng.random() < 0.5: rcc = 13
        scc = SC.get(bits, 0) if rng.random() < 0.8 else 0
        h = bytes([0xFF, 0xF8 | (1 if variable else 0), (bsc << 4) | (rcc if rcc != 13 or rate not in RC else 13), (ass << 4) | (scc << 1)]) + utf8num(num)
        if bsc == 6: h += bytes([bs - 1])
        if bsc == 7: h += (bs - 1).to_bytes(2, "big")
        if rcc == 13: h += rate.to_bytes(2, "big") if rate < 65536 else b""
        h += bytes([crc8(h)])
        w = W(); enc_subframe(w, ch[0], ch[2], rng)
        if chans == 2: enc_subframe(w, ch[1], ch[3], rng)
        w.align(); body = h + w.bytes(); out += body + crc16(body).to_bytes(2, "big"); sample += bs
    md5 = hashlib.md5(b"".join(((l & ((1 << bits) - 1)).to_bytes(4, "little")[:(bits + 7) // 8]) + (((r & ((1 << bits) - 1)).to_bytes(4, "little")[:(bits + 7) // 8]) if chans == 2 else b"") for l, r in [(p[0], p[1]) for p in pcm])).digest()
    v = (rate << 44) | ((chans - 1) << 41) | ((bits - 1) << 36) | len(pcm)
    si = min(blocks).to_bytes(2, "big") + max(blocks).to_bytes(2, "big") + b"\0\0\0\0\0\0" + v.to_bytes(8, "big") + md5
    return out, si, pcm, starts, blocks
