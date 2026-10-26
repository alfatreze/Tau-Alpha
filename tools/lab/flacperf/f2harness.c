/* flacperf harness for the Player 2 core (fw2/cores/flac2_core.h): decodes a slice of real music (mkblob2.py blob) with f2_dec_step and feeds the same ring-window sink model as harness.c
 * (int32 pairs -> to16 -> packed word stored to the PSRAM ring window, one write-pointer publish per call). Variants by -D: F2_BYPASS16 (skip rounding/clamp when the stream is 16-bit),
 * F2_BUDGET (residuals per call), F2_OUTCAP (pairs per call). */
#include "hostio.h"
#include "flac2_core.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define R_AUDIO  0xF0000124u
#define RING_WIN ((volatile uint32_t *)0xF0010000u)
#ifndef F2_BUDGET
#define F2_BUDGET 1024u
#endif
#ifndef F2_OUTCAP
#define F2_OUTCAP 128u
#endif
static uint32_t wix;
static int32_t buf[8192], outp[2 * F2_OUTCAP];
static uint8_t data[400000];
static inline int16_t to16(int32_t v, uint32_t bps)
{
    if (bps > 16u) { const int sh = (int)(bps - 16u); v = (v + (1 << (sh - 1))) >> sh; }
    else if (bps < 16u) v <<= (int)(16u - bps);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}
__attribute__((noinline)) void sink(const int32_t *src, uint32_t pairs, uint32_t bps)
{
    volatile uint32_t *win = RING_WIN; uint32_t w = wix;
    for (uint32_t i = 0; i < pairs; i++) {
#ifdef F2_BYPASS16
        int32_t l = bps == 16u ? src[2 * i] : to16(src[2 * i], bps), r = bps == 16u ? src[2 * i + 1] : to16(src[2 * i + 1], bps);
#else
        int32_t l = to16(src[2 * i], bps), r = to16(src[2 * i + 1], bps);
#endif
        win[w] = ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
        if (++w == 32768u) w = 0u;
    }
    wix = w; REG(R_AUDIO) = w;
}
int main(void)
{
    uint32_t hdr[6]; hread(0, hdr, 24);
    uint32_t n = hfilesize() - 24u - 42u - 64u;                      /* frames only: skip the minimal fLaC + STREAMINFO header and the 64 pad bytes */
    uint8_t sib[34]; hread(24u + 8u, sib, 34);
    hread(24u + 42u, data, n);
    f2_info_t si; if (f2_streaminfo(sib, &si) != F2_OK) { hputs("SI FAIL\n"); return 1; }
    static f2_dec_t d; f2_dec_init(&d, &si, buf, 8192u, F2_FLAG_CRC16);
    uint32_t off = 0, total = 0;
    while (off < n) {
        uint32_t used = 0, np = 0;
        int st = f2_dec_step(&d, data + off, n - off, outp, F2_OUTCAP, F2_BUDGET, &used, &np);
        off += used;
        if (np) { sink(outp, np, d.fh.bps); total += np; }
        if (st == F2_ERR) { hputs("ERR "); hputu(d.err); hputs(" at "); hputu(off); hnl(); return 1; }
        if (st == F2_NEED) break;
    }
    hputs("frames "); hputu(d.frames_ok); hputs(" samples/ch "); hputu(total); hnl();
    return 0;
}
