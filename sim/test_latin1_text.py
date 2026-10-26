#!/usr/bin/env python3
"""Latin-1 text (docs/features/FONT_SPEC.md 3.6): tools/tau_library.py latin1_text() and the tag decoder, and the firmware's draw-time fold table (fw2/gfx/gfx.c) that shows 0xA0..0xFF as the plain letter or sign
until the Latin-1 font lands. Checks: accents survive, typography folds, the unmappable become '?', an all-'?' text is empty (the caller falls back to the file name), nothing is split, a UTF-8 tag under ID3 encoding 0 is
read as UTF-8 and a real Latin-1 / Windows-1252 one is not, the firmware maps both ranges to the engine's glyph index. Mutants: no UTF-8 guess, no fold of curly quotes."""
import re, sys, unicodedata
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_library as tl
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
L = tl.latin1_text
check("accented Latin-1 letters stay as they are", L("Café Tacvba – Ñandú Ö Å ß") == "Café Tacvba - Ñandú Ö Å ß", L("Café Tacvba – Ñandú Ö Å ß"))
check("curly quotes, dashes and the ellipsis fold; the no-break space is a space", L("It’s “fine” — really… ok") == 'It\'s "fine" - really... ok' and L("a b") == "a b")
check("a letter that decomposes to a plain letter keeps that letter (ź is z); one with no Latin-1 form becomes '?' (Ł)", L("Łódź") == "?ódz", L("Łódź"))
check("a character with no Latin-1 form inside a text is '?'; a text of only such characters is empty", L("a日b") == "a?b" and L("Я") == "")
check("a text with nothing real left is empty (so the file name is used)", L("日本語") == "" and L("???") == "")
check("combining marks fold into the base letter (NFC), control characters vanish, runs of spaces collapse", L("éx") == "éx" and L("a\x00b\x1f  c") == "ab c")
check("the limit counts characters and never splits one", L("é" * 100, 10) == "é" * 10 and len(L("é" * 100, 10).encode("latin-1")) == 10)
check("every result encodes as Latin-1 (the index stores one byte per character)", all(L(s).encode("latin-1") is not None for s in ("Å ß ø", "日本語a", "€ 5", "™")))
D = tl._dec
check("ID3 encoding 0 holding UTF-8 bytes is read as UTF-8", D(0, "Café".encode("utf-8")) == "Café")
check("ID3 encoding 0 holding Windows-1252 bytes (not valid UTF-8) is read as Windows-1252", D(0, "Café – x".encode("cp1252")) == "Café – x")
check("ID3 encoding 0 holding plain ASCII is unchanged; UTF-16 and UTF-8 encodings still decode", D(0, b"abc") == "abc" and D(1, "é".encode("utf-16")) == "é" and D(3, "é".encode("utf-8")) == "é")
check("sorting treats a marked letter as its plain letter", tl.sort_key("Édith")[0] == 5 and tl.sort_key("Ñandú")[0] == 14 and tl.sort_key("Édith")[0] == tl.sort_key("Edith")[0])
src = (ROOT / "fw2/gfx/gfx.c").read_text()
check("the firmware draws U+0020..U+007E and U+00A0..U+00FF as real glyphs (no fold table: the font memory has them) and everything else as a space",
      "fold_a0" not in src and "c >= 0xA0u" in src and "(unsigned char)ch" in src)
check("the glyph code goes to the engine as 8 bits: bits 6:0 in R_FB_GO bits 9:3, bit 7 on bit 15", "((g & 0x7Fu) << 3) | ((g >> 7) << 15)" in src)
check("the advance index maps 0x20..0x7E to 0..94 and 0xA0..0xFF to 95..190, as the engine's glyph index does", "g < 0x80u ? g - 0x20u : g - 0xA0u + 95u" in src)
# mutants: the checks above must be able to fail
def mutant_dec(enc, b): return b.decode("latin-1") if enc == 0 else tl._dec(enc, b)
check("mutant (no UTF-8 guess) is caught", mutant_dec(0, "Café".encode("utf-8")) != "Café")
orig = dict(tl._TYPO); tl._TYPO.pop(0x2019); ok_mut = L("It’s") != "It's"; tl._TYPO.update(orig)
check("mutant (curly quote not folded) is caught", ok_mut)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
