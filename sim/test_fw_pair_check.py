#!/usr/bin/env python3
"""check_fw_bitstream_pair.py: the accept lists in fw/player.c match the checker's idea, and good/bad pairings are judged right."""
import re, sys, tempfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import check_fw_bitstream_pair as c

ROOT = c.ROOT
src = (ROOT / "fw/player.c").read_text()
consts = {k: v.upper() for k, v in re.findall(r"#define (EXPECT_VERSION\w*) 0x([0-9A-Fa-f]{8})u", src)}
accepts = re.findall(r'#define FW_ACCEPTS "([^"]+)"', src)
want = [[consts["EXPECT_VERSION_192K_CLK66"]],
        [consts["EXPECT_VERSION"], consts["EXPECT_VERSION_192K"]],
        [consts["EXPECT_VERSION"], consts["EXPECT_VERSION_CLK66"]],
        [consts["EXPECT_VERSION"]]]
fails = 0
def ok(cond, msg):
    global fails
    print(("ok   " if cond else "FAIL ") + msg); fails += 0 if cond else 1

ok([a.split(",") for a in accepts] == want, "FW_ACCEPTS strings equal the VERSION_OK constants in fw/player.c")
ok(consts["EXPECT_VERSION_192K_CLK66"] == c.tree_core_version(), "CORE_VERSION in mp3_soc.v equals the 192K+CLK66 interlock constant")

def pkg(td, blob):
    d = Path(td) / "Assets/tau/common"; d.mkdir(parents=True); (d / "tau.rom").write_bytes(blob); return td
with tempfile.TemporaryDirectory() as t1, tempfile.TemporaryDirectory() as t2, tempfile.TemporaryDirectory() as t3, tempfile.TemporaryDirectory() as t4:
    good = b"\0" * 8 + b"TAUFWPAIR:4D50331A;" + b"\0"
    old = b"\0" * 8 + b"TAUFWPAIR:4D503317;" + b"\0"
    ok(c.check(pkg(t1, good), "4D50331A") == [], "192K+CLK66 ROM on the rev 26 bitstream passes")
    ok(len(c.check(pkg(t2, old), "4D50331A")) == 1, "unflagged (256 KB) ROM on the rev 26 bitstream is refused")
    ok(len(c.check(pkg(t3, b"\0" * 64), "4D50331A")) == 1, "ROM without a marker is refused")
    ok(c.check(pkg(t4, old), "4D503317") == [], "unflagged ROM on the baseline bitstream passes")
print("PASSED" if not fails else f"{fails} FAILED")
sys.exit(1 if fails else 0)
