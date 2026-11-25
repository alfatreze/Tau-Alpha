#!/usr/bin/env python3
"""Make the scratch copy of Tau Gallery's tau_talos.sv with per-feature compile-out macros. MIT licence.

  make_variants.py GALLERY_TAU_TALOS.sv OUT.sv

OUT.sv is a scratch file (put it under work/, never commit it: it contains Gallery's text). It is the input file with
three insertions, nothing else changed:
  `ifndef NO_CORNERS  ... `endif   around the whole `OP_CORNERS: begin ... end` dispatch arm
  `ifndef NO_DSBLIT   ... `endif   around the whole `OP_DSBLIT: begin ... end` dispatch arm
  NO_DOUBLER: `src_line` (the viewer fill's l/2 line select, driven by the latched view_dbl) becomes just fill_line
The viewer fill itself is switched off by the VIEW_ENABLE parameter (no edit needed). Registers only the removed arm
set (dsb_*, rrect captures) fold away at map level because nothing sets them any more.
"""
import re, sys

src = open(sys.argv[1]).read().split("\n")
out, i = [], 0
while i < len(src):
    ln = src[i]
    m = re.match(r"(\s*)(OP_CORNERS|OP_DSBLIT): begin\s*$", ln)
    if m:
        ind, name = m.group(1), m.group(2)
        mac = "NO_CORNERS" if name == "OP_CORNERS" else "NO_DSBLIT"
        j = i + 1
        while not re.fullmatch(ind + r"end\s*", src[j]):
            j += 1
        out += [f"`ifndef {mac}"] + src[i:j + 1] + ["`endif"]
        i = j + 1
        continue
    if re.match(r"\s*wire \[9:0\] src_line = ", ln):
        out += ["`ifdef NO_DOUBLER", "    wire [9:0] src_line = fill_line;", "`else", ln, "`endif"]
        i += 1
        continue
    out.append(ln)
    i += 1
text = "\n".join(out)
for k in ("NO_CORNERS", "NO_DSBLIT", "NO_DOUBLER"):
    assert "`ifndef " + k in text or "`ifdef " + k in text, k + " not inserted"
open(sys.argv[2], "w").write(text)
