"""run.py <elf> <blob> [--N n] [--fail k] [--lines]: profile one decode; prints totals, per-function, per-line."""
import sys, subprocess, collections, json
import prof_sim
T = "/Users/abel.santos/Downloads/DEV PROJECTS/Tau Alpha/tau-alpha/toolchain/xpack-riscv-none-elf-gcc-15.2.0-1/bin/riscv-none-elf-"
args = sys.argv[1:]
elf, blob = args[0], args[1]
N = None; fail = -1; show_lines = '--lines' in args
if '--N' in args: N = int(args[args.index('--N') + 1])
if '--fail' in args: fail = int(args[args.index('--fail') + 1])
m = prof_sim.Machine(); m.lpc_N = N; m.lpc_fail_at = fail
m.load_elf(elf); m.blob = open(blob, 'rb').read()
import io, contextlib
buf = io.StringIO()
with contextlib.redirect_stdout(buf):
    rc = m.run()
out = buf.getvalue().strip()
samples = int(out.split('samples/ch ')[1]) * 2 if 'samples/ch' in out else 0
pcs = [i * 4 for i, c in enumerate(m.pcc) if c]
r = subprocess.run([T + 'addr2line', '-f', '-e', elf] + ['0x%x' % p for p in pcs], capture_output=True, text=True).stdout.split('\n')
fl = {}
for k, p in enumerate(pcs):
    fn = r[2 * k]; ln = r[2 * k + 1].rsplit('/', 1)[-1]
    fl[p] = (fn, ln)
byfn = collections.Counter(); byfn_c = collections.Counter(); byln = collections.Counter(); byln_c = collections.Counter()
for p in pcs:
    fn, ln = fl[p]
    byfn[fn] += m.pcc[p >> 2]; byfn_c[fn] += m.pcy[p >> 2]
    byln[(fn, ln)] += m.pcc[p >> 2]; byln_c[(fn, ln)] += m.pcy[p >> 2]
I = sum(byfn.values()); C = sum(byfn_c.values())
res = dict(out=out, rc=rc, samples=samples, instr=I, cyc=C, ips=I / samples, cps=C / samples,
           audio_n=m.audio_n, audio_h='%08x' % m.audio_h, lpc_writes=m.lpc_writes, lpc_polls=m.lpc_polls,
           byfn={k: (byfn[k], byfn_c[k]) for k in byfn}, byln={'%s|%s' % k: (byln[k], byln_c[k]) for k in byln})
print('%s  rc=%s  samples=%d  instr/sample=%.2f  cyc/sample=%.2f  audio=%d/%s  lpc_wr=%d polls=%d' % (
    out, rc, samples, res['ips'], res['cps'], m.audio_n, res['audio_h'], m.lpc_writes, m.lpc_polls))
for fn, v in byfn.most_common():
    print('  %-28s %7.2f i/s  %7.2f c/s' % (fn, v / samples, byfn_c[fn] / samples))
if show_lines:
    for k, v in byln.most_common(60):
        print('    %-24s %-18s %7.2f i/s %7.2f c/s' % (k[0], k[1], v / samples, byln_c[k] / samples))
tag = sys.argv[1].split('/')[-1].replace('.elf', '') + '_' + blob.replace('.bin', '') + ('_N%d' % N if N is not None else '') + ('_F%d' % fail if fail >= 0 else '')
json.dump(res, open('out/' + tag + '.json', 'w'))
