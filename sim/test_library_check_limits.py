#!/usr/bin/env python3
"""D-T20: tools/library_check.py (used by tools/sync_media.py: jpeg_dims, verdict) no longer reads the retired old player (fw/art.inc, fw/player.c).
Its four limits are pinned constants with the values the old player's headers held (ART_MAX_BYTES 2 MiB, ART_MAX_SRC_W 320, ART_FULL_MAX 1024, ART_IMG 128);
the test checks the values and that importing the module opens no file under fw/."""
import builtins, importlib, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
fails = 0
def check(msg, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + msg)
    fails += not cond

opened = []
real_open = builtins.open
def spy(f, *a, **k):
    if isinstance(f, (str, Path)) and "/fw/" in str(f).replace("\\", "/"):
        opened.append(str(f))
    return real_open(f, *a, **k)
builtins.open = spy
try:
    lib = importlib.import_module("library_check")
finally:
    builtins.open = real_open
check("importing library_check opens no file under fw/", not opened)
check("limits: ART_MAX_BYTES, ART_MAX_SRC_W, ART_FULL_MAX, ART_IMG = 2 MiB, 320, 1024, 128",
      (lib.ART_MAX_BYTES, lib.ART_MAX_SRC_W, lib.ART_FULL_MAX, lib.ART_IMG) == (2 * 1024 * 1024, 320, 1024, 128))
check("jpeg_dims still reads a baseline JPEG header", lib.jpeg_dims(bytes([0xFF, 0xD8, 0xFF, 0xC0, 0, 17, 8, 0, 90, 0, 120, 3, 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1])) == (120, 90, False))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
