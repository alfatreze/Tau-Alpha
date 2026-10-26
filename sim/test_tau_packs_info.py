#!/usr/bin/env python3
"""tools/tau_packs_info.py, the read-only view of tau-packs.bin (docs/features/TAU_PACKS_FORMAT.md), on the real bundle of TAU DEV 184 (docs/research/p2_packs/tau-packs_dev184.bin): seven packs in slot order, every CRC good, the
descriptors read (names, parameters), the committed listing equal to a fresh run; a flipped byte fails one CRC, a cut file and a wrong magic or version are refused, trailing bytes are refused."""
import json, struct, subprocess, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_packs_info as tpi
fails = 0
def check(name, ok, extra=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ((" " + str(extra)[:300]) if (extra and not ok) else ""))
    if not ok: fails += 1
def refused(b):
    try: tpi.parse(b); return False
    except ValueError: return True
data = (ROOT / "docs/research/p2_packs/tau-packs_dev184.bin").read_bytes()
r = tpi.parse(data)
check("the real bundle of TAU DEV 184: 7 packs in slot order, every CRC good", r["count"] == 7 and [p["slot"] for p in r["packs"]] == list(range(7)) and all(p["crc_ok"] for p in r["packs"]) and r["bytes"] == len(data), str(r["count"]))
check("the descriptors read: the meter names, parameters and presets", [p["name"] for p in r["packs"]] == ["WINAMP BARS", "WINAMP SCOPE", "CHLADNI", "LAYERED WAVE", "VU MASTER", "WATERFALL", "SOUND MAP"]
      and all(p["descriptor"]["params"] and p["descriptor"]["presets"] for p in r["packs"]))
check("the ABI each pack needs: Waterfall 1.2 (the horizontal scroll), SOUND MAP 1.1 (the statistics), the others 1.0", [p["abi_minor"] for p in r["packs"]] == [0, 0, 0, 0, 0, 2, 1] and all(p["abi_major"] == 1 for p in r["packs"]))
check("the committed listing equals a fresh run", json.loads((ROOT / "docs/research/p2_packs/tau-packs_dev184.listing.json").read_text()) == json.loads(json.dumps(r)))
bad = bytearray(data); bad[8 + 48 + 100] ^= 1
check("a flipped byte in a pack's image fails that pack's CRC only", [p["crc_ok"] for p in tpi.parse(bytes(bad))["packs"]].count(False) == 1)
check("a file cut short, a wrong magic, another bundle version and trailing bytes are refused", refused(data[:-10]) and refused(b"NOPE" + data[4:]) and refused(data[:4] + struct.pack("<H", 2) + data[6:]) and refused(data + b"\0\0"))
check("the command line exits 0 on the real bundle", subprocess.run([sys.executable, str(ROOT / "tools/tau_packs_info.py"), str(ROOT / "docs/research/p2_packs/tau-packs_dev184.bin")], capture_output=True).returncode == 0)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
