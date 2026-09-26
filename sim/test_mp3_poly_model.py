#!/usr/bin/env python3
"""Golden-model test (B-292): sim/mp3_poly_model.c reproduces Helix's real PolyphaseStereo exactly (all PCM shorts, 170 slots across quiet, normal,
loud, full-scale, impulse, silence and alternating-extreme inputs, including clipped samples), the committed ROM include
src/fpga/core/tau_mp3_poly_rom.svh equals a fresh generation, and the RTL test vectors are written to build/rtl/mp3_poly_vectors.txt."""
import shutil, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import gen_mp3_poly_rom as G
H = ROOT / "third_party/libhelix-mp3"
fails = 0
def check(name, ok, detail=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + detail))
    if not ok: fails += 1
taps, cf = G.build_map(), G.coefs()
check("committed ROM include matches a fresh generation", (ROOT / "src/fpga/core/tau_mp3_poly_rom.svh").read_text() == G.render_sv(taps, cf))
with tempfile.TemporaryDirectory() as td:
    td = Path(td)
    for f in ("polyphase.c", "trigtabs.c"): shutil.copy(H / "real" / f, td / f)
    shutil.copy(ROOT / "sim/helix_host/assembly.h", td / "assembly.h")
    s = (H / "real/dct32.c").read_text().replace("d[0] = d[8] = s;", "WRLOG(d, s);")
    s = s.replace('#include "assembly.h"', '#include "assembly.h"\nextern int wlog[64], wn;\n#define WRLOG(d, s) do { d[0] = d[8] = (s); wlog[wn++] = (s); } while (0)', 1)
    (td / "dct32.c").write_text(s)
    (td / "tables.h").write_text(G.render_h(taps, cf))
    shutil.copy(ROOT / "sim/mp3_poly_model_core.h", td / "mp3_poly_model_core.h")
    exe = td / "model"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-DTABLES_H=\"tables.h\"", "-I", str(td), "-I", str(H / "pub"), "-I", str(H / "real"), "-o", str(exe),
                        str(ROOT / "sim/mp3_poly_model.c"), str(td / "dct32.c"), str(td / "polyphase.c"), str(td / "trigtabs.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr); sys.exit(1)
    out_dir = ROOT / "build/rtl"; out_dir.mkdir(parents=True, exist_ok=True)
    p = subprocess.run([str(exe), str(out_dir / "mp3_poly_vectors.txt")], capture_output=True, text=True)
    check("model equals Helix PolyphaseStereo, every sample of every slot", p.returncode == 0 and "differences 0" in p.stdout, p.stdout)
    check("the vectors include clipped samples (the clip path is exercised)", "samples clipped 0" not in p.stdout, p.stdout)
print("PASSED" if not fails else f"FAILED ({fails})")
sys.exit(1 if fails else 0)
