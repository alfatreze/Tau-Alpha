#!/usr/bin/env python3
"""B-565: tools/ram_report.py parsing, grouping and diff, on fixture text (no toolchain needed)."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import ram_report as rr  # noqa: E402

READELF = """
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 1] .text             PROGBITS        00000000 001000 001000 00  AX  0   0  4
  [ 2] .rodata           PROGBITS        00001000 002000 000200 00   A  0   0  4
  [ 3] .data             PROGBITS        00001200 002200 000010 00  WA  0   0  4
  [ 4] .bss              NOBITS          00001210 002210 000400 00  WA  0   0  8
  [ 5] .cold_text        PROGBITS        24800000 003000 000800 00  AX  0   0  4
"""
NM_A = """00000000 00000400 T main
00000400 00000100 T xmp3_IMDCT
00001000 00000200 R xmp3_huffTable
00001210 00000300 b arena
24800000 00000300 t chk_do
00000000 00000000 T zero_size
"""
NM_B = """00000000 00000480 T main
00000400 00000100 T xmp3_IMDCT
00000500 00000200 T stress_hud_draw
00001000 00000200 R xmp3_huffTable
00001210 00000300 b arena
00001510 00000040 b dup
00001550 00000040 b dup
24800000 00000500 t chk_do
"""
fails = 0


def check(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += not cond


sec = rr.parse_sections(READELF)
check(sec[".text"] == (0, 0x1000) and sec[".cold_text"][0] == 0x24800000, "sections parsed")
a, b = rr.parse_symbols(NM_A, sec), rr.parse_symbols(NM_B, sec)
check(a["arena"] == (".bss", 0x300) and a["chk_do"][0] == ".cold_text", "symbols placed in their sections")
check("zero_size" not in a, "zero-size symbols skipped")
check(b["dup"] == (".bss", 0x80), "same-name statics add up")
check(rr.group_of("xmp3_IMDCT") == "helix mp3" and rr.group_of("stress_hud_draw") == "diag/check"
      and rr.group_of("arena") == "audio buffers" and rr.group_of("poll_input") == "main/other", "feature groups")
out = rr.diff((sec, a), (sec, b))
check("stress_hud_draw" in out and "(new)" in out and "+128" in out, "diff lists a new hot symbol and a grown one")
check("chk_do" not in out.split("hot symbols that changed")[-1], "cold growth is not listed as a hot symbol")
rep = rr.report(sec, b, top=3)
check("hot total" in rep and "by feature group" in rep, "report renders")
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
