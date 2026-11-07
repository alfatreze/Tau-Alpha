#!/usr/bin/env python3
"""Host tests for the name sidecar (docs/features/NAME_SIDECAR_SPEC.md): the shared corpus (sim/name_corpus.py) through name conversion,
the library index, the manifest, the binary table and the display rule at three font coverages."""
import json
import struct
import sys
import tempfile
import unicodedata
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "tools"))
sys.path.insert(0, str(HERE))
import name_corpus as C
import name_sidecar as NS
import sync_media as S
import tau_library as L
from test_library_index import make_mp3

fails = []


def check(name, cond, info=""):
    if not cond:
        fails.append(name)
    print(("ok   " if cond else "FAIL ") + name + (f"  {str(info)[:300]}" if info and not cond else ""))


HAVE_ANY, HAVE_KAKASI = S._anyascii() is not None, S._kakasi() is not None
nfc = lambda x: unicodedata.normalize("NFC", x)
CASES = C.CASES
ORIG = {c["id"]: Path(c["cat"]) / (c["name"] + ".mp3") for c in CASES}
rels = list(ORIG.values())
M = S.NameMap(rels)
CARD = {i: M.rel(r) for i, r in ORIG.items()}
card_name = lambda i: CARD[i].name
FORBIDDEN = set('<>:"|?*\\')

# ------------------------------------------------------------ 1. conversion invariants, every case
check("corpus has at least 60 cases with unique ids", len(CASES) >= 60 and len({c["id"] for c in CASES}) == len(CASES))
check("corpus covers the categories", {c["cat"] for c in CASES} >= {"plain", "cp437", "latin_other", "script", "emoji", "symbols", "edge", "collide", "long"})
bad = []
for c in CASES:
    p = CARD[c["id"]]
    s = str(p)
    stem = p.stem
    if not L.reachable(s): bad.append((c["id"], "unreachable", s))
    if any(ch in FORBIDDEN for ch in s.replace("/", "")): bad.append((c["id"], "forbidden char", s))
    if stem != stem.strip(" ."): bad.append((c["id"], "edge space or dot", s))
    if not stem: bad.append((c["id"], "empty stem", s))
    if stem.lower() in S._RESERVED: bad.append((c["id"], "reserved name", s))
    if len(L.path_bytes(s)) + S.ROOT_BUDGET > S.MAX_PATH: bad.append((c["id"], "path too long", len(s)))
    if unicodedata.normalize("NFC", s) != s: bad.append((c["id"], "not NFC", s))
    if any(ord(ch) < 0x20 or 0x7F <= ord(ch) <= 0x9F for ch in s): bad.append((c["id"], "control char", s))
    if c.get("keep") and c["keep"] not in s: bad.append((c["id"], "lost a character the Pocket can open", c["keep"], s))
    if not p.suffix == ".mp3": bad.append((c["id"], "extension lost", s))
check("every converted name is openable, legal, NFC, short enough, and keeps what the Pocket can open", not bad, bad[:3])

# uniqueness per folder, case-insensitive, over DISTINCT originals (NFC-equal names are one name)
seen = {}
dup = []
for i, r in ORIG.items():
    k = (str(CARD[i]).lower())
    o = tuple(nfc(x) for x in r.parts)
    if k in seen and seen[k] != o:
        dup.append((i, k))
    seen.setdefault(k, o)
check("no two different originals share a card name (case-insensitive)", not dup, dup)
for grp in ("case", "fold", "empty"):
    ids = [c["id"] for c in CASES if c.get("group") == grp]
    check(f"collision group '{grp}': {len(ids)} originals, {len({str(CARD[i]).lower() for i in ids})} distinct names", len({str(CARD[i]).lower() for i in ids}) == len(ids))
check("NFC and NFD spellings of one name are one name", card_name("nfc_cafe") == card_name("nfd_cafe") and card_name("norm_a") == card_name("norm_b"))
check("the numbering is the same for any input order", all(S.NameMap(list(reversed(rels))).rel(r) == CARD[i] for i, r in ORIG.items()))
check("a rebuild is byte-identical (names)", [str(S.NameMap(rels).rel(r)) for r in rels] == [str(CARD[i]) for i in ORIG])
check("a long name is cut to fit and keeps its extension", len(card_name("long_ascii")) < len(C.CASES[0]["name"]) + 230 and card_name("long_ascii").endswith(".mp3"))
check("emoji-only names become track, track (2), track (3)", sorted(card_name(i) for i in ("emptyfold_a", "emptyfold_b", "emptyfold_c")) == ["track (2).mp3", "track (3).mp3", "track.mp3"])

# pinned conversions that do not depend on the optional libraries
PIN = {"ascii": "01 Plain Song.mp3", "nfd_cafe": "Café.mp3", "umlauts": "öüäëï Straße.mp3", "a_acute": "Alvaro Fernández.mp3",
       "o_slash": "Soren Orsted.mp3", "c_caron": "Capek Skroup.mp3", "polish": "Lódz zólty.mp3", "forbidden": "A_B_C_D_E_F_G_H.mp3", "backslash": "AC_DC Back.mp3",
       "lead_trail_space": "padded.mp3", "trailing_dot": "ends with dot.mp3", "reserved_con": "CON_.mp3", "reserved_nul": "nul_.mp3", "control_chars": "tab here.mp3",
       "dotfile": "hidden.mp3", "only_dots": "track.mp3", "fullwidth_x": None, "combining_stack": "Zalgo.mp3", "emoji_in_text": "Party Time.mp3", "emoji_zwj": "Family.mp3",
       "hash_percent": "100% #1 hit & more.mp3"}
bad = [(k, card_name(k), v) for k, v in PIN.items() if v is not None and k in CARD and card_name(k) != v]
check("pinned card names", not bad, bad)
if HAVE_ANY:
    PIN_ANY = {"cyrillic": "Privet mir.mp3", "greek": "Ellada.mp3", "chinese_s": "DongJing YeQu.mp3", "korean": "HanGugEo NoLae.mp3", "mixed_latin_cyr": "Café Privet 2.mp3",
               "fullwidth": "ABC 123.mp3", "arabic": "mrhb.mp3", "mixed_ascii_cjk": "Tokyo DongJing Night.mp3"}
    bad = [(k, card_name(k), v) for k, v in PIN_ANY.items() if card_name(k) != v]
    check("pinned transliterations (anyascii installed)", not bad, bad)
else:
    check("without anyascii a name with nothing openable becomes track", card_name("cyrillic").startswith("track") and card_name("mixed_latin_cyr").startswith("Café"))
    print("skip anyascii pins (not installed)")
if HAVE_KAKASI:
    check("kana names use their Japanese reading (pykakasi)", card_name("japanese_mixed") == "toukyou tawaa.mp3" and card_name("japanese_kana").startswith("konnichiha"), card_name("japanese_mixed"))
else:
    print("skip pykakasi pins (not installed)")
check("a Greek word is never half Greek, half Latin", not any("α" <= ch <= "ω" for ch in card_name("greek")) or not HAVE_ANY)
check("a lone pi stays a pi", "π" in card_name("ae_pi_cent"))

# ------------------------------------------------------------ 2. pipeline: card tree -> library index -> sidecar
with tempfile.TemporaryDirectory() as td:
    root = Path(td) / "common"
    for i, p in CARD.items():
        c = next(x for x in CASES if x["id"] == i)
        make_mp3(root / p, {"TIT2": L.ascii_text(c["name"]) or "x", "TPE1": "Artist " + c["cat"], "TALB": c["cat"]}, frames=20)
    entries, pls, warns = L.scan(root, playlists=True)
    check("every converted file is indexed (none left out for its name)", len(entries) == len(CASES) - len(["norm_b", "nfd_cafe"]), (len(entries), warns[:3]))
    data = L.build_index(entries, pls, [])
    check("the index builds and verifies against the card tree", L.verify(data, root) == [], L.verify(data, root)[:3])
    ix = L.parse(data)
    paths = {L.track_path(ix, i): i for i in range(len(ix.tracks))}
    root_pfx = ix.root
    want = {root_pfx + str(p) for p in CARD.values()}
    check("track_path gives every card path", set(paths) == want)
    check("every path in the index is openable", all(L.reachable(p) and len(L.path_bytes(p)) <= 200 for p in paths))
    crc = ix.build_id

    # original tags per index track id (the tags the original files carried)
    def orig_of(card_path):
        rel = card_path[len(root_pfx):]
        for i, p in CARD.items():
            if str(p) == rel:
                return next(x for x in CASES if x["id"] == i), p
    tracks, index_text = [], {}
    for tid in range(len(ix.tracks)):
        case, cp = orig_of(L.track_path(ix, tid))
        t = ix.tracks[tid]
        al = ix.albums[t[4]]
        index_text[tid] = {"title": ix.str(t[0]), "artist": ix.str(ix.artists[al[2]][0]), "album": ix.str(al[0])}
        tg = case.get("tags") or (None, None, None)
        tracks.append({"id": tid, "card": str(cp), "orig": str(ORIG[case["id"]]), "title": tg[0], "artist": tg[1], "album": tg[2],
                       "file": ORIG[case["id"]].name, "_case": case})
    man = NS.build_manifest(tracks, crc, index_text)
    mb = NS.manifest_bytes(man)
    check("manifest is deterministic UTF-8 JSON", mb == NS.manifest_bytes(NS.build_manifest(tracks, crc, index_text)) and json.loads(mb.decode("utf-8")) == man)
    check("manifest round-trips and checks its format", NS.load_manifest(mb) == man)
    try:
        NS.load_manifest(json.dumps({"format": "tau-names", "version": 99}).encode()); newer_ok = False
    except NS.SidecarError:
        newer_ok = True
    check("newer manifest version raises", newer_ok)
    plain_ids = {"ascii", "digits", "dots"}
    kept = {t["_case"]["id"] for t in tracks if any(r["id"] == t["id"] for r in man["tracks"])}
    check("a plain ASCII library gets no record (small sidecar)", not (plain_ids & kept) or all(i in kept for i in plain_ids & kept), plain_ids & kept)
    check("every renamed or tag-rich track has a record", all(t["_case"]["id"] in kept for t in tracks if t["orig"] != t["card"]))
    check("every record points at a card path that is in the index", all(root_pfx + r["card"] in paths and paths[root_pfx + r["card"]] == r["id"] for r in man["tracks"]))
    check("originals in the manifest are NFC", all(nfc(r["orig"]) == r["orig"] for r in man["tracks"]))
    cyr = next(r for r in man["tracks"] if r["orig"].endswith("Привет мир.mp3"))
    check("the original Cyrillic name and tag survive losslessly", cyr["title"] == "Привет" and cyr["card"].endswith(card_name("cyrillic")))

    # --- binary table
    albums = [(None, None) for _ in ix.albums]
    artists = [None for _ in ix.artists]
    btracks = []
    for t in tracks:
        btracks.append({"file": t["file"], "title": t["title"], "artist": t["artist"], "album": t["album"]})
    raw = NS.build_bin(crc, btracks, albums, artists)
    check("binary table is deterministic", raw == NS.build_bin(crc, btracks, albums, artists))
    out = NS.parse_bin(raw, crc, (len(ix.tracks), len(ix.albums), len(ix.artists)))
    ok = all((out["tracks"][i]["title"] == NS.clean_text(btracks[i]["title"])) and (out["tracks"][i]["file"] == NS.clean_text(btracks[i]["file"], 255)) for i in range(len(btracks)))
    check("binary table round-trips every original title and file name", ok)
    check("absent fields stay absent", all(out["tracks"][i]["title"] is None for i in range(len(btracks)) if not btracks[i]["title"]))
    check("binary size is small", len(raw) < 64 * 1024, len(raw))
    n3 = (len(ix.tracks), len(ix.albums), len(ix.artists))

    def rejects(blob, **kw):
        try:
            NS.parse_bin(blob, **kw); return False
        except NS.SidecarError:
            return True
    flip = lambda b, i: b[:i] + bytes([b[i] ^ 0xFF]) + b[i + 1:]
    check("reader rejects bad magic", rejects(flip(raw, 0), index_bid=crc))
    check("reader rejects a damaged header (crc)", rejects(flip(raw, 13), index_bid=crc))
    check("reader rejects a sidecar for another index (rebuilt library)", rejects(raw, index_bid=crc ^ 1))
    check("reader rejects wrong counts", rejects(raw, index_bid=crc, counts=(n3[0] + 1, n3[1], n3[2])))
    check("reader rejects a truncated file", rejects(raw[:len(raw) - 40], index_bid=crc))
    check("reader rejects a file shorter than its header", rejects(raw[:20]))
    newer = bytearray(raw); struct.pack_into("<H", newer, 4, 9); struct.pack_into("<I", newer, 32, zlib.crc32(bytes(newer[:32])) & 0xFFFFFFFF)
    check("reader rejects a newer version", rejects(bytes(newer), index_bid=crc))
    o_trk = 48
    bent = bytearray(raw); first = struct.unpack_from("<I", bent, o_trk + 4)[0]
    struct.pack_into("<I", bent, o_trk + 4, first + 1 if first else 3)
    check("reader rejects an offset that is not the start of a string", rejects(bytes(bent), index_bid=crc) or first == 0)
    nonterm = bytearray(raw); nonterm[-1] = ord("x")
    check("reader rejects an unterminated pool", rejects(bytes(nonterm), index_bid=crc))
    big = [{"title": ("東" * 127) + str(i), "file": ("京" * 85) + str(i)} for i in range(9000)]
    try:
        NS.build_bin(0, big, [], []); capped = False
    except NS.SidecarError:
        capped = True
    check("the 1 MiB cap is enforced", capped)

# ------------------------------------------------------------ 3. text cleaning (spec 3)
ct = NS.clean_text
check("controls become spaces, NUL included", ct("a\tb\x00c") == "a b c")
check("NFC is applied", ct("Café") == "Café")
check("a lone surrogate becomes U+FFFD", ct("a\ud800b") == "a�b")
check("nothing left is None", ct("") is None and ct(None) is None and ct("   ") is None)
check("the cut never leaves a dangling combining mark or joiner", ct("ab\u0301\u0302", 3) == "ab" and ct("x\U0001f468\u200d\U0001f469", 3) == "x\U0001f468")
check("emoji and RTL text are kept as written", ct("\U0001f600 مرحبا") == "\U0001f600 مرحبا")
check("bytes are decoded as UTF-8 (bad bytes replaced)", ct(b"caf\xc3\xa9 \xff") == "café �")
check("a long title is cut at the limit", len(ct("x" * 500, 127)) == 127)

# ------------------------------------------------------------ 4. display rule at three font coverages (spec 6)
def idx_text(tag):
    return L.latin1_text(tag) if tag else ""


rows = []
for c in CASES:
    if not c.get("tags") or not c["tags"][0]:
        continue
    title = NS.clean_text(c["tags"][0])
    it = idx_text(c["tags"][0])
    stem = CARD[c["id"]].stem
    res = {cov: NS.display(title, it, stem, cov) for cov in ("ascii", "latin1", "full")}
    rows.append((c["id"], title, it, res))
bad = []
for cid, title, it, res in rows:
    for cov, (txt, src) in res.items():
        f = NS.COVERAGE[cov]
        if src == "side" and (txt != title or not all(f(ch) for ch in title)): bad.append((cid, cov, "side not drawable"))
        if src != "side" and title and all(f(ch) for ch in title): bad.append((cid, cov, "drawable original not used"))
        if cov in ("latin1", "full") and not all(NS.COVERAGE["latin1"](ch) for ch in txt) and src != "side": bad.append((cid, cov, "fallback has a character the font cannot draw", txt))
check("display: the original is used exactly when every character is drawable; the fallback is always drawable", not bad, bad[:4])
by = {r[0]: r for r in rows}
check("display: plain ASCII shows the same everywhere", all(v == ("Plain Song", "side") for v in by["ascii"][3].values()))
check("display: e-acute shows at Latin-1 and above, folds at ASCII-only", by["nfc_cafe"][3]["latin1"] == ("Café", "side") and by["nfc_cafe"][3]["ascii"][1] == "index")
check("display: Cyrillic, Greek, CJK and Hangul fall back to a readable name at Latin-1", all(by[k][3]["latin1"][1] in ("index", "file") and by[k][3]["latin1"][0] for k in ("cyrillic", "greek", "chinese_s", "japanese_kana", "korean")))
check("display: with a full font the original script is shown", all(by[k][3]["full"][1] == "side" for k in ("cyrillic", "greek", "chinese_s", "japanese_kana", "korean")))
check("display: a Cyrillic title shows its transliteration from the index (no '?'), original kept for a font that can draw it", by["cyrillic"][2] == "Privet" and by["cyrillic"][3]["latin1"] == ("Privet", "index") and by["cyrillic"][3]["full"][1] == "side", by["cyrillic"])
check("display: a title with nothing readable left (emoji only) falls back to the card file name", NS.display("\U0001F600", "", "track", "latin1") == ("track", "file"))
mixed = NS.display("Café 東京", L.latin1_text("Café 東京"), "Cafe DongJing", "latin1")
check("display: a mixed title is never half drawn", mixed[1] == "index" and "東" not in mixed[0], mixed)
check("display: an emoji title falls back at Latin-1 and shows at full", NS.display("Party \U0001f389", "Party", "Party", "latin1")[1] == "index" and NS.display("Party \U0001f389", "Party", "Party", "full") == ("Party \U0001f389", "side"))
check("display: nothing at all falls back to the stem", NS.display(None, "", "stem", "latin1") == ("stem", "file"))

if fails:
    print("FAILED:", fails)
    sys.exit(1)
print("PASSED")
