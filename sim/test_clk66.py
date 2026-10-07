#!/usr/bin/env python3
"""Host checks for the clk_sys 60 -> 66.667 MHz change (B-338, docs/HARPMUDD_UPSTREAM_1.5_REVIEW.md section 1)
and its combination with the 192 KB RAM shrink (B-347, CORE_VERSION rev 26).
Fast (in make test-host): the RTL's pcm_rate reset default for 66.667 MHz matches the same rounded formula
firmware's pcm_rate_apply() uses at runtime (pcm_rate_apply itself overwrites the reset value before playback,
so this only guards the brief window before the first call); the RTL interlock and firmware CLK_HZ/VERSION_OK
wiring are present; the combined-rev-26 branch is nested inside TAU_CLK66/TAU_RAM_192K ahead of the
single-feature `elsif`s in both the RTL and the firmware interlock (a real C harness compiles fw/player.c's
actual VERSION_OK macro under all four TAU_RAM_192K_FW/TAU_CLK66_FW combinations and checks every rev
against every build, proving old single-feature firmware is refused by the combined bitstream, combined
firmware is refused by a single-feature or baseline bitstream, combined firmware accepts only rev 26, and
neither single-feature branch accidentally accepts rev 26).
Slow (`python3 sim/test_clk66.py --build`, make test-clk66): CLK66=1 alone, RAM_192K=1 alone, and
CLK66=1 RAM_192K=1 together all link (the last no longer refused).
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

check("RTL: the Halcyon engine gets 66_666_667 under TAU_CLK66", "tau_halcyon #(.CLK_HZ(66_666_667)" in rtl)
check("RTL: CORE_VERSION rev 25 (4D503319) under plain TAU_CLK66", "CORE_VERSION = 32'h4D503319;" in rtl)
check("RTL: CORE_VERSION rev 26 (4D50331A) under combined TAU_CLK66+TAU_RAM_192K", "CORE_VERSION = 32'h4D50331A;" in rtl)
check("RTL: the combined rev-26 branch is nested inside TAU_CLK66, ahead of its plain-CLK66 `else`",
      re.search(r"`ifdef TAU_CLK66\b.*?`ifdef TAU_RAM_192K\b.*?4D50331A.*?`else\b.*?4D503319.*?`endif\b.*?`elsif TAU_RAM_192K\b",
                rtl, re.S) is not None)

check("PLL: outclk_0 is 66.666667 MHz under TAU_CLK66",
      "66.666667 MHz" in (ROOT / "src/fpga/core/mf_pllbase/mf_pllbase_0002.v").read_text())

check("firmware: CLK_HZ is 66666667 under TAU_CLK66_FW", "#define CLK_HZ      66666667u" in fw)
check("firmware: EXPECT_VERSION_CLK66 is 0x4D503319", "#define EXPECT_VERSION_CLK66 0x4D503319u" in fw)
check("firmware: EXPECT_VERSION_192K_CLK66 is 0x4D50331A", "#define EXPECT_VERSION_192K_CLK66 0x4D50331Au" in fw)
check("firmware: the combined branch is checked first, strictest (only rev 26)",
      re.search(r"#if TAU_RAM_192K_FW && TAU_CLK66_FW\s*\n#define VERSION_OK\(v\) \(\(v\) == EXPECT_VERSION_192K_CLK66\)", fw) is not None)
check("firmware: the single-feature `#elif`s are unchanged (each still also accepts baseline rev 23)",
      re.search(r"#elif TAU_RAM_192K_FW\s*\n#define VERSION_OK\(v\) \(\(v\) == EXPECT_VERSION \|\| \(v\) == EXPECT_VERSION_192K\)", fw) is not None
      and re.search(r"#elif TAU_CLK66_FW\s*\n#define VERSION_OK\(v\) \(\(v\) == EXPECT_VERSION \|\| \(v\) == EXPECT_VERSION_CLK66\)", fw) is not None)

build = (ROOT / "fw/build.sh").read_text()
check("build.sh no longer refuses RAM_192K=1 CLK66=1 together", 'not combined yet' not in build)
check("build.sh's RAM_192K and CLK66 blocks are independent (both -D flags can land in CFLAGS together)",
      "-DTAU_RAM_192K_FW=1" in build and "-DTAU_CLK66_FW=1" in build)

# Real C harness: pull VERSION_OK's exact preprocessor block out of fw/player.c and compile it under all
# four TAU_RAM_192K_FW/TAU_CLK66_FW combinations, then check every EXPECT_VERSION_* constant against every
# build -- proves the interlock's actual runtime behaviour, not just that the right strings are present.
m = re.search(
    r"(#define EXPECT_VERSION 0x4D503317u.*?#endif\n)", fw, re.S)
check("VERSION_OK macro block extracted from fw/player.c", m is not None)
if m:
    block = m.group(1)
    consts = ["EXPECT_VERSION", "EXPECT_VERSION_192K", "EXPECT_VERSION_CLK66", "EXPECT_VERSION_192K_CLK66"]
    def version_ok_matrix(ram192k, clk66):
        defs = []
        if ram192k: defs.append("#define TAU_RAM_192K_FW 1")
        if clk66: defs.append("#define TAU_CLK66_FW 1")
        prog = "#include <stdint.h>\n#include <stdio.h>\n" + "\n".join(defs) + "\n" + block + \
            "int main(void) {\n" + \
            "".join(f'printf("%d\\n", VERSION_OK({c}));\n' for c in consts) + \
            "return 0; }\n"
        import tempfile
        with tempfile.TemporaryDirectory() as td:
            c = Path(td) / "t.c"; c.write_text(prog)
            r = subprocess.run(["cc", "-o", str(Path(td) / "t"), str(c)], capture_output=True, text=True)
            if r.returncode != 0:
                return None
            out = subprocess.run([str(Path(td) / "t")], capture_output=True, text=True).stdout.split()
        return dict(zip(consts, (x == "1" for x in out)))

    baseline = version_ok_matrix(False, False)
    ram_only = version_ok_matrix(True, False)
    clk_only = version_ok_matrix(False, True)
    combined = version_ok_matrix(True, True)
    check("baseline firmware (no macros) accepts only EXPECT_VERSION", baseline is not None and
          baseline == {"EXPECT_VERSION": True, "EXPECT_VERSION_192K": False, "EXPECT_VERSION_CLK66": False, "EXPECT_VERSION_192K_CLK66": False})
    check("RAM_192K-only firmware accepts baseline + rev 24, refuses rev 25 and rev 26", ram_only is not None and
          ram_only == {"EXPECT_VERSION": True, "EXPECT_VERSION_192K": True, "EXPECT_VERSION_CLK66": False, "EXPECT_VERSION_192K_CLK66": False})
    check("CLK66-only firmware accepts baseline + rev 25, refuses rev 24 and rev 26", clk_only is not None and
          clk_only == {"EXPECT_VERSION": True, "EXPECT_VERSION_192K": False, "EXPECT_VERSION_CLK66": True, "EXPECT_VERSION_192K_CLK66": False})
    check("combined firmware accepts ONLY rev 26 -- refuses baseline, rev 24 and rev 25 (stricter than either single-feature branch)",
          combined is not None and
          combined == {"EXPECT_VERSION": False, "EXPECT_VERSION_192K": False, "EXPECT_VERSION_CLK66": False, "EXPECT_VERSION_192K_CLK66": True})
    check("old single-feature firmware (RAM_192K-only) is refused by the combined bitstream's rev 26",
          ram_only is not None and ram_only["EXPECT_VERSION_192K_CLK66"] is False)
    check("old single-feature firmware (CLK66-only) is refused by the combined bitstream's rev 26",
          clk_only is not None and clk_only["EXPECT_VERSION_192K_CLK66"] is False)
    check("combined firmware is refused by the RAM_192K-only bitstream (rev 24)",
          combined is not None and combined["EXPECT_VERSION_192K"] is False)
    check("combined firmware is refused by the CLK66-only bitstream (rev 25)",
          combined is not None and combined["EXPECT_VERSION_CLK66"] is False)

if "--build" in sys.argv:
    def run(env):
        e = {"PATH": "/usr/bin:/bin:/usr/local/bin:/Library/Frameworks/Python.framework/Versions/3.11/bin"}
        e.update(env)
        return subprocess.run(["bash", "fw/build.sh", "release"], cwd=ROOT, capture_output=True, text=True, env=e)
    r = run({"CLK66": "1"})
    check("CLK66=1 release links", r.returncode == 0 and "heap gap" in r.stdout + r.stderr)
    r = run({"RAM_192K": "1"})
    check("RAM_192K=1 release links", r.returncode == 0 and "heap gap" in r.stdout + r.stderr)
    r = run({"CLK66": "1", "RAM_192K": "1"})
    check("CLK66=1 RAM_192K=1 together now links (no longer refused)", r.returncode == 0 and "heap gap" in r.stdout + r.stderr)
    subprocess.run(["git", "checkout", "dist"], cwd=ROOT, capture_output=True)

print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
