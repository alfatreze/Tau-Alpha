#!/usr/bin/env python3
"""D-T20: tools/gen_themes.py reads the accent palette from Player 2's fw2/ui/theme.c, not from the retired old player (fw/player.c).

  1. palette() equals the 19 (name, RGB565) pairs the old player's ui_palette[] held (frozen below: the values the shipped device offers);
  2. palette(root) follows its source: a copy of the tree whose fw2/ui/theme.c has one colour changed gives a different palette (so the check really reads fw2),
     and a copy without fw/player.c still works (nothing reads the old player any more);
  3. a palette table whose name list and colour list differ in length is refused."""
import re, shutil, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_themes as gt

fails = 0
def check(msg, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += not cond

FROZEN = [("BLACK", 0x18C3), ("WHITE", 0xF7BE), ("GLOW", 0x2A83), ("TRANS_CLEAR", 0xE73B), ("TRANS_SMOKE", 0x8C51), ("TRANS_RED", 0xD1E6), ("TRANS_ORANGE", 0xE429),
          ("TRANS_GREEN", 0x5D0D), ("TRANS_BLUE", 0x4BD4), ("TRANS_PURPLE", 0x9BF6), ("CLASSIC_YELLOW", 0xFEA0), ("CLASSIC_ORANGE", 0xE429), ("CLASSIC_RED", 0xD1E6),
          ("CLASSIC_PINK", 0xE4B6), ("CLASSIC_BLUE", 0x4BD4), ("CLASSIC_GREEN", 0x5D0D), ("CLASSIC_INDIGO", 0x6AD2), ("CLASSIC_SILVER", 0xBDF7), ("ALUMINUM", 0x8C71)]
check("palette() equals the frozen 19-colour list", gt.palette() == FROZEN)

with tempfile.TemporaryDirectory() as td:
    t = Path(td)
    (t / "fw2/ui").mkdir(parents=True)
    src = (ROOT / "fw2/ui/theme.c").read_text()
    shutil.copy(ROOT / "fw2/ui/theme.c", t / "fw2/ui/theme.c")
    check("a tree with no fw/player.c still gives the palette", gt.palette(t) == FROZEN)
    (t / "fw2/ui/theme.c").write_text(src.replace("0x18C3u", "0x1234u", 1))
    p = gt.palette(t)
    check("changing a colour in fw2/ui/theme.c changes palette(root)", p[0] == ("BLACK", 0x1234) and p[1:] == FROZEN[1:])
    (t / "fw2/ui/theme.c").write_text(src.replace('"ALUMINUM" };', '};', 1))
    try:
        gt.palette(t); refused = False
    except SystemExit:
        refused = True
    check("a name list shorter than the colour list is refused", refused)

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
