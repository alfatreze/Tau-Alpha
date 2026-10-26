#!/usr/bin/env python3
"""Space Grotesk (SemiBold, Bold) as Tau font records (docs/features/FONT_SPEC.md sections 3.2 and 4).

One record per weight: the 4-bit-per-pixel coverage atlas the drawing engine expects (16 x 16 cell, 8 pixels per 32-bit word, 2 words per row, 32 words per glyph, glyphs left-aligned in the cell)
for Latin-1 (U+0020..U+007E and U+00A0..U+00FF, 191 glyphs), the per-glyph advance, and a CRC. Rasterised by 8 x supersampling and a box filter (hinted rendering collapsed Medium onto SemiBold in the
first test), so the weights really differ. Deterministic: the same inputs give the same bytes; `--check` regenerates and compares with the files in fw2/generated/fonts/.

Usage: gen_font_atlases.py [--check] [--preview]
  (no flag)   write fw2/generated/fonts/*.tfnt and src/fpga/core/font_ram.v (the writable font memory's power-up contents: the DEFAULT font, Tamzen Regular)
  --check     compare with the files on disk (exit 1 on any difference); used by make test-host
  --preview   write docs/features/font_preview/*.png (specimen: the sample line, the whole Latin-1 range)

Record layout (little endian), the same one Tau Omega writes into the FONT section of tau-assets.bin:
  "TFNT" | version u16 = 1 | flags u16 = 0 | crc32 u32 of everything after it
  family[16] | weight u16 | cell_w u8 | cell_h u8 | nranges u8 | baseline u8 | reserved u16
  nranges x { first u16, last u16 } | advance[n] zero-padded to a multiple of 4 | n x 32 x u32 atlas words
"""
import struct, sys, zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "third_party" / "font"
OUT = ROOT / "fw2" / "generated" / "fonts"
PREVIEW = ROOT / "docs" / "features" / "font_preview"
RANGES = ((0x20, 0x7E), (0xA0, 0xFF))
CELL = 16
SS = 8                                            # supersampling per axis
# per font: file, family, weight, size in pixels (may be fractional; 0 for a bitmap font, which is used at its native size and never scaled), baseline row. The tool fails when an outline font clips (see check_cell).
FONT_SET = (
    ("SpaceGrotesk-SemiBold.ttf", "Space Grotesk", 600, 15.0, 12),
    ("SpaceGrotesk-Bold.ttf", "Space Grotesk", 700, 15.0, 12),
    ("Tamzen8x16r.bdf", "Tamzen", 400, 0, 12),             # native 8 x 16 pixels: fits the 16 x 16 cell exactly (baseline row 12, 4 rows of descender), advance 8, no scaling
    ("Tamzen8x16b.bdf", "Tamzen", 700, 0, 12),
)
# Tamzen 8x16 has no glyph for these Latin-1 codes; they take the plain sign the firmware's draw-time fold uses (fw2/gfx/gfx.c fold_a0), drawn in the same font, so no hole appears.
FALLBACK = {0xA7: "S", 0xAA: "a", 0xAC: "-", 0xAE: "R", 0xAF: "-", 0xB1: "+", 0xB2: "2", 0xB3: "3", 0xB4: "'", 0xB5: "u", 0xB6: "P", 0xB7: ".", 0xB9: "1", 0xBA: "o", 0xBC: "1", 0xBD: "1", 0xBE: "3"}
DESCENDERS = set("gjpqyQ,;()[]{}|_¡¤¦§©®µ¶¿Çç¸")   # glyphs allowed to reach the bottom row


def codes():
    return [c for a, b in RANGES for c in range(a, b + 1)]


def render_glyph(font, ch, baseline):
    """-> (16 x 16 list of 8-bit coverage rows, ink right edge in pixels). Box-filtered from an SS x SS supersample."""
    from PIL import Image, ImageDraw
    big = Image.new("L", (CELL * SS, CELL * SS), 0)
    ImageDraw.Draw(big).text((0, baseline * SS), ch, font=font, fill=255, anchor="ls")
    bb = big.getbbox()
    if bb and bb[1] < SS:
        # a glyph (an accented capital, a tall bracket) whose ink would touch the top row: squash the part above the baseline just enough that the ink starts in row 1 (the usual bitmap-font rule: accented capitals are a little shorter)
        top = baseline * SS
        scale = (baseline - 1) * SS / (top - bb[1])
        upper = big.crop((0, 0, CELL * SS, top)).resize((CELL * SS, max(1, round(top * scale))), Image.BOX)
        lower = big.crop((0, top, CELL * SS, CELL * SS))
        big = Image.new("L", (CELL * SS, CELL * SS), 0)
        big.paste(upper, (0, top - upper.height)); big.paste(lower, (0, top))
    small = big.resize((CELL, CELL), Image.BOX)
    px = small.load()
    return [[px[x, y] for x in range(CELL)] for y in range(CELL)]


def cov4(v):
    return (v * 15 + 127) // 255                    # rounded, as gen_font_rom.py documents (a shift would delete the faint edge pixels)


def build_font(path, size, baseline):
    from PIL import ImageFont
    font = ImageFont.truetype(str(path), size * SS)
    glyphs, adv, notes = {}, {}, []
    for code in codes():
        ch = chr(code)
        rows = render_glyph(font, ch, baseline)
        right = max((x + 1 for x in range(CELL) for y in range(CELL) if rows[y][x] > 8), default=0)
        a = right + 2 if right else max(3, int(font.getlength(ch) / SS))
        glyphs[code] = rows
        adv[code] = min(a, CELL)
    digits = max(adv[c] for c in range(0x30, 0x3A))                # tabular figures: clocks and counters do not jitter
    for c in range(0x30, 0x3A):
        adv[c] = digits
    return glyphs, adv


def build_bdf(path, baseline):
    """A BDF bitmap font as 16 x 16 cells: every set pixel is full coverage (255), so the engine draws the exact pixels. Advance = the font's own DWIDTH (8: monospace, digits are tabular by nature)."""
    glyphs, adv, native = {}, {}, {}
    cur = None
    for line in path.read_text().splitlines():
        w = line.split()
        if not w: continue
        if w[0] == "STARTCHAR": cur = {"enc": -1, "dw": 8, "bbx": (8, 16, 0, -4), "rows": []}
        elif cur is None: continue
        elif w[0] == "ENCODING": cur["enc"] = int(w[1])
        elif w[0] == "DWIDTH": cur["dw"] = int(w[1])
        elif w[0] == "BBX": cur["bbx"] = tuple(int(x) for x in w[1:5])
        elif w[0] == "BITMAP": cur["in_bitmap"] = True
        elif w[0] == "ENDCHAR":
            if cur["enc"] >= 0: native[cur["enc"]] = cur
            cur = None
        elif cur.get("in_bitmap"): cur["rows"].append(int(w[0], 16) << (8 * (4 - len(w[0]) // 2)) if len(w[0]) <= 8 else 0)
    def cellrows(g):
        bw, bh, xo, yo = g["bbx"]
        rows = [[0] * CELL for _ in range(CELL)]
        for r, bits in enumerate(g["rows"][:bh]):
            y = baseline - (yo + bh) + r
            for x in range(bw):
                if (bits >> (31 - x)) & 1 and 0 <= y < CELL and 0 <= xo + x < CELL: rows[y][xo + x] = 255
        return rows
    for code in codes():
        src = native.get(code) or native.get(ord(FALLBACK[code])) if code in FALLBACK or code in native else native.get(0x3F)
        if src is None: src = native.get(0x3F)
        glyphs[code] = cellrows(src); adv[code] = src["dw"]
    return glyphs, adv


def check_cell(glyphs, adv):
    """Problems a human must look at: ink in row 0, ink in the last column, ink in the last row of a glyph that is not allowed a descender."""
    bad = []
    for code, rows in glyphs.items():
        ch = chr(code)
        if any(rows[0][x] > 8 for x in range(CELL)): bad.append((code, "ink in the top row"))
        if any(rows[y][CELL - 1] > 8 for y in range(CELL)): bad.append((code, "ink in the last column"))
        if ch not in DESCENDERS and any(rows[CELL - 1][x] > 8 for x in range(CELL)): bad.append((code, "ink in the bottom row"))
    return bad


def pack_words(glyphs):
    words = []
    for code in codes():
        rows = glyphs[code]
        for y in range(CELL):
            for half in range(2):
                w = 0
                for i in range(8):
                    w |= (cov4(rows[y][half * 8 + i]) & 0xF) << (4 * i)
                words.append(w)
    return words


def record(family, weight, baseline, adv, words):
    body = family.encode("ascii").ljust(16, b"\0")[:16] + struct.pack("<HBBBBH", weight, CELL, CELL, len(RANGES), baseline, 0)
    body += b"".join(struct.pack("<HH", a, b) for a, b in RANGES)
    body += bytes(adv[c] for c in codes())
    body += b"\0" * (-len(body) % 4)                    # the atlas words start on a 4-byte boundary (the CPU reads them as words; an unaligned load faults)
    body += b"".join(struct.pack("<I", w) for w in words)
    return b"TFNT" + struct.pack("<HHI", 1, 0, zlib.crc32(body) & 0xFFFFFFFF) + body


RAM_V = ROOT / "src" / "fpga" / "core" / "font_ram.v"
GEN = ROOT / "fw2" / "generated"
MENU_ORDER = ("Tamzen8x16r.bdf", "Tamzen8x16b.bdf", "SpaceGrotesk-SemiBold.ttf", "SpaceGrotesk-Bold.ttf")      # the Appearance list: index 0 is the font the bitstream powers up with


def render_font_data(built):
    """fw2/generated/font_data.c: the four records as 4-byte-aligned byte arrays (one cold object, read only when a font is chosen) and fw2/generated/font_default.h: the power-up font's advances."""
    out = ["/* GENERATED by tools/gen_font_atlases.py from fw2/generated/fonts/*.tfnt: do not edit. The Tau font records the firmware loads into the engine's font memory (fw2/gfx/font.c), in the Appearance list order. */",
           "#include <stdint.h>", "#include \"font_data.h\"", ""]
    for i, f in enumerate(MENU_ORDER):
        rec = built[f][2]
        out.append(f"/* {i}: {f} ({len(rec)} bytes) */")
        out.append(f"static const uint8_t font_rec{i}[{len(rec)}] __attribute__((aligned(4))) = {{")
        for k in range(0, len(rec), 24): out.append("    " + ", ".join(str(b) for b in rec[k:k + 24]) + ",")
        out.append("};")
    out.append("const uint8_t *const font_recs[FONT_COUNT] = { " + ", ".join(f"font_rec{i}" for i in range(len(MENU_ORDER))) + " };")
    out.append(f"const uint32_t font_rec_len = {len(built[MENU_ORDER[0]][2])}u;")
    data = "\n".join(out) + "\n"
    default_adv = [built[MENU_ORDER[0]][1][c] for c in codes()]
    hdr = ["/* GENERATED by tools/gen_font_atlases.py: do not edit. */", "#ifndef P2_FONT_DATA_H", "#define P2_FONT_DATA_H", "#include <stdint.h>",
           f"#define FONT_COUNT {len(MENU_ORDER)}u", f"#define FONT_GLYPHS {len(codes())}u",
           "extern const uint8_t *const font_recs[FONT_COUNT];      /* the records, in the Appearance order (0 = the font the bitstream powers up with) */",
           "extern const uint32_t font_rec_len;",
           "/* The advances of the power-up font (index 0): the table in use until another font is loaded. */",
           "#define FONT_DEFAULT_ADV { " + ", ".join(str(a) for a in default_adv) + " }", "#endif", ""]
    return data, "\n".join(hdr)

DEFAULT_FONT = "Tamzen8x16r.bdf"                                 # the font in the bitstream at power-up (docs/features/FONT_SPEC.md 3.1, owner 2026-10-17: Tamzen Regular): text works before the cold image is loaded


def render_font_ram(words):
    n = len(words)
    out = [
        "// ==========================================================================",
        "// font_ram.v -- GENERATED by tools/gen_font_atlases.py. DO NOT EDIT.",
        "//",
        "// The writable glyph memory of the drawing engine (docs/features/FONT_SPEC.md, TAU_FONT_RAM): %d words" % n,
        "// (Latin-1: 191 glyphs of 32 words, 16 x 16 cell, 4 bits of coverage per pixel, 8 pixels per word).",
        "// Read port in clk_r (the engine, registered); write port in clk_w (the CPU, through tau_soc.v's",
        "// FONT_IDX / FONT_DATA registers). It powers up holding the default font (Tamzen Regular),",
        "// so text works before the cold image loads; firmware replaces it with the chosen font. The CPU never",
        "// writes while the engine draws text (it fences first), so read-during-write is don't-care.",
        "// Address = glyph index * 32 + row * 2 + half; glyph 0..94 = U+0020..U+007E, 95..190 = U+00A0..U+00FF.",
        "// ==========================================================================",
        "",
        "`default_nettype none",
        "",
        "module font_ram #(parameter WORDS = %d) (" % n,
        "    input  wire        clk_r,",
        "    input  wire [12:0] raddr,",
        "    output reg  [31:0] q,",
        "    input  wire        clk_w,",
        "    input  wire        we,",
        "    input  wire [12:0] waddr,",
        "    input  wire [31:0] wdata",
        ");",
        "",
        '    (* ramstyle = "M10K, no_rw_check" *) reg [31:0] mem [0:WORDS-1];',
        "",
        "    initial begin",
    ]
    out += ["        mem[%4d] = 32'h%08X;" % (i, w) for i, w in enumerate(words)]
    out += ["    end", "", "    always @(posedge clk_r) q <= mem[raddr];", "    always @(posedge clk_w) if (we) mem[waddr] <= wdata;", "", "endmodule", ""]
    return "\n".join(out)


def out_name(file):
    return OUT / (Path(file).stem + ".tfnt")


def build_all():
    res = {}
    for file, family, weight, size, baseline in FONT_SET:
        glyphs, adv = build_bdf(FONTS / file, baseline) if file.endswith(".bdf") else build_font(FONTS / file, size, baseline)
        res[file] = (glyphs, adv, record(family, weight, baseline, adv, pack_words(glyphs)))
    return res


def specimen(glyphs, adv, title):
    from PIL import Image, ImageDraw
    sample = "Now Playing: Aphex Twin - Xtal 0123 gjpqy  ÅÖÜéñ ß £©"
    scale = 4
    w = 400
    img = Image.new("L", (w, 16 * 3 + 16 * 12), 0)
    def line(y, text):
        x = 0
        for ch in text:
            c = ord(ch)
            if c not in glyphs: c = 0x20
            cell = Image.new("L", (CELL, CELL))
            cell.putdata([cov4(glyphs[c][yy][xx]) * 17 for yy in range(CELL) for xx in range(CELL)])
            img.paste(cell, (x, y), cell)
            x += adv[c]
            if x > w - CELL: break
    line(0, sample)
    line(16, "0123456789 0123456789 :.,;!?()[]-_/\\")
    ys = 48
    per = 24
    for k, c in enumerate(codes()):
        col, row = k % per, k // per
        cell = Image.new("L", (CELL, CELL)); cell.putdata([cov4(glyphs[c][yy][xx]) * 17 for yy in range(CELL) for xx in range(CELL)])
        img.paste(cell, (col * 16, ys + row * 16), cell)
    return img.resize((w * scale // 2, img.height * scale // 2), Image.NEAREST)


def main():
    check = "--check" in sys.argv
    built = build_all()
    problems = []
    for file, (glyphs, adv, rec) in built.items():
        bad = check_cell(glyphs, adv) if not file.endswith(".bdf") else [(c, "ink outside the 8 columns of the bitmap") for c, rows in glyphs.items() if any(rows[y][x] for y in range(CELL) for x in range(8, CELL))]
        for code, why in bad: problems.append(f"{file}: U+{code:04X} {why}")
    if "--preview" in sys.argv:
        PREVIEW.mkdir(parents=True, exist_ok=True)
        for file, (glyphs, adv, rec) in built.items():
            specimen(glyphs, adv, file).save(PREVIEW / (Path(file).stem + ".png"))
        print("wrote", PREVIEW)
    if problems:
        print("\n".join(problems)); print(f"{len(problems)} cell problem(s)")
        if not check: return 1
    rc = 1 if problems else 0
    fdata, fhdr = render_font_data(built)
    for path, text in ((GEN / "font_data.c", fdata), (GEN / "font_data.h", fhdr)):
        if check:
            same = path.exists() and path.read_text() == text
            print(("ok   " if same else "STALE ") + str(path.relative_to(ROOT)))
            if not same: rc = 1
        else:
            path.write_text(text); print("wrote", path.relative_to(ROOT))
    words = pack_words(built[DEFAULT_FONT][0])
    ram = render_font_ram(words)
    if check:
        same = RAM_V.exists() and RAM_V.read_text() == ram
        print(("ok   " if same else "STALE ") + str(RAM_V.relative_to(ROOT)))
        if not same: rc = 1
    else:
        RAM_V.write_text(ram); print("wrote", RAM_V.relative_to(ROOT), len(words), "words")
    for file, (glyphs, adv, rec) in built.items():
        dst = out_name(file)
        if check:
            same = dst.exists() and dst.read_bytes() == rec
            print(("ok   " if same else "STALE ") + str(dst.relative_to(ROOT)))
            if not same: rc = 1
        else:
            OUT.mkdir(parents=True, exist_ok=True); dst.write_bytes(rec); print("wrote", dst.relative_to(ROOT), len(rec), "bytes")
    return rc


if __name__ == "__main__":
    sys.exit(main())
