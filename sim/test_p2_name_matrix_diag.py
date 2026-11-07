#!/usr/bin/env python3
"""The accented-name matrix in the Diagnostic Build (BUG-001; fw2/diagnostics/name_matrix.c, round 2 = B-992; the card side is tools/name_matrix.py): the REAL firmware file on the host against a model of the card whose files are the ones the tool writes.
  1. a card that opens ASCII paths only (what the Pocket did in round 1: NM BAD 10): nibbles, error codes (a missing file answers code 3, FILE NOT FOUND, in the 0192 docs; an over-limit path 10), nm_bad 10; a card that answers code 4 (MALFORMED PATH) for the non-ASCII names gives nibble 4, not 5;
  2. (round 2 first planned retries with other length-word values; the 0192 struct has NO length word (docs: a 256-byte NUL-terminated path, flags, size), so that was dropped);
  3. single-byte encodings: a card that also opens the Latin-1 / code page 850 path of a file (variants 17, 18, 20, 21) shows nibble 1 for them (a probe, not counted in nm_bad); probes never count as unmet;
  4. other outcomes: a name that opens another file (the title is not NM<file id>), a failed read, another target code, a timeout, a negative control that opens (counted);
  5. nothing runs without the card's help: no matrix.tnm (nm_err 2), a bad header (5), the audio under pressure (4), no library root (1);
  6. the contracts with the tool: matrix.tnm is exactly 3,072 bytes, the title 'NM07' sits at byte 21, the constants agree, every nibble/word layout holds 22 variants in three words;
  6b. Select+Start (the Report) runs the matrix once per boot by itself, so no Check run is needed first;
  7. mutant: the title not checked (a wrong file passes).
Host-simulated: it does not say what the Pocket does (that is the run on a Pocket)."""
import subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import name_matrix as nm
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + ("" if ok else "  " + str(info)[:600]))
    fails += 0 if ok else 1
HDRS = {"fw2/stream/slot.h": "#include <stdint.h>\nint slot_open_path(uint32_t, const char *); int slot_open_path_len(uint32_t, const char *, uint32_t); int slot_read_blocking(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);\nextern uint8_t slot_err, slot_errcode;\nenum { SLOT_E_OK = 0, SLOT_E_NOTEMPLATE = 1, SLOT_E_TOOLONG = 2, SLOT_E_OPEN = 3, SLOT_E_TIMEOUT = 4 };\n",
        "fw2/stream/library.h": "#include <stdint.h>\nuint32_t library_root(char *out, uint32_t cap);\n",
        "fw2/platform/mmio.h": "#include <stdint.h>\n#define CLK_HZ 66666667u\n#define P2_COLD_TEXT\n",
        "fw2/platform/platform.h": "#include <stdint.h>\n",
        "fw2/aout/aout.h": "#include <stdint.h>\ntypedef struct { uint32_t dry, under_ring; } aout_stats_t;\nextern aout_stats_t aout_stats;\n"}
def build(td, mut=None):
    d = Path(td)
    for rel, txt in HDRS.items(): (d / rel).parent.mkdir(parents=True, exist_ok=True); (d / rel).write_text(txt)
    (d / "fw2/diagnostics").mkdir(parents=True, exist_ok=True); (d / "sim").mkdir(exist_ok=True)
    src = (ROOT / "fw2/diagnostics/name_matrix.c").read_text()
    if mut: assert mut[0] in src, mut[0]; src = src.replace(*mut)
    (d / "fw2/diagnostics/name_matrix.c").write_text(src); (d / "fw2/diagnostics/name_matrix.h").write_text((ROOT / "fw2/diagnostics/name_matrix.h").read_text())
    (d / "sim/h.c").write_text((ROOT / "sim/p2_name_matrix_harness.c").read_text())
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-w", "-o", str(d / "h"), str(d / "sim/h.c")], capture_output=True, text=True)
    if r.returncode: print(r.stderr[-1500:]); sys.exit(1)
    return d / "h"
ROOTP = "/Assets/tau/common/"
def card(only_ascii=True, extra=(), drop=(), swap=None):
    """the model lines for a card that carries what tools/name_matrix.py writes. only_ascii: only the paths without a byte >= 0x80 are found (round 1); need: {variant: needmode}; extra: alias paths that open (variant ids 17, 18, 20, 21)"""
    fs = nm.files(ROOTP); vs = nm.variants(ROOTP); prefix = (ROOTP + nm.DIR + "/").encode()
    lines = ["root " + ROOTP, "file %s %s" % ((prefix + b"matrix.tnm").hex(), fs[b"matrix.tnm"].hex())]
    for vid, expect, what, path in vs:
        rel = path[len(prefix):]
        if rel not in fs or vid in drop or vid in nm.FID: continue
        if only_ascii and any(b >= 0x80 for b in path): continue
        lines.append("file %s %s" % (path.hex(), (nm.mp3(swap[vid]) if swap and vid in swap else fs[rel])[:64].hex()))
    for vid in extra: lines.append("file %s %s" % (vs[vid][3].hex(), nm.mp3(nm.FID[vid])[:64].hex()))
    return lines, vs
def run(exe, lines):
    o = subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True).stdout.split()
    w = [int(x, 16) for x in o[:6]]; nib = lambda ws: [(ws[i // 8] >> (4 * (i % 8))) & 15 for i in range(24)]
    return nib(w[0:3]), nib(w[3:6]), int(o[6]), int(o[7]), int(o[8])
with tempfile.TemporaryDirectory() as td:
    exe = build(td)
    # 1. round 1
    L, vs = card(); res, cod, bad, err, dry = run(exe, L)
    asc = [not any(b >= 0x80 for b in v[3]) and v[1] != "f" for v in vs]
    want_res = [1 if asc[i] else 5 for i in range(22)]; want_res[13] = 6; want_res[14] = 5; want_res[15] = 5
    check("1. a card that opens ASCII paths only: nibbles %s, nm_bad 10 (as the Pocket answered in round 1)" % "".join("%x" % n for n in res[:22]), res[:22] == want_res and bad == 10 and err == 0, (res[:22], bad, err))
    check("1. the error codes: 0 where it opened, 3 (file not found) where the card said no, 10 for the over-limit path", all(cod[i] == (0 if want_res[i] == 1 else 10 if i == 13 else 3) for i in range(22)), cod[:22])
    res, cod, bad, err, dry = run(exe, ["missing 4"] + L)
    check("1. a card that answers MALFORMED PATH (code 4) instead: nibble 4, code 4 for every name it does not find", all(res[i] == 4 and cod[i] == 4 for i in range(22) if want_res[i] == 5 and i != 13), (res[:22], cod[:22]))
    # 3. single-byte encodings
    res, cod, bad, err, dry = run(exe, card(extra=(17, 18, 20, 21))[0])
    check("3. the Latin-1 / code page 850 paths that open show nibble 1 (variants 17, 18, 20, 21), the UTF-8 ones (16, 19) still fail", [res[i] for i in (16, 17, 18, 19, 20, 21)] == [5, 1, 1, 5, 1, 1] and bad == 10, (res[16:22], bad))
    # 4. other outcomes
    base = card(only_ascii=False)[0]; vs0 = vs
    res, cod, bad, err, dry = run(exe, card(only_ascii=False, swap={5: 4})[0]); check("4. a name that opens ANOTHER file (title NM04, not NM05): nibble 2", res[5] == 2 and bad == 1, (res[5], bad))
    res, cod, bad, err, dry = run(exe, base + ["failread " + vs0[6][3].hex()]); check("4. a file that opens but does not read: nibble 3", res[6] == 3, res[6])
    res, cod, bad, err, dry = run(exe, base + ["errcode %s 7" % vs0[7][3].hex()]); check("4. an open that fails with target code 7: nibble 5, code 7", res[7] == 5 and cod[7] == 7, (res[7], cod[7]))
    res, cod, bad, err, dry = run(exe, base + ["timeout " + vs0[8][3].hex()]); check("4. an open that times out: nibble 7, code 12", res[8] == 7 and cod[8] == 12, (res[8], cod[8]))
    res, cod, bad, err, dry = run(exe, base + ["file %s %s" % (vs0[14][3].hex(), nm.mp3(14)[:64].hex())]); check("4. a negative control that opens is counted as unmet", res[14] in (1, 2) and bad == 1, (res[14], bad))
    # 5. no help from the card
    res, cod, bad, err, dry = run(exe, [l for l in card()[0] if "6d61747269782e746e6d" not in l]); check("5. no matrix.tnm: nm_err 2, nothing run", err == 2 and not any(res), (res, err))
    ln = [l.replace("544e4d32", "544e4d31") if "6d61747269782e746e6d" in l else l for l in card()[0]]; res, cod, bad, err, dry = run(exe, ln); check("5. a bad header (TNM1): nm_err 5", err == 5 and not any(res), (res, err))
    res, cod, bad, err, dry = run(exe, ["degrade 1"] + card()[0]); check("5. the audio under pressure: not run (nm_err 4)", err == 4 and not any(res), (res, err))
    res, cod, bad, err, dry = run(exe, ["root "] + card()[0][1:]); check("5. no library root: nm_err 1", err == 1, err)
    # 6. contracts with the tool
    fs = nm.files(); src = (ROOT / "fw2/diagnostics/name_matrix.c").read_text()
    check("6. matrix.tnm is exactly 3,072 bytes and the firmware's constants agree (slot 7, 3,072, 24 variants at most)", len(fs[b"matrix.tnm"]) == 3072 == nm.MANIFEST and "#define NM_MANIFEST 3072u" in src and "#define NM_SLOT 7u" in src and "#define NM_MAX 24u" in src and len(nm.variants()) <= 24 and "slot_open_path_len" not in src)
    mp = nm.mp3(7); check("6. the title 'NM07' sits at byte 21 of the MP3 the tool writes; the manifest header is TNM2 and the lines carry the file id", mp[21:25] == b"NM07" and fs[b"matrix.tnm"].startswith(b"TNM2 22\n") and b"\n17|p|16|" in fs[b"matrix.tnm"])
    # 6b. Select+Start: the matrix runs once by itself (name_matrix_auto), a run refused for audio pressure does not use the turn
    def auto(lines, **env):
        import os
        o = subprocess.run([str(exe)], input="\n".join(lines) + "\n", capture_output=True, text=True, env=dict(os.environ, NM_AUTO="1", **env)).stdout.split()
        return int(o[1]), int(o[-1])
    ar, _ = auto(card()[0]); check("6b. name_matrix_auto runs the matrix once (the second call does nothing): runs counted 1 then 1 (sum %d)" % ar, ar == 2)
    ar, _ = auto(["degrade 1"] + card()[0]); check("6b. a run refused for audio pressure does not use the turn (nm_runs stays 0)", ar == 0)
    # 7. mutants
    with tempfile.TemporaryDirectory() as t2:
        e2 = build(t2, ("h[23] == (uint8_t)('0' + fid / 10u) && h[24] == (uint8_t)('0' + fid % 10u)", "1"))
        res, cod, bad, err, dry = run(e2, card(only_ascii=False, swap={5: 4})[0]); check("7. mutant (the title is not checked): a wrong file passes, and the test sees it", res[5] == 1 and bad == 0)
print("PASSED" if not fails else "FAILED (%d)" % fails); sys.exit(1 if fails else 0)
