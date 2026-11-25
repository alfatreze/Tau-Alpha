#!/usr/bin/env python3
"""TEST-ONLY reference (B-1168): the ROM tables the way they were generated before the window unit's tables were derived from the standard
(tools/mp3_window_derive.py) -- coefficients parsed from Helix's trigtabs.c polyCoef[], tap map measured by running Helix's FDCT32/PolyphaseStereo
(sim/mp3_poly_map.c). Used by sim/test_mp3_poly_derive.py to prove the derived tables identical to what Helix does. Not used by any generator."""
import re, shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from helix_tree import helix_tree
H = helix_tree()      # pristine upstream + third_party/libhelix-mp3/tau.patch (tools/helix_tree.py)


def helix_build_map():
    with tempfile.TemporaryDirectory() as td:
        td = Path(td)
        for f in ("polyphase.c", "trigtabs.c"): shutil.copy(H / "real" / f, td / f)
        shutil.copy(ROOT / "sim/helix_host/assembly.h", td / "assembly.h")
        s = (H / "real/dct32.c").read_text()
        assert s.count("d[0] = d[8] = s;") == 33
        s = s.replace("d[0] = d[8] = s;", "WRLOG(d, s);")
        s = s.replace('#include "assembly.h"', '#include "assembly.h"\nextern int wlog[64], wn;\n#define WRLOG(d, s) do { d[0] = d[8] = (s); wlog[wn++] = (s); } while (0)', 1)
        (td / "dct32.c").write_text(s)
        exe = td / "map"
        r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-I", str(td), "-I", str(H / "pub"), "-I", str(H / "real"), "-o", str(exe),
                            str(ROOT / "sim/mp3_poly_map.c"), str(td / "dct32.c"), str(td / "polyphase.c"), str(td / "trigtabs.c")], capture_output=True, text=True)
        if r.returncode: sys.exit(r.stderr)
        p = subprocess.run([str(exe)], capture_output=True, text=True)
        if p.returncode: sys.exit("tap map derivation failed:\n" + p.stdout)
    taps, on = {}, False
    for line in p.stdout.splitlines():
        if line == "MAP BEGIN": on = True; continue
        if line == "MAP END": on = False; continue
        if on:
            r_, t, a, pi = map(int, line.split()); taps[(r_, t)] = (a, pi)
    assert len(taps) == 17 * 16 - 8, len(taps)
    return taps


def helix_coefs():
    s = (H / "real/trigtabs.c").read_text()
    i = s.index("const int polyCoef[264]")
    body = s[s.index("{", i) + 1:s.index("};", i)]
    v = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]{8}", body)]
    assert len(v) == 264, len(v)
    return [x - (1 << 32) if x >= 1 << 31 else x for x in v]
