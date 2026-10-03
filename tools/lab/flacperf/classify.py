"""classify.py <json> ... : per-sample instr/cycles split into RICE / LPC / EMIT / SINK / REFILL / OTHER."""
import sys, json, re, os
FN = {'unary': 'RICE', 'bits': 'RICE', 'sbits': 'RICE', '__clzdi2': 'RICE', '__clzsi2': 'RICE', 'rice_next': 'RICE',
      'rice_init': 'RICE', 'residual': 'RICE', '__lshrdi3': 'RICE', '__ashldi3': 'RICE', 'rice_fast': 'RICE',
      '__ashrdi3': 'LPC', 'tau_lpc_hw_sample': 'LPC', 'tau_lpc_hw_begin': 'LPC', 'tau_lpc_hw_put': 'LPC', 'tau_lpc_hw_get': 'LPC',
      'to16': 'EMIT',
      'sink': 'SINK', 'pcm_pack': 'SINK', 'pcm_gain_apply': 'SINK',
      'need': 'REFILL', 'fill': 'REFILL', 'byte': 'REFILL', 'hread': 'REFILL', 'rd': 'REFILL'}
CATS = ['RICE', 'REFILL', 'LPC', 'EMIT', 'SINK', 'OTHER']
_tags = {}
def tag(src, line):
    if src not in _tags:
        _tags[src] = {}
        if os.path.exists(src):
            for i, l in enumerate(open(src).read().split('\n'), 1):
                m = re.search(r'@cat=(\w+)', l)
                if m: _tags[src][i] = m.group(1)
    return _tags[src].get(line)
def split(d):
    acc = {c: [0, 0] for c in CATS}
    for k, (i, c) in d['byln'].items():
        fn, ln = k.split('|')
        cat = None
        if ':' in ln and os.path.exists(ln.split(':')[0]):
            src, rest = ln.split(':', 1)
            try: cat = tag(src, int(rest.split()[0]))
            except ValueError: pass
        if cat is None: cat = FN.get(fn, 'OTHER')
        acc[cat][0] += i; acc[cat][1] += c
    return acc
if __name__ == '__main__':
    for p in sys.argv[1:]:
        d = json.load(open(p)); s = d['samples']; a = split(d)
        print('%-26s total %6.1f i/s %6.1f c/s  audio=%s' % (os.path.basename(p), d['ips'], d['cps'], d['audio_h']))
        for c in CATS:
            print('   %-7s %6.1f i/s (%4.1f%%)  %6.1f c/s (%4.1f%%)' % (c, a[c][0] / s, 100 * a[c][0] / d['instr'], a[c][1] / s, 100 * a[c][1] / d['cyc']))
