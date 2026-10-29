#!/usr/bin/env python3
"""Summarise build/abl/<config>_<trace>.log (sched_v ablation, flat out): busy p50/max (cycles and % of the period), commands per request,
row hit rate, urgent wait max. MIT licence."""
import re, sys, glob, os
KV = re.compile(r'(\w+)=(\S+)')
def parse(path):
    pics, res = [], {}
    for ln in open(path, errors='replace'):
        if ln.startswith('PIC '):
            d = dict(KV.findall(ln)); pics.append(int(d['busy']) * 100.0 / int(d['period']))
        elif ln.startswith('RESULT'):
            res.update(dict(KV.findall(ln)))
        elif ln.startswith('VERDICT'):
            res['verdict'] = ln.split()[1]
    return pics, res
def pct(v, q):
    v = sorted(v); import math
    return v[max(0, math.ceil(q * len(v)) - 1)] if v else 0
d = sys.argv[1] if len(sys.argv) > 1 else 'build/abl'
names = ['base', 'q2', 'q1', 'nolook', 'nobatch', 'noraw', 'nocont', 'no2d', 'min']
print('| config | clip busy p50 % | clip busy max % | allbi busy max % | cmds/req (clip) | row hit (clip) | urgent wait max (allbi) | verdict clip/allbi |')
print('|---|---|---|---|---|---|---|---|')
for n in names:
    row = []
    c = os.path.join(d, n + '_clip.log'); a = os.path.join(d, n + '_allbi.log')
    if not (os.path.exists(c) and os.path.exists(a)): continue
    pc, rc = parse(c); pa, ra = parse(a)
    print('| %s | %.2f | %.2f | %.2f | %s | %s | %s | %s/%s |' % (n, pct(pc, .5), max(pc or [0]), max(pa or [0]),
          rc.get('commands_per_request', '?'), rc.get('row_hit_rate', '?'), ra.get('urgent_wait_max', '?'), rc.get('verdict', '?'), ra.get('verdict', '?')))
