#!/usr/bin/env python3
"""B-653: the fit-bundle and firmware/bitstream feature checks (tools/fit_manifest.py, tools/check_fw_bitstream_pair.py).
Checks: the macro parser; a dead macro is found (the retired eq24 bundle is the real example) and every live bundle is clean; a ROM's TAUFWNEED list is judged against a bitstream's macros
(missing feature refused, allowed on request, extra macros fine, a ROM with no marker is left to the version check); the real release ROM built by the firmware tests carries the marker and
the right features; and two mutants of the checker are caught."""
import re, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import fit_manifest as fm, check_fw_bitstream_pair as cp
fails = 0
def ok(c, m):
    global fails
    print(("ok   " if c else "FAIL ") + m); fails += 0 if c else 1

q = 'set_global_assignment -name VERILOG_MACRO "TAU_A=1"\nset_global_assignment -name VERILOG_MACRO TAU_B\nset_global_assignment -name VERILOG_MACRO "TAU_OFF=0"\n# set_global_assignment -name VERILOG_MACRO TAU_C\n'
ok(fm.macros_of(q) == ["TAU_A", "TAU_B"], "macros_of reads NAME=1 and bare NAME, skips NAME=0 and comments: %s" % fm.macros_of(q))
used = fm.used_macros()
ok("TAU_HALCYON" in used and "TAU_EQ_COEF24" not in used, "the RTL reads TAU_HALCYON and no longer reads TAU_EQ_COEF24")
old = (ROOT / "tools/fit_bundles_archive/blit_g3_poly_blend_ram192_clk66_dbuf_lpc_cymo_audio16_gain_eq24_qsf_append.txt").read_text()
ok(fm.dead_macros(old, used) == ["TAU_EQ_COEF24"], "the retired eq24 bundle's dead macro is found: %s" % fm.dead_macros(old, used))
ok(all(not fm.dead_macros(b.read_text(), used) for b in fm.live_bundles()), "every live fit bundle (%d) defines only macros the RTL reads" % len(fm.live_bundles()))

def pkg(td, blob):
    d = Path(td) / "Assets/tau/common"; d.mkdir(parents=True); (d / "tau.rom").write_bytes(blob); return td
rom = b"\0" * 8 + b"TAUFWPAIR:4D50331A;" + b"\0" + b"TAUFWNEED:HALCYON,LPC,SDRAM_BUSY,;" + b"\0"
with tempfile.TemporaryDirectory() as t1, tempfile.TemporaryDirectory() as t2, tempfile.TemporaryDirectory() as t3, tempfile.TemporaryDirectory() as t4:
    full = {"TAU_HALCYON", "TAU_LPC", "TAU_SDRAM_BUSY", "TAU_POLY", "TAU_CLK66"}
    ok(cp.check_features(pkg(t1, rom), full) == [], "a bitstream with every needed macro (and more) passes")
    e = cp.check_features(pkg(t2, rom), full - {"TAU_HALCYON"})
    ok(len(e) == 1 and "HALCYON" in e[0], "a bitstream without TAU_HALCYON is refused for a firmware that uses the Halcyon EQ")
    ok(cp.check_features(t2, full - {"TAU_HALCYON"}, ("HALCYON",)) == [], "--allow-missing HALCYON lets a deliberate fail-safe pairing through")
    ok(cp.check_features(pkg(t3, b"TAUFWPAIR:4D50331A;"), set()) == [], "a ROM with no TAUFWNEED marker is left to the version check")
    ok(cp.rom_needs(b"x" + b"TAUFWNEED:;") == [], "an empty need list reads as no features")

real = ROOT / "work/heapcheck/release/tau.rom"
if real.is_file():
    need = cp.rom_needs(real.read_bytes())
    ok(need is not None and "HALCYON" in need and "LPC" in need, "the release ROM built by this suite carries TAUFWNEED with HALCYON and LPC: %s" % need)
else:
    print("note: work/heapcheck/release/tau.rom not built yet (tools/check_targets_link.py builds it); real-ROM check skipped")

# mutants of the checker
src = (ROOT / "tools/check_fw_bitstream_pair.py").read_text()
def mutant(a, b):
    ns = {"__file__": str(ROOT / "tools/check_fw_bitstream_pair.py"), "__name__": "m"}
    exec(compile(src.replace(a, b, 1), "m", "exec"), ns)
    return ns
m1 = mutant("if fit_manifest.FEATURES.get(f, f) not in macros:", "if False:")
with tempfile.TemporaryDirectory() as t:
    ok(m1["check_features"](pkg(t, rom), set()) == [] and cp.check_features(t, set()) != [], "mutant: the feature test removed -> a missing feature passes (the real checker refuses it)")
m2 = mutant("if f in allow:\n                continue", "if True:\n                continue")
with tempfile.TemporaryDirectory() as t:
    ok(m2["check_features"](pkg(t, rom), set()) == [], "mutant: allow-everything -> nothing is ever refused (the real checker refuses)")
sys.exit(1 if fails else 0)
