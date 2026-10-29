#!/usr/bin/env python3
"""Busy % per picture of replay logs: pipe_summary.py LOG [LOG...] (MIT licence)."""
import re, sys
KV = re.compile(r'(\w+)=(\S+)')
for p in sys.argv[1:]:
    pics, res = [], {}
    for ln in open(p, errors='replace'):
        if ln.startswith('PIC '):
            d = dict(KV.findall(ln)); pics.append(int(d['busy']) * 100.0 / int(d['period']))
        elif ln.startswith('RESULT'):
            res.update(dict(KV.findall(ln)))
        elif ln.startswith('VERDICT'):
            res['v'] = ln.split()[1]
    print(p, ['%.2f' % x for x in pics], res.get('v'), 'cmds/req', res.get('commands_per_request'), 'urgent_wait_max', res.get('urgent_wait_max'),
          'phy_fault', res.get('phy_fault'), 'chip_errors', res.get('chip_errors'))
