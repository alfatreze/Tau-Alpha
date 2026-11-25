#!/usr/bin/env python3
"""Which live (non-doc) files outside fw/ read each fw/ file that fw2/ does not include? Output: fw_blockers.json + stdout summary."""
import subprocess, json, sys
sys.path.insert(0, 'docs/research/provenance-2026-11-25')
import compare
files = subprocess.run(['git', 'ls-files', 'fw'], capture_output=True, text=True).stdout.split('\n')
files = [f for f in files if f]
inc = compare.fw2_included(subprocess.run(['git', 'ls-files'], capture_output=True, text=True).stdout.split('\n'))
out = {}
for f in files:
    if f in inc: continue
    hits = set()
    for pat in {f, f.split('/')[-1]}:
        r = subprocess.run(['git', 'grep', '-l', '-F', '-e', pat, '--', '.', ':!docs', ':!*.md', ':!fw'], capture_output=True, text=True).stdout.split('\n')
        hits |= {x for x in r if x}
    out[f] = sorted(hits)
json.dump({'fw2_included': sorted(inc), 'not_included': out}, open('docs/research/provenance-2026-11-25/fw_blockers.json', 'w'), indent=1)
for f, h in out.items():
    print(f, len(h), ' '.join(h[:6]))
