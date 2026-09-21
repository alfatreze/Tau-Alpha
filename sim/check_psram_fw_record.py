#!/usr/bin/env python3
"""Verify the interact records dumped by sim/tb_psram_fw.v (firmware-in-the-loop).

Each line is `runN w0 .. w15` (hex). Runs 1-4 are mailbox records (PSR1): 1 = default,
2 = read +1 (key X), 3 = read +2 (key Y), 4 = slow +3/+3 (key B). Runs 5-7 (unless
--no-window) are CPU-window records (PSW1): 5 = window suite (key L1), 6 and 7 = the
first two soak passes (key R1). The decoders recompute every CRC from the deterministic
fill, so a firmware bug that graded itself wrongly is still caught. Options:
  --expect-fault   an injected single-bit fault must be reported on die 2 only
  --t-acc N        the controller's read-sample index (default 9)
  --no-window      the run had no window records (4 records)
  --report         print each record's verdict and do not require PASS
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from decode_tau_diag_log import (PSRAM_WIN_MAGIC, decode_psram,  # noqa: E402
                                 decode_psram_window)

argv = sys.argv[1:]
expect_fault = "--expect-fault" in argv
report = "--report" in argv
no_window = "--no-window" in argv
t_acc = 9
if "--t-acc" in argv:
    t_acc = int(argv[argv.index("--t-acc") + 1])
args = [a for i, a in enumerate(argv) if not a.startswith("--") and (i == 0 or argv[i - 1] != "--t-acc")]
path = Path(args[0] if args else "build/rtl/psram_fw_record.txt")

bad = seen = mailbox_seen = 0
for line in filter(None, path.read_text().split("\n")):
    tag, *hexwords = line.split()
    mask = int(hexwords[15], 16)
    words = [int(hexwords[i], 16) | (((mask >> i) & 1) << 31) for i in range(15)] + [0]
    seen += 1
    if words[0] == PSRAM_WIN_MAGIC:
        rec = decode_psram_window(words)
        print(f"{tag}: {rec['verdict']} window/{rec['mode']} pass={rec['passes']} checks={rec['checks']} "
              f"failures={rec['failures']} cross={rec['cross_check_failures']} guard_ok={rec['guard_ok']} "
              f"chain_ok={rec['crc_chain_match']} cost={rec['cost_cycles']} win_ops={rec['window_ops']}")
        want_mode = "window" if seen == 5 else "soak"
        if rec["mode"] != want_mode or rec["t_acc"] != t_acc:
            print("FAIL: window record mode/index not as expected:", rec["mode"], rec["t_acc"])
            bad += 1
        if report:
            continue
        if expect_fault:
            ok = (rec["verdict"] == "FAIL" and rec["die_failures_last_pass"][2] >= 1
                  and all(rec["die_failures_last_pass"][i] == 0 for i in (0, 1, 3))
                  and not rec["crc_chain_match"])
            if not ok:
                print("FAIL: injected fault not reported as expected:", rec)
                bad += 1
        elif rec["verdict"] != "PASS" or not (0 < rec["cost_cycles"]["read_avg"] < 1000):
            print("FAIL:", rec)
            bad += 1
        continue
    rec = decode_psram(words)
    mode = mailbox_seen
    mailbox_seen += 1
    print(f"{tag}: {rec['verdict']} idx={rec['sample_index']} (T_ACC {rec['t_acc']} + {rec['read_extra_clocks']}) "
          f"slow={rec['slow_dials']} checks={rec['checks']} failures={rec['failures']} "
          f"ops={rec['controller_ops']} guard_hit={rec['flags']['guard_hit']} "
          f"crc={[d['crc_match'] for d in rec['dies']]}")
    if (rec["t_acc"] != t_acc or rec["read_extra_clocks"] != mode
            or rec["slow_dials"] != (mode == 3) or rec["sample_index"] != t_acc + mode):
        print("FAIL: run mode/index not as expected:", rec["t_acc"], rec["read_extra_clocks"])
        bad += 1
    if report:
        continue
    if expect_fault:
        ok = (rec["verdict"] == "FAIL" and rec["dies"][2]["failures"] >= 1
              and not rec["dies"][2]["crc_match"]
              and all(rec["dies"][i]["failures"] == 0 and rec["dies"][i]["crc_match"] for i in (0, 1, 3)))
        if not ok:
            print("FAIL: injected fault not reported as expected:", rec)
            bad += 1
    elif rec["verdict"] != "PASS":
        print("FAIL:", rec)
        bad += 1
want = 4 if no_window else 7
if seen != want:
    print(f"FAIL: expected {want} records, found {seen}")
    bad += 1
print("PASSED (0 failures)" if not bad else f"FAILED ({bad})")
sys.exit(1 if bad else 0)
