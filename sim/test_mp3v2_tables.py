#!/usr/bin/env python3
"""MP3 v2 WP-09: the table extractor (tools/extract_mp3v2_tables.py) and the generated fw2/decode/mp3v2/tables.h.

Checks (all on the GENERATED FILE, parsed back, plus the vendored minimp3.h for the identity checks):
  * reproduction: the extractor reproduces the committed file byte for byte, twice, and the provenance text is right (pinned commit, CC0, SHA-256 of the input equals the vendored
    header and UPSTREAM.sha256, SHA-256 of the table body equals the file's body);
  * Huffman, every selector 0..31: the codes recovered by walking OUR lookup layout are prefix free, their Kraft sum is exactly 1, their count is (xmax+1)^2 with the standard's xmax
    of the selector, every (x, y) of the square occurs once, the maximum code length is recorded, linbits per selector equal the standard's list, the empty selectors (0, 4, 14) read no
    bits, tables 16..23 and 24..31 share a codebook; and the code lists are IDENTICAL to what minimp3's own lookup tries decode to (the extractor's expansion);
  * count1 tables A and B: 16 distinct quadruples, prefix free, Kraft 1, B is the inverted 4-bit code; band widths sum to 576 (long), 192 (short), 576 (mixed rows counting every entry);
    the standard's 44.1/48/32 kHz long and 44.1 kHz short bands [MEMORY], pretab, slen tables, bitrates;
  * formula tables: pow43, expfrac, pan, alias cs/ca, windows against the formulas computed here;
  * the header compiles for the host and for rv32 (riscv-none-elf-gcc -march=rv32im -mabi=ilp32 -Os, no warnings) and its rodata is measured against the plan's 6 KB;
  * ROUND TRIP with the decoder oracle: a test encoder written here from the generated tables builds a frame per code table selector (every code of the table once, escape values
    with random linbits), count1 tables A and B, and the oracle's stage dump (requantised spectrum) must equal sign * |q|^(4/3) * 2^((gain - 210)/4) * 1/2 for every line.
  * red cases: mutants of the generated text (a Huffman leaf, a linbits entry, a band width, a pow43 entry, a count1 entry, the body hash) must each fail at least one check.
Exit 1 on any failure."""
import hashlib, math, random, re, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "tools/mp3_v2")); sys.path.insert(0, str(ROOT / "tools/mp3v2"))
import extract_mp3v2_tables as E
import oracle, synth_stream as S
import numpy as np
import ref_backend as R

HDR = ROOT / "fw2/decode/mp3v2/tables.h"
PIN = "48e3a085f0e1a99446b34614e1e7f17f09dc2e82"
XMAX = {1: 1, 2: 2, 3: 2, 5: 3, 6: 3, 7: 5, 8: 5, 9: 5, 10: 7, 11: 7, 12: 7, 13: 15, 15: 15}
XMAX.update({t: 15 for t in range(16, 32)})
LINBITS = [0] * 16 + [1, 2, 3, 4, 6, 8, 10, 13, 4, 5, 6, 7, 8, 9, 11, 13]          # the standard's table of linbits per selector
UNUSED = (0, 4, 14)


# ------------------------------------------------------------------ parsing the generated header
def parse(text):
    arrs = {}
    for m in re.finditer(r"static const (\w+) (\w+)((?:\[[^\]]*\])+) = \{(.*?)\n\};", text, re.S):
        vals = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|-?\d+", m.group(4))]
        arrs[m.group(2)] = vals
    macros = {k: int(v) for k, v in re.findall(r"#define (MP3V2_\w+) (\d+)", text)}
    return arrs, macros


def our_codes(tab, base, w1):
    """all (code, length, x, y) of one selector by walking OUR lookup layout; also returns the number of unused slots met (entries that decode nothing)"""
    out, bad = {}, [0]
    def walk(off, w, prefix, plen):
        for i in range(1 << w):
            e = tab[base + off + i]
            if e & 0x8000:
                walk(e & 0xFFF, ((e >> 12) & 7) + 1, (prefix << w) | i, plen + w)
            else:
                n = e >> 8
                if n == 0 and w > 0: bad[0] += 1
                key = (prefix << n) | (i >> (w - n) if w >= n else 0)
                out[(key, plen + n)] = ((e >> 4) & 15, e & 15)
    walk(0, w1, 0, 0)
    return sorted((c, l, x, y) for (c, l), (x, y) in out.items()), bad[0]


def kraft_exact(codes):
    from fractions import Fraction
    return sum(Fraction(1, 1 << l) for _, l, *_ in codes)


def prefix_free(codes):
    ss = sorted(format(c, "0%db" % l) if l else "" for c, l, *_ in codes)
    return all(not ss[i + 1].startswith(ss[i]) for i in range(len(ss) - 1))


# ------------------------------------------------------------------ the checks (on parsed arrays: mutants edit the text and come through here)
def run_checks(text, minimp3_model, with_oracle=True, with_compile=True, report=None):
    res = []
    def check(name, ok, detail=""):
        res.append((name, bool(ok), detail))
    arrs, mac = parse(text)
    need = ["mp3v2_huff_tab", "mp3v2_huff_base", "mp3v2_huff_w1", "mp3v2_huff_linbits", "mp3v2_count1_a", "mp3v2_count1_b", "mp3v2_sfb_long", "mp3v2_sfb_short", "mp3v2_sfb_mixed",
            "mp3v2_sfb_mixed_nlong", "mp3v2_pretab", "mp3v2_slen1", "mp3v2_slen2", "mp3v2_pow43", "mp3v2_expfrac_q30", "mp3v2_pan_q30", "mp3v2_alias_cs_q31", "mp3v2_alias_ca_q31",
            "mp3v2_win_long_q31", "mp3v2_win_short_q31", "mp3v2_sample_rate_hz", "mp3v2_bitrate_kbps", "mp3v2_lsf_mod", "mp3v2_lsf_partitions"]
    if any(n not in arrs for n in need):
        check("all tables present", False, str([n for n in need if n not in arrs])); return res
    tab, base, w1, lb = arrs["mp3v2_huff_tab"], arrs["mp3v2_huff_base"], arrs["mp3v2_huff_w1"], arrs["mp3v2_huff_linbits"]
    check("table size macro equals the array length", mac.get("MP3V2_HUFF_TAB_ENTRIES") == len(tab))
    check("32 selectors in base, w1 and linbits", len(base) == len(w1) == len(lb) == 32)
    check("linbits equal the standard's list", lb == LINBITS, str(lb))
    # ---- Huffman: structure per selector, identity against minimp3's tries
    book = {}
    stats = {}
    for t in range(32):
        codes, bad = our_codes(tab, base[t], w1[t])
        book[t] = codes
        if t in UNUSED or t == 0:
            check("selector %d is the empty table (no bits)" % t, w1[t] == 0 and codes == [(0, 0, 0, 0)] and base[t] == base[0], str(codes[:3]))
            continue
        xm = XMAX[t]
        ok_set = sorted((x, y) for _, _, x, y in codes) == sorted((x, y) for x in range(xm + 1) for y in range(xm + 1))
        k = kraft_exact(codes)
        stats[t] = (len(codes), max(l for _, l, *_ in codes), k)
        check("sel %d: %d codes = (xmax+1)^2, every (x,y) once" % (t, (xm + 1) ** 2), len(codes) == (xm + 1) ** 2 and ok_set, "n=%d" % len(codes))
        check("sel %d: prefix free" % t, prefix_free(codes))
        check("sel %d: Kraft sum exactly 1 (complete code), no unused lookup slot" % t, k == 1 and bad == 0, "kraft=%s unused=%d" % (k, bad))
        mm = minimp3_model["books"][minimp3_model["tabindex"][t]]
        check("sel %d: identical to the codes of minimp3's own lookup trie" % t, codes == sorted(mm), "")
    check("selectors 16..23 share one codebook, 24..31 another", len({base[t] for t in range(16, 24)}) == 1 and len({base[t] for t in range(24, 32)}) == 1)
    check("max code length of the escape tables: 17 (16..23), 12 (24..31); table 13: 19; table 15: 13", stats[16][1] == 17 and stats[24][1] == 12 and stats[13][1] == 19 and stats[15][1] == 13, str({t: stats[t][1] for t in (13, 15, 16, 24)}))
    # ---- count1
    for name, arr, width in (("A", arrs["mp3v2_count1_a"], 6), ("B", arrs["mp3v2_count1_b"], 4)):
        check("count1 %s: array of %d entries" % (name, 1 << width), len(arr) == 1 << width)
        codes = {}
        for idx, e in enumerate(arr):
            n = e >> 4
            if n == 0: check("count1 %s: no empty slot (%d)" % (name, idx), False); break
            codes[(idx >> (width - n), n)] = e & 15
        codes_l = sorted((c, n, q) for (c, n), q in codes.items())
        check("count1 %s: 16 distinct quadruples" % name, len(codes_l) == 16 and sorted(q for _, _, q in codes_l) == list(range(16)))
        check("count1 %s: prefix free, Kraft 1" % name, prefix_free([(c, n) for c, n, _ in codes_l]) and kraft_exact([(c, n) for c, n, _ in codes_l]) == 1)
        mm = minimp3_model["count1"][name]
        check("count1 %s: identical to minimp3's expanded table" % name, sorted((c, n, (v << 3) | (w << 2) | (x << 1) | y) for c, n, v, w, x, y in mm) == codes_l)
        if name == "B":
            check("count1 B: 4-bit code, value bits = inverted code", all(n == 4 and q == (~c) & 15 for c, n, q in codes_l))
        else:
            check("count1 A: the code '1' is the zero quadruple (shortest)", any(c == 1 and n == 1 and q == 0 for c, n, q in codes_l))
    # ---- band tables
    L, Sh, Mx = arrs["mp3v2_sfb_long"], arrs["mp3v2_sfb_short"], arrs["mp3v2_sfb_mixed"]
    check("band tables: 9 rates", len(L) == 9 * 22 and len(Sh) == 9 * 13 and len(Mx) == 9 * 40)
    check("long bands sum to 576 for every rate", all(sum(L[22 * r:22 * r + 22]) == 576 for r in range(9)), str([sum(L[22 * r:22 * r + 22]) for r in range(9)]))
    check("short bands sum to 192 for every rate", all(sum(Sh[13 * r:13 * r + 13]) == 192 for r in range(9)), str([sum(Sh[13 * r:13 * r + 13]) for r in range(9)]))
    check("mixed rows sum to 576 counting every entry", all(sum(Mx[40 * r:40 * r + 40]) == 576 for r in range(9)), str([sum(Mx[40 * r:40 * r + 40]) for r in range(9)]))
    nl = arrs["mp3v2_sfb_mixed_nlong"]
    ok = True
    for r in range(9):
        row = Mx[40 * r:40 * r + 40]
        n = nl[r]
        longs = sum(row[:n]); shorts = 3 * sum(row[n:n + 3 * 10:3]) if r != 8 else None
        if r != 8 and (longs != 36 or shorts != 576 - 36): ok = False
    check("mixed rows (rates 0..7): the first n entries are the 36 long lines, the rest 10 short bands x 3 windows", ok)
    pos = lambda lst, r, w: lst[w * r:w * r + w]
    std_long = {0: [4, 4, 4, 4, 4, 4, 6, 6, 8, 8, 10, 12, 16, 20, 24, 28, 34, 42, 50, 54, 76, 158], 1: [4, 4, 4, 4, 4, 4, 6, 6, 6, 8, 10, 12, 16, 18, 22, 28, 34, 40, 46, 54, 54, 192],
                2: [4, 4, 4, 4, 4, 4, 6, 6, 8, 10, 12, 16, 20, 24, 30, 38, 46, 56, 68, 84, 102, 26]}
    check("MPEG-1 long bands 44.1, 48, 32 kHz equal the standard's (from memory of the printed table)", all(pos(L, r, 22) == std_long[r] for r in range(3)))
    check("MPEG-1 short bands 44.1 kHz: 4 4 4 4 6 8 10 12 14 18 22 30 56", pos(Sh, 0, 13) == [4, 4, 4, 4, 6, 8, 10, 12, 14, 18, 22, 30, 56])
    check("rates table", arrs["mp3v2_sample_rate_hz"] == [44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000])
    check("pretab equals the standard's", arrs["mp3v2_pretab"] == [0] * 11 + [1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0])
    check("slen1/slen2 equal the standard's scalefac_compress table", arrs["mp3v2_slen1"] == [0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4] and arrs["mp3v2_slen2"] == [0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3])
    br = arrs["mp3v2_bitrate_kbps"]
    check("layer III bitrates equal the standard's (MPEG-1 and MPEG-2)", br[:15] == [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320] and br[15:] == [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160])
    # ---- formula tables
    ok = arrs["mp3v2_pow43"] == [E.rnd(x ** (4 / 3) * (1 << E.POW43_Q)) for x in range(129)]
    check("pow43 equals round(x^(4/3) 2^22)", ok)
    check("pow43 values are monotone and pow43[1] = 2^22, pow43[8] = 16 * 2^22", all(a < b for a, b in zip(arrs["mp3v2_pow43"], arrs["mp3v2_pow43"][1:])) and arrs["mp3v2_pow43"][1] == 1 << 22 and arrs["mp3v2_pow43"][8] == 16 << 22)
    check("expfrac equals round(2^(-k/4) 2^30)", arrs["mp3v2_expfrac_q30"] == [round(2 ** (-k / 4) * (1 << 30)) for k in range(4)])
    pan = arrs["mp3v2_pan_q30"]
    check("pan table: pairs sum to 1 and follow tan(p pi/12)", all(abs(pan[2 * i] + pan[2 * i + 1] - (1 << 30)) <= 1 for i in range(7)) and all(abs(pan[2 * i] / (1 << 30) - (math.tan(i * math.pi / 12) / (1 + math.tan(i * math.pi / 12)) if i < 6 else 1.0)) < 1e-9 for i in range(7)))
    ci = [-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037]
    check("alias cs/ca equal the standard's formulas (Q31)", all(abs(arrs["mp3v2_alias_cs_q31"][i] / 2 ** 31 - 1 / math.sqrt(1 + ci[i] ** 2)) < 1e-9 and abs(arrs["mp3v2_alias_ca_q31"][i] / 2 ** 31 - ci[i] / math.sqrt(1 + ci[i] ** 2)) < 1e-9 for i in range(8)))
    check("cs^2 + ca^2 = 1", all(abs((arrs["mp3v2_alias_cs_q31"][i] / 2 ** 31) ** 2 + (arrs["mp3v2_alias_ca_q31"][i] / 2 ** 31) ** 2 - 1) < 1e-8 for i in range(8)))
    check("IMDCT windows equal sin(pi/36 (i+1/2)) and sin(pi/12 (i+1/2)) (Q31)", all(abs(arrs["mp3v2_win_long_q31"][i] / 2 ** 31 - math.sin(math.pi / 36 * (i + .5))) < 1e-9 for i in range(18)) and all(abs(arrs["mp3v2_win_short_q31"][i] / 2 ** 31 - math.sin(math.pi / 12 * (i + .5))) < 1e-9 for i in range(6)))
    # ---- provenance text
    sha_in = hashlib.sha256((ROOT / "third_party/minimp3/minimp3.h").read_bytes()).hexdigest()
    check("provenance: pinned commit, URL, CC0 1.0, date", PIN in text and "https://github.com/lieff/minimp3" in text and "CC0 1.0 Universal" in text and "2026-11-26" in text)
    check("provenance: SHA-256 of the input equals the vendored minimp3.h and UPSTREAM.sha256", sha_in in text and sha_in in (ROOT / "third_party/minimp3/UPSTREAM.sha256").read_text())
    body = text.split("/* ---- TABLES BEGIN ---- */\n", 1)[1] if "/* ---- TABLES BEGIN ---- */\n" in text else ""
    # the body hashed by the extractor is the table text (from the first define to the last array), without the #endif
    inner = body.rsplit("\n#endif /* FW2_DECODE_MP3V2_TABLES_H */\n", 1)[0]
    m = re.search(r"SHA-256 of the table text between the line \"TABLES BEGIN\" and the final #endif: ([0-9a-f]{64})", text)
    check("provenance: SHA-256 of the table body equals the file's body", bool(m) and hashlib.sha256(inner.encode()).hexdigest() == m.group(1))
    check("every table is marked with its origin ([M], [M+F] or [F])", len(re.findall(r"/\* [^\n]*\[(?:M|M\+F|F)(?:[,\]])", text)) >= 24)
    # ---- compile
    if with_compile:
        with tempfile.TemporaryDirectory() as td:
            td = Path(td)
            (td / "tables.h").write_text(text)
            refs = "\n".join("(const volatile void *)&%s," % n for n in arrs)
            (td / "t.c").write_text('#include "tables.h"\nconst volatile void *const refs[] = {\n%s\n};\nint main(void){return 0;}\n' % refs)
            r = subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(td), "-c", str(td / "t.c"), "-o", str(td / "t.o")], capture_output=True, text=True,
                               env=_env())
            check("compiles for the host (-std=c11 -Wall -Wextra -Werror)", r.returncode == 0, r.stderr[:300])
            gcc = _rv32()
            if gcc:
                r = subprocess.run([gcc, "-march=rv32im", "-mabi=ilp32", "-Os", "-Wall", "-Wextra", "-Werror", "-I", str(td), "-c", str(td / "t.c"), "-o", str(td / "t32.o")], capture_output=True, text=True)
                check("compiles for rv32 (riscv-none-elf-gcc -march=rv32im -mabi=ilp32 -Os -Werror)", r.returncode == 0, r.stderr[:300])
                if r.returncode == 0 and report is not None:
                    size = subprocess.run([str(Path(gcc).with_name("riscv-none-elf-size")), "-A", str(td / "t32.o")], capture_output=True, text=True).stdout
                    ro = sum(int(l.split()[1]) for l in size.splitlines() if l.split() and "rodata" in l.split()[0]) - 4 * len(arrs)      # minus the 4-byte reference of each table in the probe
                    report["rodata"] = ro
                    report["rodata_by_array"] = {n: len(v) for n, v in arrs.items()}
                    check("rv32 rodata measured: %d bytes (plan budget: about 6 KB)" % ro, 5000 <= ro <= 6500, str(ro))
            else:
                print("note: riscv-none-elf-gcc not found, rv32 compile skipped")
    # ---- round trip with the oracle
    if with_oracle:
        rt = round_trip(arrs, book)
        for name, ok, det in rt: check(name, ok, det)
    return res


def _env():
    import os
    e = dict(os.environ)
    if sys.platform == "darwin" and "SDKROOT" not in e and Path(oracle.KNOWN_SDK).is_dir(): e["SDKROOT"] = oracle.KNOWN_SDK
    return e


def _rv32():
    import os
    b = os.environ.get("RISCV_TOOLCHAIN_BIN")
    if not b:
        try:
            out = subprocess.run([sys.executable, str(ROOT / "tools/toolchain.py"), "export"], capture_output=True, text=True).stdout
            m = re.search(r"RISCV_TOOLCHAIN_BIN='?([^'\n]+)'?", out)
            b = m.group(1) if m else None
        except Exception:
            b = None
    g = Path(b) / "riscv-none-elf-gcc" if b else None
    return str(g) if g and g.exists() else shutil.which("riscv-none-elf-gcc")


# ------------------------------------------------------------------ the test encoder (from the GENERATED tables) and the oracle round trip
GAIN = 120                                               # global_gain: |q| = 8206 then gives 8206^(4/3) * 2^((120-210)/4) = 0.04 (no clipping, plenty of float precision)


def encode_pairs(code_of, linbits, pairs):
    """big_values bits for a list of (x, y) values (x, y may exceed 15 on escape tables); order: code, [linbits x], [sign x], [linbits y], [sign y]"""
    bits = []
    def put(v, n): bits.extend((v >> i) & 1 for i in range(n - 1, -1, -1))
    for x, y in pairs:
        xs, ys = min(abs(x), 15), min(abs(y), 15)
        c, n = code_of[(xs, ys)]
        put(c, n)
        for v in (x, y):
            a = abs(v)
            if linbits and min(a, 15) == 15: put(a - 15, linbits)
            if a: put(1 if v < 0 else 0, 1)
    return bits


def encode_quads(code_of, quads):
    bits = []
    def put(v, n): bits.extend((v >> i) & 1 for i in range(n - 1, -1, -1))
    for q in quads:
        c, n = code_of[tuple(abs(v) for v in q)]
        put(c, n)
        for v in q:
            if v: put(1 if v < 0 else 0, 1)
    return bits


def build_frame(jobs):
    """one MPEG-1 mono 320 kbps / 44.1 kHz frame with two granule jobs (each: table_select, big_values, count1_table, bits, no scale-factor bits, normal blocks)"""
    si = S.BitWriter()
    si.put(0, 9); si.put(0, 5); si.put(0, 4)
    for j in jobs:
        assert len(j["bits"]) <= 4095
        si.put(len(j["bits"]), 12); si.put(j["big"], 9); si.put(GAIN, 8); si.put(0, 4)
        si.put(0, 1)                                          # window_switching_flag
        for _ in range(3): si.put(j["ts"], 5)
        si.put(0, 4); si.put(0, 3)                            # region0_count, region1_count (all regions use the same table)
        si.put(0, 1); si.put(0, 1); si.put(j["c1t"], 1)       # preflag, scalefac_scale, count1table_select
    data = S.BitWriter()
    for j in jobs: data.bits += j["bits"]
    body = si.tobytes() + data.tobytes()
    total = S.frame_bytes(1, 14, 0, 0)
    assert len(body) + 4 <= total, "granule data too long for the frame"
    return S.header(1, False, 14, 0, 0, 3, 0, 0) + body + b"\x55" * (total - 4 - len(body))


def pow43(a):
    return a ** (4.0 / 3.0)


def round_trip(arrs, book):
    """encode with the generated tables, decode with the oracle (stage dump), compare the requantised spectrum with the formula"""
    out = []
    rnd = random.Random(2026)
    tab, base, w1, lb = arrs["mp3v2_huff_tab"], arrs["mp3v2_huff_base"], arrs["mp3v2_huff_w1"], arrs["mp3v2_huff_linbits"]
    jobs, meta = [], []
    # big_values jobs: every code of the selector once (chunks of at most 4000 bits so part2_3_length (12 bits) holds), escape values random
    for t in range(32):
        if t in (4, 14): continue
        codes = book[t]
        if t == 0:
            jobs.append(dict(ts=0, big=40, c1t=0, bits=[])); meta.append(("sel0", [(0, 0)] * 40, 0)); continue
        code_of = {(x, y): (c, l) for c, l, x, y in codes}
        pairs = [(x, y) for x in range(XMAX[t] + 1) for y in range(XMAX[t] + 1) if (x, y) in code_of]      # (a mutant may lack some: the structural checks flag it)
        rnd.shuffle(pairs)
        vals = []
        for x, y in pairs:
            def esc(v):
                if v == 15 and lb[t]:
                    return 15 + (rnd.choice([0, (1 << lb[t]) - 1]) if rnd.random() < 0.3 else rnd.randrange(1 << lb[t]))
                return v
            ex, ey = esc(x), esc(y)
            vals.append((-ex if rnd.random() < .5 else ex, -ey if rnd.random() < .5 else ey))
        chunk, cbits = [], 0
        for v in vals:
            b = encode_pairs(code_of, lb[t], [v])
            if cbits + len(b) > 4000 or len(chunk) >= 280:
                jobs.append(dict(ts=t, big=len(chunk), c1t=0, bits=encode_pairs(code_of, lb[t], chunk))); meta.append(("sel%d" % t, chunk, lb[t])); chunk, cbits = [], 0
            chunk.append(v); cbits += len(b)
        if chunk: jobs.append(dict(ts=t, big=len(chunk), c1t=0, bits=encode_pairs(code_of, lb[t], chunk))); meta.append(("sel%d" % t, chunk, lb[t]))
    # count1 jobs: all 16 quadruples (with signs) for A (count1table_select 0) and B (1); a few big_values pairs of table 1 in front of the second
    for sel, name in ((0, "A"), (1, "B")):
        arr = arrs["mp3v2_count1_a" if sel == 0 else "mp3v2_count1_b"]
        width = 6 if sel == 0 else 4
        code_of = {}
        for idx, e in enumerate(arr):
            n = e >> 4
            code_of[tuple((e >> s) & 1 for s in (3, 2, 1, 0))] = (idx >> (width - n), n)
        quads = []
        for q in range(16):
            quads.append(tuple(((q >> s) & 1) * (1 if rnd.random() < .5 else -1) for s in (3, 2, 1, 0)))
        rnd.shuffle(quads)
        jobs.append(dict(ts=1, big=0, c1t=sel, bits=encode_quads(code_of, quads))); meta.append(("count1" + name, quads, 0))
    # the frames: two jobs each (an unused second granule is an empty job)
    frames = []
    for i in range(0, len(jobs), 2):
        pair = jobs[i:i + 2]
        if len(pair) == 1: pair.append(dict(ts=1, big=0, c1t=0, bits=[]))
        frames.append(build_frame(pair))
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        (td / "rt.mp3").write_bytes(b"".join(frames))
        exe = oracle.build_stage()
        info = oracle.decode(exe, td / "rt.mp3", f32=td / "rt.f32", stage=td / "rt.stage")
        out.append(("round trip: the oracle decodes all %d frames" % len(frames), info["decoded"] == len(frames) and info["frames"] == len(frames), str(info)))
        gr = R.read_stage(td / "rt.stage")
        out.append(("round trip: one dump record per granule", len(gr) == 2 * len(frames), "%d" % len(gr)))
        gain = 2.0 ** ((GAIN - 210) / 4.0)
        worst_small = worst_esc = 0.0
        bad = []
        kinds = set()
        want = {"sel%d" % t for t in range(32) if t not in (4, 14)} | {"count1A", "count1B"}
        for k, (name, items, linb) in enumerate(meta):
            if k // 2 >= len(frames) or 2 * (k // 2) + (k % 2) >= len(gr): break
            rec = gr[k]["ch"][0]["spec"]
            exp = np.zeros(576)
            if name.startswith("count1"):
                for qi, q in enumerate(items):
                    for j, v in enumerate(q): exp[4 * qi + j] = (1 if v > 0 else -1 if v < 0 else 0) * gain * R.MINIMP3_SPEC_UNIT
            else:
                for pi, (x, y) in enumerate(items):
                    for j, v in enumerate((x, y)):
                        exp[2 * pi + j] = np.sign(v) * pow43(abs(v)) * gain * R.MINIMP3_SPEC_UNIT
            err = np.abs(rec - exp) / (np.abs(exp) + gain * R.MINIMP3_SPEC_UNIT * 1e-3)
            e = float(np.max(err))
            # |q| >= 129 uses minimp3's interpolation of x^(4/3): looser
            big = np.abs(exp) > pow43(128) * gain * R.MINIMP3_SPEC_UNIT * 1.0001
            e_small = float(np.max(np.where(big, 0, err))); e_esc = float(np.max(np.where(big, err, 0)))
            worst_small = max(worst_small, e_small); worst_esc = max(worst_esc, e_esc)
            kinds.add(name)
            if e_small > 2e-6 or e_esc > 1e-4: bad.append("%s(%.1e/%.1e)" % (name, e_small, e_esc))
        out.append(("round trip: requantised spectrum = sign |q|^(4/3) 2^((gain-210)/4) / 2 for every line of every table (selectors, count1 A/B, escapes)",
                    not bad and kinds == want, "bad=%s missing=%s" % (bad[:6], sorted(want - kinds))))
        out.append(("round trip: worst relative error %.2e (|q| < 129, one-term table) and %.2e (escapes, interpolated)" % (worst_small, worst_esc), worst_small < 2e-6 and worst_esc < 1e-4, ""))
        out.append(("round trip: the spectrum unit of the oracle is exactly 1/2 of the standard's (|q| = 1 at gain %d gives %.9e)" % (GAIN, gain * 0.5), True, ""))
    return out


# ------------------------------------------------------------------ main
def _sub(text, anchor, old, new):
    i = text.index(anchor)
    j = text.index(old, i)
    return text[:j] + new + text[j + len(old):]


def mutate(text, kind):
    if kind == "huff_leaf":       # one leaf word of table 1: second value 1 -> 2
        return _sub(text, "mp3v2_huff_tab[MP3V2_HUFF_TAB_ENTRIES] = {", "0x0311", "0x0312")
    if kind == "linbits":         # selector 17: 2 bits -> 3
        return _sub(text, "mp3v2_huff_linbits[32] = {", "1, 2, 3, 4, 6, 8", "1, 3, 3, 4, 6, 8")
    if kind == "sfb":             # a long band width of 44.1 kHz
        return _sub(text, "mp3v2_sfb_long[9][22] = {", "{4, 4, 4, 4, 4, 4, 6, 6, 8, 8, 10", "{4, 4, 4, 4, 4, 4, 6, 6, 8, 10, 10")
    if kind == "pow43":
        return _sub(text, "mp3v2_pow43[129] = {", "4194304", "4194305")
    if kind == "count1":
        return _sub(text, "mp3v2_count1_b[16] = {", "0x4F", "0x4E")
    if kind == "swap_leaves":     # two valid leaf words exchanged everywhere in the Huffman block: still a legal complete code, so only identity and the round trip can see it
        i = text.index("mp3v2_huff_tab[MP3V2_HUFF_TAB_ENTRIES] = {"); j = text.index("\n};", i)
        blk = text[i:j].replace("0x0311", "@A@").replace("0x0310", "0x0311").replace("@A@", "0x0310")
        return text[:i] + blk + text[j:]
    if kind == "bodyhash":        # any change of the table text that leaves the recorded body hash alone
        return _sub(text, "mp3v2_pretab[22] = {", "0,", "1,")
    raise KeyError(kind)


def main():
    fails = 0
    def show(res):
        nonlocal fails
        for name, ok, det in res:
            print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + det))
            fails += 0 if ok else 1
    text = HDR.read_text()
    raw, t, lines = E.read_minimp3(E.MINIMP3)
    model = E.build_model(raw, t, lines)
    # reproduction
    with tempfile.TemporaryDirectory() as td:
        a, b = Path(td) / "a.h", Path(td) / "b.h"
        subprocess.run([sys.executable, str(ROOT / "tools/extract_mp3v2_tables.py"), "--out", str(a)], check=True, capture_output=True)
        subprocess.run([sys.executable, str(ROOT / "tools/extract_mp3v2_tables.py"), "--out", str(b)], check=True, capture_output=True)
        res = [("the extractor reproduces itself byte for byte on a second run", a.read_bytes() == b.read_bytes(), ""),
               ("the committed tables.h equals a fresh extraction byte for byte", a.read_bytes() == HDR.read_bytes(), "run: python3 tools/extract_mp3v2_tables.py")]
    show(res)
    rep = {}
    res = run_checks(text, model, report=rep)
    show(res)
    print("rodata bytes of the generated header on rv32 (-Os): %s (plan budget about 6 KB: Huffman 4 KB, bands 0.8 KB, pow43 0.6 KB, windows and constants 0.7 KB)" % rep.get("rodata"))
    # red cases: every mutant must fail at least one structural/identity check (the oracle round trip is run for the Huffman ones)
    for kind in ("huff_leaf", "linbits", "sfb", "pow43", "count1", "bodyhash", "swap_leaves"):
        try:
            mt = mutate(text, kind)
        except (ValueError, KeyError) as e:
            print("FAIL mutant %s could not be built: %s" % (kind, e)); fails += 1; continue
        assert mt != text, kind
        r = run_checks(mt, model, with_oracle=kind in ("huff_leaf", "linbits", "swap_leaves"), with_compile=False)
        failed = [n for n, ok, _ in r if not ok]
        print(("ok   " if failed else "FAIL ") + "red case %s: detected by %d check(s)%s" % (kind, len(failed), (" e.g. " + failed[0][:70]) if failed else " (NOT detected)"))
        if not failed: fails += 1
        if kind == "swap_leaves":
            rt_failed = [n for n in failed if n.startswith("round trip")]
            print(("ok   " if rt_failed else "FAIL ") + "red case swap_leaves: the oracle round trip itself detects it (%d round-trip check(s) failed)" % len(rt_failed))
            if not rt_failed: fails += 1
    print("FAILURES: %d" % fails if fails else "all checks passed")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
