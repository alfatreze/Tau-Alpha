#!/usr/bin/env python3
"""Host test for fw/pcm_push.h (Cymo C1, B-533; dB taper and ramp B-598).
1. The volume table is the dB formula (position 100 = 0 dB, 0.6 dB per position, position 0 = mute), monotonic, Q15, with the end points exact.
2. A volume change never jumps: the gain moves toward its target by at most PCM_VOL_RAMP per pair, reaches it in at most ceil(32768 / RAMP) pairs, and a steady gain costs nothing.
3. The arithmetic after the gain (fade-in, pack) is the old arithmetic: checked against an independent reference over samples, positions and fade positions.
4. Muting is exact silence; unity leaves samples untouched."""
import math, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "%s"
#define FADE_SAMPLES 2048u
static long n, bad;
static void fail(const char *m, long a, long b) { if (bad++ < 8) printf("FAIL %%s %%ld %%ld\n", m, a, b); }
/* independent reference of the gain stage with a settled gain (no ramp), written from the spec */
static uint32_t ref(int32_t l, int32_t r, int32_t g15, uint32_t *fade_left)
{
    if (g15 != 32768) { l = (int32_t)(((int64_t)l * g15 + 16384) >> 15); r = (int32_t)(((int64_t)r * g15 + 16384) >> 15); }
    if (*fade_left) { int32_t g = (int32_t)((FADE_SAMPLES - *fade_left) >> 3); l = (l * g + 128) >> 8; r = (r * g + 128) >> 8; (*fade_left)--; }
    return ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
}
int main(void)
{
    static const int32_t samp[] = { 0, 1, -1, 2, -2, 255, -255, 256, 12345, -12345, 32766, 32767, -32767, -32768 };
    printf("TAB");
    for (int v = 0; v <= 100; v++) printf(" %%d", (int)pcm_vol_tab[v]);
    printf("\n");
    /* settled gain: every position, samples, fade positions */
    for (int step = 0; step <= 100; step++) {
        const int32_t g = pcm_vol_target((uint32_t)step);
        for (uint32_t f0 = 0; f0 <= FADE_SAMPLES + 2; f0 += (f0 < 40 ? 1 : 97)) {
            for (unsigned a = 0; a < sizeof samp / sizeof *samp; a++) for (unsigned b = 0; b < sizeof samp / sizeof *samp; b++) {
                pcm_vol_t v = { g, g };
                uint32_t fa = f0 > FADE_SAMPLES ? FADE_SAMPLES : f0, fb = fa;
                int32_t l = samp[a], r = samp[b];
                pcm_gain_apply(&l, &r, &v, &fa, FADE_SAMPLES);
                const uint32_t got = pcm_pack(l, r), want = ref(samp[a], samp[b], g, &fb);
                n++; if (got != want || fa != fb || v.cur != g) fail("settled gain", step, f0);
            }
        }
    }
    /* ramp: from every position to every position (coarse grid): per-pair move <= RAMP, monotone, arrives, then stays */
    for (int from = 0; from <= 100; from += 4) for (int to = 0; to <= 100; to += 4) {
        pcm_vol_t v = { pcm_vol_target((uint32_t)from), pcm_vol_target((uint32_t)to) };
        const int32_t start = v.cur, tgt = v.target;
        int pairs = 0; uint32_t fl = 0;
        int32_t prev = v.cur;
        while (v.cur != tgt && pairs < 1000) {
            int32_t l = 1000, r = -1000;
            pcm_gain_apply(&l, &r, &v, &fl, FADE_SAMPLES);
            const int32_t d = v.cur - prev; prev = v.cur; pairs++;
            if (d > PCM_VOL_RAMP || d < -PCM_VOL_RAMP) fail("ramp step too big", from, to);
            if ((tgt > start && d < 0) || (tgt < start && d > 0)) fail("ramp not monotone", from, to);
        }
        n++; if (v.cur != tgt || pairs > 220) fail("ramp did not arrive in 220 pairs", from, to);
        int32_t l = 1000, r = -1000; pcm_gain_apply(&l, &r, &v, &fl, FADE_SAMPLES);
        if (v.cur != tgt) fail("ramp moved after arriving", from, to);
    }
    /* mute is exact silence, unity is untouched */
    { pcm_vol_t v = { 0, 0 }; uint32_t fl = 0; int32_t l = 32767, r = -32768; pcm_gain_apply(&l, &r, &v, &fl, FADE_SAMPLES); n++; if (l != 0 || r != 0) fail("mute", l, r); }
    { pcm_vol_t v = { 32768, 32768 }; uint32_t fl = 0; int32_t l = 32767, r = -32768; pcm_gain_apply(&l, &r, &v, &fl, FADE_SAMPLES); n++; if (l != 32767 || r != -32768) fail("unity", l, r); }
    /* B-615: with the hardware gain stage owning the gain (hw = 1) this path must leave the samples, the ramp state and the fade counter untouched: a second application doubles the gain */
    { long bad0 = bad;
      for (int step = 0; step <= 100; step += 5) for (uint32_t f0 = 0; f0 <= FADE_SAMPLES; f0 += 301) {
        pcm_vol_t v = { 32768, pcm_vol_target((uint32_t)step), 1 }; uint32_t fl = f0; int32_t l = 12345, r = -23456;
        pcm_gain_apply(&l, &r, &v, &fl, FADE_SAMPLES); n++;
        if (l != 12345 || r != -23456 || fl != f0 || v.cur != 32768) fail("hw gain: software applied it too", step, (long)f0);
      }
      printf("HW %%s\n", bad == bad0 ? "single owner" : "DOUBLE"); }
    /* B-602: rounding. Over every 16-bit sample the mean error against the exact product is ~0 at every position (a floor shift gives -0.5) and no sample is off by more than 0.5 LSB */
    { double worst_mean = 0, worst_abs = 0;
      for (int step = 1; step < 100; step++) {
        const int32_t g = pcm_vol_target((uint32_t)step); double sum = 0; long cnt = 0;
        for (int32_t x = -32768; x <= 32767; x++) {
            pcm_vol_t v = { g, g }; uint32_t fl = 0; int32_t l = x, r = x;
            pcm_gain_apply(&l, &r, &v, &fl, FADE_SAMPLES);
            const double e = (double)l - (double)x * g / 32768.0; sum += e; cnt++;
            if ((e < 0 ? -e : e) > worst_abs) worst_abs = e < 0 ? -e : e;
        }
        const double m = sum / cnt; if ((m < 0 ? -m : m) > worst_mean) worst_mean = m < 0 ? -m : m;
      }
      printf("BIAS %%.5f %%.5f\n", worst_mean, worst_abs); n++; if (worst_mean > 0.01 || worst_abs > 0.5001) fail("rounding bias", 0, 0); }
    printf("%%s: %%ld cases, %%ld failures\n", bad ? "FAIL" : "ok", n, bad);
    return bad != 0;
}
'''
def main():
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; exe = Path(d) / "t"
        src.write_text(HARNESS % str(ROOT / "fw/pcm_push.h"))
        subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-o", str(exe), str(src)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        lines = r.stdout.splitlines()
        tab = [int(x) for x in lines[0].split()[1:]]
        sys.stdout.write("\n".join(lines[1:]) + "\n")
        fails = 0
        def check(name, ok):
            nonlocal fails
            print(("ok   " if ok else "FAIL ") + name); fails += 0 if ok else 1
        want = [0] + [round(32768 * 10 ** ((v - 100) * 0.6 / 20)) for v in range(1, 101)]
        check("table equals the dB formula (position 100 = 0 dB, 0.6 dB per position, 0 = mute)", tab == want)
        check("table is strictly increasing from position 1 and ends at unity", all(tab[i] < tab[i + 1] for i in range(1, 100)) and tab[100] == 32768 and tab[0] == 0)
        steps = [20 * math.log10(tab[i + 1] / tab[i]) for i in range(40, 100)]
        check("every position from 40 up is 0.6 dB (within rounding) above the last", all(abs(s - 0.6) < 0.02 for s in steps))
        check("position 1 is about -59.4 dB", abs(20 * math.log10(tab[1] / 32768) + 59.4) < 0.5)
        check("default position 94 is about today's old default loudness (-3.7 dB)", abs(20 * math.log10(tab[94] / 32768) + 3.6) < 0.2)
        if r.returncode or fails: sys.exit(1)
    print("PASSED")
if __name__ == "__main__": main()
