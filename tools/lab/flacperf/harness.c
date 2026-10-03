/* flacperf: drives the real fw/flac.c (or a variant) over a slice of a real FLAC under prof_sim.py.
 * Blob layout: u32 channels, u32 bps, u32 maxblock, u32 nframes, then raw frame bytes (frame-aligned). */
#include "hostio.h"
#include "flac.h"
#include "pcm_push.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define R_PCM_ST 0xF0000120u
#define R_AUDIO  0xF0000124u
#define FADE_SAMPLES 4096u

static flac_t f;
static int32_t ch0[8192];
static uint32_t cur = 16;
int32_t vol_gain = 256;
uint32_t fade_left = 0;
static int16_t mpcm[2304];
static uint32_t mn, meter_flushes;

static int rd(void *ctx, uint8_t *dst, int n)
{
    (void)ctx;
    uint32_t got = hread(cur, dst, (uint32_t)n);
    cur += got;
    return (int)got;
}

/* Model of player.c flac_emit() per-sample work (meter copy, gain/fade, FIFO status, pack, write);
 * omits ui_draw_dynamic() and meters_feed() (separate subsystems, once per 64/1152 pairs). */
__attribute__((noinline)) void sink(void *ctx, const int16_t *src, uint32_t frames)
{
    (void)ctx;
    (void)REG(R_PCM_ST);                       /* stands in for the cycles() read once per call */
    for (uint32_t i = 0; i < frames; i++) {
        mpcm[mn * 2u] = src[i * 2]; mpcm[mn * 2u + 1u] = src[i * 2 + 1];
        if (++mn == 1152u) { mn = 0; meter_flushes++; }
        int32_t l = src[i * 2], r = src[i * 2 + 1];
        pcm_gain_apply(&l, &r, vol_gain, &fade_left, FADE_SAMPLES);
        uint32_t st = REG(R_PCM_ST);
        if (st & 1u) { while (REG(R_PCM_ST) & 1u) { } }
        REG(R_AUDIO) = pcm_pack(l, r);
    }
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
