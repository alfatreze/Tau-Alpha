#!/usr/bin/env python3
"""List docs that exist both at docs/<name> and in a subdirectory: identical or not, last commit date of each, line counts, inbound reference counts."""
import os, subprocess, sys, difflib
def run(*a): return subprocess.run(a, capture_output=True, text=True).stdout
files = [f for f in run('git', 'ls-files', 'docs').split('\n') if f.endswith('.md')]
root = {os.path.basename(f): f for f in files if f.count('/') == 1}
for f in sorted(files):
    b = os.path.basename(f)
    if f.count('/') > 1 and b in root and not f.startswith(('docs/archive/', 'docs/interop/')) and b != 'README.md':
        r = root[b]
        a, c = open(r, errors='replace').read(), open(f, errors='replace').read()
        same = a == c
        dr = run('git', 'log', '-1', '--format=%cs', '--', r).strip(); ds = run('git', 'log', '-1', '--format=%cs', '--', f).strip()
        d = 0 if same else sum(1 for l in difflib.unified_diff(a.splitlines(), c.splitlines(), lineterm='') if l[:1] in '+-' and l[:3] not in ('+++', '---'))
        print(f'{"SAME" if same else "DIFF"}\t{r} ({dr}, {len(a.splitlines())}L)\t{f} ({ds}, {len(c.splitlines())}L)\tdifflines={d}')
