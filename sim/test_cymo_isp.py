#!/usr/bin/env python3
"""Host test for tools/lab/cymo_isp.py (inter-sample-peak test files and analyser, B-617): known true peaks, and verdicts on simulated linear and clipping chains."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'lab')); sys.path.insert(0, os.path.join(ROOT, 'tools'))
import cymo_isp as t
sys.exit(1 if t.selftest(verbose=True) else 0)
