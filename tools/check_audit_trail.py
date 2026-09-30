#!/usr/bin/env python3
"""Fail if the chronological audit trail assigns an ID to two entries.
Series: A-NNN (SDRAM/UI/firmware) and B-NNN (PSRAM)."""
from collections import Counter
from pathlib import Path
import re
import sys


trail = Path(__file__).resolve().parents[1] / "docs" / "AUDIT_TRAIL.md"
text = trail.read_text(encoding="utf-8")
# Two heading styles have been used across this file's history: the older "### X-NNN\xa0— ..."
# and the "## X-NNN: ..." style every entry since roughly the B-300s uses (including the exact
# B-417..B-420 collision this check was extended to catch -- a merge of two branches that had each
# independently assigned those same four numbers to unrelated entries, silently invisible to the
# old regex alone since it never matched the "##"-with-colon style at all).
#
# A single ID legitimately reused across several headings for "addendum"/"relaunch"/"(final)"-style
# follow-ups on the SAME entry is not a collision -- only flag an ID when a NON-continuation heading
# reuses it (a different entry entirely got the same number).
CONTINUATION = re.compile(r"^\s*(addendum|relaunch|\(final\))", re.I)
matches = re.findall(r"^#{2,3} ([AB]-\d{3})([:\s].*)$", text, re.M)
ids = [m[0] for m in matches]
counts = Counter(ids)
primaries = Counter(m[0] for m in matches if not CONTINUATION.match(m[1].lstrip(": ")))
dupes = sorted(entry for entry, count in primaries.items() if count > 1)

if dupes:
    print("FAIL: duplicate audit IDs: " + ", ".join(dupes))
    sys.exit(1)

print(f"PASS: {len(ids)} unique audit IDs in {trail.name}")
