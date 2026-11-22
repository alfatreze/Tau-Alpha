#!/usr/bin/env python3
"""sdk/docs/SNIPPETS.md and the how-to text of sdk/docs/ON_A_POCKET.md step 3b stay true.
  1. every ```c block of SNIPPETS.md compiles together (-Wall -Wextra -Werror) and the helpers give the right values: clamp, the integer sine against math.sin over a full turn (within 1 in 1024), the colour mix at its ends and middle, the line helper's rectangles;
  2. the unzip command of step 3b, run on a real package of the shipped seven, puts index.tpi and the p<N>.tpr files directly in the target folder (the folder name being the first 8 hex digits of the id);
  3. the documents that make promises about the limits say what the header says: LIMITS.md states that `wave` is NULL today and the header says the same."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import toolchain
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)[:500]))
    fails += 0 if ok else 1
text = (ROOT / "sdk/docs/SNIPPETS.md").read_text()
blocks = re.findall(r"```c\n(.*?)```", text, re.S)
check("SNIPPETS.md has its four code blocks", len(blocks) == 4, len(blocks))
harness = r'''
#include <math.h>
#include <stdio.h>
#include <string.h>
''' + "\n".join(blocks) + r'''
static int calls; static int rc[8][4]; static unsigned short rcol[8];
static void fake(unsigned x, unsigned y, unsigned w, unsigned h, unsigned short c) { if (calls < 8) { rc[calls][0] = (int)x; rc[calls][1] = (int)y; rc[calls][2] = (int)w; rc[calls][3] = (int)h; rcol[calls] = c; } calls++; }
int main(void)
{
    int bad = 0;
    if (iclamp(5, 0, 3) != 3 || iclamp(-5, 0, 3) != 0 || iclamp(2, 0, 3) != 2 || imin(1, 2) != 1 || imax(1, 2) != 2) { puts("clamp"); bad++; }
    for (unsigned p = 0; p < 1024; p++) { double want = 1024.0 * sin(2.0 * 3.14159265358979323846 * (double)p / 256.0); if (fabs((double)isin(p) - want) > 1.01) { printf("sin %u %d %f\n", p, isin(p), want); bad++; break; } }
    if (isin(0) != 0 || isin(64) != 1024 || isin(128) != 0 || isin(192) != -1024) { puts("sin quarter points"); bad++; }
    if (mix565(0xF800, 0x001F, 0) != 0xF800 || mix565(0xF800, 0x001F, 32) != 0x001F) { puts("mix ends"); bad++; }
    if (mix565(0x0000, 0xFFFF, 16) != 0x7BEF) { printf("mix mid %04x\n", mix565(0x0000, 0xFFFF, 16)); bad++; }
    seg(fake, 10, 10, 20, 15, 2, 7);                                   /* a run right and down: two rectangles */
    if (calls != 2 || rc[0][0] != 10 || rc[0][2] != 12 || rc[0][3] != 2 || rc[1][0] != 20 || rc[1][1] != 10 || rc[1][3] != 7) { puts("seg"); bad++; }
    calls = 0; seg(fake, 5, 5, 5, 5, 3, 1);                            /* a point: one rectangle */
    if (calls != 1 || rc[0][2] != 3 || rc[0][3] != 3) { puts("seg point"); bad++; }
    printf(bad ? "BAD %d\n" : "GOOD\n", bad); return bad != 0;
}
'''
with tempfile.TemporaryDirectory() as td:
    td = Path(td); src = td / "snip.c"; src.write_text(harness)
    r = subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-o", str(td / "snip"), str(src), "-lm"], capture_output=True, text=True, env=toolchain.host_env())
    check("every block of SNIPPETS.md compiles together without a warning", r.returncode == 0, r.stderr)
    if r.returncode == 0:
        o = subprocess.run([str(td / "snip")], capture_output=True, text=True)
        check("the helpers give the right values (clamp, sine over four turns, colour mix, line rectangles)", o.stdout.strip() == "GOOD", o.stdout + o.stderr)
    # the table in the document is the one the formula gives
    import math
    m = re.search(r"sine_quarter\[65\] = \{ ([^}]*) \}", text)
    tab = [int(x) for x in m.group(1).split(",")]
    check("the sine table in the document is round(1024 sin) of the formula, 65 entries", tab == [round(1024 * math.sin(math.pi / 2 * i / 64)) for i in range(65)] and len(tab) == 65)
    # step 3b: the unzip command on a real package
    try: sys.path.insert(0, str(ROOT / "sdk" / "sdk")); from tau_meter import toolchain as tc; tc.find(); have_llvm = True
    except Exception: have_llvm = False
    if not have_llvm: print("SKIP: no LLVM toolchain for the SDK (step 3b's unzip check needs one real package)")
    else:
        b = subprocess.run([sys.executable, str(ROOT / "sdk/sdk/tau-meter"), "build", str(ROOT / "sdk/meters/vu_master"), "--out", str(td / "dist")], capture_output=True, text=True)
        pk = sorted((td / "dist").glob("*.tmeter")) if b.returncode == 0 else []
        check("a shipped meter builds to try the command on", len(pk) == 1, b.stdout + b.stderr)
        if pk:
            pid = pk[0].stem; folder = td / "card/Assets/tau/common/tau-presets" / pid[:8]
            folder.mkdir(parents=True)                                   # the doc says `mkdir -p` first: unzip -d creates only the last folder (this test found that)
            r = subprocess.run(["unzip", "-j", "-q", str(pk[0]), "presets/*", "-d", str(folder)], capture_output=True, text=True)
            names = sorted(p.name for p in folder.iterdir()) if folder.is_dir() else []
            check("step 3b's unzip command leaves index.tpi and the p<N>.tpr files in the id's 8-digit folder", r.returncode == 0 and "index.tpi" in names and any(re.fullmatch(r"p\d+\.tpr", n) for n in names), (r.stderr, names))
on = (ROOT / "sdk/docs/ON_A_POCKET.md").read_text()
check("ON_A_POCKET.md step 3b names the folder rule (first 8 hex digits) and the unzip command", "3b." in on and "tau-presets/<first 8 hex digits of yourid>" in on and "unzip -j" in on and "mkdir -p" in on)
lim = (ROOT / "sdk/docs/LIMITS.md").read_text(); hdr = (ROOT / "sdk/sdk/include/tau_meter.h").read_text()
check("LIMITS.md says `wave` is NULL today, and the header still says the same (red when the player starts filling it: update both)", "NULL in the player today" in hdr and "NULL in the player today" in lim.replace("is **NULL in the player today**", "NULL in the player today"), "")
print("%d failure(s)" % fails); sys.exit(1 if fails else 0)
