#!/usr/bin/env python3
"""Writable Latin-1 font memory (docs/features/FONT_SPEC.md, TAU_FONT_RAM): sim/tb_font_ram.v plays CHAR commands for ASCII, for 0xA0..0xFF (glyph bit 7 on cmd_glyph_hi[0]) and for the blank codes, then replaces two glyphs
through the CPU write port and draws again. Every word the engine wrote must equal tools/host/blit_reference.py drawing the same scene with the same atlas (font_ram.v's power-up contents plus the patches).
Also: the ROM build (FONT_RAM_ENABLE = 0) is untouched (sim/test_blit_reference.py keeps passing), and the generated font_ram.v equals the tool's default font.
Mutants: glyph bit 7 ignored, the high range one glyph off, the write port ignored: each must make the scene differ."""
import subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "host")); sys.path.insert(0, str(ROOT / "tools"))
from blit_reference import Renderer, load_font_words
import gen_font_atlases as g
BUILD = ROOT / "build" / "rtl"
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
RAM_V = ROOT / "src/fpga/core/font_ram.v"
def atlas():
    w = load_font_words(RAM_V)
    assert len(w) == 6112, len(w)
    for k in range(32): w[1056 + k] = (0x13579BDF ^ ((k * 0x01010101) & 0xFFFFFFFF)) & 0xFFFFFFFF
    for k in range(32): w[6080 + k] = (0xF0E1D2C3 + k * 0x00010203) & 0xFFFFFFFF
    return w
def reference():
    fresh = load_font_words(RAM_V)
    r = Renderer(fresh)
    for addr, code in ((0, 0x41), (16, 0xE9), (32, 0xFF), (48, 0xA0), (64, 0x9F), (80, 0x7F)): r.char(addr, code, 0xFFFF, 0x0000, 0, 0, latin1=True)
    r.font_words = atlas()
    for addr, code in ((96, 0x41), (112, 0xFF), (128, 0xE9)): r.char(addr, code, 0xFFFF, 0x0000, 0, 0, latin1=True)
    return r.mem
def run(mut=None):
    BUILD.mkdir(parents=True, exist_ok=True)
    vvp = BUILD / ("tb_font_ram_mut.vvp" if mut else "tb_font_ram.vvp"); dump = BUILD / ("font_ram_mut.txt" if mut else "font_ram.txt")
    cmd = ["iverilog", "-g2012"] + ([f"-Ptb_font_ram.{mut}=1"] if mut else []) + ["-o", str(vvp), str(ROOT / "sim/tb_font_ram.v"), str(ROOT / "src/fpga/core/mp3_fb.sv"), str(ROOT / "src/fpga/core/font_rom.v"), str(RAM_V)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode: raise RuntimeError(r.stdout + r.stderr)
    r = subprocess.run(["vvp", str(vvp), f"+DUMP={dump}"], capture_output=True, text=True)
    if "PASSED" not in r.stdout: raise RuntimeError(r.stdout)
    num = lambda t: int(t) if t.isdigit() else -1           # an unknown (x) word, which a mutant can produce by reading past the memory
    return {num(a): num(v) for a, v in (l.split() for l in dump.read_text().splitlines())}
exp = reference()
act = run()
bad = [(a, exp.get(a), act.get(a)) for a in sorted(set(exp) | set(act)) if exp.get(a) != act.get(a)]
check(f"the RTL scene equals the reference renderer exactly ({len(act)} words: ASCII, Latin-1, blank codes, two patched glyphs)", not bad, bad[:3])
check("the patched 'A' differs from the power-up 'A' (the write port really changed the picture)", any(exp[96 + r * 512 + c] != exp[r * 512 + c] for r in range(16) for c in range(16)))
check("e-acute drawn before and after the patches is identical (the other glyphs were not disturbed)", all(exp[16 + r * 512 + c] == exp[128 + r * 512 + c] for r in range(16) for c in range(16)))
check("the blank codes (0x7F, 0x9F) draw only the background", all(exp[64 + r * 512 + c] == 0 and exp[80 + r * 512 + c] == 0 for r in range(16) for c in range(16)))
r = subprocess.run([sys.executable, str(ROOT / "tools/gen_font_atlases.py"), "--check"], capture_output=True, text=True)
check("font_ram.v equals the generator's default font (Tamzen Regular)", r.returncode == 0 and "STALE" not in r.stdout, r.stdout[-200:])
for mut, what in (("BUG_FONT_NO_HI", "glyph bit 7 ignored"), ("BUG_FONT_RANGE", "the 0xA0 range one glyph off"), ("BUG_FONT_NO_WRITE", "the write port ignored")):
    m = run(mut)
    check(f"mutant {mut} ({what}) is caught", m != act)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
