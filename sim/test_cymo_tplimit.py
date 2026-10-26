#!/usr/bin/env python3
"""Host test for tools/lab/cymo_tplimit_model.py (true-peak limiter model, B-622)."""
import os, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'lab'))
import cymo_tplimit_model as m
sys.exit(1 if m.selftest(verbose=True) else 0)
