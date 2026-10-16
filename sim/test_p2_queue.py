#!/usr/bin/env python3
"""Player 2 queue (fw2/cores/queue_core.h): the C core equals an independent Python statement of the spec over random command scripts; the shuffle is a permutation,
keeps the playing track when asked to, is reproducible from its seed and (with keep_current off) equals the library's lib_queue_shuffle_all bit for bit; repeat modes,
the wrap seam and the shuffle history behave as specified. Five mutants must each be caught."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1
M = 0xFFFFFFFF
def rng(s):
    x = s[0] or 0x9E3779B9
    x ^= (x << 13) & M; x ^= x >> 17; x ^= (x << 5) & M; s[0] = x; return x
def py_shuffle(ids, seed, from_=0):
    s = [seed]; m = len(ids) - from_
    for i in range(m, 1, -1):
        j = rng(s) % i; a, b = from_ + i - 1, from_ + j; ids[a], ids[b] = ids[b], ids[a]

class Q:                                                  # an independent statement of the spec
    def __init__(s): s.ids = []; s.pos = 0; s.rep = 0; s.sh = 0; s.seed = 0; s.hist = []
    def set(s, n): s.ids = list(range(n)); s.orig = list(range(n)); s.pos = 0; s.sh = 0; s.hist = []
    def cur(s): return s.ids[s.pos] if s.ids else 0xFFFF
    def shuffle(s, seed, keep):
        frm = 0
        if keep and s.ids: s.ids[0], s.ids[s.pos] = s.ids[s.pos], s.ids[0]; frm = 1; s.pos = 0
        s.seed = seed; s.sh = 1; py_shuffle(s.ids, seed, frm)
    def restore(s):
        c = s.cur(); s.ids = list(s.orig); s.sh = 0; s.pos = s.ids.index(c) if c in s.ids else 0
    def push(s, i):
        if len(s.hist) == 32: s.hist.pop(0)
        s.hist.append(i)
    def peek(s, k):
        if not s.ids: return 0xFFFF
        if s.rep == 2: return s.ids[s.pos]
        p = s.pos + k
        if p >= len(s.ids):
            if s.rep != 1: return 0xFFFF
            p %= len(s.ids)
        return s.ids[p]
    def next(s, auto):
        if not s.ids: return 0xFFFF
        if auto and s.rep == 2: return s.ids[s.pos]
        if s.pos + 1 >= len(s.ids):
            if s.rep == 0: return 0xFFFF
            if s.sh:
                last = s.ids[s.pos]; s.shuffle((s.seed + 0x9E3779B9) & M, 0)
                if len(s.ids) > 1 and s.ids[0] == last: s.ids[0], s.ids[1] = s.ids[1], s.ids[0]
                s.pos = 0; s.push(last); return s.ids[0]
            s.push(s.ids[s.pos]); s.pos = 0; return s.ids[0]
        s.push(s.ids[s.pos]); s.pos += 1; return s.ids[s.pos]
    def prev(s):
        if not s.ids: return 0xFFFF
        if s.sh and s.hist:
            i = s.hist.pop(); s.pos = s.ids.index(i); return i
        if s.pos == 0:
            if s.rep == 0: return 0xFFFF
            s.pos = len(s.ids) - 1; return s.ids[s.pos]
        if not s.sh and s.hist: s.hist.pop()
        s.pos -= 1; return s.ids[s.pos]
    def goto(s, i):
        if i in s.ids: s.push(s.ids[s.pos]); s.pos = s.ids.index(i); return 1
        return 0

def script(seed):
    r = random.Random(seed); n = r.choice([1, 2, 3, 7, 40, 300]); cmds = [f"set {n}"]; ops = []
    for _ in range(r.randrange(20, 120)):
        k = r.random()
        if k < 0.25: c = f"next {r.randrange(2)}"
        elif k < 0.40: c = "prev"
        elif k < 0.50: c = f"shuffle {r.randrange(1, 10**6)} {r.randrange(2)}"
        elif k < 0.58: c = "restore"
        elif k < 0.66: c = f"repeat {r.randrange(3)}"
        elif k < 0.76: c = f"goto {r.randrange(n + 2)}"
        elif k < 0.90: c = f"peek {r.randrange(4)}"
        else: c = "cur"
        cmds.append(c)
    return cmds

def twin(cmds):
    q = Q(); out = []
    for c in cmds:
        w = c.split()
        if w[0] == "set": q.set(int(w[1]))
        elif w[0] == "shuffle": q.shuffle(int(w[1]), int(w[2]))
        elif w[0] == "restore": q.restore()
        elif w[0] == "repeat": q.rep = int(w[1])
        elif w[0] == "next": out.append((q.next(int(w[1])), q.pos, len(q.ids))); continue
        elif w[0] == "prev": out.append((q.prev(), q.pos, len(q.ids))); continue
        elif w[0] == "goto": out.append((q.goto(int(w[1])), q.pos, len(q.ids))); continue
        elif w[0] == "peek": out.append((q.peek(int(w[1])), q.pos, len(q.ids))); continue
        out.append((q.cur(), q.pos, len(q.ids)))
    return out

def build(d, bug):
    exe = d / f"q{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_queue_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
def run(exe, cmds):
    return subprocess.run([str(exe)], input="\n".join(cmds) + "\n", capture_output=True, text=True).stdout

def agrees(exe):
    for sd in range(300):
        cmds = script(sd); got = run(exe, cmds).strip().split("\n"); exp = twin(cmds)
        # only commands that print are compared: the harness prints one line per command, the twin one per command
        lines = [tuple(map(int, l.split())) for l in got if l and not l.endswith(" ") ]
        want = [tuple(e) for e in twin(cmds)]
        # the twin skips set/shuffle/restore/repeat prints? it appends for them too (cur): align by recomputing
        if lines != [e for e in want]: return False
    return True

def props(exe):
    ok = {}
    for n, seed, keep in ((50, 7, 1), (50, 7, 0), (1, 3, 1), (2, 9, 1), (1000, 123456, 1), (1000, 5, 0)):
        for cur in (0, n // 2, n - 1):
            cmds = [f"set {n}"] + (["goto %d" % cur] if cur else []) + [f"shuffle {seed} {keep}", "dump"]
            lines = run(exe, cmds).strip().split("\n")
            order = list(map(int, lines[-1].split()))
            if sorted(order) != list(range(n)): ok["shuffle is a permutation"] = False
            if keep and order[0] != cur: ok["shuffle keeps the playing track"] = False
            if not keep:
                lib = list(range(n)); py_shuffle(lib, seed); 
                if order != lib: ok["equals the library shuffle"] = False
            lines2 = run(exe, cmds).strip().split("\n")
            if lines2 != lines: ok["reproducible"] = False
    for k in ("shuffle is a permutation", "shuffle keeps the playing track", "equals the library shuffle", "reproducible"): ok.setdefault(k, True)
    # repeat all across the seam in shuffle: the same track never plays twice in a row, every track appears once per pass
    cmds = ["set 9", "shuffle 11 1", "repeat 1"] + ["next 1"] * 40
    seq = [int(l.split()[0]) for l in run(exe, cmds).strip().split("\n")[3:]]
    ok["no immediate repeat across the seam"] = all(a != b for a, b in zip(seq, seq[1:]))
    # repeat one: auto stays, manual moves
    seq = [l.split()[0] for l in run(exe, ["set 5", "repeat 2", "next 1", "next 1", "next 0"]).strip().split("\n")[2:]]
    ok["repeat one: auto stays, manual moves"] = seq == ["0", "0", "1"]
    # off: the end is the end and pos stays on the last track
    out = run(exe, ["set 3", "next 0", "next 0", "next 0", "cur"]).strip().split("\n")
    ok["repeat off ends"] = out[3].split()[0] == str(0xFFFF) and out[4].split()[1] == "2"
    # shuffle history: previous returns to what was heard
    out = run(exe, ["set 20", "shuffle 5 1", "next 1", "next 1", "next 1", "prev", "prev"]).strip().split("\n")
    heard = [int(l.split()[0]) for l in out[2:5]]
    ok["shuffle prev returns to what was heard"] = [int(l.split()[0]) for l in out[5:7]] == [heard[1], heard[0]]
    return ok

with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    exe = build(d, 0)
    check("C core equals the independent statement (300 random scripts)", agrees(exe))
    for k, v in props(exe).items(): check(k, v)
    for bug, name in ((1, "shuffle moves the playing track"), (2, "shuffle index overrun"), (3, "manual next repeats"), (4, "history is not popped"), (5, "same track across the seam")):
        b = build(d, bug); caught = (not agrees(b)) or not all(props(b).values())
        check(f"mutant {bug} ({name}) is caught", caught)
print("FAILED" if fails else "all ok", fails)
sys.exit(1 if fails else 0)
