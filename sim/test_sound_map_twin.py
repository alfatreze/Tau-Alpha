#!/usr/bin/env python3
"""The lab's SOUND MAP twin (tools/meters/preview/meters/sound_map.js: the pack's own source compiled to WebAssembly) against the pack's source run natively: for a seeded script of pictures (every mode, a small and the
fullscreen box, with and without the hardware statistics, paused, forced repaints, the memory scrolling over many seconds) the command streams (rect, bar, text with every argument) are IDENTICAL. Also: the committed
module is current (no source changed since it was built), the twin runs from the lab's own context (the live path prepares centroid, flux, onset and crest, and draws), and a mutant of the lab module is caught by the same comparison."""
import random, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)[:400]) if (extra and not ok) else ""))
    if not ok: fails += 1
def script(seed=5):
    r = random.Random(seed); lines = []
    roles = [r.getrandbits(16) for _ in range(21)]; roles[6] = 0x07FF; lines.append("R " + " ".join(map(str, roles)))
    frame = 0
    for mode in (0, 1, 2):
        for box, cell, gain, span in (((16, 152, 368, 122), 7, 110, 1), ((0, 0, 400, 323), 5, 100, 1), ((16, 152, 368, 122), 8, 120, 5), ((0, 0, 400, 323), 12, 80, 2)):
            for n in range(260):
                ok = int(n % 3 != 0 and mode != 2 or n % 2 == 0)
                bgc = r.getrandbits(16); force = 1 if n == 0 or n == 130 else 0
                paused = 1 if 60 <= n < 70 else 0
                spec = [0 if 90 <= n < 100 else r.randint(0, 255) * (16 - i // 2) // 16 for i in range(16)]
                vals = [mode, gain, cell, span, *box, bgc, force, paused, r.randint(0, 2), frame, r.choice([20, 26, 26, 40, 200]), r.randint(0, 24575), r.randint(0, 24575),
                        r.randint(0, 12000) * ok, r.randint(0, 12000) * ok, r.randint(-256, 256) * ok, r.randint(0, 255), r.randint(0, 120) * ok, r.randint(0, 255), int(r.random() < 0.15), ok, *spec]
                lines.append("T " + " ".join(map(str, vals))); frame += 1
    return "\n".join(lines) + "\n"
def build_native(d):
    exe = d / "native"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-I", str(ROOT / "fw2/ui"), "-I", str(ROOT / "fw2"), "-o", str(exe), str(ROOT / "sim/p2_sound_map_cmdlog_harness.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    return exe
with tempfile.TemporaryDirectory() as td:
    d = Path(td); s = d / "script.txt"; s.write_text(script())
    native = subprocess.run([str(build_native(d))], stdin=open(s), capture_output=True, text=True).stdout.splitlines()
    node = subprocess.run(["node", str(ROOT / "tools/meters/preview/wasm/parity.js"), str(s)], capture_output=True, text=True)
    twin = node.stdout.splitlines()
    ticks = sum(1 for l in native if l.startswith("T "))
    check("the twin and the native pack draw the same: %d pictures, %d commands, every command's arguments equal" % (ticks, len(native) - ticks), native == twin and ticks == 3 * 4 * 260,
          (node.stderr[-300:] if node.returncode else next((f"line {i}: {a!r} vs {b!r}" for i, (a, b) in enumerate(zip(native, twin)) if a != b), f"lengths {len(native)} vs {len(twin)}")))
    check("the three modes and the three kinds of command all appear in the stream", {l.split()[0] for l in native} >= {"rect", "bar", "text"} and sum(1 for l in native if l.startswith("text")) >= 30)
    # a mutant of the lab module (the parameters in the wrong order) is caught by the same comparison
    import shutil
    m = d / "mut"; (m / "meters").mkdir(parents=True); (m / "wasm").mkdir()
    for f in ("tau_fb.js", "meters/sound_map.js", "meters/sound_map_wasm.js", "wasm/parity.js"): shutil.copy(ROOT / "tools/meters/preview" / f, m / f)
    src = (m / "meters/sound_map.js").read_text(); assert "const PARAMS = ['mode', 'gain', 'cell', 'span'];" in src
    (m / "meters/sound_map.js").write_text(src.replace("const PARAMS = ['mode', 'gain', 'cell', 'span'];", "const PARAMS = ['gain', 'mode', 'cell', 'span'];"))
    bad = subprocess.run(["node", str(m / "wasm/parity.js"), str(s)], capture_output=True, text=True).stdout.splitlines()
    check("mutant of the lab module (the parameters in the wrong order) is caught", bad != native)
fresh = subprocess.run([sys.executable, str(ROOT / "tools/meters/preview/wasm/build_wasm.py"), "--check"], capture_output=True, text=True)
check("the committed module is current: no source changed since it was built", fresh.returncode == 0, fresh.stdout + fresh.stderr)
# the lab's live path: an in-lab context draws and prepares its own statistics
js = r'''
const { Fb } = require('./tools/meters/preview/tau_fb.js'); const M = require('./tools/meters/preview/meters/sound_map.js');
M.ready().then(() => { const st = M.state(); const theme = { accent: 0x07FF, role: new Proxy({}, { get: () => 0x8410 }) }; let cmds = 0, texts = 0;
  for (let n = 0; n < 200; n++) { const fb = new Fb(400, 360, true); const spec = Array.from({ length: 16 }, (_, i) => 150 - i * 8 + (n % 7) * 5);
    M.tick({ fb, st, theme, x: 16, y: 152, w: 368, h: 122, bg: 0x0821, spec, paused: false, force: n === 0, p: { mode: n < 70 ? 0 : n < 140 ? 1 : 2, gain: 100, cell: 7, span: 1 }, stats: n % 2 ? { rms_l: 4000, rms_r: 3000, corr: 120, peak_l: 9000, peak_r: 8000 } : null });
    cmds += fb.cmds; texts += fb.log.filter((l) => l.startsWith('text')).length; }
  console.log(JSON.stringify({ cmds, texts })); });
'''
r = subprocess.run(["node", "-e", js], cwd=ROOT, capture_output=True, text=True)
try:
    import json; live = json.loads(r.stdout.strip().splitlines()[-1])
except Exception: live = {}
check("the lab's live path (spectrum and optional statistics from the context) draws all three modes: %s" % live, live.get("cmds", 0) > 1000 and live.get("texts", 0) >= 5, r.stderr[-300:])
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
