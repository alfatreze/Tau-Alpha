#!/usr/bin/env python3
"""fw/suite_core.h (real firmware code under rv32sim) against tools/decode_tau_suite.py: record bytes, QR text, persisted words
and short code must equal the Python reference; corruption must be rejected; unknown tags must be skipped."""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import decode_tau_suite as D

GCC = ROOT / "toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-gcc"
fails = []


def check(name, ok, info=""):
    if not ok:
        fails.append(name)
    print(("ok   " if ok else "FAIL ") + name + (f"  {info}" if info and not ok else ""))


def run_fw(tmp):
    elf = Path(tmp) / "suite.elf"
    cmd = [str(GCC), "-march=rv32im", "-mabi=ilp32", "-mno-relax", "-O2", "-ffreestanding", "-nostdlib", "-nostartfiles",
           "-Wall", "-Wextra", "-Wno-unused-function", "-Wl,--no-warn-rwx-segments", "-T", str(ROOT / "tools/host/link.ld"),
           str(ROOT / "tools/host/start.S"), str(ROOT / "tools/host/suite_harness.c"), "-o", str(elf)]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode or "warning" in r.stderr:
        print(r.stderr)
        raise SystemExit("suite harness build failed or warned")
    f = Path(tmp) / "in.bin"
    f.write_bytes(b"\0")
    r = subprocess.run([sys.executable, str(ROOT / "tools/rv32sim.py"), str(elf), str(f)], capture_output=True, text=True)
    return {ln.split()[0]: ln.split(None, 1)[1] for ln in r.stdout.splitlines() if ln[:3] in ("REC", "TXT", "WOR", "SHO")}


def le(width, *v):
    return b"".join(x.to_bytes(width, "little") for x in v)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        fw = run_fw(tmp)
    ref = D.build_record(1, [(1, le(4, 0x0300, 0x4D503317, 0x1F, 5008)),
                             (3, bytes([0, 0]) + le(4, 89)), (3, bytes([1, 0]) + le(4, 380)), (3, bytes([2, 0]) + le(4, 1049216)),
                             (3, bytes([3, 1]) + le(4, 12)), (3, bytes([4, 2]) + le(4, 0)),
                             (4, le(2, 48, 57, 31, 37)), (8, le(2, 2, 0, 3, 380)), (13, le(2, 5, 13, 57, 68))])
    check("record bytes", fw.get("REC") == ref.hex(), fw.get("REC"))
    check("qr text", fw.get("TXT") == D.to_text(ref), fw.get("TXT"))
    rec = D.parse_record(D.from_text(fw["TXT"]))
    check("decode verdict", rec["verdict"] == "some checks failed" and len(rec["tests"]) == 5)
    check("decode build", rec["entries"]["build"] == {"firmware": "0.3.0", "bitstream": "4D503317", "flags": 31, "heap_gap": 5008}, rec["entries"].get("build"))
    check("decode sdram legacy", rec["entries"]["sdram"] == {"read_avg": 48, "read_max": 57, "write_avg": 31, "write_max": 37}, rec["entries"]["sdram"])
    six = D.parse_record(D.build_record(1, [(4, le(2, 48, 219, 506, 31, 62, 325))]))
    check("decode sdram six", six["entries"]["sdram"]["read_min"] == 48 and six["entries"]["sdram"]["write_max"] == 325)
    check("decode audio", rec["entries"]["audio"] == [2, 0, 3, 380])
    check("decode decprof", rec["entries"]["decprof"] == {"h_pct": 5, "i_pct": 13, "s_pct": 57, "r_pct": 68}, rec["entries"].get("decprof"))
    # persisted words: fields at the documented bit positions
    w = [int(x, 16) for x in fw["WORDS"].split()]
    exp = [1 | 1 << 4 | 7 << 7 | 2 << 15, 0x0007 | 0x0008 << 15, 380 | 316 << 9 | 0 << 18 | 3 << 24, 154 | 0 << 12 | 18 << 18 | 3 << 24]
    check("persist words", w == exp, f"{w} {exp}")
    d = D.unpack_words(w)
    check("persist decode", d["run"] == 7 and d["failed"] == ["Cold code test"] and d["cold_cycles_per_word"] == 31.6 and d["last_load_s"] == 15.4, d)
    check("short code", fw["SHORT"] == D.short_code(w, 0x4D503317), fw["SHORT"])
    w2, rev = D.from_short(fw["SHORT"][:6] + "-" + fw["SHORT"][6:].lower())
    check("short round trip", w2 == w and rev == 0x4D503317)
    # rejection and extension
    bad = bytearray(ref); bad[9] ^= 1
    try:
        D.parse_record(bytes(bad)); check("corrupt rejected", False)
    except ValueError:
        check("corrupt rejected", True)
    try:
        D.parse_record(ref[:-3]); check("truncated rejected", False)
    except ValueError:
        check("truncated rejected", True)
    ext = D.parse_record(D.build_record(1, [(3, bytes([0, 0]) + le(4, 1)), (99, b"\x01\x02")]))
    check("unknown tag skipped", ext["unknown"] == [{"tag": 99, "hex": "0102"}] and ext["verdict"] == "all checks passed")
    try:
        D.from_short("0" * 35 + "1"); check("short typo rejected", False)
    except ValueError:
        check("short typo rejected", True)
    print("PASSED" if not fails else f"FAILED {fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    raise SystemExit(main())
