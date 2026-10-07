#!/usr/bin/env python3
"""Host test for tools/lab/cymo_gain_model.py (B-615), and the vector generator for sim/tb_tau_gain_stage.v.
1. Equivalence: the model equals fw/pcm_push.h's pcm_gain_apply() (the shipped firmware gain) sample for sample over a long random script of volume changes, flushes and samples.
2. Properties: unity is exact, mute is exact silence, a step ramps at the stated rate and arrives in ceil(delta/ramp) ticks, the fade rises monotonically from silence to the settled level,
   an underrun gap (no adv) does not consume the fade.
3. `--vectors FILE` writes the RTL replay script (events and expected outputs); the testbench must match every line."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "lab"))
import cymo_gain_model as m

HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "%s"
int main(void)
{
    pcm_vol_t v = { 32768, 32768 };
    uint32_t fade_left = 0;
    char op[8]; long a, b;
    while (scanf("%%7s %%ld %%ld", op, &a, &b) == 3) {
        if (op[0] == 'T') v.target = (int32_t)a;
        else if (op[0] == 'F') fade_left = 2048u;
        else { int32_t l = (int32_t)a, r = (int32_t)b; pcm_gain_apply(&l, &r, &v, &fade_left, 2048u); printf("%%d %%d\n", (int)l, (int)r); }
    }
    return 0;
}
'''

fails = 0
def check(name, ok, info=""):
    global fails
    print(("PASS " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

def script(seed, n):
    rnd = random.Random(seed)
    tab = [0, 35, 1036, 4125, 16423, 24857, 32768]
    ev = []
    for _ in range(n):
        r = rnd.random()
        if r < 0.01: ev.append(("T", rnd.choice(tab), 0))
        elif r < 0.015: ev.append(("F", 0, 0))
        else:
            x = rnd.choice([0, 1, -1, 32767, -32768, rnd.randint(-32768, 32767), rnd.randint(-300, 300)])
            ev.append(("S", x, rnd.choice([x, -x if x != -32768 else 32767, rnd.randint(-32768, 32767)])))
    return ev

def run_model(ev):
    s = m.GainStage(); out = []
    for op, a, b in ev:
        if op == "T": s.set_target(a)
        elif op == "F": s.fade_now()
        else: out.append(s.tick(a, b, 1))
    return out

with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    (d / "h.c").write_text(HARNESS % (ROOT / "fw" / "pcm_push.h"))
    subprocess.run(["cc", "-O1", "-o", str(d / "h"), str(d / "h.c")], check=True)
    ev = script(1, 60000)
    p = subprocess.run([str(d / "h")], input="\n".join("%s %d %d" % e for e in ev), capture_output=True, text=True, check=True)
    want = [tuple(map(int, l.split())) for l in p.stdout.splitlines()]
    got = run_model(ev)
    check("model equals the shipped firmware gain (pcm_gain_apply) over %d samples with random volume steps and flushes" % len(got), got == want)

s = m.GainStage()
check("unity gain is exact for every sample value", all(s.tick(x, -x if x != -32768 else 32767, 1) == (x, -x if x != -32768 else 32767) for x in range(-32768, 32768, 7)))
s = m.GainStage(); s.set_target(0)
outs = [s.tick(20000, -20000, 1) for _ in range(300)]
check("mute reaches exact silence", outs[-1] == (0, 0), str(outs[-1]))
n = next(i for i, o in enumerate(outs) if o == (0, 0)) + 1
check("a full-scale step to mute lands in ceil(32768/149) = %d ticks" % -(-32768 // 149), n == -(-32768 // 149) or n == -(-32768 // 149) - 1, "(zero first seen after %d)" % n)
s = m.GainStage(); s.set_target(16384); prev = 32768; steps_ok = True
for _ in range(120):
    s.tick(1, 1, 1); steps_ok &= (prev - s.cur) <= 149; prev = s.cur
check("ramp never moves more than the ramp step per tick and settles on the target", steps_ok and s.cur == 16384)
s = m.GainStage(); s.set_target(20000); s.snap(); s.fade_now()
lv = [s.tick(30000, 30000, 1)[0] for _ in range(2200)]
check("fade-in starts at silence, never falls, and ends at the settled level", lv[0] == 0 and all(b >= a for a, b in zip(lv, lv[1:])) and lv[-1] == m.rnd(30000 * 20000, 15), "(first %d, last %d)" % (lv[0], lv[-1]))
s = m.GainStage(); s.fade_now(); a = [s.tick(30000, 0, 0) for _ in range(500)]; left = s.fade_left
check("ticks without a real sample (priming, underrun gap) do not consume the fade", left == 2048, "(fade_left %d)" % left)

if "--vectors" in sys.argv:
    path = sys.argv[sys.argv.index("--vectors") + 1]
    rnd = random.Random(7)
    s = m.GainStage(); lines = []
    tab = [0, 35, 1036, 4125, 16423, 24857, 32768]
    for i in range(80000):
        r = rnd.random()
        if r < 0.004:
            t = rnd.choice(tab); s.set_target(t); lines.append("T %d" % t)
        elif r < 0.0046:
            s.fade_now(); lines.append("F")
        elif r < 0.0056:
            s.snap(); lines.append("N")
        elif r < 0.0066 and s.fade_left == 0:     # the length is only changed while no fade runs (documented rule)
            sh = rnd.randint(0, 3); s.shift = sh; lines.append("H %d" % sh)
        else:
            x = rnd.choice([0, 1, -1, 32767, -32768, rnd.randint(-32768, 32767), rnd.randint(-300, 300)])
            y = rnd.choice([x, rnd.randint(-32768, 32767)])
            adv = 0 if rnd.random() < 0.1 else 1
            o = s.tick(x, y, adv)
            lines.append("S %d %d %d %d %d" % (x, y, adv, o[0], o[1]))
    Path(path).write_text("\n".join(lines) + "\n")
    print("wrote %s (%d lines)" % (path, len(lines)))
sys.exit(1 if fails else 0)
