#!/usr/bin/env python3
"""Host test for tools/lab/cymo_loopback.py (Cymo analog loopback analyser).

Checks the analyser against known signals so a later loopback result can be trusted: level readout,
the nearest-neighbour resampling error the Cymo plan predicts for 44.1 -> 48 kHz (about 28 dB SINAD and
an image at 4.9 kHz), a much cleaner cubic resampler, a flat synthetic sweep, and the WAV reader.
Also confirms the generated test FLAC is bit-exact through the repo's own verifier."""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'lab'))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import cymo_loopback as c  # noqa: E402

fails = c.selftest(nfft=1 << 15, verbose=False)

# one generated file, decoded bit-exact by tools/flac_verify.py
with tempfile.TemporaryDirectory() as d:
    path = os.path.join(d, 'probe.flac')
    c._write_flac(path, c._tone(44100, 1000, 1, -6.0), 44100)
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'flac_verify.py'), path],
                       capture_output=True, text=True)
    if 'BIT-EXACT' not in r.stdout:
        fails.append('generated FLAC is not bit-exact:\n' + r.stdout[-300:] + r.stderr[-300:])

if fails:
    print('FAIL cymo_loopback:\n  ' + '\n  '.join(fails))
    sys.exit(1)
print('PASS cymo_loopback (analyser self-test, sweep, WAV reader, FLAC bit-exact)')
