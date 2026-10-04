#!/usr/bin/env python3
"""fw/pixgrid.h (C) and tools/pixgrid_check.py (numpy) must generate identical patterns."""
import os, subprocess, sys, tempfile
import numpy as np
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from pixgrid_check import pg_pixel_grid, W, H

C = r'''#include <stdio.h>
#include "pixgrid.h"
int main(void){ for(unsigned p=0;p<PG_PATTERNS;p++) for(unsigned y=0;y<PG_H;y++) for(unsigned x=0;x<PG_W;x++){ unsigned v=pg_pixel(p,x,y); fwrite(&v,2,1,stdout);} return 0; }
'''
with tempfile.TemporaryDirectory() as d:
    src = os.path.join(d, "h.c"); open(src, "w").write(C)
    subprocess.check_call(["cc", "-O2", "-I", os.path.join(ROOT, "fw"), src, "-o", os.path.join(d, "h")])
    raw = np.frombuffer(subprocess.check_output([os.path.join(d, "h")]), dtype="<u2").reshape(4, H, W)
bad = [p for p in range(4) if not np.array_equal(raw[p], pg_pixel_grid(p))]
if bad: sys.exit(f"pixel grid patterns differ for {bad}")
print("pixgrid: C and Python patterns identical (4 patterns, 400x360)")
