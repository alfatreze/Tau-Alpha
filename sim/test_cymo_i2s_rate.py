#!/usr/bin/env python3
"""Compare the simulated pcm_fifo -> sound_i2s output (sim/tb_cymo_i2s_rate.v) with the ideal 44.1 kHz -> 48 kHz hold.

Prints level / SINAD / spurs of the simulated stream next to the model's, plus how many output slots differ from the
ideal hold. Not in `make test-host`: it needs iverilog and runs for minutes. Usage: python3 sim/test_cymo_i2s_rate.py
"""
import math, os, subprocess, sys
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'lab'))
import cymo_loopback as c

def main():
    build = os.path.join(ROOT, 'build', 'rtl')
    os.makedirs(build, exist_ok=True)
    sim = os.path.join(build, 'sound_i2s_sim.v')
    src = open(os.path.join(ROOT, 'src/fpga/core/sound_i2s.v')).read()
    fixed = src.replace("{(15 - CHANNEL_WIDTH){1'b0}}", "{((CHANNEL_WIDTH < 15) ? (15 - CHANNEL_WIDTH) : 1){1'b0}}")
    open(sim, 'w').write(fixed)          # Icarus rejects the unused negative-repeat branch; content is unchanged for 16-bit
    vvp = os.path.join(build, 'tb_cymo_i2s_rate.vvp')
    subprocess.check_call(['iverilog', '-g2012', '-o', vvp, os.path.join(ROOT, 'sim/tb_cymo_i2s_rate.v'),
                           os.path.join(ROOT, 'src/fpga/core/pcm_fifo.v'), sim,
                           os.path.join(ROOT, 'src/fpga/core/sync_fifo.v')], cwd=ROOT)
    print(subprocess.check_output(['vvp', vvp], cwd=ROOT).decode().strip().splitlines()[-1])
    rows = [tuple(map(int, l.split())) for l in open(os.path.join(build, 'cymo_i2s_rate.txt'))]
    got = [l / 32768.0 for l, _ in rows][200:]          # skip priming/glide at the start
    n = len(got)
    nfft = 1 << (n.bit_length() - 1)
    r = c.analyze_tone(got, 48000, 1000, nfft=min(nfft, 1 << 13))
    print('simulated  level %.2f dBFS  SINAD %.2f dB  spurs %s' % (r['level_dbfs'], r['sinad_db'],
          ', '.join('%.0f Hz %.1f dBc' % (s['hz'], s['dbc']) for s in r['spurs'][:4])))
    src_s = [round(16383.5 * math.sin(2 * math.pi * 1000 * k / 44100) - 0.5 + 0.5) / 32768.0 for k in range(20000)]
    ideal = [src_s[int(k / 48000 * 44100)] for k in range(n)]
    m = c.analyze_tone(ideal, 48000, 1000, nfft=min(nfft, 1 << 13))
    print('ideal hold level %.2f dBFS  SINAD %.2f dB' % (m['level_dbfs'], m['sinad_db']))

if __name__ == '__main__':
    main()
