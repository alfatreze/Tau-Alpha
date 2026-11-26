#!/usr/bin/env python3
"""MP3 decoder v2, WP-09: the Layer III table extractor. Generates fw2/decode/mp3v2/tables.h (our layout) from table DATA in the vendored
third_party/minimp3/minimp3.h (lieff/minimp3 commit 48e3a085f0e1a99446b34614e1e7f17f09dc2e82, CC0 1.0 Universal), plus tables generated here from the
formulas of the standard. Own code (Tau Alpha); no minimp3 code is read as code and none is copied, only the numbers of the constant arrays named below.

    python3 tools/extract_mp3v2_tables.py [--minimp3 PATH] [--out PATH] [--check] [--model]

  default      write fw2/decode/mp3v2/tables.h
  --check      regenerate in memory and compare with the committed file (exit 1 on a difference); no write
  --model      print the extracted model (Huffman code lists, statistics) as JSON on stdout (used by sim/test_mp3v2_tables.py)

What is read from minimp3.h (the constant arrays only, found by name; their line ranges are written into the provenance header):
  Huffman:   tabs[] (2,164 int16), tabindex[32], g_linbits[32], tab32[28], tab33[16]       the big_values code tables 0..31 and the two count1 tables
  Bands:     g_scf_long[8][23], g_scf_short[8][40], g_scf_mixed[8][40]                      scale-factor band widths (minimp3 row = its own sample-rate index)
  Scale factors / stereo: g_scfc_decode[16], g_mod[24], g_scf_partitions[3][28], g_preamp[10], g_pan[14]
  Requantisation: g_pow43[145], g_expfrac[4]  (the extractor checks them against the formulas and emits the formula values, not minimp3's rounded floats)
  Back end:  g_aa[2][8] (checked against the formula, formula emitted)
  Headers:   halfrate[2][3][15], g_hz[3]
Everything minimp3 stores in a transformed form is expanded here and the transformation is recorded in the header of the generated file:
  * Huffman codes are stored by minimp3 as a two-level lookup trie (first level 5 bits, sub-tables addressed by negative "pointer" entries, entries replicated
    for short codes, a leaf word = (code length << 8) | (second value << 4) | first value). This script walks that trie back into explicit code lists
    (code, length, x, y) per table, checks them (prefix free, Kraft sum, replication consistent), and builds OUR OWN lookup layout from the lists (section "layout").
  * the count1 tables (quadruples v, w, x, y) are stored as a 4-bit first level plus a pointer level (table A) and a flat 4-bit table (table B); expanded the same way.
  * the scale-factor band tables are stored as rows of widths with a 0 terminator, short-block bands three times each (one per window); our layout stores one width
    per band, and mixed-block rows as (n long widths, short widths from short band 3).
  * `g_scfc_decode[16]` packs (slen1 << 2 | slen2) of the MPEG-1 scalefac_compress table; we store slen1[16] and slen2[16].
  * `g_preamp[10]` is the pretab for bands 11..20 only; we store the standard's 22-entry pretab.
  * g_pow43 holds sign * x^(4/3) for x = -16..128 in 6-decimal floats; we emit round(x^(4/3) * 2^22) from the formula for x = 0..128 (a provisional Q22 pending WP-12).
Origin of every table is marked in the generated file: [M] minimp3 data (repacked), [M+F] minimp3 data checked against and replaced by the standard's formula, [F] formula.
"""
import argparse, hashlib, json, math, re, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MINIMP3 = ROOT / "third_party/minimp3/minimp3.h"
OUT = ROOT / "fw2/decode/mp3v2/tables.h"
COMMIT = "48e3a085f0e1a99446b34614e1e7f17f09dc2e82"
URL = "https://github.com/lieff/minimp3"
DATE = "2026-11-26"          # fixed: a re-run must reproduce the file byte for byte
POW43_Q = 22                  # fractional bits of the generated pow43 table
L1_CANDIDATES = range(3, 9)   # first-level width of the Huffman lookup: the extractor reports the total size of each
DEFAULT_L1 = 5                # 5 bits keeps most codes to one or two lookups (3 bits is smaller, 1,843 entries, but needs more levels per code); --l1 N changes it


# ------------------------------------------------------------------ reading minimp3.h
def _find_array(text, name):
    m = re.search(r"\b" + re.escape(name) + r"\s*(\[[^\]]*\]\s*)+=\s*\{", text)
    if not m: raise SystemExit("array %s not found in minimp3.h" % name)
    i = m.end() - 1; depth = 0
    for j in range(i, len(text)):
        if text[j] == "{": depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return m.start(), j + 1, text[i:j + 1]
    raise SystemExit("unterminated array " + name)


NUM = re.compile(r"-?\d+\.\d+(?:e-?\d+)?f?|-?\d+e-?\d+f?|-?\d+")


def read_array(text, name, floats=False):
    s, e, body = _find_array(text, name)
    vals = [(float(x.rstrip("f")) if floats else int(x)) for x in NUM.findall(body)]
    l0 = text.count("\n", 0, s) + 1; l1 = text.count("\n", 0, e) + 1
    return vals, (l0, l1), body


def read_minimp3(path):
    raw = Path(path).read_bytes()
    text = raw.decode()
    t, lines = {}, {}
    for n, fl in [("tabs", 0), ("tabindex", 0), ("g_linbits", 0), ("tab32", 0), ("tab33", 0), ("g_scf_long", 0), ("g_scf_short", 0), ("g_scf_mixed", 0),
                  ("g_scfc_decode", 0), ("g_mod", 0), ("g_scf_partitions", 0), ("g_preamp", 0), ("g_pan", 1), ("g_pow43", 1), ("g_expfrac", 1), ("g_aa", 1),
                  ("halfrate", 0), ("g_hz", 0)]:
        t[n], lines[n], body = read_array(text, n, bool(fl))
        if n in ("g_scf_long", "g_scf_short", "g_scf_mixed"):      # rows are shorter than the declared width (zero padded by C): keep the row structure
            rws = [[int(x) for x in NUM.findall(r)] for r in re.findall(r"\{([^{}]*)\}", body[1:])]
            w = {"g_scf_long": 23, "g_scf_short": 40, "g_scf_mixed": 40}[n]
            t[n] = [v for r in rws for v in (r + [0] * (w - len(r)))]
    return raw, t, lines


# ------------------------------------------------------------------ Huffman: minimp3's trie -> explicit code lists
def expand_big_value_table(tabs, base):
    """Walk the lookup trie of one table (minimp3 layout) and return the list of (code, length, x, y). Entries are replicated for codes shorter than the level
    width: every replica must be identical (checked)."""
    out = {}
    def walk(off, width, prefix, plen, depth=0):
        assert depth < 6
        slots = {}
        for i in range(1 << width):
            e = tabs[base + off + i]
            if e < 0:                                   # pointer: sub-table of (e & 7) bits at offset -(e >> 3)
                walk(-(e >> 3), e & 7, (prefix << width) | i, plen + width, depth + 1)
            else:
                ln = e >> 8
                assert ln <= width, "leaf longer than its level"
                key = i >> (width - ln)
                assert slots.setdefault((ln, key), e) == e, "inconsistent replicas"
                code = (prefix << ln) | key
                tot = plen + ln
                assert out.setdefault((code, tot), (e & 15, (e >> 4) & 15)) == (e & 15, (e >> 4) & 15)
    walk(0, 5, 0, 0)
    return sorted((c, l, x, y) for (c, l), (x, y) in out.items())


def expand_count1(tab, idx_bits_first=4):
    """tab32 / tab33 -> list of (code, length, v, w, x, y); leaf word: bit 3 set = leaf, bits 0..2 = total code length, bits 7..4 = v w x y."""
    out = {}
    for i in range(16):
        e = tab[i]
        if e & 8:
            ln = e & 7
            assert ln <= 4
            code = i >> (4 - ln)
            out[(code, ln)] = ((e >> 7) & 1, (e >> 6) & 1, (e >> 5) & 1, (e >> 4) & 1)
        else:                                            # pointer: base (e >> 3), (e & 3) extra bits after the 4
            nb, base = e & 3, e >> 3
            for j in range(1 << nb):
                f = tab[base + j]
                assert f & 8
                ln = f & 7
                assert 4 < ln <= 4 + nb
                code = (i << (ln - 4)) | (j >> (4 + nb - ln))
                q = ((f >> 7) & 1, (f >> 6) & 1, (f >> 5) & 1, (f >> 4) & 1)
                assert out.setdefault((code, ln), q) == q
    return sorted((c, l) + q for (c, l), q in out.items())


def kraft(codes):
    """exact Kraft sum as a fraction (numerator, denominator 2^maxlen)"""
    if not codes: return (0, 1)
    ml = max(c[1] for c in codes)
    return (sum(1 << (ml - c[1]) for c in codes), 1 << ml)


def prefix_free(codes):
    ss = sorted(format(c[0], "0%db" % c[1]) if c[1] else "" for c in codes)
    return all(not ss[i + 1].startswith(ss[i]) for i in range(len(ss) - 1))


# ------------------------------------------------------------------ our own Huffman lookup layout
# int16 entries. leaf (bit 15 clear): (n << 8) | (x << 4) | y with x the first and y the second decoded value and n the bits consumed AT THIS LEVEL (<= its width).
# pointer (bit 15 set): 0x8000 | ((w - 1) << 12) | offset with w the width of the next level (1..8) and offset relative to the table base. A level of width w
# is indexed by the next w bits; the first level of every table has the width w1[t] (0 for the empty tables: one leaf, no bits).
def build_trie(codes):
    root = {}
    for code, ln, x, y in codes:
        n = root
        for b in format(code, "0%db" % ln):
            n = n.setdefault(int(b), {})
        n["leaf"] = (x, y)
    return root


def depth_of(n):
    return 0 if "leaf" in n else 1 + max(depth_of(n[b]) for b in (0, 1) if b in n)


def level_cost(n, w, memo):
    """cost in entries of a level of width w rooted at trie node n (sub-levels chosen optimally, widths 1..8)"""
    key = (id(n), w)
    if key in memo: return memo[key]
    total = 1 << w
    for i in range(1 << w):
        c = _descend(n, w, i)
        if c is not None and "leaf" not in c and depth_of(c) > 0:
            total += best_cost(c, memo)[0]
    memo[key] = total
    return total


def _descend(n, w, i):
    """node reached after consuming the w bits of i (MSB first), or ('leaf' node earlier -> returned as that leaf node), or None if the path is absent"""
    for k in range(w - 1, -1, -1):
        if "leaf" in n: return n
        b = (i >> k) & 1
        if b not in n: return None
        n = n[b]
    return n


def best_cost(n, memo):
    d = depth_of(n)
    return min((level_cost(n, w, memo), w) for w in range(1, min(d, 8) + 1))


def emit_level(n, w, tab, base, memo):
    """append a level of width w for node n at the end of `tab`; return its offset relative to base"""
    off = len(tab) - base
    tab.extend([0] * (1 << w))
    for i in range(1 << w):
        c = _descend(n, w, i)
        if c is None:
            tab[base + off + i] = 0            # unused slot (code space not covered): decodes as value 0 pair of length 0 -- never reached by a valid stream
            continue
        if "leaf" in c:
            # consumed bits at this level = depth of that leaf below n
            used = _leaf_depth(n, w, i)
            x, y = c["leaf"]
            tab[base + off + i] = (used << 8) | (x << 4) | y
        else:
            d = depth_of(c); w2 = best_cost(c, memo)[1]
            sub = emit_level(c, w2, tab, base, memo)
            assert sub < 4096
            tab[base + off + i] = 0x8000 | ((w2 - 1) << 12) | sub
    return off


def _leaf_depth(n, w, i):
    used = 0
    for k in range(w - 1, -1, -1):
        if "leaf" in n: return used
        n = n[(i >> k) & 1]; used += 1
    return used


def to_i16(v): return v - 0x10000 if v >= 0x8000 else v


def build_layout(codebooks, l1):
    """codebooks: dict distinct key -> codes. Returns (tab int list, base {key: off}, w1 {key: width})"""
    tab, base, w1 = [], {}, {}
    memo = {}
    for key, codes in codebooks.items():
        b = len(tab)
        base[key] = b
        if all(c[1] == 0 for c in codes):          # the empty table: one leaf, no bits
            tab.append(0); w1[key] = 0; continue
        root = build_trie(codes)
        memo.setdefault("_keep", []).append(root)       # the memo is keyed by id(): keep every trie alive for the whole build
        d = depth_of(root)
        w = min(l1, d)
        w1[key] = w
        emit_level(root, w, tab, b, memo)
    return tab, base, w1


def decode_with_layout(tab, base, w1, bits):
    """reference decoder of OUR layout (used by the extractor's self-check and by the tests): bits = iterable of 0/1; returns (x, y, bits_consumed)"""
    it = list(bits)
    pos = 0
    off = 0; w = w1
    while True:
        idx = 0
        for k in range(w):
            idx = (idx << 1) | (it[pos + k] if pos + k < len(it) else 0)
        e = tab[base + off + idx] & 0xFFFF
        if e & 0x8000:
            pos += w; w = ((e >> 12) & 7) + 1; off = e & 0xFFF
        else:
            return (e >> 4) & 15, e & 15, pos + (e >> 8)


# ------------------------------------------------------------------ the model
def build_model(raw, t, lines):
    M = {}
    M["sha_in"] = hashlib.sha256(raw).hexdigest()
    M["lines"] = lines
    tabs, ti, lb = t["tabs"], t["tabindex"], t["g_linbits"]
    assert len(tabs) == 2164 and len(ti) == 32 and len(lb) == 32
    # distinct codebooks: tables sharing a tabindex share a codebook (0, 4 and 14 point at the empty table; 16..23 and 24..31 share)
    keys = sorted(set(ti))
    books = {}
    for k in keys:
        books[k] = expand_big_value_table(tabs, k) if k != 0 else [(0, 0, 0, 0)]
    M["books"] = books
    M["tabindex"] = ti
    M["linbits"] = lb
    M["unused"] = [4, 14]
    # count1
    M["count1"] = {"A": expand_count1(t["tab32"]), "B": expand_count1(t["tab33"])}
    # bands. minimp3 row for our 9 rates (MPEG-1 44.1, 48, 32; MPEG-2 22.05, 24, 16; MPEG-2.5 11.025, 12, 8): sr_idx = my_rate - (my_rate != 0) with
    # my_rate = sample_rate_index + 3 * ((hdr[1] >> 3 & 1) + (hdr[1] >> 4 & 1)); MPEG-1 -> 5..7, MPEG-2 -> 2..4, MPEG-2.5 -> 0, 0, 1
    M["rate_rows"] = [5, 6, 7, 2, 3, 4, 0, 0, 1]
    M["rates"] = [44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000]
    def rows(name, w):
        v = t[name]; assert len(v) == 8 * w
        return [v[i * w:(i + 1) * w] for i in range(8)]
    L, S, X = rows("g_scf_long", 23), rows("g_scf_short", 40), rows("g_scf_mixed", 40)
    long_w, short_w, mixed = [], [], []
    for r in M["rate_rows"]:
        lw = L[r]; assert lw[22] == 0; long_w.append(lw[:22])
        sw = S[r]; assert sw[39] == 0 and all(sw[3 * i] == sw[3 * i + 1] == sw[3 * i + 2] for i in range(13))
        short_w.append([sw[3 * i] for i in range(13)])
        mx = X[r]
        nlong = 8 if r >= 5 else 6                        # minimp3: n_long_sfb = 8 (MPEG-1) or 6 (LSF) entries are long bands, the rest short bands (each width x3)
        n = max(i for i, v in enumerate(mx) if v) + 1
        assert mx[n] == 0 if n < 40 else True
        mixed.append((nlong, mx[:n]))
    M["sfb_long"], M["sfb_short"], M["sfb_mixed"] = long_w, short_w, mixed
    # scale factors
    dec = t["g_scfc_decode"]; assert len(dec) == 16
    M["slen1"] = [d >> 2 for d in dec]; M["slen2"] = [d & 3 for d in dec]
    M["lsf_mod"] = [t["g_mod"][i * 4:(i + 1) * 4] for i in range(6)]
    M["scf_partitions"] = [t["g_scf_partitions"][i * 28:(i + 1) * 28] for i in range(3)]
    pre = t["g_preamp"]; assert len(pre) == 10
    M["pretab"] = [0] * 11 + pre + [0]
    # headers
    hr = t["halfrate"]; assert len(hr) == 2 * 3 * 15
    M["bitrate_l3"] = [[2 * hr[(v * 3 + 0) * 15 + i] for i in range(15)] for v in (1, 0)]     # [MPEG-1, MPEG-2/2.5] layer III, kbps (minimp3 indexes layer by the header field 4 - layer, so III is 0)
    M["hz"] = t["g_hz"]
    # formula-checked tables
    pw = t["g_pow43"]; assert len(pw) == 145
    err = max(abs(pw[16 + x] - x ** (4 / 3)) / max(1.0, x ** (4 / 3)) for x in range(129))
    assert err < 2e-7, err
    assert all(pw[x] == -pw[16 + x] for x in range(16)) and pw[16] == 0      # minimp3 indexes the negative half by the magnitude, offset 0
    M["pow43_err"] = err
    ef = t["g_expfrac"]
    e2 = max(abs(ef[k] / (2.0 ** -30 * 2 ** (-k / 4)) - 1) for k in range(4)); assert e2 < 1e-7, e2
    pan = t["g_pan"]
    pe = 0
    for i in range(7):
        r = math.tan(i * math.pi / 12) if i < 6 else None
        l, rr = (r / (1 + r), 1 / (1 + r)) if r is not None else (1.0, 0.0)
        pe = max(pe, abs(pan[2 * i] - l), abs(pan[2 * i + 1] - rr))
    assert pe < 1e-7, pe
    M["pan_err"] = pe
    aa = t["g_aa"]
    ci = [-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037]
    ae = max(max(abs(aa[i] - 1 / math.sqrt(1 + ci[i] ** 2)), abs(aa[8 + i] + ci[i] / math.sqrt(1 + ci[i] ** 2))) for i in range(8))
    assert ae < 1e-7, ae
    M["aa_err"] = ae
    M["ci"] = ci
    return M


# ------------------------------------------------------------------ rendering
def rnd(v): return int(math.floor(v + 0.5)) if v >= 0 else -int(math.floor(-v + 0.5))


def fmt_list(vals, per=16, hexw=None):
    rows = []
    for i in range(0, len(vals), per):
        rows.append("    " + ", ".join(str(v) if hexw is None else ("0x%0*X" % (hexw, v & ((1 << (4 * hexw)) - 1))) for v in vals[i:i + per]) + ",")
    return "\n".join(rows)


def render(M, l1=DEFAULT_L1):
    books = M["books"]
    tab, base, w1 = build_layout(books, l1)
    sizes = {l: len(build_layout(books, l)[0]) for l in L1_CANDIDATES}
    # self-check of the layout against the code lists
    for k, codes in books.items():
        for code, ln, x, y in codes:
            bits = [int(b) for b in format(code, "0%db" % ln)] if ln else []
            assert decode_with_layout(tab, base[k], w1[k], bits + [1, 0, 1]) == (x, y, ln), (k, code, ln)
    hbase = [base[M["tabindex"][t]] for t in range(32)]
    hw1 = [w1[M["tabindex"][t]] for t in range(32)]
    # count1 flat lookups: table A 6-bit index, table B 4-bit index; entry (len << 4) | v w x y (v the MSB of the low nibble)
    def flat(codes, width):
        arr = [0] * (1 << width)
        for code, ln, v, w, x, y in codes:
            for r in range(1 << (width - ln)):
                arr[(code << (width - ln)) | r] = (ln << 4) | (v << 3) | (w << 2) | (x << 1) | y
        return arr
    c1a, c1b = flat(M["count1"]["A"], 6), flat(M["count1"]["B"], 4)
    # formula tables
    pow43 = [rnd(x ** (4 / 3) * (1 << POW43_Q)) for x in range(129)]
    frac4 = [rnd((2.0 ** (-k / 4)) * (1 << 30)) for k in range(4)]
    pan = []
    for i in range(7):
        if i < 6:
            r = math.tan(i * math.pi / 12); l, rr = r / (1 + r), 1 / (1 + r)
        else:
            l, rr = 1.0, 0.0
        pan += [min(rnd(l * (1 << 30)), 1 << 30), rnd(rr * (1 << 30))]
    cs = [rnd(min(1 / math.sqrt(1 + c * c), 1 - 2.0 ** -31) * (1 << 31)) for c in M["ci"]]
    ca = [rnd(c / math.sqrt(1 + c * c) * (1 << 31)) for c in M["ci"]]
    wl = [rnd(math.sin(math.pi / 36 * (i + 0.5)) * (1 << 31)) for i in range(18)]
    ws = [rnd(math.sin(math.pi / 12 * (i + 0.5)) * (1 << 31)) for i in range(6)]

    # sections
    S = []
    def sec(title, origin, ctype, name, dims, vals, per, note="", hexw=None):
        n = len(vals)
        S.append("/* %s  [%s]%s */" % (title, origin, ("  " + note) if note else ""))
        bare = name.split("[")[0]
        S.append("static const %s %s%s = {" % (ctype, name, dims))
        two = re.findall(r"\[(\d+)\]", name)
        if len(two) == 2:                               # 2-D: one braced row per first index (no -Wmissing-braces)
            w = int(two[1])
            S.extend("    {" + ", ".join(str(v) for v in vals[i:i + w]) + "}," for i in range(0, len(vals), w))
        else:
            S.append(fmt_list(vals, per, hexw))
        S.append("};")
        S.append("_Static_assert(sizeof(%s) == %d * sizeof(%s), \"%s size\");" % (bare, n, ctype, bare))
        S.append("")
    S.append("#define MP3V2_HUFF_L1_BITS %d   /* first-level width of the Huffman lookup (chosen by --l1; total entries for widths %s: %s) */"
             % (l1, "/".join(map(str, L1_CANDIDATES)), "/".join(str(sizes[l]) for l in L1_CANDIDATES)))
    S.append("#define MP3V2_HUFF_TAB_ENTRIES %d" % len(tab))
    S.append("#define MP3V2_POW43_Q %d" % POW43_Q)
    S.append("")
    tab_u = [v if v >= 0 else v for v in tab]
    sec("Huffman lookup entries (leaf: (n << 8) | (x << 4) | y with n = bits consumed at this level; pointer: 0x8000 | ((w - 1) << 12) | offset from the table base)",
        "M, repacked from tabs[] by tools/extract_mp3v2_tables.py", "uint16_t", "mp3v2_huff_tab[MP3V2_HUFF_TAB_ENTRIES]", "", tab_u, 12, hexw=4)
    sec("start of the lookup of each big_values table selector 0..31 in mp3v2_huff_tab (4 and 14 are not used by the standard and point at the empty table)", "M",
        "uint16_t", "mp3v2_huff_base[32]", "", hbase, 16)
    sec("first-level width of each selector (0 = the empty table: one leaf of 0 bits)", "M", "uint8_t", "mp3v2_huff_w1[32]", "", hw1, 16)
    sec("linbits per selector (an escape value 15 is followed by this many bits, added to 15)", "M", "uint8_t", "mp3v2_huff_linbits[32]", "", M["linbits"], 16)
    sec("count1 table A (quadruples), indexed by the next 6 bits: (n << 4) | (v << 3) | (w << 2) | (x << 1) | y, n = code length", "M, expanded from tab32[]",
        "uint8_t", "mp3v2_count1_a[64]", "", c1a, 16, hexw=2)
    sec("count1 table B, indexed by the next 4 bits (the code is the four value bits inverted), same entry format", "M, expanded from tab33[]", "uint8_t",
        "mp3v2_count1_b[16]", "", c1b, 16, hexw=2)
    flat_long = [w for r in M["sfb_long"] for w in r]
    flat_short = [w for r in M["sfb_short"] for w in r]
    sec("rates in the order of the band tables: MPEG-1 44100 48000 32000, MPEG-2 22050 24000 16000, MPEG-2.5 11025 12000 8000", "M, g_hz[] and the version rule", "uint16_t",
        "mp3v2_sample_rate_hz[9]", "", M["rates"], 9)
    sec("long-block scale-factor band widths [rate][22] (each row sums to 576)", "M, expanded from g_scf_long[]", "uint8_t", "mp3v2_sfb_long[9][22]", "", flat_long, 22)
    sec("short-block band widths per window [rate][13] (each row sums to 192)", "M, one entry per band from g_scf_short[] (stored three times there)", "uint8_t",
        "mp3v2_sfb_short[9][13]", "", flat_short, 13)
    sec("mixed blocks: number of leading entries of mp3v2_sfb_mixed that are long bands (8 for MPEG-1, 6 for LSF)", "M, from n_long_sfb in the side-info reader", "uint8_t",
        "mp3v2_sfb_mixed_nlong[9]", "", [m[0] for m in M["sfb_mixed"]], 9)
    sec("mixed-block band widths [rate][40], zero terminated as minimp3 stores them: the first nlong entries are long bands, the rest short bands with each width "
        "repeated once per window (each row sums to 576 counting every entry; the 8 kHz row is not of that shape, see the WP-09 report)", "M, g_scf_mixed[] verbatim",
        "uint8_t", "mp3v2_sfb_mixed[9][40]", "", [w for m in M["sfb_mixed"] for w in (m[1] + [0] * (40 - len(m[1])))], 20)
    sec("pretab[22] (the standard's Table: added to the long scale factors when preflag is set)", "M, expanded from g_preamp[10] (bands 11..20)", "uint8_t",
        "mp3v2_pretab[22]", "", M["pretab"], 22)
    sec("MPEG-1 scalefac_compress -> slen1", "M, unpacked from g_scfc_decode[]", "uint8_t", "mp3v2_slen1[16]", "", M["slen1"], 16)
    sec("MPEG-1 scalefac_compress -> slen2", "M, unpacked from g_scfc_decode[]", "uint8_t", "mp3v2_slen2[16]", "", M["slen2"], 16)
    sec("LSF scalefac_compress decomposition moduli [6][4] (rows 0..2 normal, 3..5 for the intensity channel)", "M, g_mod[]", "uint8_t", "mp3v2_lsf_mod[6][4]", "",
        [v for r in M["lsf_mod"] for v in r], 4)
    sec("LSF scale-factor partition sizes [3][28]: rows long / mixed / short, seven groups of four per row (minimp3's indexing: row = (short bands present) + "
        "(no long bands), column offset 4 per modulus step)", "M, g_scf_partitions[]", "uint8_t", "mp3v2_lsf_partitions[3][28]", "",
        [v for r in M["scf_partitions"] for v in r], 14)
    sec("layer III bitrates in kbps [0 = MPEG-1, 1 = MPEG-2 and 2.5][15] (index 0 is free format)", "M, 2 x halfrate[][0][] (minimp3 indexes layers by the header bits: III is 0)", "uint16_t", "mp3v2_bitrate_kbps[2][15]", "",
        [v for r in M["bitrate_l3"] for v in r], 15)
    sec("requantisation: round(x^(4/3) * 2^MP3V2_POW43_Q), x = 0..128 (larger |x| use an interpolation by the decoder)", "M+F, minimp3's g_pow43 agrees with the formula to "
        "%.2e relative" % M["pow43_err"], "uint32_t", "mp3v2_pow43[129]", "", pow43, 8)
    sec("requantisation: round(2^(-k/4) * 2^30), k = 0..3 (the quarter-step gain fractions)", "M+F, minimp3's g_expfrac agrees to 1e-7", "uint32_t", "mp3v2_expfrac_q30[4]", "",
        frac4, 4)
    sec("intensity stereo (MPEG-1) left and right ratios, Q30, is_pos 0..6: (tan(p pi/12)/(1+tan), 1/(1+tan)), is_pos 6 = (1, 0)", "M+F, minimp3's g_pan agrees to %.1e" % M["pan_err"],
        "uint32_t", "mp3v2_pan_q30[14]", "", pan, 7)
    sec("alias reduction cs[i] = 1/sqrt(1 + c_i^2), Q31 (c = -0.6 -0.535 -0.33 -0.185 -0.095 -0.041 -0.0142 -0.0037, the standard's table)", "F, c_i from the standard; minimp3's "
        "g_aa agrees to %.1e" % M["aa_err"], "int32_t", "mp3v2_alias_cs_q31[8]", "", cs, 4)
    sec("alias reduction ca[i] = c_i / sqrt(1 + c_i^2), Q31", "F, as above", "int32_t", "mp3v2_alias_ca_q31[8]", "", ca, 4)
    sec("IMDCT window, long blocks: sin(pi/36 (i + 1/2)), i = 0..17, Q31 (the second half of the 36 points is the mirror; start and stop blocks are built from this "
        "and the short window by the standard's rule)", "F", "int32_t", "mp3v2_win_long_q31[18]", "", wl, 6)
    sec("IMDCT window, short blocks: sin(pi/12 (i + 1/2)), i = 0..5, Q31 (mirror for 6..11)", "F", "int32_t", "mp3v2_win_short_q31[6]", "", ws, 6)
    body = "\n".join(S)

    # statistics for the header comment
    stat = ["   sel linbits distinct-codebook codes maxlen kraft(num/den)"]
    for t_ in range(32):
        codes = books[M["tabindex"][t_]]
        k = kraft(codes)
        stat.append("   %3d %7d %16d %5d %6d %d/%d%s" % (t_, M["linbits"][t_], M["tabindex"][t_], len(codes), max(c[1] for c in codes), k[0], k[1],
                                                       "  (unused: empty table)" if t_ in M["unused"] else ""))
    stat.append("   count1 A: %d codes, max length %d, Kraft %d/%d; count1 B: %d codes, max length %d, Kraft %d/%d"
                % (len(M["count1"]["A"]), max(c[1] for c in M["count1"]["A"]), kraft(M["count1"]["A"])[0], kraft(M["count1"]["A"])[1],
                   len(M["count1"]["B"]), max(c[1] for c in M["count1"]["B"]), kraft(M["count1"]["B"])[0], kraft(M["count1"]["B"])[1]))
    l0, l1_ = M["lines"]["tabs"]
    rng = ", ".join("%s lines %d-%d" % (n, a, b) for n, (a, b) in sorted(M["lines"].items(), key=lambda kv: kv[1]))
    bodyhash = hashlib.sha256(body.encode()).hexdigest()
    sizes_b = {"huff_tab": 2 * len(tab), "huff_base_w1_linbits": 2 * 32 + 32 + 32, "count1": 64 + 16}
    head = """/* GENERATED FILE: do not edit. Written by tools/extract_mp3v2_tables.py (MP3 decoder v2, WP-09); reproduced byte for byte by `--check`.
 *
 * PROVENANCE
 *   Source of table data: lieff/minimp3, %(url)s, commit %(commit)s,
 *     file minimp3.h (SHA-256 %(shain)s), vendored unchanged in third_party/minimp3/.
 *   Licence of the source: CC0 1.0 Universal (public domain dedication; no credit required; minimp3 is credited as a courtesy in the project's provenance record).
 *   Extraction method: only the NUMBERS of the constant arrays below were read from minimp3.h (no code); lines read in the pinned file: %(rng)s.
 *     Transformations of the stored form (lookup tries, packed words, repeated band widths) are listed in the header of the extractor and expanded back to explicit
 *     code lists before our own layout is built; tables marked [F] are generated from the formulas of ISO/IEC 11172-3 and cross-checked against minimp3's data.
 *   Origin marks: [M] minimp3 data repacked, [M+F] minimp3 data checked against the formula and replaced by the formula value, [F] formula.
 *   Extracted on %(date)s. SHA-256 of the table text between the line "TABLES BEGIN" and the final #endif: %(bodyhash)s.
 *   Table text is data, not code: nothing here is copied from any program text.
 *
 * STRUCTURE CHECKED BY sim/test_mp3v2_tables.py (all selectors; extractor asserts the same): every codebook is prefix free with Kraft sum <= 1; code counts are the
 *   standard's (x_max + 1)^2; value range; linbits per selector; count1 tables of 16 quadruples; band widths sum to 576 (long), 192 (short); the oracle round trip.
 *
%(stat)s
 *
 * LAYOUT: Huffman codes: selectors 0..31 -> mp3v2_huff_base[sel] and mp3v2_huff_w1[sel]. Decode: width w = w1; loop { idx = next w bits (not consumed yet);
 *   e = mp3v2_huff_tab[base + off + idx]; if (e & 0x8000) { consume w bits; w = ((e >> 12) & 7) + 1; off = e & 0xFFF; } else { consume (e >> 8) bits; x = (e >> 4) & 15;
 *   y = e & 15; done } } with off = 0 at the first level. x is the first decoded value (the even spectral line), y the second. A value of 15 on a selector with
 *   linbits > 0 is followed by linbits more bits (added to 15); every nonzero value is followed by its sign bit (x first). Selector 0, 4 and 14 read no bits.
 *   Sizes in bytes (rodata): huffman lookup %(sz_tab)d, selector tables %(sz_sel)d, count1 %(sz_c1)d.
 */
#ifndef FW2_DECODE_MP3V2_TABLES_H
#define FW2_DECODE_MP3V2_TABLES_H
#include <stdint.h>

/* ---- TABLES BEGIN ---- */
""" % dict(url=URL, commit=COMMIT, shain=M["sha_in"], rng=rng, date=DATE, bodyhash=bodyhash, stat="\n".join(" *" + s for s in stat),
           sz_tab=sizes_b["huff_tab"], sz_sel=sizes_b["huff_base_w1_linbits"], sz_c1=sizes_b["count1"])
    return head + body + "\n#endif /* FW2_DECODE_MP3V2_TABLES_H */\n", tab, base, w1


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--minimp3", type=Path, default=MINIMP3)
    ap.add_argument("--out", type=Path, default=OUT)
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--model", action="store_true")
    ap.add_argument("--l1", type=int, default=DEFAULT_L1)
    a = ap.parse_args()
    raw, t, lines = read_minimp3(a.minimp3)
    M = build_model(raw, t, lines)
    if a.model:
        J = dict(M); J["books"] = {str(k): v for k, v in M["books"].items()}; J["mixed"] = M["sfb_mixed"]
        json.dump(J, sys.stdout); print(); return 0
    txt, tab, base, w1 = render(M, a.l1)
    if a.check:
        same = a.out.exists() and a.out.read_text() == txt
        print("tables.h reproduced byte for byte" if same else "tables.h DIFFERS from a fresh extraction"); return 0 if same else 1
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(txt)
    print("wrote %s (%d bytes, sha256 %s)" % (a.out, len(txt.encode()), hashlib.sha256(txt.encode()).hexdigest()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
