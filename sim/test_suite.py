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
                             (4, le(2, 48, 57, 31, 37)), (8, le(2, 2, 0, 3, 380)), (13, le(2, 5, 13, 57, 68)),
                             (14, bytes([0, 100, 3, 0, 11, 0, 55, 0, 0, 0]) + b"Trk A"), (14, bytes([1, 125, 0, 0, 0, 0, 0, 0, 68, 0]) + b"Trk B")])
    check("record bytes", fw.get("REC") == ref.hex(), fw.get("REC"))
    check("qr text", fw.get("TXT") == D.to_text(ref), fw.get("TXT"))
    rec = D.parse_record(D.from_text(fw["TXT"]))
    check("decode verdict", rec["verdict"] == "some checks failed" and len(rec["tests"]) == 5)
    check("decode build", rec["entries"]["build"] == {"firmware": "0.3.0", "bitstream": "4D503317", "flags": 31, "heap_gap": 5008}, rec["entries"].get("build"))
    check("decode sdram legacy", rec["entries"]["sdram"] == {"read_avg": 48, "read_max": 57, "write_avg": 31, "write_max": 37}, rec["entries"]["sdram"])
    six = D.parse_record(D.build_record(1, [(4, le(2, 48, 219, 506, 31, 62, 325))]))
    check("decode sdram six", six["entries"]["sdram"]["read_min"] == 48 and six["entries"]["sdram"]["write_max"] == 325)
    # CT_BLT (B-139): value packs busy-permille (low 16) + "audio ran the whole window" (bit 16).
    blt_full = D.parse_record(D.build_record(1, [(3, bytes([13, 0]) + le(4, 42 | 0x10000))]))["tests"][0]
    check("decode blt full", blt_full["busy_permille"] == 42 and blt_full["audio_full"] is True, blt_full)
    blt_dropped = D.parse_record(D.build_record(1, [(3, bytes([13, 1]) + le(4, 0xFFFF))]))["tests"][0]
    check("decode blt dropped/no-counter", blt_dropped["busy_permille"] is None and blt_dropped["audio_full"] is False, blt_dropped)
    # CT_TRK (B-165): a FAIL packs changes done (low 8) + queue length (next 16) + lib_src (bit 24) + skip_req still pending (bit 25).
    trk_fail_val = 3 | (11 << 8) | (1 << 24) | (1 << 25)
    trk_fail = D.parse_record(D.build_record(1, [(3, bytes([11, 1]) + le(4, trk_fail_val))]))["tests"][0]
    check("decode trk fail", trk_fail == {"id": 11, "name": "Track changes (10)", "result": "FAIL",
          "value": trk_fail_val, "changes_done": 3, "queue_len": 11, "lib_src": True, "skip_req_pending": True}, trk_fail)
    trk_pass = D.parse_record(D.build_record(1, [(3, bytes([11, 0]) + le(4, 10))]))["tests"][0]
    check("decode trk pass (unpacked)", trk_pass == {"id": 11, "name": "Track changes (10)", "result": "PASS", "value": 10}, trk_pass)
    # Blit Test (B-166): one entry per (opcode, level) -- op_id u8, level u8, result u8, stall_pct u8, ops_done u16 LE.
    bt = D.parse_record(D.build_record(8, [(15, bytes([4, 2, 0, 37]) + le(2, 812))]))["entries"]["blittest"]
    check("decode blit test", bt == [{"op": "BLIT", "level": 2, "result": "PASS", "stall_pct": 37, "ops_done": 812}], bt)
    check("decode audio", rec["entries"]["audio"] == {"late_underruns": 2, "audio_full": False, "stall_ms": 3, "window_s": 380}, rec["entries"].get("audio"))
    check("decode decprof", rec["entries"]["decprof"] == {"h_pct": 5, "i_pct": 13, "s_pct": 57, "r_pct": 68}, rec["entries"].get("decprof"))
    check("decode decsweep", rec["entries"]["decsweep"] == [
        {"track": 0, "title": "Trk A", "speed_pct": 100, "h_pct": 3, "i_pct": 11, "s_pct": 55, "r_pct": 0},
        {"track": 1, "title": "Trk B", "speed_pct": 125, "h_pct": 0, "i_pct": 0, "s_pct": 0, "r_pct": 68}], rec["entries"].get("decsweep"))
    # SR_T_STACK (B-204): peak bytes used (the stack-painting high-water mark) + the region size, u32 each.
    stack = D.parse_record(D.build_record(1, [(16, le(4, 2048, 16384))]))["entries"]["stack"]
    check("decode stack", stack == {"peak_bytes": 2048, "stack_size": 16384, "free_bytes": 14336}, stack)
    # SR_T_WVIZCFG (B-218): a one-off Configure-page export, not part of a Check run -- 13 raw bytes,
    # mixed widths (see fw/suite_core.h's own comment for the exact layout).
    wviz_bytes = bytes([1, 0xFF, 12, 3, 60, 25, 1, 1]) + (250).to_bytes(2, "little") + bytes([30, 45, 20])
    wviz = D.parse_record(D.build_record(0, [(17, wviz_bytes)]))["entries"]["wvizcfg"]
    check("decode wvizcfg", wviz == {
        "mode": "scope", "preset": None, "bands": 12, "ease_mode": "spring", "attack": 60, "release": 25,
        "peak_on": True, "peak_gravity": True, "peak_hold_ms": 250, "peak_fall": 30,
        "scope_smooth": 45, "scope_trail": 20}, wviz)
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
