#!/usr/bin/env python3
"""Cymo A/B listening render: real music through the current hold vs candidate resamplers.

cymo_resamp_model.py answers "how good is this in theory" against synthetic tones; this answers
the question that actually matters and that nobody has tested yet -- can a human hear the
difference on real material. Decodes a real 44.1 kHz FLAC with tools/flac_ref.py's proven
bit-exact decoder (the same one fw/flac.c is checked against), then renders the SAME audio
through three candidate output paths, all at 48 kHz / 16-bit, as separate WAV files:

  <name>_A_hold.wav    -- the current pcm_fifo.v behaviour: nearest/held sample, no filtering
  <name>_B_fir32.wav   -- 32-tap Kaiser polyphase FIR (the recommended candidate, section 15)
  <name>_C_cubic.wav   -- 4-point Catmull-Rom, no coefficient ROM (the cheap alternative)

Usage:
    python3 tools/lab/cymo_ab_listen.py "path/to/track.flac" outdir/ [--seconds 20]

Listen to A vs B (and C) back to back. If B is not audibly better than A on real music, that is
real evidence against spending RTL/Quartus effort on this -- exactly the test this project's own
"prove it before spending a Quartus slot" discipline calls for, and the one thing the SINAD tables
alone cannot answer.
"""
import argparse
import os
import sys
import wave

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
import flac_ref  # noqa: E402
from cymo_resamp_model import P, Q, FS_IN, FS_OUT, build_banks, catmull_rom  # noqa: E402


def decode_pcm(path, max_seconds=None):
    """Full PCM decode using flac_ref.py's own proven bit-exact primitives (Bits/open_stream/
    frame_header/subframe) -- the same functions its own MD5 self-check verifies, just collecting
    samples into lists instead of an MD5 digest."""
    data = open(path, "rb").read()
    b = flac_ref.Bits(data)
    si = flac_ref.open_stream(b)
    cap = si["maxb"]
    ch0, ch1 = [0] * cap, [0] * cap
    l_out, r_out = [], []
    max_samples = int(max_seconds * si["rate"]) if max_seconds else None
    while True:
        if max_samples is not None and len(l_out) >= max_samples:
            break
        try:
            blocksize, m = flac_ref.frame_header(b, cap)
        except (EOFError, ValueError):
            break
        try:
            bps0 = si["bps"] + (1 if m == 9 else 0)
            flac_ref.subframe(b, blocksize, ch0, bps0)
            if si["ch"] == 1:
                for i in range(blocksize):
                    l_out.append(ch0[i]); r_out.append(ch0[i])
            else:
                bps1 = si["bps"] + (1 if m in (8, 10) else 0)
                flac_ref.subframe(b, blocksize, ch1, bps1)
                for i in range(blocksize):
                    a, s = ch0[i], ch1[i]
                    if m == 8:
                        l, r = a, a - s
                    elif m == 9:
                        r, l = s, s + a
                    elif m == 10:
                        mid = (a << 1) | (s & 1)
                        l, r = (mid + s) >> 1, (mid - s) >> 1
                    else:
                        l, r = a, s
                    l_out.append(l); r_out.append(r)
            b.align(); b.bits(16)
        except (EOFError, ValueError):
            break
    return si["rate"], si["bps"], l_out, r_out


def hold_resample(ch, n_out):
    """Matches pcm_fifo.v exactly: for output tick k (at FS_OUT), the input sample 'currently
    held' is floor(k * FS_IN / FS_OUT) -- no interpolation, no filtering."""
    n_in = len(ch)
    return [ch[min(int(k * FS_IN / FS_OUT), n_in - 1)] for k in range(n_out)]


def fir_resample(ch, banks, taps_per_bank, n_out):
    out = []
    phase, in_pos = 0, 0
    n_in = len(ch)
    for _ in range(n_out):
        bank = banks[phase]
        acc = 0.0
        for t in range(taps_per_bank):
            idx = in_pos - 1 - t
            if 0 <= idx < n_in:
                acc += bank[t] * ch[idx]
        out.append(acc)
        phase += Q
        if phase >= P:
            phase -= P
            in_pos += 1
    return out


def cubic_resample(ch, n_out, frac_bits=16):
    out = []
    phase, in_pos = 0, 0
    n_in = len(ch)
    frac_scale = 1 << frac_bits
    for _ in range(n_out):
        mu = round((phase / P) * frac_scale) / frac_scale
        p0 = ch[in_pos - 1] if 0 <= in_pos - 1 < n_in else 0.0
        p1 = ch[in_pos] if 0 <= in_pos < n_in else 0.0
        p2 = ch[in_pos + 1] if 0 <= in_pos + 1 < n_in else 0.0
        p3 = ch[in_pos + 2] if 0 <= in_pos + 2 < n_in else 0.0
        out.append(catmull_rom(p0, p1, p2, p3, mu))
        phase += Q
        if phase >= P:
            phase -= P
            in_pos += 1
    return out


def write_wav(path, l, r, rate=FS_OUT):
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        buf = bytearray(len(l) * 4)
        for i in range(len(l)):
            lv = max(-32768, min(32767, int(round(l[i]))))
            rv = max(-32768, min(32767, int(round(r[i]))))
            buf[i * 4:i * 4 + 2] = lv.to_bytes(2, "little", signed=True)
            buf[i * 4 + 2:i * 4 + 4] = rv.to_bytes(2, "little", signed=True)
        w.writeframes(bytes(buf))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("flac", help="source FLAC file (44.1 kHz)")
    ap.add_argument("outdir", help="directory for the rendered WAV files")
    ap.add_argument("--seconds", type=float, default=20.0, help="how much of the track to render")
    ap.add_argument("--taps", type=int, default=32)
    ap.add_argument("--window", default="kaiser")
    a = ap.parse_args()

    rate, bps, l16, r16 = decode_pcm(a.flac, max_seconds=a.seconds)
    if rate != FS_IN:
        sys.exit("this tool models the %d -> %d Hz path; source is %d Hz" % (FS_IN, FS_OUT, rate))
    print("decoded %d samples (%.1fs) at %d Hz, %d-bit" % (len(l16), len(l16) / rate, rate, bps))

    n_out = int(len(l16) * FS_OUT / FS_IN)
    os.makedirs(a.outdir, exist_ok=True)
    base = os.path.splitext(os.path.basename(a.flac))[0]

    print("rendering A (hold, current pcm_fifo.v behaviour)...")
    write_wav(os.path.join(a.outdir, base + "_A_hold.wav"), hold_resample(l16, n_out), hold_resample(r16, n_out))

    print("rendering B (%d-tap %s polyphase FIR)..." % (a.taps, a.window))
    banks = build_banks(a.taps, a.window)
    write_wav(os.path.join(a.outdir, base + "_B_fir%d.wav" % a.taps),
              fir_resample(l16, banks, a.taps, n_out), fir_resample(r16, banks, a.taps, n_out))

    print("rendering C (cubic Catmull-Rom, no coefficient ROM)...")
    write_wav(os.path.join(a.outdir, base + "_C_cubic.wav"),
              cubic_resample(l16, n_out), cubic_resample(r16, n_out))

    print("\nDone. Listen to A vs B (and C) back to back -- ideally on headphones, at matched volume.")


if __name__ == "__main__":
    main()
