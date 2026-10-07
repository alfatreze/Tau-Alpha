#!/usr/bin/env python3
"""B-644: the Halcyon page (fw/halcyon_page.inc and fw/halcyon.inc, the real firmware code) compiled against stub drawing primitives and a logging register interface.
Checks: every rectangle the page draws lies on the 400x360 screen; the curve stays inside its plot and is made of at most 140 rectangles; a flat setting draws a flat curve (infrasonic
roll-off aside) and a boosted one draws above the axis; control moves apply LIVE (a commit, never a clear), clamp at the ends (sibilance 0..5, others -5..+5), and the selected row wraps;
L1/R1 step through the presets inside a gain dip with a clear; X takes the engine out of the path and back; Y resets to FLAT (bypassed); with no unit the page says so and writes nothing;
and four mutants of the page source (a live move that clears, no upper clamp, sibilance allowed negative, no curve clamp) are killed."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
tmp = tempfile.TemporaryDirectory()
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

_cache = {}
def build(incdir=None):
    key = str(incdir)
    if key in _cache: return _cache[key]
    exe = Path(tmp.name) / f"hp{len(_cache)}"
    cmd = ["cc", "-std=gnu11", "-O1", "-Wall", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-misleading-indentation"] + (["-I", str(incdir)] if incdir else []) + ["-I", str(ROOT / "fw"), "-o", str(exe), str(ROOT / "sim/halcyon_page_harness.c")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); sys.exit(1)
    _cache[key] = exe
    return exe

def run(script, incdir=None):
    return subprocess.run([str(build(incdir))], input=script, capture_output=True, text=True, check=True).stdout.splitlines()

def blocks(out):
    res, cur = [], None
    for l in out:
        if l.startswith("BEGIN"): cur = []
        elif l.startswith("END"): res.append(cur); cur = None
        elif cur is not None: cur.append(l)
    return res

def rects(b): return [tuple(map(int, l.split()[1:])) for l in b if l.startswith("R ")]
def writes(b): return [(int(l.split()[1], 16) & 0xFFF, int(l.split()[2], 16)) for l in b if l.startswith("W ")]
def state(b):
    s = [l for l in b if l.startswith("S ")][-1].split()[1:]
    return dict(sel=int(s[0]), c=list(map(int, s[1:7])), hal_sel=int(s[7]), cmp=int(s[8]))

K = dict(UP=1, DOWN=2, LEFT=4, RIGHT=8, A=16, B=32, X=64, Y=128, L1=256, R1=512)
COMMIT, CLEAR = 4, 8

def script_checks(incdir=None):
    global fails
    f0 = fails
    out = run("draw\n", incdir)
    d = blocks(out)[0]
    rs = rects(d)
    check("every drawn rectangle is on the screen (400 x 360)", all(0 <= x and 0 <= y and w > 0 and h > 0 and x + w <= 400 and y + h <= 360 for x, y, w, h in rs), "(%d rectangles)" % len(rs))
    curve = [r for r in rs if r[2] == 3 and 38 <= r[1] and r[1] + r[3] <= 158 and r[0] >= 24]
    check("the curve is at most 140 three-pixel columns inside its plot (y 38..158)", 90 <= len(curve) <= 140 and all(38 <= y and y + h <= 158 for x, y, w, h in curve), "(%d columns)" % len(curve))
    # flat setting: the middle of the curve sits on the 0 dB axis (y = 98), the lowest columns dip (infrasonic roll-off)
    mid = [r for r in curve if 120 <= r[0] - 24 <= 260]
    check("a flat setting draws the curve on the 0 dB axis in the middle of the band", mid and all(abs(r[1] - 98) <= 3 for r in mid))
    # warmth +4: the low end is lifted against the top end (the preamp gives the peak back, so the whole curve sits at or just under the 0 dB axis)
    out = run("draw\nkey %d\nkey %d\nkey %d\nkey %d\ndraw\n" % (K["RIGHT"], K["RIGHT"], K["RIGHT"], K["RIGHT"]), incdir)
    bl = blocks(out)
    cur = [r for r in rects(bl[-1]) if r[2] == 3 and 38 <= r[1] and r[1] + r[3] <= 158 and r[0] >= 24]
    lows = [r[1] for r in cur if 60 <= r[0] - 24 <= 140]
    highs = [r[1] for r in cur if 330 <= r[0] - 24 <= 357]
    check("warmth +4 lifts the lows against the top end, with the curve at or just below 0 dB", lows and highs and min(highs) - min(lows) >= 4 and 96 <= min(lows) <= 108, "(low top %d, high top %d)" % (min(lows), min(highs)))
    # an extreme setting: the curve dips furthest below the axis and must still stay inside the plot
    ex = ""
    for row, key in enumerate(("RIGHT", "RIGHT", "LEFT", "LEFT", "RIGHT", "LEFT")):          # warmth +5, bass +5, vocal -5, punch -5, sibilance +5, air -5: -15.7 dB at the top end
        ex += ("key %d\n" % K["DOWN"] if row else "") + ("key %d\n" % K[key]) * 5
    ex += "draw\n"
    exb = blocks(run(ex, incdir))[-1]
    excv = [r for r in rects(exb) if r[2] == 3 and r[0] >= 24]
    check("an extreme setting keeps every curve column inside the plot", excv and all(38 <= y and y + h <= 158 for x, y, w, h in excv), "(lowest edge %d of 158)" % max(y + h for x, y, w, h in excv))
    # live moves: commit, never clear
    kk = blocks(run("key %d\nkey %d\n" % (K["RIGHT"], K["RIGHT"]), incdir))
    w = writes(kk[0])
    ctrl = [v for a, v in w if a == 0x178]
    st1 = state(kk[0])
    check("a live control move commits without a clear and selects CUSTOM", ctrl and all(v & COMMIT and not v & CLEAR for v in ctrl) and st1["c"][0] == 1 and st1["hal_sel"] == 200, "(%s)" % st1)
    # clamps and the sibilance row
    seq = "key %d\n" % K["DOWN"]
    seq += "key %d\n" % K["DOWN"] * 3                                           # row 4 = sibilance
    seq += "key %d\n" % K["LEFT"] * 3                                           # stays 0
    s0 = state(blocks(run(seq, incdir))[-1])
    seq += "key %d\n" % K["RIGHT"] * 8                                          # clamps at 5
    s = state(blocks(run(seq, incdir))[-1])
    check("sibilance clamps at 0 and 5 (and is row 4)", s0["sel"] == 4 and s0["c"][4] == 0 and s["c"][4] == 5, str(s))
    seq = "key %d\n" % K["UP"] * 1 + "key %d\n" % K["LEFT"] * 9
    s = state(blocks(run(seq, incdir))[-1])
    check("the selected row wraps upward and a normal control clamps at -5", s["sel"] == 5 and s["c"][5] == -5, str(s))
    # presets
    pb = blocks(run("key %d\nkey %d\nkey %d\n" % (K["R1"], K["R1"], K["L1"]), incdir))
    s1, s2, s3 = state(pb[0]), state(pb[1]), state(pb[2])
    wr = writes(pb[0])
    ctl = [v for a, v in wr if a == 0x178]
    check("R1 / L1 step the presets (FLAT, WARM, FLAT), each a commit WITH a clear", (s1["hal_sel"], s2["hal_sel"], s3["hal_sel"]) == (1, 2, 1) and any(v & CLEAR for v in ctl) and s2["c"][:2] == [3, 1], "(%s)" % [s1["hal_sel"], s2["hal_sel"], s3["hal_sel"]])
    out = run("key %d\nkey %d\n" % (K["R1"], K["R1"]), incdir)
    check("each preset step runs inside a gain dip", out[-1] == "DIPS 4", out[-1])
    # compare and reset
    cb = blocks(run("key %d\nkey %d\nkey %d\nkey %d\n" % (K["RIGHT"], K["X"], K["X"], K["Y"]), incdir))
    x1, x2, y1 = state(cb[1]), state(cb[2]), state(cb[3])
    off_w = [v for a, v in writes(cb[1]) if a == 0x178]
    check("X takes the engine out of the path (enable 0) and back (commit)", x1["cmp"] == 1 and off_w == [0] and x2["cmp"] == 0 and any(v & COMMIT for a, v in writes(cb[2]) if a == 0x178))
    yw = [v for a, v in writes(cb[3]) if a == 0x178]
    check("Y resets all six controls and applies FLAT bypassed", y1["c"] == [0] * 6 and yw and yw[-1] & 2 and yw[-1] & COMMIT and not yw[-1] & CLEAR, str(y1))
    # no unit
    nb = blocks(run("nounit\ndraw\nkey %d\nkey %d\n" % (K["RIGHT"], K["R1"]), incdir))
    texts = [l for l in nb[0] if l.startswith("T ") or l.startswith("F ")]
    check("with no unit the page says so and writes nothing", any("NO UNIT" in t for t in texts) and not writes(nb[1]) and not writes(nb[2]))
    return fails - f0

n = script_checks()
src = (ROOT / "fw/halcyon_page.inc").read_text()
mutants = {
    "a live move clears the state": ("hal_hw_apply_ctl_live(&hal_c);", "hal_hw_apply_ctl(&hal_c);"),
    "no upper clamp": ("if (v > 5) { v = 5; }", ""),
    "curve may leave the plot at the bottom": ("if (y > (int32_t)(HP_Y + HP_H - 2u)) y = (int32_t)(HP_Y + HP_H - 2u);", ""),
    "sibilance may go negative": ("const int32_t lo = hal_pg_sel == 4u ? 0 : -5;", "const int32_t lo = -5;"),
}
for name, (a, b) in mutants.items():
    assert a in src, name
    d = Path(tmp.name) / ("m_" + name.replace(" ", "_")); d.mkdir()
    (d / "halcyon_page.inc").write_text(src.replace(a, b, 1))
    import io, contextlib
    saved = fails
    with contextlib.redirect_stdout(io.StringIO()):
        k = script_checks(d)
    fails = saved
    print(("ok   mutant killed: " if k else "FAIL mutant survived: ") + name)
    if not k: fails += 1
sys.exit(1 if fails else 0)
