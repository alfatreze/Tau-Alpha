#!/usr/bin/env python3
"""Write a simulation copy of core_bridge_cmd.v for Icarus (the APF file declares b_datatable_* after their first use, which Quartus accepts and Icarus does not).
This hoists exactly those three declarations to just before `reg [31:0] host_0;`. The synthesised source is never modified.
usage: make_bridge_sim.py <in core_bridge_cmd.v> <out core_bridge_cmd_sim.v>"""
import re
import sys

src = open(sys.argv[1]).read()
m = re.search(r"    wire    \[31:0\]  b_datatable_q;\n    reg     \[9:0\]   b_datatable_addr;\n    reg             b_datatable_wren;\n", src)
anchor = "    reg     [31:0]  host_0;\n"
if not m or anchor not in src:
    raise SystemExit("core_bridge_cmd.v layout changed: cannot hoist the declarations")
out = src.replace(m.group(0), "")
out = out.replace(anchor, "    // hoisted for Icarus by sim/make_bridge_sim.py\n" + m.group(0) + anchor, 1)
open(sys.argv[2], "w").write(out)
