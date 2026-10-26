#!/usr/bin/env python3
"""B-602: sound_i2s full16 switch. Runs sim/tb_sound_i2s_full16.v (needs iverilog; skips cleanly without it), and a mutant that must FAIL
(full16 ignored) so the test is known to be able to fail."""
import os, shutil, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if not shutil.which("iverilog"):
    print("iverilog not found: sound_i2s full16 test skipped"); sys.exit(0)
build = os.path.join(ROOT, "build", "rtl"); os.makedirs(build, exist_ok=True)
src = open(os.path.join(ROOT, "src/fpga/core/sound_i2s.v")).read()
fix = lambda s: s.replace("{(15 - CHANNEL_WIDTH){1'b0}}", "{((CHANNEL_WIDTH < 15) ? (15 - CHANNEL_WIDTH) : 1){1'b0}}")   # Icarus rejects the unused negative repeat
def run(name, text):
    f = os.path.join(build, name + ".v"); open(f, "w").write(fix(text))
    vvp = os.path.join(build, name + ".vvp")
    subprocess.check_call(["iverilog", "-g2012", "-o", vvp, os.path.join(ROOT, "sim/tb_sound_i2s_full16.v"), f, os.path.join(ROOT, "src/fpga/core/sync_fifo.v")], cwd=ROOT)
    out = subprocess.run(["vvp", vvp], capture_output=True, text=True, cwd=ROOT).stdout.strip().splitlines()
    res = [l for l in out if l.startswith(("PASS", "FAIL"))]
    return res[-1] if res else ""
good = run("sound_i2s_f16_good", src)
print(good)
mut = src.replace("use16 ? audio_l[15:0]", "1'b0 ? audio_l[15:0]")
assert mut != src
bad = run("sound_i2s_f16_mutant", mut)
print("mutant (full16 ignored):", bad)
ok = good.startswith("PASS") and bad.startswith("FAIL")
print("PASSED" if ok else "FAILED"); sys.exit(0 if ok else 1)
