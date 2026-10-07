#!/usr/bin/env python3
"""B-653: the runtime hand-over of the gain between the firmware and the hardware stage (fw/gain_handover.h, the real C, compiled and run here).
The C logs its writes; this test replays them against a behavioural model of the pipeline (decoder -> software gain -> 2048-sample FIFO -> hardware stage at the FIFO output, ramp 149 per
sample, snap, enable) playing a 1 kHz sine, and checks, for both directions and volumes from full to very low:
  * no step at the output larger than the sine's own steepest slope (the old procedure, kept here as the baseline, steps by the whole volume difference below full volume);
  * after the hand-over the steady amplitude is A x volume (never scaled twice, never unscaled);
  * the final state (owner, software fade armed only for the software owner, target kept) and that a hand-over to the owner already in charge writes nothing;
  * the order: unity snap first only when software owned it, mute before flush, flush before the owner flip.
Mutants of the header (no flush, no mute, no unity snap, wrong fade arming) must each be caught."""
import math, re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
tmp = tempfile.TemporaryDirectory()
fails = 0
def ok(c, m):
    global fails
    print(("ok   " if c else "FAIL ") + m); fails += 0 if c else 1

def build(hdr_text=None, tag="real"):
    inc = ROOT / "fw"
    exe = Path(tmp.name) / f"gh_{tag}"
    src = ROOT / "sim/gain_handover_harness.c"
    if hdr_text is not None:
        d = Path(tmp.name) / f"inc_{tag}"; d.mkdir(); (d / "gain_handover.h").write_text(hdr_text); (d / "pcm_push.h").write_text((inc / "pcm_push.h").read_text())
        (d / "harness.c").write_text(src.read_text().replace('"../fw/gain_handover.h"', '"gain_handover.h"')); src = d / "harness.c"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-o", str(exe), str(src)], capture_output=True, text=True)
    if r.returncode: print(r.stdout, r.stderr); sys.exit(1)
    return exe

def run_c(exe, on, hw, target):
    out = subprocess.run([str(exe)], input=f"{on} {hw} {target}\n", capture_output=True, text=True, check=True).stdout.splitlines()
    ev = [l for l in out if l[:4] in ("CTRL", "TARG", "WAIT", "FLUS")]
    st = re.search(r"STATE hw=(\d+) cur=(-?\d+) target=(-?\d+) fade=(\d+)", "\n".join(out))
    return ev, tuple(map(int, st.groups()))

FS, AMP, F0, DEPTH, PRIME = 48000, 12000, 1000, 2048, 1024
class Pipe:
    """decoder -> software gain -> FIFO -> stage. One call of tick() is one output sample."""
    def __init__(s, hw, vol):
        s.hw, s.cur, s.target, s.fade_left, s.fade_n = hw, vol, vol, 0, 2048
        s.en, s.gcur, s.gt = (1 if hw else 0), (vol if hw else 32768), (vol if hw else 32768)
        s.fifo, s.phase, s.out, s.primed = [], 0, [], True
        s.fill()
    def sample(s):
        v = int(AMP * math.sin(2 * math.pi * F0 * s.phase / FS)); s.phase += 1; return v
    def soft(s, x):
        if s.hw: return x
        x = (x * s.cur + 16384) >> 15
        if s.fade_left:
            g = (s.fade_n - s.fade_left) >> 3; x = (x * g + 128) >> 8; s.fade_left -= 1
        return x
    def fill(s):
        while len(s.fifo) < DEPTH: s.fifo.append(s.soft(s.sample()))
    def tick(s, decode=True):
        if not s.primed:
            y = 0
            if decode and len(s.fifo) < PRIME: s.fill_to(PRIME)
            if len(s.fifo) >= PRIME: s.primed = True
        if s.primed and s.fifo:
            x = s.fifo.pop(0)
            if s.en:
                d = s.gt - s.gcur; d = max(-149, min(149, d)); s.gcur += d
                x = (x * s.gcur + 16384) >> 15
            y = x
        else:
            y = 0
        s.out.append(y)
        if decode and s.primed: s.fill()
    def fill_to(s, n):
        while len(s.fifo) < n: s.fifo.append(s.soft(s.sample()))
    def event(s, e):
        k, _, v = e.partition(" "); v = int(v) if v else 0
        if k == "TARGET": s.gt = v
        elif k == "CTRL":
            s.en = v & 1
            if v & 2: s.gcur = s.gt
        elif k == "WAIT":
            for _ in range(v * FS // 1000): s.tick(decode=False)
        elif k == "FLUSH": s.fifo = []; s.primed = False

def scenario(exe, on, hw, vol):
    ev, st = run_c(exe, on, hw, vol)
    p = Pipe(hw, vol)
    for _ in range(3000): p.tick()
    p.out.clear()
    # the owner flip happens at the end of the events: the C changed vol_st.hw/fade_left itself, mirror them into the model after the event list
    for e in ev: p.event(e)
    p.hw, p.cur, p.fade_left = bool(st[0]), st[1], st[3]
    for _ in range(8000): p.tick()
    o = p.out
    steps = max(abs(o[i] - o[i - 1]) for i in range(1, len(o)))
    peak = max(abs(x) for x in o[-2000:])
    return ev, st, steps, peak

nat = AMP * 2 * math.pi * F0 / FS          # the sine's own steepest slope, per sample, at unity gain
exe = build()
for on, name in ((0, "to software"), (1, "to hardware")):
    for vol in (32768, 9830, 1000):
        ev, st, steps, peak = scenario(exe, on, 1 - on, vol)
        want = AMP * vol / 32768
        ok(steps <= nat * 1.25 + 4, f"{name}, volume {vol / 32768:.2f}: largest output step {steps} <= the sine's own {nat:.0f}")
        ok(abs(peak - want) <= max(40, want * 0.04), f"{name}, volume {vol / 32768:.2f}: steady peak {peak} equals {want:.0f} (not scaled twice, not unscaled)")
        ok(st == (on, vol, vol, 0 if on else 2048), f"{name}, volume {vol / 32768:.2f}: final state hw/cur/target/fade = {st}")
ev, st = run_c(exe, 1, 1, 9830); ok(ev == [] and st[0] == 1, "a hand-over to the owner already in charge writes nothing")
ev, _ = run_c(exe, 0, 1, 9830)
ok([e.split()[0] for e in ev] == ["TARGET", "WAIT", "FLUSH", "CTRL"], f"hardware to software: mute, wait, flush, disable: {ev}")
ev, _ = run_c(exe, 1, 0, 9830)
ok([e.split()[0] for e in ev] == ["TARGET", "CTRL", "TARGET", "WAIT", "FLUSH", "TARGET", "CTRL"] and ev[0] == "TARGET 32768" and ev[2] == "TARGET 0", f"software to hardware: unity snap, mute, wait, flush, volume, fade: {ev}")
ok(any(e.startswith("WAIT") and int(e.split()[1]) >= 5 for e in run_c(exe, 0, 1, 9830)[0]), "the mute waits at least the stage's 5 ms ramp")

# the baseline: the old procedure (flip the owner, nothing else) steps at any volume below full
class Old(Pipe): pass
p = Pipe(1, 9830)
for _ in range(3000): p.tick()
p.out.clear(); p.en = 0; p.hw = False; p.cur = 9830                   # the old gain_hw_set(0): stage off, software owns it, FIFO untouched
for _ in range(3000): p.tick()
old_step = max(abs(p.out[i] - p.out[i - 1]) for i in range(1, len(p.out)))
ok(old_step > nat * 3, f"baseline: the old hand-over (owner flipped, FIFO untouched) steps by {old_step}, {old_step / nat:.1f}x the sine's own slope")

# mutants of the header
hdr = (ROOT / "fw/gain_handover.h").read_text()
for name, a, b in (("no flush", "    o->flush();\n", ""), ("no mute", "    o->target(0u);\n", ""),
                   ("no unity snap for a software owner", "if (!v->hw) { o->target(GH_UNITY); o->ctrl(ctrl_base | GH_CTRL_SNAP); }", ""),
                   ("software fade not armed", "*fade_left = fade_samples;", "*fade_left = 0u;")):
    assert a in hdr, name
    m = build(hdr.replace(a, b, 1), name.replace(" ", "_"))
    dead = False
    for on in (0, 1):
        for vol in (9830, 1000):
            try:
                ev, st, steps, peak = scenario(m, on, 1 - on, vol)
            except Exception:
                dead = True; continue
            want = AMP * vol / 32768
            if steps > nat * 1.25 + 4 or abs(peak - want) > max(40, want * 0.04) or st != (on, vol, vol, 0 if on else 2048): dead = True
    ok(dead, f"mutant caught: {name}")
sys.exit(1 if fails else 0)
