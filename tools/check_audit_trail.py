#!/usr/bin/env python3
"""Fail if the chronological audit trail assigns an ID to two entries."""
from collections import Counter
from pathlib import Path
import re
import sys


trail = Path(__file__).resolve().parents[1] / "docs" / "AUDIT_TRAIL.md"
ids = re.findall(r"^### (A-\d{3})\s+—", trail.read_text(encoding="utf-8"), re.M)
counts = Counter(ids)
dupes = sorted(entry for entry, count in counts.items() if count > 1)

if dupes:
    print("FAIL: duplicate audit IDs: " + ", ".join(dupes))
    sys.exit(1)

print(f"PASS: {len(ids)} unique audit IDs in {trail.name}")
