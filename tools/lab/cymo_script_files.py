#!/usr/bin/env python3
"""Test files for docs/features/CYMO_RECORDING_SCRIPT.md section 3 that `cymo_loopback.py gen` does not make.

  python3 tools/lab/cymo_script_files.py OUTDIR [--only NAME ...]

16-bit (or 24-bit for fade24) VERBATIM FLAC through the repo's own writer; RG tags with mutagen; the silence MP3
with lame. Existing files in OUTDIR are left alone unless --force.
"""
import argparse, math, os, subprocess, sys, tempfile, wave

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import cymo_loopback as cl


def tone(rate, f, sec, db, ch='lr'):
    a = 10.0 ** (db / 20.0)
    out = []
    for i in range(int(rate * sec)):
        v = cl._to_pcm(a * math.sin(2 * math.pi * f * i / rate))
        out.append((v if 'l' in ch else 0, v if 'r' in ch else 0))
    return out


def lf_tones(rate):
    out = []
    for f in (10, 17, 20, 30, 50):
        out += tone(rate, f, 6, -12.0)
    return out


def impulses(rate, ch, sec=12):
    a = cl._to_pcm(10.0 ** (-6.0 / 20.0))
    out = [(0, 0)] * (rate * sec)
    for s in range(sec):
        i = rate // 2 + s * rate
        out[i] = (a if ch == 'l' else 0, a if ch == 'r' else 0)
    return out


def levels(rate):
    out = []
    for db in (-20, -40, -60, -70, -80, -90):
        out += tone(rate, 1000, 10, float(db))
    return out


def write_flac(path, samples, rate, bps=16):
    import flac_make_test as fm
    block = 4096
    total = cl._pad_block(len(samples), block)
    samples = samples + [(0, 0)] * (total - len(samples))
    frames = b''.join(fm.make_frame(i, samples[i * block:(i + 1) * block], bps, rate, block, 1)
                      for i in range(total // block))
    si = fm.BitWriter()
    for v, n in ((block, 16), (block, 16), (0, 24), (0, 24), (rate, 20), (1, 3), (bps - 1, 5), (total, 36)):
        si.write(v, n)
    for b in fm.pcm_md5(samples, bps, 2):
        si.write(b, 8)
    sib = si.bytes()
    with open(path, 'wb') as f:
        f.write(b'fLaC' + bytes([0x80]) + len(sib).to_bytes(3, 'big') + sib + frames)


def fade24(rate=48000, sec=40):
    peak = (1 << 23) - 1
    out = []
    n = rate * sec
    for i in range(n):
        db = -6.0 + (-100.0 + 6.0) * i / n
        v = int(round(10.0 ** (db / 20.0) * math.sin(2 * math.pi * 1000 * i / rate) * peak))
        out.append((v, v))
    return out


def tag_rg(path):
    from mutagen.flac import FLAC
    t = FLAC(path)
    t['REPLAYGAIN_TRACK_GAIN'] = '-6.00 dB'
    t['REPLAYGAIN_ALBUM_GAIN'] = '-3.00 dB'
    t['TITLE'] = 'RG tone -6 dBFS'
    t.save()


def silence_mp3(path, rate=44100, sec=30):
    with tempfile.TemporaryDirectory() as d:
        w = os.path.join(d, 's.wav')
        with wave.open(w, 'wb') as f:
            f.setnchannels(2); f.setsampwidth(2); f.setframerate(rate)
            f.writeframes(b'\0' * 4 * rate * sec)
        subprocess.run(['lame', '--quiet', '-b', '128', w, path], check=True)


def plan():
    P = []
    P.append(('tone_1k_48000_60s.flac', lambda p: write_flac(p, tone(48000, 1000, 60, -6.0), 48000)))
    P.append(('tone_1k_44100_60s.flac', lambda p: write_flac(p, tone(44100, 1000, 60, -6.0), 44100)))
    P.append(('tone_1500_48000.flac', lambda p: write_flac(p, tone(48000, 1500, 12, -6.0), 48000)))
    P.append(('tone_18k_44100.flac', lambda p: write_flac(p, tone(44100, 18000, 12, -6.0), 44100)))
    for r in (48000, 44100):
        P.append(('sweep_20_20k_%d.flac' % r, lambda p, r=r: write_flac(p, cl._sweep(r, 20.0, 20000.0, 30, -12.0), r)))
    P.append(('lf_tones_48000.flac', lambda p: write_flac(p, lf_tones(48000), 48000)))
    P.append(('ch_left_only_48000.flac', lambda p: write_flac(p, tone(48000, 1000, 12, -12.0, 'l'), 48000)))
    P.append(('ch_right_only_48000.flac', lambda p: write_flac(p, tone(48000, 1000, 12, -12.0, 'r'), 48000)))
    P.append(('ch_impulse_l_48000.flac', lambda p: write_flac(p, impulses(48000, 'l'), 48000)))
    P.append(('ch_impulse_r_48000.flac', lambda p: write_flac(p, impulses(48000, 'r'), 48000)))
    P.append(('levels_48000.flac', lambda p: write_flac(p, levels(48000), 48000)))
    P.append(('fade24_48000.flac', lambda p: write_flac(p, fade24(), 48000, 24)))

    def rg(p):
        write_flac(p, tone(48000, 1000, 12, -6.0), 48000)
        tag_rg(p)
    P.append(('rg_tone_m6_48000.flac', rg))
    P.append(('silence_44100.mp3', silence_mp3))
    return P


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir')
    ap.add_argument('--only', nargs='*')
    ap.add_argument('--force', action='store_true')
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    for name, fn in plan():
        if a.only and name not in a.only:
            continue
        p = os.path.join(a.outdir, name)
        if os.path.exists(p) and not a.force:
            print('%-28s exists' % name)
            continue
        fn(p)
        print('%-28s %5.1f MB' % (name, os.path.getsize(p) / 1e6))


if __name__ == '__main__':
    main()
