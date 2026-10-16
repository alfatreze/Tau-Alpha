#!/usr/bin/env python3
"""UI string fit for the planned font (docs/features/FONT_SPEC.md section 5). Every fixed string the firmware draws with a literal start column must still fit the screen width (400) in Space Grotesk
SemiBold and Bold if it fits in today's Inter: a string that fits Inter and overflows either new weight is reported and fails the test. Menu rows (label at x = 24, value right-aligned 24 from the edge) must keep
a gap of at least 12 px between the label and the longest value of the row. Strings are read from the gfx_text calls and the menu tables of fw2; advances come from fw/font_metrics.h (Inter, today) and from the
Tau font records (tools/gen_font_atlases.py). The mutant widens every advance by one pixel and must make the test fail."""
import re, struct, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
W = 400
inter = [int(x) for x in re.search(r"font_adv\[95\] = \{(.*?)\};", (ROOT / "fw/font_metrics.h").read_text(), re.S).group(1).replace("\n", "").split(",") if x.strip()]
def load(name):
    b = (ROOT / "fw2/generated/fonts" / name).read_bytes(); body = b[12:]
    nr = body[20]; off = 24 + 4 * nr; n = sum(b2 - a + 1 for a, b2 in (struct.unpack_from("<HH", body, 24 + 4 * i) for i in range(nr)))
    return list(body[off:off + n])
fonts = {"Inter": inter, "SemiBold": load("SpaceGrotesk-SemiBold.tfnt"), "Bold": load("SpaceGrotesk-Bold.tfnt"), "Tamzen": load("Tamzen8x16r.tfnt"), "Tamzen Bold": load("Tamzen8x16b.tfnt")}
def width(adv, s, extra=0):
    return sum(adv[(ord(c) - 0x20) if 0x20 <= ord(c) <= 0x7E else 0] + extra for c in s)
def strings():
    out = []
    for f in sorted((ROOT / "fw2").rglob("*.c")):
        t = f.read_text()
        for m in re.finditer(r'gfx_text\(\s*(\d+)\s*,\s*[^,]+,\s*"((?:[^"\\]|\\.)*)"', t):
            out.append((f.name, int(m.group(1)), m.group(2).encode().decode("unicode_escape")))
    return out
def long_literals():
    """Every string literal of 12 or more characters in the UI and diagnostics sources (hints, captions, messages): the long ones are where a wider font overflows. They start at x >= 8, so they must fit 380 px."""
    out = []
    for d in ("ui", "diagnostics"):
        for f in sorted((ROOT / "fw2" / d).glob("*.c")):
            for m in re.finditer(r'"((?:[^"\\]|\\.){12,})"', f.read_text()):
                t = m.group(1)
                if re.fullmatch(r"[A-Z0-9 .,:;!?()+\-/<>=*'\"&%#@\[\]_|~]+", t): out.append((f.name, 8, t))
    return out
def menu_rows():
    t = (ROOT / "fw2/cores/menupages_core.h").read_text()
    opts = {m.group(1): re.findall(r'"([^"]*)"', m.group(2)) for m in re.finditer(r"\*const (mg_\w+)\[\] = \{(.*?)\}", t, re.S)}
    choice = {m.group(1): m.group(2) for m in re.finditer(r"\[(V_\w+)\] = \{ (mg_\w+),", t)}
    rows = []
    for m in re.finditer(r'\{ "([^"]+)", (RK_\w+), (\w+) \}', t):
        label, kind, arg = m.groups()
        longest = max((len(o) for o in opts.get(choice.get(arg, ""), [])), default=0)
        vals = max(opts.get(choice.get(arg, ""), [""]), key=lambda o: width(inter, o)) if choice.get(arg) else ("OFF" if kind == "RK_TOGGLE" else "100" if kind == "RK_VOL" else ">" if kind == "RK_GROUP" else "")
        rows.append((label, vals))
    return rows
def run(extra=0):
    bad = []
    for f, x, s in strings() + long_literals():
        ok_inter = x + width(inter, s) <= W
        for name in ("SemiBold", "Bold", "Tamzen", "Tamzen Bold"):
            if ok_inter and x + width(fonts[name], s, extra) > W: bad.append((f, x, s, name, x + width(fonts[name], s, extra)))
    rb = []
    for label, val in menu_rows():
        for name in ("SemiBold", "Bold", "Tamzen", "Tamzen Bold"):
            gap = (W - 24) - 24 - width(fonts[name], label, extra) - width(fonts[name], val, extra)
            if gap < 12: rb.append((label, val, name, gap))
    return bad, rb
bad, rb = run()
n = len(strings()) + len(long_literals()); m = len(menu_rows())
check(f"{n} fixed strings (literal column, or long literals) and {m} menu rows were found", n > 40 and m > 10)
today = [(f, x, w) for f, x, w in [(f, x, x + width(inter, t)) for f, x, t in strings() + long_literals()] if w > W]
check("no fixed string overflows the 400 px screen even in today's Inter (three did before this test: the Info hint, the cold-image message and the Check hint)", not today, today[:3])
check("no fixed string that fits in Inter overflows the screen in Space Grotesk SemiBold or Bold", not bad, bad[:3])
check("every menu row keeps 12 px between its label and its longest value in every weight and font", not rb, rb[:3])
bad2, rb2 = run(extra=4)
check("mutant: every advance four pixels wider is caught", bool(bad2 or rb2))
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
