#!/usr/bin/env python3
"""The save-file BACKEND of the meter-settings interface (fw2/ui/meters_settings.c, W2 S2 of docs/features/meters/METER_SAVE_FILE_SPEC.md) on the host: the real get / put / preset / drop over a model of the save RAM and of
the old pool of two tunings (sim/p2_mset_backend_harness.c). Checks: the backend is chosen at run time (no RAM, a RAM with no or a short file, a newer file version: the pool serves and the RAM is not touched); with a loaded file the
records go into the file in the format of tools/meter_settings.py (byte for byte against the reference); schema filtering; removal; another params_rev is never applied; the migration of a pool tuning into the file on first lookup
and no resurrection after a removal; a damaged file starts from defaults and heals on the first edit; more meters than fit (oldest evicted)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import meter_settings as ms
import toolchain
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(extra)))
    if not ok: fails += 1
td = Path(tempfile.mkdtemp())
exe = td / "msb"
r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror", "-Wno-unknown-pragmas", "-DP2_HOST", "-o", str(exe), str(ROOT / "sim/p2_mset_backend_harness.c")], capture_output=True, text=True, env=toolchain.host_env())
if r.returncode: print(r.stderr); sys.exit(1)
def run(lines, ram=None):
    sc = td / "s.txt"; sc.write_text("\n".join(lines) + "\n")
    o = subprocess.run([str(exe), str(sc)], capture_output=True, text=True, cwd=td)
    assert o.returncode == 0, (o.returncode, o.stderr)
    return o.stdout.strip().split("\n")
LOADED = "80000800"          # built in, 2,048 words loaded
zero = td / "zero.bin"; zero.write_bytes(bytes(8192))
def ramfile(name, data): p = td / name; p.write_bytes(data); return str(p)
# 1 no RAM / short file: the pool serves, the RAM stays as it was
for st, what in (("0", "no save RAM in the bitstream"), ("80000000", "the RAM built in but no file loaded"), ("800003FF", "a file of another size loaded")):
    out = run([f"st {st}", "ram zero", "put AAAA0001 1 2 1:100,5:200", "get AAAA0001 1", "poolfind AAAA0001 1", "dump d1.bin"])
    check(f"{what}: the pool of two tunings serves (stored, read back, in the pool) and the RAM is untouched", out[0] == "put kept=2" and out[1].startswith("get 1 preset=2 n=2") and out[2] == "poolfind 1" and (td / "d1.bin").read_bytes() == bytes(8192), out)
# 2 a loaded zero seed: the file serves
out = run([f"st {LOADED}", "ram zero", "put AAAA0001 1 2 1:100,5:200", "get AAAA0001 1", "preset AAAA0001 1", "poolfind AAAA0001 1", "dump d2.bin"])
d2 = (td / "d2.bin").read_bytes()
check("a loaded zero seed: put goes into the FILE, get and preset read it back, the pool stays empty", out[0] == "put kept=2" and out[1] == "get 1 preset=2 n=2 1:100,5:200" and out[2] == "preset 1 2" and out[3] == "poolfind 0", out)
check("the bytes are exactly what the Python reference writes for the same edit", d2 == ms.apply_change(bytes(8192), 0xAAAA0001, 1, 2, [(1, 100), (5, 200)]))
# 3 schema filtering, removal, another revision
out = run([f"st {LOADED}", "ram zero", "put BBBB0002 1 0 3:9,7:70,50:5,9:99", "get BBBB0002 1", "put BBBB0002 1 0 -", "get BBBB0002 1", "put CCCC0003 1 1 2:2", "get CCCC0003 2", "preset CCCC0003 2", "preset CCCC0003 1"])
check("entries outside the schema (value above the maximum, index at or above np) are left out; a put with none removes the record", out[0] == "put kept=2" and out[1] == "get 1 preset=0 n=2 7:70,9:99" and out[2] == "put kept=0" and out[3] == "get 0", out)
check("a record of ANOTHER params_rev is never applied (get and preset say none), the right one is found", out[4] == "put kept=1" and out[5] == "get 0" and out[6] == "preset 0 0" and out[7] == "preset 1 1", out)
# 4 migration
out = run([f"st {LOADED}", "ram zero", "poolput DDDD0004 1 2 1:100,5:200", "poolput EEEE0005 1 0 6:600", "preset DDDD0004 1", "get DDDD0004 1", "poolfind DDDD0004 1", "poolfind EEEE0005 1", "get DDDD0004 1", "dump d4.bin"])
check("migration: a pool tuning is found before it is moved (preset), moves into the file on its first get, leaves the pool, and the other tuning stays in the pool until its own meter is looked up", out[2] == "preset 1 2" and out[3] == "get 1 preset=2 n=2 1:100,5:200" and out[4] == "poolfind 0" and out[5] == "poolfind 1" and out[6] == "get 1 preset=2 n=2 1:100,5:200", out)
check("the migrated record is in the file (reference parse)", ms.find((td / "d4.bin").read_bytes(), 0xDDDD0004, 1) is not None and ms.find((td / "d4.bin").read_bytes(), 0xEEEE0005, 1) is None)
out = run([f"st {LOADED}", "ram zero", "poolput FFFF0006 1 0 1:5", "get FFFF0006 1", "drop FFFF0006 1", "get FFFF0006 1", "poolfind FFFF0006 1"])
check("no resurrection: after a migrated meter is dropped it is gone from the file and from the pool", out[1].startswith("get 1") and out[3] == "get 0" and out[4] == "poolfind 0", out)
# 5 a newer file version: nothing is written, the pool serves
newer = bytearray(d2); newer[4:6] = (2).to_bytes(2, "little")
out = run([f"st {LOADED}", f"ram {ramfile('newer.bin', bytes(newer))}", "put AAAA0009 1 0 1:1", "get AAAA0009 1", "poolfind AAAA0009 1", "dump d5.bin"])
check("a file of a newer version: the pool serves and the 8,192 bytes are left exactly as they were", out[2] == "poolfind 1" and (td / "d5.bin").read_bytes() == bytes(newer), out)
# 6 damaged files heal
dmg = bytearray(d2); dmg[24] ^= 0x01        # inside the record (bytes 20..33), not the zero tail
out = run([f"st {LOADED}", f"ram {ramfile('dmg.bin', bytes(dmg))}", "get AAAA0001 1", "put AAAA0001 1 3 4:44", "get AAAA0001 1", "dump d6.bin"])
check("a damaged only generation: defaults (get none), the first edit writes a good one", out[0] == "get 0" and out[1] == "put kept=1" and out[2] == "get 1 preset=3 n=1 4:44" and ms.parse((td / "d6.bin").read_bytes())["status"] == "ok", out)
# 7 many meters, eviction, order, byte for byte with the reference
lines = [f"st {LOADED}", "ram zero"]; ref = bytes(8192)
ents = ",".join(f"{i}:{1000 + i}" for i in range(40)); ent_l = [(i, 1000 + i) for i in range(40)]
ent_l[3] = (3, 3); ents = ",".join(f"{i}:{v}" for i, v in ent_l)
for k in range(45):
    lines.append(f"put 7000{k:04X} 1 0 {ents}"); ref = ms.apply_change(ref, 0x70000000 + k, 1, 0, ent_l)
lines += ["get 70000000 1", "get 7000002C 1", "dump d7.bin"]
out = run(lines)
check("45 meters x 40 changes: the file equals the reference byte for byte, the oldest meter went back to defaults, the newest is intact", (td / "d7.bin").read_bytes() == ref and out[-2] == "get 0" and out[-1].startswith("get 1"), out[-3:])
# 8 the size-table entry of slot 11 (probe 2b, DEV 232): set to 8,192 by a put only when the RAM looks loaded and the entry is still 0
out = run([f"st {LOADED}", "ram zero", "dt 5 11 0", "dt 6 12 77", "put AAAA0001 1 0 1:1", "dtget 5", "dtget 6"])
check("the slot's size entry is 0 (no file existed) and the RAM looks loaded: the first put sets it to 8,192 and leaves other slots alone", out[1] == "dt 11 8192" and out[2] == "dt 12 77", out)
out = run([f"st {LOADED}", "ram zero", "dt 5 11 8192", "dt 7 11 4096", "put AAAA0001 1 0 1:1", "dtget 5", "dtget 7"])
check("an entry APF filled (8,192) or any nonzero size is never changed", out[1] == "dt 11 8192" and out[2] == "dt 11 4096", out)
out = run([f"st {LOADED}", "ram zero", "dt 5 12 0", "put AAAA0001 1 0 1:1", "dtget 5"])
check("no entry for slot 11 in the table: nothing is written", out[1] == "dt 12 0", out)
out = run(["st 0", "ram zero", "dt 5 11 0", "put AAAA0001 1 0 1:1", "dtget 5"])
check("the file backend off (no RAM, or no file loaded): the table is left alone", out[1] == "dt 11 0", out)
print("FAILED" if fails else "all ok")
sys.exit(1 if fails else 0)
