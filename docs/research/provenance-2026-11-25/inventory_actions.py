#!/usr/bin/env python3
"""Add the 'action' column (what the 2026-11-25 pass did to each document) to doc_inventory.csv, using docs_moved.csv. Run once after docs_archive_move.py."""
import csv, collections
d = 'docs/research/provenance-2026-11-25/'
mv = {r['old_path_now_stub']: r for r in csv.DictReader(open(d + 'docs_moved.csv'))}
rows = list(csv.DictReader(open(d + 'doc_inventory.csv')))
for r in rows:
    m = mv.get(r['path'])
    if m: r['action'] = f"moved to {m['archived_copy']} (stub left at the old path)"
    elif 'PROTECTED' in r['rule']: r['action'] = 'left in place: Tau Omega links this path'
    elif 'DIVERGED' in r['rule']: r['action'] = 'left in place: the two copies differ, owner picks the canonical one'
    else: r['action'] = r.get('action', '')
with open(d + 'doc_inventory.csv', 'w', newline='') as o:
    w = csv.DictWriter(o, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
print(collections.Counter(r['class'] for r in rows), sum(1 for r in rows if r['action'].startswith('moved')))
for r in rows:
    if 'DIVERGED' in r['rule'] or 'PROTECTED' in r['rule']: print(r['path'], r['last_commit'], r['rule'][:80])
