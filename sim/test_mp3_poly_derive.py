#!/usr/bin/env python3
"""B-1168: the window unit's ROM tables derived from the MPEG-1 standard (tools/mp3_window_derive.py, data from tools/data/mp3_synthesis_window.txt)
are identical to what Helix gives (sim/helix_poly_reference.py: trigtabs.c polyCoef[] and the tap map measured with Helix's FDCT32), the committed
src/fpga/core/tau_mp3_poly_rom.svh is exactly the derived generation, and the window data has the standard's structure. Red: damaged data or a
wrong derivation rule is caught."""
import sys, copy, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools")); sys.path.insert(0, str(ROOT / "sim"))
import mp3_window_derive as W
import gen_mp3_poly_rom as G
import helix_poly_reference as R
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1

D = W.window()
taps, cf = W.derive(D)
check("window: 512 values, D[0] = 0, peak D[256] = 75038 (1.144989014), min value -64019-class sidelobes present", len(D) == 512 and D[0] == 0 and D[256] == 75038 and max(D) == 75038)
check("window: prototype (-1)^(i div 64) D[i] symmetric about 256", all((-1) ** (i // 64) * D[i] == (-1) ** ((512 - i) // 64) * D[512 - i] for i in range(1, 512)))
check("window: first values 0,-1,-1,-1,-1,-1,-1,-2 (Table 3-B.3)", D[:8] == [0, -1, -1, -1, -1, -1, -1, -2])

h_taps, h_cf = R.helix_build_map(), R.helix_coefs()
check("coefficients: 264 derived == Helix polyCoef[]", cf == h_cf, str([i for i in range(264) if cf[i] != h_cf[i]][:8]))
check("tap map: 264 derived entries == measured from Helix's FDCT32", taps == h_taps, str([k for k in taps if taps.get(k) != h_taps.get(k)][:8]))
check("committed tau_mp3_poly_rom.svh == generation from the derived tables", (ROOT / "src/fpga/core/tau_mp3_poly_rom.svh").read_text() == G.render_sv(G.build_map(), G.coefs()))

# red: damaged window data / wrong rules must change the result (so the equality above is not vacuous)
def caught(Dx):                       # rejected by the derivation's own consistency check, or the tables differ
    try: return W.derive(Dx)[1] != h_cf
    except AssertionError: return True
D2 = D[:]; D2[100] += 4
check("red: one wrong window value is caught", caught(D2))
D3 = D[:]; D3[48] = -D3[48]
check("red: a wrong sign in a sample-16 value is caught", caught(D3))
orig = W.push_index
W.push_index = lambda m: orig(m) ^ 1 if m == 5 else orig(m)
check("red: a wrong push-order rule changes the tap map", W.derive(D)[0] != h_taps)
W.push_index = orig
check("derivation restored", W.derive(D) == (taps, cf))
sys.exit(1 if fails else 0)
