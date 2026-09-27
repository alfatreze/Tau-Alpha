#!/usr/bin/env python3
"""Host checks for the 192 KB RAM-shrink preparation (B-333, docs/RAM_SHRINK_192K_PLAN.md).

Fast (in make test-host):
  * udiv64() in fw/player.c equals native 64-bit division on random and edge inputs (it replaces libgcc's __udivdi3);
  * the RTL interlock: the 192 KB bitstream reports CORE_VERSION rev 24, every other bitstream rev 23, and the firmware accepts rev 24
    only when linked for 192 KB (so a 256 KB image is refused on the 192 KB bitstream).
  * B-347: RAM_192K-alone (rev 24) is unaffected by the new combined-with-CLK66 rev 26 branch nested inside TAU_CLK66 -- the
    plain `elsif TAU_RAM_192K` branch (rev 24) is still reached whenever TAU_CLK66 is undefined. The combined interlock matrix
    itself (all four TAU_RAM_192K_FW/TAU_CLK66_FW combinations) is exercised in sim/test_clk66.py, which owns VERSION_OK's full truth
    table so it isn't duplicated here.
Slow (`python3 sim/test_ram192k.py --build`, make test-ram192k): the 192 KB link of the release and the Diagnostic Build succeeds
with their heap floors, and the normal 256 KB release still builds. (Until B-334 the 256 KB ROM was byte-identical to v0.5.0; new features now change it.)
"""
import re, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    fails += 0 if cond else 1

src = (ROOT / "fw/player.c").read_text()
m = re.search(r"__attribute__\(\(noinline, unused\)\) static uint64_t udiv64\(uint64_t n, uint64_t d\)\n\{.*?\n\}\n", src, re.S)
check("udiv64 found in fw/player.c", m is not None)
if m:
    fn = m.group(0).replace("__attribute__((noinline, unused)) ", "")
    prog = "#include <stdint.h>\n#include <stdio.h>\n#include <stdlib.h>\n" + fn + """
int main(void) {
    uint64_t s = 88172645463325252ull; int bad = 0;
    uint64_t edge[] = {0, 1, 2, 999, 1000, 60000000ull, 0xFFFFFFFFull, 0x100000000ull, 0x7FFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull};
    for (int i = 0; i < 10; i++) for (int j = 0; j < 10; j++) if (edge[j]) if (udiv64(edge[i], edge[j]) != edge[i] / edge[j]) bad++;
    for (int i = 0; i < 200000; i++) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; uint64_t n = s >> (s & 31);
        s ^= s << 13; s ^= s >> 7; s ^= s << 17; uint64_t d = (s >> (s & 47)) | 1u;
        if (udiv64(n, d) != n / d) bad++;
    }
    printf("%d\\n", bad); return 0; }
"""
    with tempfile.TemporaryDirectory() as td:
        c = Path(td) / "t.c"; c.write_text(prog)
        r = subprocess.run(["cc", "-O2", "-o", str(Path(td) / "t"), str(c)], capture_output=True, text=True)
        out = subprocess.run([str(Path(td) / "t")], capture_output=True, text=True).stdout.strip() if r.returncode == 0 else "compile failed"
    check("udiv64 equals native 64-bit division (200k random + edge cases)", out == "0")

rtl = (ROOT / "src/fpga/core/mp3_soc.v").read_text()
check("RTL: the TAU_RAM_192K-alone bitstream (TAU_CLK66 undefined) reports CORE_VERSION rev 24 (4D503318)",
      re.search(r"`elsif TAU_RAM_192K\b.*?localparam \[31:0\] CORE_VERSION = 32'h4D503318;", rtl, re.S) is not None)   # B-338 put TAU_CLK66 first in the chain
check("RTL: every other bitstream keeps rev 23 (4D503317)", "CORE_VERSION = 32'h4D503317;" in rtl)
check("RTL: the combined TAU_CLK66+TAU_RAM_192K branch (rev 26) does not shadow the RAM_192K-alone `elsif` (rev 24 unaffected)",
      re.search(r"`ifdef TAU_CLK66\b.*?`endif\b\s*`elsif TAU_RAM_192K\b.*?4D503318", rtl, re.S) is not None)
check("firmware: the interlock accepts rev 24 only in the 192 KB link",
      "#define VERSION_OK(v) ((v) == EXPECT_VERSION || (v) == EXPECT_VERSION_192K)" in src
      and "#define VERSION_OK(v) ((v) == EXPECT_VERSION)" in src
      # B-347: the plain RAM_192K-only branch moved from `#if` to `#elif` (the new combined
      # TAU_RAM_192K_FW && TAU_CLK66_FW branch is checked first, `#if`), so it is still reached
      # whenever RAM_192K_FW is set without CLK66_FW.
      and re.search(r"#elif TAU_RAM_192K_FW\s*\n#define VERSION_OK", src) is not None)
check("firmware: the boot interlock uses VERSION_OK", "if (!VERSION_OK(REG(R_VERSION)))" in src)

if "--build" in sys.argv:
    def build(target, ram192):
        env = {"RAM_192K": "1" if ram192 else "0", "ART_TIMG": "1", "SDRAM_BUSY": "1", "PATH": "/usr/bin:/bin:/usr/local/bin:/Library/Frameworks/Python.framework/Versions/3.11/bin"}
        r = subprocess.run(["bash", "fw/build.sh", target], cwd=ROOT, capture_output=True, text=True, env=env)
        return r.returncode, r.stdout + r.stderr
    for t, floor in (("release", 6144), ("player-library-diagnostic", 4096)):
        rc, out = build(t, True)
        g = re.search(r"heap gap: (\d+) B", out)
        check(f"192 KB link of {t} succeeds with its heap floor ({floor} B)", rc == 0 and g is not None and int(g.group(1)) >= floor)
    rc, out = build("release", False)
    g = re.search(r"heap gap: (\d+) B", out)
    check("the normal 256 KB release still builds with its heap floor", rc == 0 and g is not None and int(g.group(1)) >= 6144)
    subprocess.run(["git", "checkout", "dist"], cwd=ROOT, capture_output=True)      # a default release build writes into dist/; put the tracked artefacts back

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
