#!/usr/bin/env python3
"""Create the side-by-side player-based SDRAM contention core."""
import hashlib, json, re, shutil
from pathlib import Path

root = Path(__file__).resolve().parent.parent
src = root / "dist"
out = root / "work/diagnostics/sdram-stress/pocket"
core_id, platform = "alfatreze.TAU_SDRAM_STRESS", "tau_sdram_strs"

def save(p, v): p.write_text(json.dumps(v, indent=4) + "\n")
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def bitrev(a, b):
    table = bytes(int(f"{x:08b}"[::-1], 2) for x in range(256))
    b.write_bytes(a.read_bytes().translate(table))

def main():
    rbf = root / "work/diagnostics/sdram/fpga/ap_core.rbf"
    rom = root / "work/diagnostics/sdram-stress/tau.rom"
    if out.exists(): shutil.rmtree(out)
    c = out / "Cores" / core_id
    shutil.copytree(src / "Cores/alfatreze.TAU", c)
    bitrev(rbf, c / "bitstream.rbf_r")
    j = json.loads((c / "core.json").read_text())
    m = j["core"]["metadata"]; m["shortname"] = "TAU_SDRAM_STRESS"; m["platform_ids"] = [platform]
    m["description"] = "TAU developer SDRAM contention stress player"; save(c / "core.json", j)
    if len(platform) > 15 or not re.fullmatch(r"[a-z0-9][a-z0-9_]*", platform):
        raise ValueError(f"invalid Analogue Pocket platform shortname: {platform!r}")
    if len(m["shortname"]) > 31:
        raise ValueError(f"core shortname exceeds Pocket limit: {m['shortname']!r}")
    if c.name != f"{m['author']}.{m['shortname']}":
        raise ValueError("core folder does not match author.shortname metadata")
    a = out / "Assets" / platform
    (a / "common").mkdir(parents=True); (a / core_id).mkdir()
    shutil.copy2(rom, a / "common/tau.rom")
    shutil.copy2(src / "Assets/tau/common/tau-loading.bin", a / "common/tau-loading.bin")
    save(a / core_id / "TAU SDRAM Stress.json", {"instance":{"magic":"APF_VER_1","variant_select":{"id":0,"select":False},"data_path":"","data_slots":[{"id":1,"filename":"tau.rom"}],"memory_writes":[]}})
    p = out / "Platforms"; (p / "_images").mkdir(parents=True)
    shutil.copy2(src / "Platforms/_images/tau.bin", p / "_images" / f"{platform}.bin")
    save(p / f"{platform}.json", {"platform":{"category":"Media Players","name":"TAU SDRAM Stress","year":2026,"manufacturer":"alfatreze"}})
    print(core_id, digest(c / "bitstream.rbf_r"), digest(a / "common/tau.rom"))
if __name__ == "__main__": main()
