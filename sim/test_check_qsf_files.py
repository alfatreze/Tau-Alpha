#!/usr/bin/env python3
"""tools/check_qsf_files.py: the real ap_core.qsf is consistent, and a synthetic tree shows each failure kind is caught (a missing listed file, an instantiated module whose file is not
listed, a module behind an undefined `ifdef that is rightly ignored, a library-qualified qip entry, a listed module nothing instantiates is fine)."""
import sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import check_qsf_files as C

fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1

problems, _ = C.check(ROOT / "src/fpga/ap_core.qsf")
check("the real ap_core.qsf: every file exists and every instantiated module's file is listed", not problems, problems)
qsf_text = (ROOT / "src/fpga/ap_core.qsf").read_text()
for gone in ("tau_sdram_arbiter.sv", "tau_sdram_cpu_bridge.sv", "tau_sdram_ctrl.sv", "core_constraints_sched_a.sdc"):
    check(f"the retired {gone} is not in the QSF", gone not in qsf_text)
check("the new-path files are listed", all(n in qsf_text for n in ("core/tau_sdram_phy.sv", "core/tau_sdram_sched_v.sv", "core/tau_sdram_sched_a.sv", "core/core_constraints.sdc")))
check("core_constraints.sdc keeps the generated clock and the multicycle", all(n in (ROOT / "src/fpga/core/core_constraints.sdc").read_text() for n in ("create_generated_clock -name sdram_clk_pin", "set_multicycle_path -setup 2", "set_multicycle_path -hold  1")))

with tempfile.TemporaryDirectory() as td:
    r = Path(td) / "fpga"; (r / "core").mkdir(parents=True)
    (r / "core/top.v").write_text("module top;\n`ifdef NOPE\n  hidden h0 (.a(1));\n`endif\n  listed_m u1 (.a(1));\n  unlisted_m #(.W(2)) u2 (.a(1));\nendmodule\n")
    (r / "core/listed.sv").write_text("module listed_m; endmodule\n")
    (r / "core/unlisted.sv").write_text("module unlisted_m; endmodule\n")
    (r / "core/hidden.sv").write_text("module hidden; endmodule\n")
    (r / "core/dead.sv").write_text("module dead_m; endmodule\n")
    (r / "core/lib.qip").write_text('set_global_assignment -library "x" -name VERILOG_FILE [file join $::quartus(qip_path) "listed.sv"]\n')
    q = r / "ap.qsf"
    q.write_text("set_global_assignment -name VERILOG_FILE core/top.v\nset_global_assignment -name QIP_FILE core/lib.qip\nset_global_assignment -name SDC_FILE core/missing.sdc\n")
    p, w = C.check(q)
    check("a missing listed file is reported", any("missing.sdc" in x for x in p), p)
    check("an instantiated module whose file is unlisted is reported", any("unlisted_m" in x for x in p), p)
    check("a module behind an undefined `ifdef is not reported", not any("hidden" in x for x in p), p)
    check("a library-qualified qip entry counts as listed", not any("module listed_m" in x for x in p), p)
    check("a dead unlisted file is only a warning", any("dead_m" in x for x in w) and not any("dead_m" in x for x in p), (p, w))
    q.write_text(q.read_text() + "set_global_assignment -name VERILOG_MACRO \"NOPE=1\"\n")
    p, _ = C.check(q)
    check("with the macro defined in the QSF the hidden module is required", any("hidden" in x for x in p), p)

print("FAILURES: %d" % fails if fails else "ALL PASS")
sys.exit(1 if fails else 0)
