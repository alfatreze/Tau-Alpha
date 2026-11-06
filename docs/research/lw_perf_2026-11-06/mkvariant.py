import sys, os, shutil
from pathlib import Path
R = Path(sys.argv[1]); S = Path(sys.argv[2]); name = sys.argv[3]; text_line = sys.argv[4]
T = S / ("root_" + name)
if T.exists(): shutil.rmtree(T)
(T / "fw2/ui").mkdir(parents=True)
for e in (R / "fw2").iterdir():
    if e.name != "ui": os.symlink(e, T / "fw2" / e.name)
for e in (R / "fw2/ui").iterdir():
    if e.name != "meter_pack.ld": os.symlink(e, T / "fw2/ui" / e.name)
os.symlink(R / "fw", T / "fw"); os.symlink(R / "meters", T / "meters"); os.symlink(R / "toolchain", T / "toolchain")
ld = (R / "fw2/ui/meter_pack.ld").read_text()
old = ".text   : { KEEP(*(.text.mtr_pack_entry)) *(.text*) }"
assert old in ld
(T / "fw2/ui/meter_pack.ld").write_text(ld.replace(old, text_line))
if len(sys.argv) > 5:
    inc = Path(sys.argv[5]); (T / "fw2/ui/layered_wave.inc").unlink(); (T / "fw2/ui/layered_wave.inc").write_text(inc.read_text())
sys.path.insert(0, str(R / "tools/player2")); sys.path.insert(0, str(R / "tools"))
import pack_meter as pm
pm.ROOT = T
SIMREGS = ["-include", str(R / "sim/p2_pack_sim_regs.h")]
blob, info = pm.build("layered_wave", 0x400000, S / ("lwv_%s.elf" % name), SIMREGS, scratch=0x300000)
(S / ("lwv_%s.tmpk" % name)).write_bytes(blob)
print(name, "built", len(blob), "bytes")
