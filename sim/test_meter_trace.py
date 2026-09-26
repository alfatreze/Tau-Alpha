#!/usr/bin/env python3
"""End to end for the meter trace pipeline (M3), without hardware: a synthetic 'recorded' trace is encoded exactly as the firmware's
SR_T_METERTRACE record (fw/suite.inc mt_take, same layout, same TAUD1 text), decoded by tools/decode_tau_suite.py (--trace), then replayed
through the REAL firmware drawing code and the JS preview modules; the command logs must be identical (sim/test_meter_golden.py's harness).
That is the "a recorded real trace reproduces exactly" proof; a trace captured on a Pocket enters the same path."""
import json, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import decode_tau_suite as D  # noqa: E402


def synth(n=20):
    s, out = 4242, []
    def r():
        nonlocal s
        s = (s * 1664525 + 1013904223) & 0xFFFFFFFF
        return s >> 8
    lvl = [0] * 16
    for i in range(n):
        for b in range(16):
            v = (r() % 220) if (i + b) % 5 else (r() % 60)
            lvl[b] = v if v >= lvl[b] else lvl[b] - ((lvl[b] - v) // 4 + 1)     # the firmware ballistics, as a recorder would see them
        wave = [((r() % 201) - 100) for _ in range(64)]
        out.append((0 if i == 0 else 26 + (r() % 3), list(lvl), wave))
    return out


def main():
    frames = synth()
    entries = [(1, bytes(16))]                                  # SR_T_BUILD, 4 x u32, as mt_begin() writes it
    for dt, spec, wave in frames:
        entries.append((21, dt.to_bytes(2, "little") + bytes(spec) + bytes(w & 0xFF for w in wave)))
    rec = D.build_record(8, entries)
    if len(rec) > 2048:
        print("FAIL: a 20-frame trace record exceeds the firmware's 2048-byte record capacity:", len(rec)); sys.exit(1)
    text = D.to_text(rec)
    if len(text) > 2331:      # byte-mode capacity of QR version 38 at error level L, the largest the firmware encoder builds (fw/qrcode.h)
        print("FAIL: the trace text is %d characters, more than one QR (v38-L, 2331 bytes) holds" % len(text)); sys.exit(1)
    with tempfile.TemporaryDirectory() as d:
        out = Path(d) / "trace.json"
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "decode_tau_suite.py"), "--text", text, "--trace", str(out)], capture_output=True, text=True)
        if r.returncode:
            print(r.stderr); sys.exit(1)
        tr = json.loads(out.read_text())
        if [(f["dt_ms"], f["spec"], f["wave"]) for f in tr["frames"]] != frames:
            print("FAIL: the decoded trace differs from what was recorded"); sys.exit(1)
        # replay: the golden harness with this trace instead of the demo source
        g = subprocess.run([sys.executable, str(ROOT / "sim" / "test_meter_golden.py"), "--trace", str(out)], capture_output=True, text=True)
        print(g.stdout.strip())
        if g.returncode:
            print(g.stderr); sys.exit(1)
    print(f"meter trace OK: {len(frames)} frames, record {len(rec)} bytes ({len(text)} chars), decode and replay identical")


if __name__ == "__main__":
    main()
