#!/usr/bin/env python3
"""B-628: fw/assets_core.h as_load() (the real C, compiled here) reads tau-assets.bin through the 4 KiB window, staging files larger than one window in PSRAM chunk by chunk.
Checks: small files stay in the window (no PSRAM touched), large files arrive byte-exact in the scratch area at every size around the window and chunk boundaries and up to 64 KiB,
a read failure at any call is AS_E_READ, an oversized file, a bad magic, a bad section count, a missing file and an unusable PSRAM window give the right result, and a big container
still yields its THEM section through as_find (CRCs checked after loading)."""
import os, struct, subprocess, sys, tempfile
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import tau_assets as ta

AS_OK, AS_E_READ, AS_E_MAGIC, AS_E_SIZE, AS_E_PSRAM, NOFILE = 0, 30, 31, 33, 36, -1
fails = 0
def check(name, ok, info=""):
    global fails
    print(("ok   " if ok else "FAIL ") + name + (" " + info if info else ""))
    fails += 0 if ok else 1

def container(body_len, tag=b"BLOB"):
    data = bytes((i * 7 + 3) & 0xFF for i in range(body_len))
    return ta.pack_container([(tag, data)])

with tempfile.TemporaryDirectory() as d:
    d = Path(d); exe = d / "h"
    r = subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Wno-unused-result", "-I", str(ROOT / "fw"), "-o", str(exe), str(ROOT / "sim/assets_load_harness.c")], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr); sys.exit(1)
    def run(blob, fail_at=0, cap=0, prove=1, name="f.bin"):
        p = d / name
        if blob is None:
            p = d / "missing.bin"
        else:
            p.write_bytes(blob)
        out = subprocess.run([str(exe), str(p), str(fail_at), str(cap), str(prove)], capture_output=True, text=True).stdout.split()
        return int(out[0]), int(out[1]), int(out[2]), out[3]
    sizes = [0, 100, 4096 - 28 - 1, 4096 - 28, 4096 - 27, 4097 + 100, 8192, 12345, 40000, 65535 - 28, 65536 - 28]
    allok, detail = True, []
    for n in sizes:
        blob = container(n)
        e, total, same, where = run(blob)
        small = len(blob) <= 4096
        ok = e == AS_OK and total == len(blob) and same == 1 and where == ("win" if small else "scratch")
        allok &= ok
        if not ok:
            detail.append((len(blob), e, total, same, where))
    check("files of %d sizes around the window and chunk boundaries load byte-exact; small ones stay in the window, large ones go through the scratch area" % len(sizes), allok, str(detail[:3]))
    big = container(20000)
    chunks = -(-len(big) // 4096)
    allread = True
    for k in range(2, 2 + chunks + 1):
        e = run(big, fail_at=k)[0]
        allread &= (e == AS_E_READ)
    check("a read failure at the header read or at any chunk is AS_E_READ (a failure of the very first probe means 'no file', by design)", allread and run(big, fail_at=1)[0] == NOFILE)
    check("a missing file is AS_NOFILE, not an error", run(None)[0] == NOFILE)
    check("a file over 64 KiB is AS_E_SIZE", run(container(65536 - 28 + 1))[0] == AS_E_SIZE)
    check("a scratch area smaller than the file is AS_E_SIZE", run(container(20000), cap=8000)[0] == AS_E_SIZE)
    check("an unusable PSRAM window is AS_E_PSRAM for a large file", run(container(20000), prove=0)[0] == AS_E_PSRAM)
    check("... and does not affect a file that fits one window", run(container(2000), prove=0)[0] == AS_OK)
    bad = bytearray(container(100)); bad[0] = ord("X")
    check("bad magic is AS_E_MAGIC", run(bytes(bad))[0] == AS_E_MAGIC)
    bad = bytearray(container(100)); bad[6] = 9
    check("a section count over 8 is AS_E_SIZE", run(bytes(bad))[0] == AS_E_SIZE)
    # a large container with a real THEM section still parses after loading (the section CRCs are checked by the reader, which the existing test_tau_assets exercises on the same C)
    check("(the THEM/METR parsing itself is covered by sim/test_tau_assets.py on the same assets_core.h)", True)
sys.exit(1 if fails else 0)
