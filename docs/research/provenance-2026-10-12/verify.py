#!/usr/bin/env python3
"""Validate analysis evidence without building or changing the firmware."""
import ast
import collections
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[2]
spec = importlib.util.spec_from_file_location('measure', OUT / 'measure.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
j = json.loads((OUT / 'summary.json').read_text())
ranges = json.loads((OUT / 'line-origins.json').read_text())
rows = list(csv.DictReader((OUT / 'files.csv').open()))
tracked = list(csv.DictReader((OUT / 'tracked-files.csv').open()))
functions = list(csv.DictReader((OUT / 'firmware-functions.csv').open()))
assert len(rows) == j['source_files'] == 482
assert len(tracked) == j['tracked_files'] == 933
assert len(functions) == 767
assert {x['path'] for x in rows} == {x['path'] for x in j['files']} == set(ranges)
assert {x['path'] for x in tracked} == {x['path'] for x in j['inventory']}
checks = []

for row in j['files']:
    p = row['path']
    source = m.git(ROOT, 'show', j['tau_revision'] + ':' + p)
    assert hashlib.sha256(source.encode()).hexdigest() == row['sha256'], p
    mask = m.sloc_mask(p, source)
    assert sum(mask) == row['sloc'], p
    covered = []
    count = collections.Counter()
    for r in ranges[p]:
        assert 1 <= r['start'] <= r['end'] <= row['physical_lines'], (p, r)
        covered.extend(range(r['start'], r['end'] + 1))
        n = r['end'] - r['start'] + 1
        count[r['origin']] += n
        if r['author'].casefold() == 'harpmudd':
            count['harpmudd_blame'] += n
    assert len(covered) == len(set(covered)) == row['sloc'], p
    assert set(covered) == {i+1 for i, include in enumerate(mask) if include}, p
    for key in ['fork_baseline', 'later_upstream', 'declared_upstream_port', 'tau_lineage', 'harpmudd_blame']:
        assert count[key] == row[key], (p, key)
    assert row['sloc'] == sum(row[k] for k in ['fork_baseline', 'later_upstream', 'declared_upstream_port', 'tau_lineage']), p
    expected = next(x for x in rows if x['path'] == p)
    for k, v in expected.items():
        assert str(row[k]) == v, (p, k)
checks.append('All 482 pinned source hashes, SLOC masks, CSV rows and complete non-overlapping origin ranges reconcile')

for bucket, totals in j['totals_by_bucket'].items():
    members = [x for x in j['files'] if x['bucket'] == bucket]
    for key, value in totals.items():
        assert value == (len(members) if key == 'files' else sum(x[key] for x in members)), (bucket, key)
assert sum(j['firstparty_authors_sloc'].values()) == 54668
assert sum(x['sloc'] for x in j['files']) == 86723
assert sum(j['totals_by_bucket'][g]['sloc'] for g in ['firmware', 'rtl']) == 21014
assert sum(j['totals_by_bucket'][g]['harpmudd_blame'] for g in ['firmware', 'rtl']) == 5776
assert sum(j['totals_by_bucket'][g]['declared_upstream_port'] for g in ['firmware', 'rtl']) == 50
assert j['original_production_survival']['fork_production_unique_origins'] == 7788
assert j['original_production_survival']['retained_production_unique_origins'] == 5481
checks.append('Scopes, author totals, percentages and unique-original survival counts reconcile')

for f in functions:
    row = next(x for x in j['files'] if x['path'] == f['path'])
    assert 1 <= int(f['start']) <= int(f['end']) <= row['physical_lines']
    assert int(f['harpmudd_blame']) <= int(f['sloc'])
checks.append('All 767 lexical firmware function records have valid pinned-source ranges and counts')

cases = [
    ('a.c', '/* comment */\nint x = 1; // x\n/* two\nlines */ int y;\nchar *url = "http://x";\n', [False, True, False, True, True]),
    ('a.sv', "// comment\nassign x = 1'b0; // yes\n", [False, True]),
    ('a.py', '"""text\n\ntext"""\n# comment\nx=1\n', [True, False, True, False, True]),
    ('a.S', '# comment\n#include "x.h"\nli a0, 1\n', [False, True, True]),
]
for path, source, expected in cases:
    assert m.sloc_mask(path, source) == expected, path
checks.append('Comment/string/preprocessor masks checked for C, SystemVerilog, Python and assembler')

report = ROOT / 'docs/research/HARPMUDD_COMPARISON_AND_REWRITE_2026-10-12.md'
for path in [report, OUT / 'MODULE_INVENTORY.md']:
    content = path.read_text()
    assert content.count('```') % 2 == 0
    for target in re.findall(r'\]\(([^)]+)\)', content):
        if not target.startswith(('https:', 'http:', '#')):
            assert (path.parent / target.split('#')[0]).resolve().exists(), (path, target)
checks.append('Report and complete module inventory links resolve; Markdown code fences are paired')

for path in OUT.glob('*.py'):
    ast.parse(path.read_text(), filename=str(path))
for path in OUT.glob('*.json'):
    json.loads(path.read_text())
checks.append('All evidence Python sources parse and JSON outputs load')

# Empty final TSV columns are real data, not source whitespace errors.
for path in [report, *OUT.glob('*.md'), *OUT.glob('*.py'), *OUT.glob('*.json')]:
    assert all(line.rstrip() == line for line in path.read_text().splitlines()), path
checks.append('New report/evidence source whitespace passes; empty TSV fields preserved')

subprocess.run(['python3', 'tools/check_audit_trail.py'], cwd=ROOT, check=True)
subprocess.run(['git', 'diff', '--check'], cwd=ROOT, check=True)
assert m.git(ROOT, 'diff', '--name-only', '--', 'fw', 'src/fpga', 'third_party', 'sim', 'tools', 'package.py') == ''
checks.append('Audit IDs and tracked diff whitespace pass; production sources unchanged')
result = {'verified_date': '2026-10-13', 'evidence': 'host-analysis-only', 'tau_revision': j['tau_revision'], 'source_files': len(rows), 'tracked_files': len(tracked), 'lexical_firmware_functions': len(functions), 'checks': checks, 'production_build_or_hardware_tested': False}
(OUT / 'verification.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
