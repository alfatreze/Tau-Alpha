#!/usr/bin/env python3
"""Write a simulation copy of mp3_soc.v for Icarus.

Quartus accepts mp3_soc's use-before-declaration of the MMIO register constants
and mmio_reg; Icarus does not. This hoists exactly those two declarations to
just before their first use. The synthesised source is never modified.
usage: make_soc_sim.py <in mp3_soc.v> <out mp3_soc_sim.v>
"""
import re
import sys

src = open(sys.argv[1]).read()
m1 = re.search(r"    localparam \[7:0\] R_CONSOLE.*?R_SDR_STATUS=8'h84;\n", src, re.S)
m2 = re.search(r"    wire \[8:0\] mmio_reg = \{dADR\[6:0\], 2'b00\};[^\n]*\n", src)
if not (m1 and m2):
    raise SystemExit("mp3_soc.v layout changed: cannot hoist MMIO declarations")
hoist = m1.group(0) + m2.group(0)
out = src.replace(m1.group(0), "").replace(m2.group(0), "")
anchor = "    wire wr_reload"
i = out.index(anchor)
out = out[:i] + "    // hoisted for Icarus by sim/make_soc_sim.py\n" + hoist + out[i:]
open(sys.argv[2], "w").write(out)
