#!/usr/bin/env python3
"""Compare two measure.py files.csv outputs by area; list which fw/ files fw2/ includes.
Read-only. Usage: python3 compare.py <old files.csv> <new files.csv> > areas.md
Area rules mirror docs/research/HARPMUDD_DISCONTINUATION_STATUS_2026-10-18.md."""
import csv, re, subprocess, sys, os
from collections import defaultdict

def load(p):
    return {r['path']: r for r in csv.DictReader(open(p))}

def fw2_included(paths):
    """fw/ files reachable from fw2/ by #include, transitively (headers only count; .c/.inc named)."""
    inc = re.compile(r'#\s*include\s+"([^"]+)"')
    have = set(paths)
    seen, todo = set(), [p for p in have if p.startswith('fw2/') or p.startswith('sdk/')]
    def resolve(src, name):
        d = os.path.dirname(src)
        for cand in (os.path.normpath(os.path.join(d, name)), os.path.normpath(os.path.join('fw', name))):
            if cand in have: return cand
        return None
    while todo:
        p = todo.pop()
        if p in seen: continue
        seen.add(p)
        try: s = open(p, errors='replace').read()
        except OSError: continue
        for n in inc.findall(s):
            r = resolve(p, n)
            if r and r not in seen: todo.append(r)
    return {p for p in seen if p.startswith('fw/')}

def area(p, fwinc):
    if p.startswith('fw2/'): return 'fw2 (Player 2 firmware)'
    if p.startswith('fw/'): return 'fw/ headers read by fw2' if p in fwinc else 'fw/ rest (old player)'
    if p.startswith('src/fpga/core/'): return 'src/fpga/core'
    if p.startswith('src/fpga/rtl/'): return 'src/fpga/rtl (vendored CPU, sdram)'
    if p.startswith('src/fpga/apf/'): return 'src/fpga/apf (Analogue framework)'
    if p.startswith('third_party/'): return 'third_party'
    if p.startswith('tools/'): return 'tools'
    if p.startswith('sim/'): return 'sim'
    if p.startswith('sdk/'): return 'sdk'
    return 'other (root, packaging, assets)'

def tabulate(rows, fwinc):
    t = defaultdict(lambda: [0, 0])
    for p, r in rows.items():
        a = area(p, fwinc); t[a][0] += int(r['sloc']); t[a][1] += int(r['harpmudd_blame'])
    return t

if __name__ == '__main__':
    old, new = load(sys.argv[1]), load(sys.argv[2])
    # buckets whose files are generated or vendor are counted separately (first-party excludes them)
    FIRST = {'firmware', 'rtl', 'test', 'tooling', 'framework'}  # same set as the 2026-10-18 first-party figure
    inc_new = fw2_included(list(new)); inc_old = fw2_included(list(old))
    to, tn = tabulate(old, inc_old), tabulate(new, inc_new)
    print('| Area | 2026-10-18 lines | HarpMudd | % | 2026-11-25 lines | HarpMudd | % | delta HarpMudd |')
    print('|---|---:|---:|---:|---:|---:|---:|---:|')
    for a in sorted(set(to) | set(tn)):
        o, n = to.get(a, [0, 0]), tn.get(a, [0, 0])
        pc = lambda x: f'{100*x[1]/x[0]:.1f}%' if x[0] else '-'
        print(f'| {a} | {o[0]:,} | {o[1]:,} | {pc(o)} | {n[0]:,} | {n[1]:,} | {pc(n)} | {n[1]-o[1]:+,} |')
    def fp(rows):
        s = h = 0
        for r in rows.values():
            if r['bucket'] in FIRST: s += int(r['sloc']); h += int(r['harpmudd_blame'])
        return s, h
    for name, rows in (('2026-10-18', old), ('2026-11-25', new)):
        s, h = fp(rows); print(f'\nFirst-party (firmware, rtl, test, tooling) {name}: {s:,} lines, {h:,} HarpMudd-origin ({100*h/s:.1f}%)')
    print('\nsrc/fpga/core by module (new vs old HarpMudd lines, sloc):')
    for p in sorted(set(old) | set(new)):
        if not p.startswith('src/fpga/core/'): continue
        o, n = old.get(p), new.get(p)
        oh = int(o['harpmudd_blame']) if o else None; nh = int(n['harpmudd_blame']) if n else None
        if (oh or 0) or (nh or 0):
            print(f"  {p}: old {oh if o else 'absent'}/{o['sloc'] if o else '-'}  new {nh if n else 'absent'}/{n['sloc'] if n else '-'}")
    print('\nfw/ files read by fw2 (new):', len(inc_new))
