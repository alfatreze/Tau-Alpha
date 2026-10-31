#!/usr/bin/env python3
"""fw2/cores/rtc_core.h: the wall clock the Report shows (boot time from the host's BCD date / time words, plus the uptime) against Python's datetime, over random boots and uptimes (carries
through minutes, hours, days, months, years, leap years and the year-2000 rule), the validation of the words (never sent, bad BCD digits, impossible dates), and the three mutants."""
import random, subprocess, sys, tempfile
from datetime import datetime, timedelta
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
def bcd(v, digits): return int("".join(str((v // 10 ** i) % 10) for i in reversed(range(digits))), 16)
def words(dt, dow=3): return bcd(dt.year, 4) << 16 | bcd(dt.month, 2) << 8 | bcd(dt.day, 2), dow << 24 | bcd(dt.hour, 2) << 16 | bcd(dt.minute, 2) << 8 | bcd(dt.second, 2)
def run(d, bug, lines):
    exe = d / f"r{bug}"
    if not exe.exists():
        r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_rtc_harness.c")], capture_output=True, text=True)
        if r.returncode: print(r.stderr); sys.exit(1)
    return subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True).stdout.split("\n")[:len(lines)]
rnd = random.Random(7)
cases = []
for _ in range(3000):
    dt = datetime(2000, 1, 1) + timedelta(seconds=rnd.randrange(0, 100 * 365 * 86400 - 400 * 86400))
    up = rnd.choice([0, 1, 59, 60, 3599, 3600, 86399, 86400, rnd.randrange(0, 90 * 86400), rnd.randrange(0, 400 * 86400)])
    cases.append((dt, up))
for dt, up in [(datetime(2026, 12, 31, 23, 59, 59), 1), (datetime(2028, 2, 28, 23, 59, 59), 1), (datetime(2100 - 1, 12, 31, 23, 59, 0), 61), (datetime(2000, 2, 28, 12, 0, 0), 86400), (datetime(2024, 2, 29, 0, 0, 0), 365 * 86400)]:
    cases.append((dt, up))
lines = []
for dt, up in cases: dd, tt = words(dt); lines.append(f"1 {dd:08x} {tt:08x} {up}")
def model(dt, up):
    e = dt + timedelta(seconds=up)
    return e.strftime("%Y-%m-%d %H:%M:%S") if e.year < 10000 else None
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    out = run(d, 0, lines)
    bad = [(l, o, model(dt, up)) for l, o, (dt, up) in zip(lines, out, cases) if o != model(dt, up)]
    check(f"{len(cases)} boots + uptimes agree with datetime (leap years, month and year carries, up to 400 days)", not bad, bad[:3])
    dd, tt = words(datetime(2026, 10, 31, 12, 34, 56), dow=6)
    check("the day-of-week nibble is ignored", run(d, 0, [f"1 {dd:08x} {tt:08x} 0"])[0] == "2026-10-31 12:34:56")
    inv = ["0 20261031 00123456 0",               # not valid (the command has not come)
           "1 00000000 00000000 0",               # sent as zeros
           "1 20261331 00123456 0",               # month 13
           "1 20260230 00123456 0",               # 30 February
           "1 20250229 00123456 0",               # 29 February of a common year
           "1 20261031 00240000 0",               # hour 24
           "1 20261031 00126000 0",               # minute 60
           "1 20261031 0012345A 0",               # not a BCD digit
           "1 2026A031 00123456 0",
           "1 19991231 00123456 0",               # before 2000
           "1 21000101 00123456 0"]               # after 2099
    o = run(d, 0, inv)
    check("a clock never sent, zero, or with impossible / non-BCD fields reads '-'", o == ["-"] * len(inv), o)
    check("29 February of a leap year is real", run(d, 0, ["1 20240229 00000000 0", "1 20000229 00000000 86400"]) == ["2024-02-29 00:00:00", "2000-03-01 00:00:00"])
    for b, nm in {1: "the 400-year exception is missing (2000)", 2: "BCD tens counted as sixteens", 3: "February never has 29 days"}.items():
        ob = run(d, b, lines + inv + ["1 20240229 00000000 0"])
        good = out + o + ["2024-02-29 00:00:00"]
        check(f"mutant caught: {nm}", ob != good)
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
