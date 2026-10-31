#!/usr/bin/env python3
"""The mutant of sim/tb_wall_soc.v MODE 3: tau_soc's I2S jitter interval sum with the old 32-bit adder (the carry out of bit 31 is dropped). usage: mutate_i2s_sum.py <tau_soc_sim.v> <out>"""
import sys
s = open(sys.argv[1]).read()
a = "i2s_diag_sum_r <= i2s_diag_sum_r + {32'd0, i2s_diag_interval};"
assert a in s, "tau_soc.v layout changed"
open(sys.argv[2], "w").write(s.replace(a, "i2s_diag_sum_r <= {16'd0, i2s_diag_sum_r[31:0] + {16'd0, i2s_diag_interval}};"))
