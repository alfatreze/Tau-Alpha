#!/usr/bin/env python3
"""Build complete source inventory and a lexical C-function map from pinned evidence.

Function detection is lexical (not compiler/preprocessor reachability); alternative
build definitions remain separate. Header macros expanding into functions are not
expanded. No product files or Git state are modified.
"""
import csv
import importlib.util
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('measure', OUT / 'measure.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
j = json.loads((OUT / 'summary.json').read_text())
ranges = json.loads((OUT / 'line-origins.json').read_text())


def pct(a, b):
    return f'{100*a/b:.1f}%' if b else '—'


def clean_c(s):
    pattern = r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    s = re.sub(pattern, lambda x: ''.join('\n' if c == '\n' else ' ' for c in x[0]), s, flags=re.S)
    # GNU attributes otherwise resemble a function named __attribute__.
    return re.sub(r'__attribute__\s*\(\(.*?\)\)', lambda x: ' ' * len(x[0]), s, flags=re.S)


functions = []
pattern = re.compile(r'^(?:[\w*]+[ \t]+)+(?P<name>[A-Za-z_]\w*)\s*\([^;{}]*?\)\s*\{', re.M)
for row in j['files']:
    p = row['path']
    if not p.startswith('fw/') or Path(p).suffix not in {'.c', '.h', '.inc'}:
        continue
    s = m.git(ROOT, 'show', j['tau_revision'] + ':' + p)
    clean = clean_c(s)
    mask = m.sloc_mask(p, s)
    origins = {}
    for r in ranges[p]:
        for i in range(r['start'], r['end'] + 1):
            origins[i] = r
    for match in pattern.finditer(clean):
        start = clean[:match.start()].count('\n') + 1
        depth = 1
        endpos = match.end()
        while depth and endpos < len(clean):
            depth += (clean[endpos] == '{') - (clean[endpos] == '}')
            endpos += 1
        if depth:
            raise ValueError((p, match['name'], 'unbalanced function body'))
        end = clean[:endpos].count('\n') + 1
        ls = [i for i in range(start, end + 1) if i <= len(mask) and mask[i-1]]
        h = sum(origins.get(i, {}).get('author', '').casefold() == 'harpmudd' for i in ls)
        port = sum(origins.get(i, {}).get('origin') == 'declared_upstream_port' for i in ls)
        functions.append({'path': p, 'function': match['name'], 'start': start, 'end': end, 'sloc': len(ls), 'harpmudd_blame': h, 'declared_upstream_port': port, 'harpmudd_percent': pct(h, len(ls))})
with (OUT / 'firmware-functions.csv').open('w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=list(functions[0]))
    w.writeheader()
    w.writerows(functions)

lines = ['# Complete Tau Alpha source/module inventory', '',
         f'Audited revision: `{j["tau_revision"]}`. This inventory has **{j["source_files"]} source/build-script files**. All {j["tracked_files"]} tracked paths, including configuration, media, licences and binaries, are in `tracked-files.csv`.', '',
         'SLOC excludes blank lines and comments; Python docstrings count as source. HarpMudd columns describe copy-aware Git origin, not exclusive copyright authorship. Vendor/framework/generated buckets must not be interpreted as HarpMudd-authored. “Port” is surviving code last recorded by the explicitly documented upstream-port commit. A tiny inherited count in a new file can be copied boilerplate rather than a derived implementation.', '',
         'Base comparison is against v1.4.0-era fork `7ef8f0f`; upstream comparison is against `f4b8f01` (v1.6.0 main). `absent` means no same-path file at that revision; it does not prove no copied code. All relative source links point into this checkout; line numbers refer to the audited revision.', '']
for bucket in j['totals_by_bucket']:
    lines += [f'## {bucket}', '', '| Source / module | SLOC | HarpMudd Git-origin lines | % | Port | Fork / latest upstream |', '|---|---:|---:|---:|---:|---|']
    for x in j['files']:
        if x['bucket'] == bucket:
            p = x['path']
            lines.append(f'| [{p}](../../../{p}) | {x["sloc"]:,} | {x["harpmudd_blame"]:,} | {pct(x["harpmudd_blame"],x["sloc"])} | {x["declared_upstream_port"]} | {x["base_status"]} / {x["upstream_status"]} |')
    lines.append('')
lines += ['## HDL module declarations', '', 'Every lexical HDL module declaration, including generated CPU/IP internals. This is a declaration inventory, not the elaborated release hierarchy.', '', '| Source | Declared modules |', '|---|---|']
for x in j['files']:
    if Path(x['path']).suffix in {'.v', '.sv'}:
        s = m.git(ROOT, 'show', j['tau_revision'] + ':' + x['path'])
        names = re.findall(r'^\s*module\s+(\w+)', clean_c(s), re.M)
        if names:
            lines.append(f'| `{x["path"]}` | ' + ', '.join(f'`{n}`' for n in names) + ' |')
lines += ['', '## Removed fork sources', '', *[f'- `{p}`' for p in j['deleted_fork_source_paths']], '',
          'Removed means absent at the old path, not proof that its implementation was eliminated everywhere. The archive holds `archive/cassette_meter/cassette_meter_code.c.txt`, preserved C code with a `.txt` suffix; it is in the tracked-file ledger and outside the source-extension percentage denominator.', '',
          f'## Firmware function definitions', '', f'`firmware-functions.csv` maps **{len(functions)} lexical function definitions** across firmware C/header/include files to current line ranges and Git-origin counts. It includes conditional alternative definitions and host-testable header functions. It is not a compiler call graph; include-expanded reachability and complete global ownership are Phase 0 implementation gates.', '']
(OUT / 'MODULE_INVENTORY.md').write_text('\n'.join(lines))
with (OUT / 'tracked-files.csv').open('w', newline='') as f:
    w = csv.DictWriter(f, fieldnames=['path', 'source'])
    w.writeheader()
    w.writerows(j['inventory'])
print(f'Inventory: {len(j["files"])} source files, {len(j["inventory"])} tracked files; {len(functions)} firmware function definitions')
print('Largest inherited firmware functions:')
for f in sorted(functions, key=lambda x: -x['harpmudd_blame'])[:20]:
    print(f)
