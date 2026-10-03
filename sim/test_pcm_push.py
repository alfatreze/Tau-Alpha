#!/usr/bin/env python3
"""Host test for fw/pcm_push.h (Cymo C1, B-533): the shared volume + fade + pack arithmetic must equal, bit for bit, the two copies it replaced in fw/player.c
(the MP3 loop and flac_emit). The reference below is those lines verbatim, over a sweep of samples, volumes and fade positions, including the fade running out."""
import subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
HARNESS = r'''
#include <stdint.h>
#include <stdio.h>
#include "%s"
#define FADE_SAMPLES 2048u
static uint32_t ref(int32_t l, int32_t r, int32_t vol_gain, uint32_t *fade_left)
{
    if (vol_gain != 256) { l = (l * vol_gain) >> 8; r = (r * vol_gain) >> 8; }
    if (*fade_left) { int32_t g = (int32_t)((FADE_SAMPLES - *fade_left) >> 3); l = (l * g) >> 8; r = (r * g) >> 8; (*fade_left)--; }
    return ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
}
int main(void)
{
    long n = 0, bad = 0;
    static const int32_t samp[] = { 0, 1, -1, 2, -2, 255, -255, 256, 12345, -12345, 32766, 32767, -32767, -32768 };
    for (int vol = 0; vol <= 100; vol++) {
        const int32_t g = (int32_t)((uint32_t)vol * 256u / 100u);
        for (uint32_t f0 = 0; f0 <= FADE_SAMPLES + 2; f0 += (f0 < 40 ? 1 : 97)) {
            for (unsigned a = 0; a < sizeof samp / sizeof *samp; a++) for (unsigned b = 0; b < sizeof samp / sizeof *samp; b++) {
                uint32_t fa = f0 > FADE_SAMPLES ? FADE_SAMPLES : f0, fb = fa;
                int32_t l = samp[a], r = samp[b];
                pcm_gain_apply(&l, &r, g, &fa, FADE_SAMPLES);
                const uint32_t got = pcm_pack(l, r), want = ref(samp[a], samp[b], g, &fb);
                n++; if (got != want || fa != fb) { if (bad++ < 5) printf("FAIL vol %%d fade %%u l %%d r %%d\n", vol, f0, samp[a], samp[b]); }
            }
        }
    }
    printf("%%s: %%ld cases, %%ld mismatches\n", bad ? "FAIL" : "ok", n, bad);
    return bad != 0;
}
'''
def main():
    with tempfile.TemporaryDirectory() as d:
        src = Path(d) / "t.c"; exe = Path(d) / "t"
        src.write_text(HARNESS % str(ROOT / "fw/pcm_push.h"))
        subprocess.run(["cc", "-O1", "-Wall", "-Werror", "-o", str(exe), str(src)], check=True)
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode: sys.exit(1)
    print("PASSED")
if __name__ == "__main__": main()
