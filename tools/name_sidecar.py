#!/usr/bin/env python3
"""Reference implementation of docs/features/NAME_SIDECAR_SPEC.md: the original-name sidecar (tau-names.json + tau-names.bin)
and the display rule the firmware will implement. Host-tested by sim/test_name_sidecar.py; the firmware does not read it yet.

  clean_text(s, limit)                  spec 3: NFC, no controls, no surrogates, cut at a character boundary
  build_manifest(tracks, index_crc)     spec 4: the JSON source of truth (records that carry nothing extra are left out)
  manifest_bytes(m) / load_manifest(b)  deterministic UTF-8 JSON
  build_bin(...) / parse_bin(...)       spec 5: the firmware table
  display(side, index, stem, cov)       spec 6: which text is drawn
"""
import json
import struct
import unicodedata
import zlib

LIMITS = {"title": 127, "artist": 127, "album": 127, "file": 255}
MAGIC = b"TNMS"
VERSION = 1
BIN_CAP = 1 << 20


class SidecarError(Exception):
    pass


def clean_text(s, limit=127):
    """spec 3. Controls become a space, a lone surrogate or NUL byte becomes U+FFFD / space, the result is NFC and cut at `limit` characters
    without leaving a dangling combining mark, joiner or variation selector at the end. Returns None when nothing is left."""
    if s is None:
        return None
    if isinstance(s, bytes):
        s = s.decode("utf-8", errors="replace")
    out = []
    for c in s:
        o = ord(c)
        if 0xD800 <= o <= 0xDFFF:
            out.append("�")
        elif o < 0x20 or 0x7F <= o <= 0x9F:
            out.append(" ")
        else:
            out.append(c)
    t = unicodedata.normalize("NFC", "".join(out))
    if len(t) > limit:
        t = t[:limit]
        while t and (unicodedata.combining(t[-1]) or t[-1] in "\u200d\ufe0e\ufe0f"):
            t = t[:-1]
    t = t.strip()
    return t or None


# ----------------------------------------------------------------- display rule (spec 6)
def cov_ascii(c):
    return 0x20 <= ord(c) <= 0x7E


def cov_latin1(c):
    o = ord(c)
    return 0x20 <= o <= 0x7E or 0xA1 <= o <= 0xFF and o != 0xAD


def cov_full(c):
    return not (ord(c) < 0x20 or 0x7F <= ord(c) <= 0xA0)


COVERAGE = {"ascii": cov_ascii, "latin1": cov_latin1, "full": cov_full}


def display(side, index, stem, cov="latin1"):
    """spec 6: the sidecar text when present and EVERY character is drawable, else the index text, else the file name stem.
    Returns (text, source) with source in side / index / file."""
    f = COVERAGE[cov] if isinstance(cov, str) else cov
    if side and all(f(c) for c in side):
        return side, "side"
    if index:
        return index, "index"
    return stem, "file"


# ----------------------------------------------------------------- manifest (spec 4)
def build_manifest(tracks, index_crc, index_text=None):
    """tracks: list of dicts {id, card, orig, title, artist, album} (tags are raw original text). index_text: {id: {title, artist, album}} as
    stored in the index (Latin-1 text); a record is kept only when the original differs from the card path or a tag says more than the index."""
    recs = []
    for t in tracks:
        rec = {"id": t["id"], "card": t["card"], "orig": unicodedata.normalize("NFC", t["orig"])}
        extra = rec["orig"] != rec["card"]
        for k in ("title", "artist", "album"):
            v = clean_text(t.get(k), LIMITS[k])
            if v is None:
                continue
            idx = (index_text or {}).get(t["id"], {}).get(k)
            if v != idx:
                extra = True
            rec[k] = v
        if extra:
            recs.append(rec)
    return {"format": "tau-names", "version": VERSION, "index_crc32": index_crc, "tracks": recs}


def manifest_bytes(m):
    return (json.dumps(m, ensure_ascii=False, sort_keys=True, indent=1) + "\n").encode("utf-8")


def load_manifest(b):
    m = json.loads(b.decode("utf-8"))
    if m.get("format") != "tau-names":
        raise SidecarError("not a tau-names manifest")
    if m.get("version", 0) > VERSION:
        raise SidecarError(f"manifest version {m['version']} is newer than this reader ({VERSION})")
    return m


# ----------------------------------------------------------------- binary (spec 5)
def _align(n):
    return (n + 15) & ~15


def build_bin(index_crc, tracks, albums, artists):
    """tracks: list (by track id) of dicts with optional file/title/artist/album text; albums: list of (title, artist); artists: list of names.
    None or '' = absent. Returns the file bytes; raises SidecarError over the 1 MiB cap."""
    pool = bytearray(b"\x00")
    idx = {}

    def add(s, key):
        s = clean_text(s, LIMITS[key])
        if not s:
            return 0
        if s not in idx:
            idx[s] = len(pool)
            pool.extend(s.encode("utf-8") + b"\x00")
        return idx[s]

    trk = b"".join(struct.pack("<4I", add(t.get("file"), "file"), add(t.get("title"), "title"), add(t.get("artist"), "artist"), add(t.get("album"), "album")) for t in tracks)
    alb = b"".join(struct.pack("<2I", add(a[0], "title"), add(a[1], "artist")) for a in albums)
    art = b"".join(struct.pack("<I", add(a, "artist")) for a in artists)
    o_trk = 48
    o_alb = _align(o_trk + len(trk))
    o_art = _align(o_alb + len(alb))
    o_pool = _align(o_art + len(art))
    hdr = MAGIC + struct.pack("<HHIIIIII", VERSION, 1, index_crc, len(tracks), len(albums), len(artists), o_pool, len(pool))
    hdr = hdr[:32] + struct.pack("<I", zlib.crc32(hdr[:32]) & 0xFFFFFFFF) + bytes(12)
    out = bytearray(hdr)
    for off, blob in ((o_trk, trk), (o_alb, alb), (o_art, art), (o_pool, bytes(pool))):
        out.extend(bytes(off - len(out)))
        out.extend(blob)
    if len(out) > BIN_CAP:
        raise SidecarError(f"sidecar is {len(out)} bytes, over the {BIN_CAP} cap")
    return bytes(out)


def parse_bin(data, index_crc=None, counts=None):
    """returns {'tracks': [{file,title,artist,album}], 'albums': [(title, artist)], 'artists': [name]} with None for absent fields.
    Raises SidecarError for anything the reader must treat as 'no sidecar' (spec 5 reader checks)."""
    if len(data) < 48 or data[:4] != MAGIC:
        raise SidecarError("bad magic")
    ver, flags, crc, nt, na, nr, o_pool, n_pool, hcrc = struct.unpack_from("<HHIIIIIII", data, 4)
    if ver > VERSION:
        raise SidecarError(f"version {ver}")
    if zlib.crc32(data[:32]) & 0xFFFFFFFF != hcrc:
        raise SidecarError("header crc")
    if index_crc is not None and crc != index_crc:
        raise SidecarError("index crc differs: the library was rebuilt")
    if counts is not None and tuple(counts) != (nt, na, nr):
        raise SidecarError(f"counts {(nt, na, nr)} != index {tuple(counts)}")
    o_trk = 48
    o_alb = _align(o_trk + nt * 16)
    o_art = _align(o_alb + na * 8)
    if o_pool != _align(o_art + nr * 4) or o_pool + n_pool > len(data) or n_pool < 1 or data[o_pool] != 0 or data[o_pool + n_pool - 1] != 0:
        raise SidecarError("layout")
    pool = data[o_pool:o_pool + n_pool]

    def s(off):
        if off == 0:
            return None
        if off >= n_pool or pool[off - 1] != 0:
            raise SidecarError(f"offset {off} is not the start of a string")
        return pool[off:pool.index(b"\x00", off)].decode("utf-8")

    tracks = [dict(zip(("file", "title", "artist", "album"), (s(x) for x in struct.unpack_from("<4I", data, o_trk + 16 * i)))) for i in range(nt)]
    albums = [tuple(s(x) for x in struct.unpack_from("<2I", data, o_alb + 8 * i)) for i in range(na)]
    artists = [s(struct.unpack_from("<I", data, o_art + 4 * i)[0]) for i in range(nr)]
    return {"tracks": tracks, "albums": albums, "artists": artists}
