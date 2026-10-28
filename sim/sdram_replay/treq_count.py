#!/usr/bin/env python3
"""Prints the number of pictures (PIC_START markers) of a .treq file. MIT licence."""
import sys

d = open(sys.argv[1], 'rb').read()
print(sum(1 for i in range(0, len(d), 16) if d[i + 1] == 2 and d[i + 2] == 1 and d[i + 3] == 0))
