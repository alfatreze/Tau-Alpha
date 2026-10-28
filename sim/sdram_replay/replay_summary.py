#!/usr/bin/env python3
"""Summarise SDRAM replay logs (MIT licence).

  python3 sim/sdram_replay/replay_summary.py LOG [LOG ...]            one run per log, one block each
  python3 sim/sdram_replay/replay_summary.py --merge LOG [LOG ...]    the logs are shards of ONE run (+skip_pictures/+limit_pictures)
  python3 sim/sdram_replay/replay_summary.py --json OUT.json ...      also write the numbers as JSON
  python3 sim/sdram_replay/replay_summary.py --self-test              checks the parser on a synthetic log

The harness (tb_replay.sv) prints "PIC", "PORT" and "RESULT" lines and "VERDICT PASS|FAIL". This script recomputes the pass
rule from the PIC lines (it does not trust the harness' own verdict), adds the percentiles and prints a table.
Percentiles are nearest-rank: p50 = sorted[ceil(0.50 n) - 1], p99 = sorted[ceil(0.99 n) - 1] (for fewer than 100 pictures p99 is the maximum).
Exit status 0 only when every run passes (harness verdict and the recomputed rule).
"""
import json
import math
import re
import sys

KV = re.compile(r'(\w+)=(\S+)')
MAX_KEYS = {'urgent_wait_max', 'urgent_bound', 'busy_max', 'lag_max', 'period', 'level_min_max', 'lat_max', 'first_word_max'}
MIN_KEYS = {'level_min'}
FLAG_KEYS = {'norelease'}


def pct(vals, q):
    if not vals:
        return 0
    v = sorted(vals)
    return v[max(0, math.ceil(q * len(v)) - 1)]


def parse(path):
    run = {'path': path, 'pics': [], 'res': {}, 'ports': {}, 'verdict': None, 'reasons': [], 'stream': {}, 'tb': {}}
    with open(path, errors='replace') as f:
        for line in f:
            line = line.rstrip('\n')
            if line.startswith('PIC '):
                d = {k: int(v) for k, v in KV.findall(line)}
                run['pics'].append(d)
            elif line.startswith('PORT '):
                m = re.match(r'PORT (\d+) (.*)', line)
                run['ports'][int(m.group(1))] = {k: int(v) for k, v in KV.findall(m.group(2))}
            elif line.startswith('RESULT '):
                for k, v in KV.findall(line[7:]):
                    try:
                        run['res'][k] = float(v) if '.' in v else int(v)
                    except ValueError:
                        run['res'][k] = v
            elif line.startswith('STREAM '):
                run['stream'] = {k: v for k, v in KV.findall(line)}
            elif line.startswith('TB '):
                run['tb'] = {k: v for k, v in KV.findall(line)}
                m = re.search(r'treq=(.*?) scan=', line)
                if m:
                    run['tb']['treq'] = m.group(1)
            elif line.startswith('VERDICT'):
                run['verdict'] = line.split()[1]
            elif line.strip().startswith('reason:'):
                run['reasons'].append(line.strip()[7:].strip())
    return run


def merge(runs):
    """Shards of one run: concatenate the pictures, sum the counters, take max/min where that is what the key means."""
    out = {'path': ' + '.join(r['path'] for r in runs), 'pics': [], 'res': {}, 'ports': {}, 'verdict': 'PASS', 'reasons': [],
           'stream': runs[0]['stream'], 'tb': runs[0]['tb']}
    for r in runs:
        out['pics'].extend(r['pics'])
        if r['verdict'] != 'PASS':
            out['verdict'] = 'FAIL'
        out['reasons'].extend(r['reasons'])
        for k, v in r['res'].items():
            if isinstance(v, str):
                out['res'].setdefault(k, v)
            elif k in MAX_KEYS:
                out['res'][k] = max(out['res'].get(k, v), v)
            elif k in MIN_KEYS:
                out['res'][k] = min(out['res'].get(k, v), v)
            elif k in FLAG_KEYS:
                out['res'][k] = v
            elif k in ('requests_per_mb', 'commands_per_request', 'row_hit_rate'):
                pass                                  # recomputed below
            else:
                out['res'][k] = out['res'].get(k, 0) + v
        for p, d in r['ports'].items():
            o = out['ports'].setdefault(p, {'requests': 0, 'words': 0, 'lat_avg_x100': 0, 'lat_max': 0, 'first_word_max': 0, '_w': 0})
            o['_w'] += d['requests']
            o['lat_avg_x100'] += d['lat_avg_x100'] * d['requests']
            o['requests'] += d['requests']
            o['words'] += d['words']
            o['lat_max'] = max(o['lat_max'], d['lat_max'])
            o['first_word_max'] = max(o['first_word_max'], d['first_word_max'])
    for o in out['ports'].values():
        o['lat_avg_x100'] = o['lat_avg_x100'] // o['_w'] if o['_w'] else 0
    out['pics'].sort(key=lambda d: d['n'])
    res = out['res']
    if res.get('mbs'):
        res['requests_per_mb'] = round(res.get('decoder_requests', 0) / res['mbs'], 2)
    if res.get('requests_all'):
        res['commands_per_request'] = round(res.get('commands', 0) / res['requests_all'], 3)
    cc = res.get('rd', 0) + res.get('wr', 0)
    if cc:
        res['row_hit_rate'] = round(1 - res.get('act', 0) / cc, 4)
    return out


def evaluate(run):
    """Recompute the pass rule of contract 7.4 from the PIC lines. Returns a dict."""
    pics = run['pics']
    res = run['res']
    timed = not res.get('norelease', 0)
    busy = [p['busy'] for p in pics]
    period = pics[0]['period'] if pics else 1
    busy_pct = [100.0 * b / p['period'] for b, p in zip(busy, pics)]
    over = [p['n'] for p in pics if p['busy'] * 100 > 90 * p['period']]
    late = [p['n'] for p in pics if timed and p['late']]
    lag = [p['lag'] for p in pics] if timed else []
    span = [p['span'] for p in pics] if timed else []
    ev = {'pictures': len(pics), 'timed': timed, 'period': period,
          'busy_p50': pct(busy, 0.50), 'busy_p99': pct(busy, 0.99), 'busy_max': max(busy) if busy else 0,
          'busy_pct_p50': pct(busy_pct, 0.50), 'busy_pct_p99': pct(busy_pct, 0.99), 'busy_pct_max': max(busy_pct) if busy_pct else 0,
          'over90': over, 'late': late}
    if timed:
        ev.update({'lag_p50': pct(lag, 0.50), 'lag_p99': pct(lag, 0.99), 'lag_max': max(lag) if lag else 0,
                   'span_pct_max': 100.0 * max(span) / period if span else 0})
    # other pass conditions, from the counters
    r = res
    bad = []
    for key in ('mismatches', 'chip_diffs', 'proto', 'hang', 'fence_viol', 'barrier_viol', 'release_viol', 'bad_address'):
        if r.get(key, 0):
            bad.append(f'{key}={r[key]}')
    if r.get('underflow_words', 0):
        bad.append(f"scan underflow {r['underflow_words']} words")
    for key in ('phy_fault', 'phy_fault_count', 'chip_errors', 'dq_conflicts', 'dut_bad_req'):
        if r.get(key, 0):
            bad.append(f'{key}={r[key]}')
    if over:
        bad.append(f'{len(over)} pictures above 90 % of the period')
    if late:
        bad.append(f'{len(late)} pictures past their deadline')
    if not pics:
        bad.append('no picture completed')
    ev['fail_reasons'] = bad
    ev['pass'] = not bad
    return ev


def fmt_pct(c, period):
    return f'{c:>9,d} cycles = {100.0 * c / period:6.2f} % of the period'


def report(run):
    ev = evaluate(run)
    r = run['res']
    name = run['tb'].get('treq', r.get('treq', run['path']))
    name = name.rsplit('/', 1)[-1]
    L = []
    L.append(f"== {name}   ({run['path']})")
    L.append(f"   mode: {'timed (release_ns honoured)' if ev['timed'] else 'norelease (memory time only; release_ns ignored)'};"
             f" pictures {ev['pictures']}, MBs {r.get('mbs', 0)}, simulated cycles {r.get('cycles', 0):,}")
    L.append(f"   picture period {ev['period']:,} cycles; busy = reached(n) - max(release(n), reached(n-1))")
    L.append('   busy p50  ' + fmt_pct(ev['busy_p50'], ev['period']))
    L.append('   busy p99  ' + fmt_pct(ev['busy_p99'], ev['period']))
    L.append('   busy max  ' + fmt_pct(ev['busy_max'], ev['period']))
    if ev['timed']:
        L.append('   lag p50   ' + fmt_pct(ev['lag_p50'], ev['period']) + '   (PIC_END reached minus the last macroblock release)')
        L.append('   lag p99   ' + fmt_pct(ev['lag_p99'], ev['period']))
        L.append('   lag max   ' + fmt_pct(ev['lag_max'], ev['period']))
        L.append(f"   parser span (last MB release - picture release): up to {ev['span_pct_max']:.2f} % of the period, a floor on busy that no memory system can lower")
    L.append(f"   pictures above 90 %: {len(ev['over90'])}{(' ' + str(ev['over90'][:8])) if ev['over90'] else ''};"
             f" past the deadline: {len(ev['late']) if ev['timed'] else 'n/a (norelease)'}")
    L.append(f"   requests per macroblock {r.get('requests_per_mb', 0)}  (decoder, after 2D fusing: {r.get('decoder_requests', 0):,} requests, "
             f"{r.get('records', 0):,} records);  commands per request {r.get('commands_per_request', 0)}  (all ports, {r.get('commands', 0):,} commands)")
    L.append(f"   row hit rate {r.get('row_hit_rate', 0)}  (1 - ACT / column commands: ACT {r.get('act', 0):,}, RD {r.get('rd', 0):,}, WR {r.get('wr', 0):,}, "
             f"PRE {r.get('pre', 0):,}, PREA {r.get('prea', 0):,}, REF {r.get('ref', 0):,}, BST {r.get('bst', 0):,})")
    if 'chip_rd_words' in r:
        L.append(f"   burst-tail waste: {r.get('burst_tail_words', 0):,} words (chip-driven read words {r['chip_rd_words']:,} minus words delivered to clients {r.get('client_rd', 0):,})")
    L.append(f"   scan-out: underflow {r.get('underflow_words', 0)} words, FIFO minimum level {r.get('level_min', 0)}; urgent waits measured {r.get('urgent_waits', 0)},"
             f" worst {r.get('urgent_wait_max', 0)} cycles against the bound {r.get('urgent_bound', 0)} ({r.get('urgent_over', 0)} over)")
    L.append(f"   integrity: mismatches {r.get('mismatches', 0)}, chip/shadow diffs {r.get('chip_diffs', 0)}, unchecked (never written) words {r.get('unchecked_uninit_words', 0)}, races {r.get('races', 0)}")
    L.append(f"   phy_fault {r.get('phy_fault', 0)} (count {r.get('phy_fault_count', 0)}), chip-model errors {r.get('chip_errors', 0)}, DQ conflicts {r.get('dq_conflicts', 0)},"
             f" scheduler bad_req {r.get('dut_bad_req', 0)}, monitors (fence/barrier/release) {r.get('fence_viol', 0)}/{r.get('barrier_viol', 0)}/{r.get('release_viol', 0)}")
    for p in sorted(run['ports']):
        d = run['ports'][p]
        if d['requests']:
            L.append(f"   port {p}: {d['requests']:,} requests, {d['words']:,} words, latency avg {d['lat_avg_x100'] / 100:.1f} max {d['lat_max']} cycles"
                     f"{', first word max ' + str(d['first_word_max']) if d['first_word_max'] else ''}")
    tb_ok = run['verdict'] == 'PASS'
    L.append(f"   harness verdict: {run['verdict']};  recomputed from the PIC lines and counters: {'PASS' if ev['pass'] else 'FAIL'}"
             f"{(' (' + '; '.join(ev['fail_reasons']) + ')') if ev['fail_reasons'] else ''}")
    if tb_ok != ev['pass']:
        L.append('   !! the harness verdict and the recomputed verdict disagree')
    return '\n'.join(L), ev, (tb_ok and ev['pass'])


def self_test():
    import tempfile
    import os
    text = """TB treq=/x/y.treq scan=s
STREAM fps=30.000 mb_rate=67500 ahead=1 period_cycles=1000
PIC n=0 type=0 begin=0 reached=500 busy=500 period=1000 deadline=0 late=0 over90=0 lag=0 span=0 mbs=2 recs=4 reqs=4 words=8 cmds=10
PIC n=1 type=1 begin=500 reached=1450 busy=950 period=1000 deadline=0 late=0 over90=1 lag=0 span=0 mbs=2 recs=4 reqs=4 words=8 cmds=10
RESULT cycles=1450 pictures=2 mbs=4 records=8 decoder_requests=8 fused_read_records=0 unfused_reads=0
RESULT mismatches=0 chip_diffs=0 races=0 unchecked_uninit_words=0
RESULT norelease=1 period=1000
PORT 1 requests=4 words=8 lat_avg_x100=1000 lat_max=20 first_word_max=5
VERDICT PASS
"""
    d = tempfile.mkdtemp()
    p = os.path.join(d, 't.log')
    open(p, 'w').write(text)
    run = parse(p)
    ev = evaluate(run)
    assert ev['over90'] == [1], ev
    assert not ev['pass'], ev
    assert pct([1, 2, 3, 4], 0.5) == 2 and pct(list(range(1, 101)), 0.99) == 99
    m = merge([run, run])
    assert len(m['pics']) == 4 and m['res']['mbs'] == 8
    print('self-test ok')


def main(argv):
    if '--self-test' in argv:
        self_test()
        return 0
    do_merge = '--merge' in argv
    jpath = None
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '--json':
            jpath = argv[i + 1]
            i += 2
            continue
        if not a.startswith('--'):
            args.append(a)
        i += 1
    if not args:
        print(__doc__)
        return 2
    runs = [parse(a) for a in args]
    if do_merge:
        runs = [merge(runs)]
    ok = True
    js = []
    for r in runs:
        text, ev, good = report(r)
        print(text)
        print()
        ok = ok and good
        js.append({'path': r['path'], 'evaluation': ev, 'results': r['res'], 'ports': r['ports'], 'harness_verdict': r['verdict']})
    if jpath:
        with open(jpath, 'w') as f:
            json.dump(js, f, indent=1)
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
