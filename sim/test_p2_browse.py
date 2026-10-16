#!/usr/bin/env python3
"""Player 2 library browser navigation (fw2/cores/browse_core.h): the C core equals an independent Python model over random key scripts on a synthetic library (5 artists, 10 albums, 43 tracks,
2 playlists, one of them empty); the selection is always inside the visible window and the window never runs past the end; up/down wrap; Back restores the parent's selection and window exactly;
Enter on a track row says PLAY with the right track id. Two mutants must be caught."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)) if (extra and not ok) else ""))
    if not ok: fails += 1
ART = [3, 0, 2, 4, 1]
def tr(a): return 3 + a % 4
def afirst(a): return sum(ART[:a])
def tfirst(a): return sum(tr(i) for i in range(a))
NT = tfirst(10)
H, AR, AL, ALO, TO, T, L, TL = range(8)
def count(k, p): return {H: 5, AR: 5, AL: 10, ALO: ART[p] if k == ALO else 0, TO: tr(p) if k == TO else 0, T: NT, L: 2, TL: 4 if p == 0 else 0}[k]
def child(k, p, pos): return {AR: pos, AL: 9 - pos, ALO: afirst(p) + pos if k == ALO else 0, TO: tfirst(p) + pos if k == TO else 0, T: NT - 1 - pos, L: pos, TL: 100 + pos}[k]
class B:
    def __init__(s, rows): s.rows = rows; s.st = [[H, 0, 0, 0, 5]]
    def cur(s): return s.st[-1]
    def fix(s):
        l = s.cur()
        if not l[4]: l[2] = l[3] = 0; return
        l[2] = min(l[2], l[4] - 1)
        if l[2] < l[3]: l[3] = l[2]
        if l[2] >= l[3] + s.rows: l[3] = l[2] + 1 - s.rows
        l[3] = min(l[3], max(l[4] - s.rows, 0))
    def move(s, d):
        l = s.cur()
        if not l[4]: return
        l[2] = (0 if l[2] + 1 >= l[4] else l[2] + 1) if d > 0 else (l[4] - 1 if l[2] == 0 else l[2] - 1); s.fix()
    def page(s, d):
        l = s.cur()
        if not l[4]: return
        l[2] = (l[4] - 1 if l[2] + s.rows >= l[4] else l[2] + s.rows) if d > 0 else (0 if l[2] < s.rows else l[2] - s.rows); s.fix()
    def back(s):
        if len(s.st) > 1: s.st.pop()
    def enter(s):
        l = s.cur(); k, p, sel = l[0], l[1], l[2]
        if not l[4]: return (0, 0, 0)
        if k == H:
            if sel == 3: return (3, 0, sel)
            nk = [AR, AL, T][sel] if sel < 3 else L; par = 0
        elif k == AR: nk, par = ALO, child(k, p, sel)
        elif k in (AL, ALO): nk, par = TO, child(k, p, sel)
        elif k == L: nk, par = TL, child(k, p, sel)
        else: return (2, child(k, p, sel), sel)
        if len(s.st) + 0 >= 4: return (0, 0, 0)
        s.st.append([nk, par, 0, 0, count(nk, par)]); return (1, 0, sel)
def build(d, bug):
    exe = d / f"b{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_browse_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def script(seed):
    r = random.Random(seed); rows = r.choice([1, 3, 7, 10]); out = [f"init {rows}"]
    for _ in range(r.randrange(20, 200)): out.append(r.choice(["up", "down", "down", "pgup", "pgdn", "enter", "enter", "back"]))
    return rows, out
def run(exe, cmds): return subprocess.run([str(exe)], input="\n".join(cmds) + "\n", capture_output=True, text=True).stdout.strip().split("\n")
def model(rows, cmds):
    b = B(rows); out = []
    for c in cmds[1:]:
        act = (0, 0, 0)
        if c == "up": b.move(-1)
        elif c == "down": b.move(1)
        elif c == "pgup": b.page(-1)
        elif c == "pgdn": b.page(1)
        elif c == "enter": act = b.enter()
        elif c == "back": b.back()
        l = b.cur(); out.append((len(b.st) - 1, l[0], l[1], l[2], l[3], l[4], act))
    return out, b
def agrees(exe):
    for sd in range(300):
        rows, cmds = script(sd); got = run(exe, cmds)[1:]; want, b = model(rows, cmds)
        for g, w in zip(got, want):
            f = list(map(int, g.split()))
            if tuple(f[:6]) != w[:6] or f[6] != w[6][0]: return False
            if f[6] in (2,) and (f[7], f[8]) != (w[6][1], w[6][2]): return False
            l_sel, l_top, cnt = f[3], f[4], f[5]
            if cnt and not (l_top <= l_sel < l_top + rows): return False          # the selection is visible
            if l_top > max(cnt - rows, 0): return False                           # the window never runs past the end
    return True
def back_restores(exe):
    out = run(exe, ["init 4", "down", "down", "down", "enter", "down", "down", "enter", "back", "back"])
    # Home sel 3 is the Shuffle All row: no push; use the Albums row instead
    out = run(exe, ["init 4", "down", "enter", "pgdn", "down", "enter", "back", "back"])
    h = list(map(int, out[0].split())) ; return out[-1].split()[:6] == ["0", "0", "0", "1", "0", "5"] and out[-2].split()[0] == "1" and out[-2].split()[3] > "0"
with tempfile.TemporaryDirectory() as td:
    d = Path(td); g = build(d, 0)
    check("C core equals the independent model over 300 random key scripts (and the window invariants hold)", agrees(g))
    check("Back restores the parent's selection", back_restores(g))
    pl = run(g, ["init 4", "down", "down", "enter", "enter"])
    check("Enter on a track row plays the right track", pl[-1].split()[6] == "2" and pl[-1].split()[7] == str(NT - 1))
    sh = run(g, ["init 4", "down", "down", "down", "enter"]); check("Home row 4 is Shuffle All", sh[-1].split()[6] == "3")
    for bug, name in ((1, "window does not follow the selection"), (2, "back forgets the selection")):
        b = build(d, bug); check(f"mutant {bug} ({name}) is caught", not agrees(b) or not back_restores(b))
print("FAILED" if fails else "all ok", fails); sys.exit(1 if fails else 0)
