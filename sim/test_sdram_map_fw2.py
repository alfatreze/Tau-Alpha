#!/usr/bin/env python3
"""The off-screen SDRAM rows of Player 2 (T0 of the Talos plan): tools/sdram_map.json is the register, this test keeps it true.
It (1) reads each named C constant and compares it with the register, (2) refuses any two bands that overlap, any band outside rows 360..1023
or inside the marker columns, and any marker cell that sits in a band or on another cell unless the collision is listed as known, and
(3) checks the display buffer constants of the RTL and the firmware agree."""
import json, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
m = json.loads((ROOT / "tools/sdram_map.json").read_text())
fails = 0
def check(c, what):
    global fails
    print(("ok   " if c else "FAIL ") + what)
    fails += 0 if c else 1
def define(path, name):
    s = (ROOT / path).read_text()
    r = re.search(r"#define\s+" + re.escape(name) + r"\s+\(?(\d+)u?\)?", s)
    return int(r.group(1)) if r else None
bands = m["bands"]
for b in bands:
    f, n, v = b["const"]
    if v is not None:
        got = define(f, n)
        check(got == v == b["rows"][0], f"{b['name']}: {n} in {f} is {got}, the register says {v} and its rows start at {b['rows'][0]}")
for i, a in enumerate(bands):
    check(m["visible_rows"] <= a["rows"][0] < a["rows"][1] <= m["total_rows"], f"{a['name']} rows {a['rows']} lie in the stash ({m['visible_rows']}..{m['total_rows']})")
    check(0 <= a["cols"][0] < a["cols"][1] <= m["pixel_cols"], f"{a['name']} columns {a['cols']} stay out of the marker columns (>= {m['pixel_cols']})")
    for b in bands[i + 1:]:
        ov = a["rows"][0] < b["rows"][1] and b["rows"][0] < a["rows"][1] and a["cols"][0] < b["cols"][1] and b["cols"][0] < a["cols"][1]
        check(not ov, f"{a['name']} and {b['name']} do not overlap")
known = {frozenset((k["a"], k["b"])) for k in m["known_collisions"]}
cells = m["cells"]
for i, a in enumerate(cells):
    for b in cells[i + 1:]:
        same = (a["row"], a["col"]) == (b["row"], b["col"])
        if same:
            if frozenset((a["name"], b["name"])) in known:
                print(f"KNOWN {a['name']} and {b['name']} share cell ({a['row']},{a['col']}): listed in known_collisions")
            else:
                check(False, f"{a['name']} and {b['name']} share cell ({a['row']},{a['col']})")
    for b in bands:
        inside = b["rows"][0] <= a["row"] < b["rows"][1] and b["cols"][0] <= a["col"] < b["cols"][1]
        check(not inside, f"cell {a['name']} ({a['row']},{a['col']}) is not inside band {b['name']}")
for k in m["known_collisions"]:
    names = {c["name"] for c in cells}
    check(k["a"] in names and k["b"] in names, f"known collision {k['a']} / {k['b']} names real cells")
fb = (ROOT / "src/fpga/core/mp3_fb.sv").read_text()
rtl = int(re.search(r"DBUF_BASE1\s*=\s*25'd(\d+)", fb).group(1))
c = (ROOT / "fw2/gfx/gfx.c").read_text()
cw = int(re.search(r"DBUF_BASE1_W\s+(\d+)u", c).group(1))
check(rtl == cw == m["double_buffer"]["base_words"], f"DBUF_BASE1: RTL {rtl}, fw2 {cw}, register {m['double_buffer']['base_words']}")
check(m["double_buffer"]["base_words"] >= m["total_rows"] * m["stride"], "display buffer 1 starts above buffer 0 and its stash (1024 rows x 512)")
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
