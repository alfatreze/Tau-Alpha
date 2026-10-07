#!/usr/bin/env python3
"""Host test for tools/lab/cymo_crossfeed_model.py (headphone crossfeed model, B-621): response, delay, mono invariance, worst-case overshoot, integer reference."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'lab'))
import cymo_crossfeed_model as m
sys.exit(1 if m.selftest(verbose=True) else 0)
