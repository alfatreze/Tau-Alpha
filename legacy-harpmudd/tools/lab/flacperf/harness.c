/* flacperf: drives the real fw/flac.c (or a variant) over a slice of a real FLAC under prof_sim.py.
 * Blob layout: u32 channels, u32 bps, u32 maxblock, u32 nframes, then raw frame bytes (frame-aligned). */
#include "hostio.h"
#include "flac.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define R_PCM_ST 0xF0000120u
#define R_AUDIO  0xF0000124u
#define FADE_SAMPLES 4096u

static flac_t f;
static int32_t ch0[8192];
static uint32_t cur = 16;

static int rd(void *ctx, uint8_t *dst, int n)
{
    (void)ctx;
    uint32_t got = hread(cur, dst, (uint32_t)n);
    cur += got;
    return (int)got;
}

/* Player 2 sink: flac_sink() -> aout_put(): one packed word per pair stored to the PSRAM ring window (uncached, ~26 cycles a store), ring index wrap, one MMIO write of the
 * write pointer per block (fw2/aout/aout.c). tl_put accounting is a few adds. */
#define RING_WIN ((volatile uint32_t *)0xF0010000u)
static uint32_t wix;
__attribute__((noinline)) void sink(void *ctx, const int16_t *src, uint32_t frames)
{
    (void)ctx;
    volatile uint32_t *win = RING_WIN;
    uint32_t w = wix;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t l = src[i * 2], r = src[i * 2 + 1];
        win[w] = ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
        if (++w == 32768u) w = 0u;
    }
    wix = w;
    REG(R_AUDIO) = w;
}

int main(void)
{
    uint32_t hdr[4];
    hread(0, hdr, 16);
    f.read = rd;
    f.channels = (uint8_t)hdr[0];
    f.bps = (uint8_t)hdr[1];
    f.max_blocksize = hdr[2];
    f.ch0 = ch0; f.ch0_cap = 8192;
    uint32_t total = 0;
    for (uint32_t k = 0; k < hdr[3]; k++) {
        flac_err e = flac_decode_frame(&f, sink, 0);
        if (e) { hputs("ERR "); hputu((uint32_t)e); hputs(" at frame "); hputu(k); hnl(); return 1; }
        total += f.blocksize;
    }
    hputs("frames "); hputu(hdr[3]); hputs(" samples/ch "); hputu(total); hnl();
    return 0;
}
