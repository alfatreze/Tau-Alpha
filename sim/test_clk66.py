#!/usr/bin/env python3
"""Host checks for the clk_sys 60 -> 66.667 MHz change (B-338, docs/HARPMUDD_UPSTREAM_1.5_REVIEW.md section 1).
Fast (in make test-host): the RTL's pcm_rate reset default for 66.667 MHz matches the same rounded formula
firmware's pcm_rate_apply() uses at runtime (pcm_rate_apply itself overwrites the reset value before playback,
so this only guards the brief window before the first call); the RTL interlock and firmware CLK_HZ/VERSION_OK
wiring are present and mutually exclusive with TAU_RAM_192K/_FW as build.sh enforces.
Slow (`python3 sim/test_clk66.py --build`, make test-clk66): the CLK66=1 firmware links; RAM_192K=1 CLK66=1 is refused.
"""
import re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
fails = 0
def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    fails += 0 if cond else 1

rtl = (ROOT / "src/fpga/core/mp3_soc.v").read_text()
fw = (ROOT / "fw/player.c").read_text()

m = re.search(r"TAU_CLK66\s*\n\s*pcm_rate <= 32'd(\d+);", rtl)
check("RTL pcm_rate reset default found under TAU_CLK66", m is not None)
if m:
    want = round(48000 * (2**32) / 66666667)
    check(f"pcm_rate reset default ({m.group(1)}) matches the rounded 48 kHz/66.667 MHz formula ({want})",
          int(m.group(1)) == want)

check("RTL: eq_biquad gets 66_666_667 under TAU_CLK66", "eq_biquad #(.CLK_HZ(66_666_667)" in rtl)
check("RTL: CORE_VERSION rev 25 (4D503319) under TAU_CLK66", "CORE_VERSION = 32'h4D503319u;" in rtl)
check("RTL: TAU_CLK66 is checked before TAU_RAM_192K (mutually exclusive)",
      re.search(r"`ifdef TAU_CLK66\b.*?`elsif TAU_RAM_192K\b", rtl, re.S) is not None)

check("PLL: outclk_0 is 66.666667 MHz under TAU_CLK66",
      "66.666667 MHz" in (ROOT / "src/fpga/core/mf_pllbase/mf_pllbase_0002.v").read_text())

check("firmware: CLK_HZ is 66666667 under TAU_CLK66_FW", "#define CLK_HZ      66666667u" in fw)
check("firmware: EXPECT_VERSION_CLK66 is 0x4D503319", "#define EXPECT_VERSION_CLK66 0x4D503319u" in fw)
check("firmware: VERSION_OK accepts CLK66's version under TAU_CLK66_FW",
      re.search(r"#elif TAU_CLK66_FW\s*\n#define VERSION_OK\(v\) \(\(v\) == EXPECT_VERSION \|\| \(v\) == EXPECT_VERSION_CLK66\)", fw) is not None)

build = (ROOT / "fw/build.sh").read_text()
check("build.sh refuses RAM_192K=1 CLK66=1 together", 'CLK66 and RAM_192K are not combined yet' in build)

if "--build" in sys.argv:
    def run(env):
        e = {"PATH": "/usr/bin:/bin:/usr/local/bin:/Library/Frameworks/Python.framework/Versions/3.11/bin"}
        e.update(env)
        return subprocess.run(["bash", "fw/build.sh", "release"], cwd=ROOT, capture_output=True, text=True, env=e)
    r = run({"CLK66": "1"})
    check("CLK66=1 release links", r.returncode == 0 and "heap gap" in r.stdout + r.stderr)
    r = run({"CLK66": "1", "RAM_192K": "1"})
    check("CLK66=1 RAM_192K=1 together is refused, not silently ignored", r.returncode != 0)
    subprocess.run(["git", "checkout", "dist"], cwd=ROOT, capture_output=True)

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
