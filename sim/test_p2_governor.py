#!/usr/bin/env python3
"""Player 2 governor (fw2/cores/governor_core.h): (1) the C core equals an independent Python twin over random and threshold-hugging fill sequences, mutants killed;
(2) a discrete scheduling simulation of the loop of spec section 9 with the twin as policy tests the spec's claims: an MP3 never needs shedding; a hard FLAC (decode
barely faster than real time) underruns WITHOUT the governor and does not with it; a CPU stall shorter than the lead never underruns and one longer than the lead does;
and a decoder slower than real time cannot be saved by any scheduling (the model must say so)."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok):
    global fails
    print(("ok   " if ok else "FAIL ") + name)
    if not ok: fails += 1

IDLE = 0xFFFFFFFF
CAP = 48672                                         # usable pairs of the shipped ring

class Gov:                                          # an independent statement of the spec table
    def __init__(s, cap):
        s.cap = cap; s.t = [cap // 2, cap // 4, cap // 10]; s.hi = cap - cap // 20; s.h = cap // 20; s.mode = 0; s.entered = [1, 0, 0, 0]
    def raw(s, f):
        return 0 if f >= s.t[0] else 1 if f >= s.t[1] else 2 if f >= s.t[2] else 3
    def update(s, f):
        w = s.raw(f)
        if w > s.mode: s.mode = w; s.entered[w] += 1
        elif w < s.mode:
            thr = s.t[2] if s.mode == 3 else s.t[1] if s.mode == 2 else s.t[0]
            if f >= thr + s.h: s.mode -= 1; s.entered[s.mode] += 1
        return s.mode
    def want(s, f): return f < s.hi
    def degrade(s, f):
        if s.mode != 0: return 3
        span = s.hi - s.t[0]; above = max(0, f - s.t[0])
        return 0 if above >= span * 2 // 3 else 1 if above >= span // 3 else 2

def fills(seed):
    r = random.Random(seed); g = Gov(CAP); f = r.randrange(0, CAP); out = []
    for _ in range(600):
        k = r.random()
        if k < 0.15: f = r.randrange(0, CAP + 1)
        elif k < 0.6: f = max(0, min(CAP, f + r.randrange(-2500, 2500)))
        else:                                              # hug a threshold or its hysteresis edge
            thr = r.choice(g.t); f = max(0, thr + r.choice([-g.h, -1, 0, 1, g.h - 1, g.h, g.h + 1]))
        out.append(f)
    for i in range(len(out)):
        if r.random() < 0.06: out[i] = IDLE                  # nothing decoding for a pass (idle, stopped, between tracks)
    return out

def build(d, bug):
    exe = d / f"g{bug}"
    r = subprocess.run(["cc", "-O1", "-Wall", "-Werror", f"-DBUG={bug}", "-I", str(ROOT / "fw2/cores"), "-o", str(exe), str(ROOT / "sim/p2_governor_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe

def agrees(exe):
    for sd in range(120):
        fl = fills(sd); g = Gov(CAP)
        out = subprocess.run([str(exe)], input=f"{CAP}\n" + "\n".join(map(str, fl)) + "\n", capture_output=True, text=True).stdout.split("\n")
        for f, line in zip(fl, out):
            m, w, dg = map(int, line.split())
            if f == IDLE:
                if (m, w, dg) != (0, 0, 0): return False                        # published at once as full speed; the governor's own state is kept
                continue
            if m != g.update(f) or bool(w) != g.want(f) or dg != g.degrade(f): return False
    return True

# ---- the scheduling simulation -----------------------------------------------------------------------------------------------------------------------------------
def simulate(step_ms, step_pairs, ui_ms, governed, seconds=60, stall=None, start_fill=None):
    """Loop of section 9.1. Time in ms, lead in pairs (ring + FIFO). Playback starts once 4,096 pairs are queued. Returns dict(underruns, ui_normal_share, min_fill, ui_ms_total)."""
    rate = 44.1                                              # pairs per ms consumed
    g = Gov(CAP); fill = 0.0 if start_fill is None else float(start_fill); t = 0.0; playing = start_fill is not None
    und = 0; was_dry = False; ui_total = 0.0; iters = 0; normal = 0; minf = CAP; stalled = False
    def advance(dt):
        nonlocal fill, und, was_dry, minf
        if playing:
            fill -= rate * dt
            if fill <= 0: fill = 0.0
            if fill == 0.0 and not was_dry: und += 1; was_dry = True
            if fill > 0: was_dry = False
            if t > 1000: minf = min(minf, fill)
    while t < seconds * 1000:
        if stall and not stalled and t >= stall[0]:
            stalled = True; advance(stall[1]); t += stall[1]
        mode = g.update(int(fill)) if governed else 0
        iters += 1; normal += (mode == 0)
        dt = 0.4; advance(dt); t += dt                                   # input + io
        if (g.want(int(fill)) if True else True) and (mode < 3 or True):
            advance(step_ms); t += step_ms; fill = min(float(CAP), fill + step_pairs)
        ui = ui_ms if mode == 0 else ui_ms / 2 if mode == 1 else 0.0
        if ui: advance(ui); t += ui; ui_total += ui
        if not playing and fill >= 4096: playing = True
        if not governed and not playing: pass
        if playing is False and t > 5000: break
    return dict(underruns=und, normal=normal / max(1, iters), min_fill=minf, ui_ms=ui_total)

with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    good = build(d, 0)
    check("governor core equals the twin over 120 random and threshold-hugging sequences (modes and the decode flag)", agrees(good))
    for b, nm in {1: "a level exactly at a threshold falls a mode", 2: "no hysteresis (the mode flaps around a threshold)", 3: "recovery jumps straight to the wanted mode instead of one step", 4: "the meter degrade level uses halves and quarters instead of thirds", 5: "an idle decoder keeps publishing the last mode and degrade level"}.items():
        check(f"mutant caught: {nm}", not agrees(build(d, b)))

# MP3 128 kbps stereo: 11.5 ms of CPU for a 1152-pair frame; the UI costs 2 ms a pass
mp3 = simulate(11.5, 1152, 2.0, True)
check(f"MP3: no underrun, never leaves NORMAL for long (normal share {mp3['normal']:.3f}, min lead {mp3['min_fill']:.0f} pairs)", mp3["underruns"] == 0 and mp3["normal"] > 0.97)
# hard FLAC: 4,096 pairs cost 85 ms of CPU, sliced into 8 ms steps of about 385 pairs; real time needs 44.1 pairs a ms, the decoder makes 48.2 while it runs
args = (8.0, 4096 * 8.0 / 85.0, 2.0)
ung = simulate(*args, governed=False); gov = simulate(*args, governed=True)
check(f"hard FLAC without the governor underruns ({ung['underruns']} underruns in 60 s)", ung["underruns"] > 0)
check(f"hard FLAC with the governor does not ({gov['underruns']} underruns, min lead {gov['min_fill']:.0f} pairs, UI got {gov['ui_ms']/1000:.1f} s of the 60 s)", gov["underruns"] == 0 and gov["ui_ms"] > 0)
# the lead absorbs a stall: from a full ring, a stall shorter than the lead does not underrun, one longer does
full = int(CAP * 0.95)
s900 = simulate(11.5, 1152, 2.0, True, seconds=30, stall=(5000, 900), start_fill=full); s1300 = simulate(11.5, 1152, 2.0, True, seconds=30, stall=(5000, 1300), start_fill=full)
check(f"a 900 ms stall from a full ring does not underrun (lead {full/44.1:.0f} ms)", s900["underruns"] == 0)
check("a 1,300 ms stall does (the documented limit of the ring)", s1300["underruns"] >= 1)
# honesty check of the model: a decoder slower than real time cannot be saved by scheduling
slow = simulate(8.0, 44.1 * 8.0 * 0.9, 2.0, True, seconds=60)
check(f"a decoder slower than real time underruns even governed ({slow['underruns']}): the model does not pretend otherwise", slow["underruns"] > 0)
print("PASSED" if not fails else f"{fails} FAILED"); sys.exit(1 if fails else 0)
