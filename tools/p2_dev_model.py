"""A model of the hardware registers a meter pack reads, for tools/rv32sim.py --dev (the pack under test reaches them at 0xF0001000 + the register's low 12 bits, sim/p2_pack_sim_regs.h). The PSRAM status words (0x88, 0xA0) and the Winamp Scope capture block:
ST (0xFC) reads 1 (never busy), IDX (0xF0) selects a column, DATA (0xF4) reads that column of the capture that the number of arms so far (writes of bit 1 to CTL 0xEC) decides. The same function is in
sim/p2_pack_native.c, so the built-in meter and the pack see the same captures; modes: 0 normal, 1 DC-biased high, 2 one-sided, 3 loud."""
M32 = 0xFFFFFFFF
arms = 0
idx = 0
def data(a, i):
    x = (a * 0x9E3779B1 + i * 0x85EBCA6B + 12345) & M32
    x ^= x >> 15; x = (x * 0x2C1B3C6D) & M32; x ^= x >> 12
    mode = a % 4
    sc = 32000 if mode == 3 else 6000
    m = (x >> 16) % sc
    lo = -((x & 0xFFFF) % sc)
    if mode == 1: lo += 12000; m += 12000
    if mode == 2: lo = 0
    lo = max(-32768, min(32767, lo)); m = max(-32768, min(32767, m))
    return ((lo & 0xFFFF) << 16) | (m & 0xFFFF)
def load(off):
    if off == 0xFC: return 1
    if off == 0x88: return 0x50535231                     # the PSRAM bus is in the bitstream (Layered Wave's window proof)
    if off == 0xA0: return 0x10000                         # ...and its CPU window
    if off == 0xF4: return data(arms, idx & 255)
    return 0
def store(off, val):
    global arms, idx
    if off == 0xF0: idx = val
    elif off == 0xEC and (val & 2): arms += 1
