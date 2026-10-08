#!/usr/bin/env python3
"""tools/tau_version.py: the full version stamped into a Tau ROM (splash, Info > FIRMWARE). Synthetic ROM cases, then a real ROM built by
this suite (work/heapcheck/release/tau.rom) when present."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_version as tv  # noqa: E402

fails = 0
def check(cond, what):
    global fails
    print(("ok   " if cond else "FAIL ") + what)
    fails += 0 if cond else 1

def refused(fn, what, needle):
    try:
        fn()
    except tv.VersionError as e:
        check(needle in str(e), f"{what} (refused: {e})")
        return
    check(False, f"{what} (was NOT refused)")

field = b"TAUVER:0.6.0" + b"\0" * (tv.FIELD - 12)
rom = b"\x13" * 100 + field + b"\x37" * 100
check(tv.read(rom) == "0.6.0", "an unstamped ROM reads its built APP_VER")
s = tv.stamp(rom, "0.6.0-preview.1+f2d3373")
check(tv.read(s) == "0.6.0-preview.1+f2d3373", "a stamp reads back")
check(len(s) == len(rom) and s[:100] == rom[:100] and s[100 + tv.FIELD:] == rom[100 + tv.FIELD:], "only the field changes")
check(tv.read(tv.stamp(s, "0.6.0")) == "0.6.0", "a shorter stamp clears the rest of the field")
check(tv.full_version("0.6.0-dev.385", "f2d3373.dirty") == "0.6.0-dev.385+f2d3373.dirty", "SemVer + build metadata")
refused(lambda: tv.full_version("0.6.0-preview.1234567890", "f2d3373abcdef0123456"), "a version over the 40-character field", "holds 40")
refused(lambda: tv.full_version("v0.6.0", None), "a 'v'-prefixed or malformed version", "not a version")
refused(lambda: tv.full_version("0.6.0-dev 5", None), "a version with a space", "not a version")
refused(lambda: tv.stamp(b"\0" * 300, "0.6.0"), "a ROM without the field (built before 2026-10-08)", "no TAUVER field")
refused(lambda: tv.stamp(rom + field, "0.6.0"), "a ROM with two fields", "more than one")
check(tv.read(b"\0" * 300) is None, "reading a ROM without the field gives None")
check(tv.SEMVER.fullmatch("0.6.0-dev.barcode.5") is not None, "dev barcode labels are valid SemVer")

real = ROOT / "work/heapcheck/release/tau.rom"
if real.is_file() and real.stat().st_mtime < (ROOT / "fw/player.c").stat().st_mtime:
    print("note: work/heapcheck/release/tau.rom is older than fw/player.c (rebuilt later by check_heap_gap); real-ROM check skipped")
elif real.is_file():
    b = real.read_bytes()
    app = next(l.split('"')[1] for l in (ROOT / "fw/player.c").read_text().splitlines() if l.startswith("#define APP_VER "))
    check(tv.read(b) == app, f"the firmware built by this suite carries the TAUVER field with APP_VER ({tv.read(b)!r})")
    st = tv.stamp(b, "0.6.0-preview.1+abc1234")
    check(sum(x != y for x, y in zip(b, st)) <= tv.MAX_TEXT and tv.read(st) == "0.6.0-preview.1+abc1234", "stamping the real ROM changes only the field")
else:
    print("note: work/heapcheck/release/tau.rom not built yet; real-ROM check skipped")

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
