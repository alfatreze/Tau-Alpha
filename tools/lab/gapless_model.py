#!/usr/bin/env python3
"""Gapless playback sizing model (B-723, docs/features/GAPLESS_SPEC.md): how deep must the decoded-audio buffer be for the next track to start with no gap?

A natural track end is gapless when the next track's first sample reaches the DAC before the old track's last buffered sample has played. The audio left in the buffer when the
old track's decoder hits end of file is `depth`; the work that must fit inside it is the load (`load_ms`: head read, setup, size probe, cover, prefill up to the first pushed pair).
gap_ms = max(0, load_ms - depth_ms). Load times are MEASURED on a Pocket (LOAD MS rows in docs/AUDIT_TRAIL.md: B-562 263/266 ms totals, 292 ms with a cover decode, 1,170 ms for a
FLAC with a 1.1 s head read, B-544) and the manual track change of B-717 (1.5 s press to sound, which includes the press and the UI). Memory: 4 bytes per stereo pair; M10K = 10,240 bits."""
LOADS = [("album track, cover cached (B-562)", 263), ("album track, cover decode (B-562)", 292), ("FLAC with a 1.1 s head read (B-544)", 1170), ("manual change press to sound (B-717)", 1480)]
RATES = (44100, 48000)

def depth_ms(pairs, rate): return 1000.0 * pairs / rate
def m10k(pairs): return -(-pairs * 32 // 10240)

OPTIONS = [("today's FIFO (2,048 pairs)", 2048), ("RTL FIFO 8,192 pairs", 8192), ("RTL FIFO 16,384 pairs", 16384)]
RING_S = (0.5, 1.0, 1.5, 2.0, 3.0)
def table():
    rows = []
    for name, pairs in OPTIONS:
        rows.append((name, pairs, m10k(pairs), 0))
    for s in RING_S:
        pairs = int(s * 44100)
        rows.append((f"PSRAM ring {s:g} s at 44.1 kHz", pairs, 0, pairs * 4))
    return rows

def need(load_ms, margin_ms=100): return load_ms + margin_ms        # depth that hides a load, with a safety margin for SD jitter

if __name__ == "__main__":
    print("%-34s %8s %7s %8s | gap (ms) at 44.1 kHz for each measured load" % ("buffer", "pairs", "M10K", "bytes"))
    print(" " * 62 + " | " + "  ".join("%6d" % l for _, l in LOADS))
    for name, pairs, blocks, byt in table():
        print("%-34s %8d %7d %8d | " % (name, pairs, blocks, byt) + "  ".join("%6.0f" % max(0.0, l - depth_ms(pairs, 44100)) for _, l in LOADS))
    print()
    for n, l in LOADS:
        d = need(l); print("depth that hides %-38s %5d ms (+100 ms margin) = %6d pairs = %5.0f KiB at 44.1 kHz, %5.0f KiB at 48 kHz" % (n + ":", d, d * 44100 // 1000, d * 44100 // 1000 * 4 / 1024, d * 48000 // 1000 * 4 / 1024))
