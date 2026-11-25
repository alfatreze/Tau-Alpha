/* WP-01 rv32 harness: decodes the first FRAMES frames of a file with the PRISTINE minimp3 (float internals, int16 output) built for rv32im,
 * under tools/mp3_v2_measure/rvprof.c (read-only use). Own code; contains no decoder, only the call loop, the stack watermark and the DMA read.
 * Prints: frames=<n> samples=<pairs> hz=<hz> ch=<ch> stackpeak=<bytes below main's frame that were touched>.
 * Build flags are set by tools/mp3_v2/rv32_size.py (MINIMP3_ONLY_MP3, MINIMP3_NO_SIMD, -march=rv32im -mabi=ilp32). */
#include <stdint.h>
#include <stddef.h>
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#define IO(a) (*(volatile uint32_t *)(a))
#define FILEBUF ((unsigned char *)0x00400000)
#ifndef FRAMES
#define FRAMES 200
#endif
int memcmp(const void *a, const void *b, size_t n)   /* minimp3 uses memcmp; the project's libc_min.c has none */
{
    const unsigned char *x = a, *y = b;
    while (n--) { if (*x != *y) return *x - *y; x++; y++; }
    return 0;
}
static mp3dec_t dec;
static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
static void pr(const char *s) { while (*s) IO(0xF0000000) = (uint8_t)*s++; }
static void pn(unsigned v) { char b[12]; int i = 11; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + v % 10; v /= 10; } pr(b + i); }
#define PAINT 0x10000u
int main(void)
{
    volatile uint32_t here; uint32_t top = (uint32_t)&here & ~3u;
    volatile uint32_t *lo = (volatile uint32_t *)(top - PAINT), *hi = (volatile uint32_t *)(top - 256);
    for (volatile uint32_t *q = lo; q < hi; q++) *q = 0xA5A5A5A5u;
    unsigned size = IO(0xF000001C), done = 0;
    while (done < size) {
        IO(0xF0000010) = (uint32_t)(FILEBUF + done); IO(0xF0000014) = done;
        IO(0xF0000018) = size - done > 65536 ? 65536 : size - done;
        unsigned k = IO(0xF0000018); if (!k) break; done += k;
    }
    const unsigned char *p = FILEBUF; int left = (int)size;
    if (left >= 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3') { int skip = 10 + ((p[6] << 21) | (p[7] << 14) | (p[8] << 7) | p[9]); p += skip; left -= skip; }
    mp3dec_init(&dec);
    mp3dec_frame_info_t fi = {0};
    unsigned frames = 0, pairs = 0;
    while (left > 0 && frames < FRAMES) {
        int s = mp3dec_decode_frame(&dec, p, left, pcm, &fi);
        if (!fi.frame_bytes) break;
        p += fi.frame_bytes; left -= fi.frame_bytes;
        if (s > 0) { frames++; pairs += (unsigned)s; }
    }
    volatile uint32_t *q = lo; while (q < hi && *q == 0xA5A5A5A5u) q++;
    unsigned peak = (unsigned)((uintptr_t)hi - (uintptr_t)q) + 256u;   /* bytes used below main's own frame (plus the 256 B guard) */
    pr("frames="); pn(frames); pr(" samples="); pn(pairs); pr(" hz="); pn((unsigned)fi.hz); pr(" ch="); pn((unsigned)fi.channels); pr(" stackpeak="); pn(peak); pr("\n");
    return 0;
}
