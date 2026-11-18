#!/usr/bin/env python3
"""Checks the console of sim/tb_save_soc.v MODE 1 (the self-test booted three times on the real CPU): reads the vvp output on stdin. Exit 0 and a line 'PASSED' when
boot 1 saw a zero file, boot 2 saw boot 1's file intact and boot 1's value, boot 3 saw a file whose tail was lost and said so. usage: vvp ... | check_save_soc.py"""
import re, sys
txt = sys.stdin.read()
sv = [dict(kv.split("=") for kv in m.split()) for m in re.findall(r"fw: SV (boots=.*)", txt)]
ok = "PASSED: tb_save_soc" in txt and len(sv) == 3 and "SAVE FAIL" not in txt
if ok:
    a, b, c = sv
    ok = (a["boots"], a["crc"], a["first"], a["pat_bad"]) == ("1", "0", "1", "0") \
        and (b["boots"], b["crc"], b["first"], b["pat_bad"], b["last"]) == ("2", "1", "0", "0", a["wrote"]) \
        and (c["boots"], c["crc"]) == ("3", "2") and int(c["pat_bad"]) > 0 and c["last"] == b["wrote"]
print("PASSED" if ok else "FAILED: " + str(sv))
sys.exit(0 if ok else 1)
