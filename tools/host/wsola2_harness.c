/* Runs the v2 core of fw/wsola_core.h (feed in chunks, reader callback; the real firmware code, built by the vendored rv32im toolchain) under tools/rv32sim.py on a raw mono
 * int16 file given as the simulator's data file, and prints a hash of the output (identical to tools/host/wsola_harness.c's for the same input: v2 is the same algorithm)
 * and the grain count. rv32sim prints the instruction count on exit; run with a short and a long input and subtract to get the cost per grain. */
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
#ifndef WSOLA_CHUNK
#define WSOLA_CHUNK 64u
#endif
static int16_t x[WSOLA_MAX_SAMPLES] __attribute__((aligned(16)));
static ws2_t st;

static void rd(void *ctx, uint32_t ch, uint32_t lo, uint32_t n, int16_t *dst)
{
    (void)ctx; (void)ch;
    for (uint32_t i = 0; i < n; i++) dst[i] = x[lo + i];
}

int main(void)
{
    uint32_t n = hfilesize() / 2u;
    if (n > WSOLA_MAX_SAMPLES) n = WSOLA_MAX_SAMPLES;
    hread(0, (uint8_t *)x, n * 2u);
    ws2_init(&st, WSOLA_FS, 1u, WSOLA_SPEED_Q8);
    int16_t out[WS_MAX_HS];
    uint32_t h = 2166136261u, produced = 0u, pos = 0u;
    while (pos < n) {
        uint32_t m = n - pos < WSOLA_CHUNK ? n - pos : WSOLA_CHUNK;
        ws2_feed(&st, x + pos, (const int16_t *)0, m);
        pos += m;
        while (ws2_ready(&st)) {
            const uint32_t k = ws2_step(&st, rd, 0, out, (int16_t *)0);
            for (uint32_t i = 0; i < k; i++) { h ^= (uint16_t)out[i]; h *= 16777619u; }
            produced += k;
        }
    }
    hputs("grains "); hputu(st.core.k); hputs(" samples "); hputu(produced); hputs(" hash "); hputx(h); hnl();
    hexit(0);
    return 0;
}
