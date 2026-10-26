/* Harness for fw2/cores/audio_stats_core.h: one command a line on stdin, one answer a line. */
#include <stdio.h>
#include "audio_stats_core.h"
int main(void)
{
    char c; as_onset_t os = {{0}, 0, 0, 0, 0};
    while (scanf(" %c", &c) == 1) {
        if (c == 'r') { unsigned long long s; unsigned n; scanf("%llu %u", &s, &n); printf("%u\n", as_rms(s, n)); }
        else if (c == 'c') { unsigned long long l, r; long long x; scanf("%llu %llu %lld", &l, &r, &x); printf("%d\n", as_corr_q8(l, r, x)); }
        else if (c == 'm') { unsigned v[16]; uint8_t sp[16]; for (int i = 0; i < 16; i++) { scanf("%u", &v[i]); sp[i] = (uint8_t)v[i]; } printf("%u\n", as_centroid(sp)); }
        else if (c == 'k') { unsigned p, r; scanf("%u %u", &p, &r); printf("%u\n", as_crest_q4(p, r)); }
        else if (c == 'z') { os = (as_onset_t){{0}, 0, 0, 0, 0}; }
        else if (c == 'o') { unsigned v[16], t; uint8_t sp[16]; for (int i = 0; i < 16; i++) { scanf("%u", &v[i]); sp[i] = (uint8_t)v[i]; } scanf("%u", &t); as_onset_step(&os, sp, t); printf("%u %u\n", os.flux, os.onset); }
    }
    return 0;
}
