#!/usr/bin/env python3
"""For each file with HarpMudd-origin lines (>=10 lines or >=25 %), list the tracked non-doc files that
mention its path or basename (its live readers). Output: readers.json. Run from the repo root."""
import csv, subprocess, json
rows = list(csv.DictReader(open('docs/research/provenance-2026-11-25/files.csv')))
sel = [r for r in rows if int(r['harpmudd_blame']) >= 10 or int(r['harpmudd_blame']) >= .25 * max(int(r['sloc']), 1)]
out = {}
for r in sel:
    p = r['path']; b = p.split('/')[-1]
    hits = set()
    for pat in {p, b}:
        res = subprocess.run(['git', 'grep', '-l', '-F', '-e', pat, '--', '.', ':!docs', ':!*.md'],
                             capture_output=True, text=True).stdout.split('\n')
        hits |= {x for x in res if x}
    hits.discard(p)
    out[p] = sorted(hits)
json.dump(out, open('docs/research/provenance-2026-11-25/readers.json', 'w'), indent=1)
print(len(out))
