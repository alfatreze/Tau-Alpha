/* Runs fw/wsola_core.h (the real firmware code, built by the vendored rv32im toolchain) under tools/rv32sim.py on a raw mono int16 file given as the simulator's
 * data file, at the speed in WSOLA_SPEED_Q8 and rate WSOLA_FS, and prints a hash of the output and the number of grains. rv32sim prints the instruction count
 * on exit; run with a short and a long input and subtract to get the cost per grain without the start-up. */
#include "hostio.h"
#include "../../fw/wsola_core.h"

#ifndef WSOLA_SPEED_Q8
#define WSOLA_SPEED_Q8 384u
#endif
#ifndef WSOLA_FS
#define WSOLA_FS 44100u
#endif
#ifndef WSOLA_MAX_SAMPLES
#define WSOLA_MAX_SAMPLES 300000u
#endif
static int16_t x[WSOLA_MAX_SAMPLES] __attribute__((aligned(16)));

int main(void)
{
    uint32_t n = hfilesize() / 2u;
    if (n > WSOLA_MAX_SAMPLES) n = WSOLA_MAX_SAMPLES;
    hread(0, (uint8_t *)x, n * 2u);
    ws_t s;
    ws_init(&s, WSOLA_FS, 1u, WSOLA_SPEED_Q8);
    int16_t out[WS_MAX_HS];
    uint32_t h = 2166136261u, produced = 0u;
    for (;;) {
        uint32_t lo, hi;
        ws_need(&s, &lo, &hi);
        if (hi > n) break;
        const uint32_t k = ws_step(&s, x, (const int16_t *)0, 0u, out, (int16_t *)0);
        for (uint32_t i = 0; i < k; i++) { h ^= (uint16_t)out[i]; h *= 16777619u; }
        produced += k;
    }
    hputs("grains "); hputu(s.k); hputs(" samples "); hputu(produced); hputs(" hash "); hputx(h); hnl();
    hexit(0);
    return 0;
}
