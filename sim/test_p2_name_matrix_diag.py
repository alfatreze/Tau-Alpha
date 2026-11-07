#!/usr/bin/env python3
"""The accented-name matrix in the Diagnostic Build (BUG-001 step 3; fw2/diagnostics/name_matrix.c; the card side is tools/name_matrix.py): the REAL firmware file on the host against a model of the card whose files are the ones the tool writes.
  1. an ideal card: every 'o' variant opens the right file (nibble 1), the negative controls fail cleanly (the over-limit path is refused by the firmware: nibble 6; the missing file and the invalid-UTF-8 path: 4); nm_bad is 0;
  2. a card on which a decomposed (NFD) name is not found (the Pocket or APF normalising): those nibbles are 4 and nm_bad counts them; opening ANOTHER file under a name (the ID3 title is not NM<id>): nibble 2; a read failure: 3; another target
     code: 5; a timeout: 7; a negative control that opens: counted as unmet;
  3. nothing runs without the card's help: no matrix.tnm (nm_err 2), a bad header (5), the audio under pressure (4, not run), no library root (1);
  4. the manifest the tool writes is exactly the 2,048 bytes the firmware reads, and the firmware's idea of the title position (byte 21) is the tool's MP3 layout;
  5. mutant: the firmware not checking the title lets a wrong file pass (the test sees it).
Host-simulated: it does not say what the Pocket does with the names (that is the run on a Pocket)."""
import re, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import name_matrix as nm
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:500]))
    fails += 0 if ok else 1
HDRS = {"fw2/stream/slot.h": "#include <stdint.h>\nint slot_open_path(uint32_t, const char *); int slot_read_blocking(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);\nextern uint8_t slot_err, slot_errcode;\nenum { SLOT_E_OK = 0, SLOT_E_NOTEMPLATE = 1, SLOT_E_TOOLONG = 2, SLOT_E_OPEN = 3, SLOT_E_TIMEOUT = 4 };\n",
        "fw2/stream/library.h": "#include <stdint.h>\nuint32_t library_root(char *out, uint32_t cap);\n",
        "fw2/platform/mmio.h": "#include <stdint.h>\n#define CLK_HZ 66666667u\n#define P2_COLD_TEXT\n",
        "fw2/platform/platform.h": "#include <stdint.h>\n",
        "fw2/aout/aout.h": "#include <stdint.h>\ntypedef struct { uint32_t dry, under_ring; } aout_stats_t;\nextern aout_stats_t aout_stats;\n"}
def build(td, mut=None):
    d = Path(td)
    for rel, txt in HDRS.items(): (d / rel).parent.mkdir(parents=True, exist_ok=True); (d / rel).write_text(txt)
    (d / "fw2/diagnostics").mkdir(parents=True, exist_ok=True); (d / "sim").mkdir(exist_ok=True)
    src = (ROOT / "fw2/diagnostics/name_matrix.c").read_text()
    if mut: assert mut[0] in src; src = src.replace(*mut)
    (d / "fw2/diagnostics/name_matrix.c").write_text(src); (d / "fw2/diagnostics/name_matrix.h").write_text((ROOT / "fw2/diagnostics/name_matrix.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_name_matrix_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr[-1500:]); sys.exit(1)
    return d / "h"
def card(drop=(), swap=None, extra=(), root="/Assets/tau/common/"):
    """the model lines for a card carrying what tools/name_matrix.py writes (minus `drop` variants, with `swap`: {id: other id's content}, plus the `extra` paths)"""
    fs = nm.files(root); vs = nm.variants(root); lines = ["root " + root]
    prefix = (root + nm.DIR + "/").encode()
    lines.append("file %s %s" % ((prefix + b"matrix.tnm").hex(), fs[b"matrix.tnm"].hex()))
    for vid, expect, what, path in vs:
        rel = path[len(prefix):]
        if rel in fs and vid not in drop: lines.append("file %s %s" % (path.hex(), (nm.mp3(swap[vid]) if swap and vid in swap else fs[rel])[:64].hex()))      # only the first bytes matter (the firmware reads 32)
    for p in extra: lines.append("file %s %s" % (p.hex(), nm.mp3(14)[:64].hex()))
    return lines, vs
def run(exe, lines): 
    out = subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True).stdout.split()
    lo, hi = int(out[0], 16), int(out[1], 16); return [(lo >> (4 * i)) & 15 for i in range(8)] + [(hi >> (4 * i)) & 15 for i in range(8)], int(out[2]), int(out[3]), int(out[5])
with tempfile.TemporaryDirectory() as td:
    exe = build(td)
    lines, vs = card(); nib, bad, err, ret = run(exe, lines)
    want = [1] * 13 + [6, 4, 4]
    check("1. an ideal card: nibbles %s, nm_bad 0, nm_err 0" % "".join("%x" % n for n in nib), nib == want and bad == 0 and err == 0 and ret == 0, (nib, bad, err))
    nib, bad, err, ret = run(exe, card(drop=(2, 9))[0])
    check("2. NFD names not found (variants 2 and 9): nibble 4 each, nm_bad 2", nib[2] == 4 and nib[9] == 4 and bad == 2 and [n for i, n in enumerate(nib) if i not in (2, 9)] == [w for i, w in enumerate(want) if i not in (2, 9)], (nib, bad))
    nib, bad, err, ret = run(exe, card(swap={5: 4})[0])
    check("2. a name that opens ANOTHER file (the title says NM04, not NM05): nibble 2, nm_bad 1", nib[5] == 2 and bad == 1, (nib, bad))
    l, _ = card(); p6 = vs[6][3].hex(); nib, bad, err, ret = run(exe, l + ["failread " + p6])
    check("2. a file that opens but does not read: nibble 3", nib[6] == 3 and bad == 1, (nib, bad))
    nib, bad, err, ret = run(exe, l + ["errcode %s 7" % vs[7][3].hex()]); check("2. an open that fails with another target code: nibble 5", nib[7] == 5 and bad == 1, (nib, bad))
    nib, bad, err, ret = run(exe, l + ["timeout " + vs[8][3].hex()]); check("2. an open that times out: nibble 7", nib[8] == 7 and bad == 1, (nib, bad))
    nib, bad, err, ret = run(exe, card(extra=(vs[14][3],))[0]); check("2. a negative control that opens (the missing file is there): counted as unmet", nib[14] == 2 or nib[14] == 1, nib) 
    check("2. ... and nm_bad counts it", bad == 1, (nib, bad))
    nib, bad, err, ret = run(exe, [l for l in card()[0] if "6d61747269782e746e6d" not in l]); check("3. no matrix.tnm on the card: nm_err 2 and nothing else", err == 2 and nib == [0] * 16, (nib, err))
    ln = card()[0]; ln = [l.replace("544e4d31", "544e4d32") if "6d61747269782e746e6d" in l else l for l in ln]; nib, bad, err, ret = run(exe, ln)
    check("3. a bad header (TNM2): nm_err 5", err == 5 and nib == [0] * 16, (nib, err))
    nib, bad, err, ret = run(exe, ["degrade 1"] + card()[0]); check("3. the audio under pressure: not run (nm_err 4)", err == 4 and nib == [0] * 16, (nib, err))
    nib, bad, err, ret = run(exe, ["root "] + card()[0][1:]); check("3. no library root: nm_err 1", err == 1, (nib, err))
    fs = nm.files(); man = fs[b"matrix.tnm"]
    check("4. matrix.tnm is exactly 2,048 bytes (a fixed read works)", len(man) == 2048)
    mp = nm.mp3(7); check("4. the title 'NM07' sits at byte 21 of the MP3 the tool writes (the firmware reads it there)", mp[:3] == b"ID3" and mp[21:25] == b"NM07")
    src_ok = (ROOT / "fw2/diagnostics/name_matrix.c").read_text()
    check("4. the firmware's slot and size constants match the tool (slot 7, 2,048-byte manifest, 16 variants)", "#define NM_SLOT 7u" in src_ok and "#define NM_MANIFEST 2048u" in src_ok and "#define NM_MAX 16u" in src_ok and nm.MANIFEST == 2048 and len(nm.variants()) == 16)
    # mutant: the title is not checked
    with tempfile.TemporaryDirectory() as td2:
        exe2 = build(td2, ("h[23] == (uint8_t)('0' + id / 10u) && h[24] == (uint8_t)('0' + id % 10u)", "1"))
        nib, bad, err, ret = run(exe2, card(swap={5: 4})[0]); check("5. mutant (the title is not checked): a wrong file passes, and the test sees it", nib[5] == 1 and bad == 0)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
