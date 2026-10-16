/* dr_flac spike harness: whole-file decoder fed from a minimal header + frame slice, 16-bit out in 64-pair blocks into the same ring-window sink model as harness.c.
 * Blob: u32 ch,bps,maxb,nframes,hdrlen,pairs, then file bytes (fLaC + STREAMINFO + frames). */
#include "hostio.h"
#include <stddef.h>
#define DR_FLAC_IMPLEMENTATION
#define DR_FLAC_NO_STDIO
#define DR_FLAC_NO_OGG
#define DR_FLAC_NO_SIMD
#define DR_FLAC_NO_WCHAR
#ifdef NOCRC
#define DR_FLAC_NO_CRC
#endif
#define DRFLAC_NO_THREAD_SAFETY
extern char _heap_start;
static uint32_t heap_cur, heap_peak, nalloc;
static void *hmalloc(size_t n, void *u) { (void)u; if (!heap_cur) heap_cur = ((uint32_t)&_heap_start + 15u) & ~15u; uint32_t p = heap_cur; heap_cur += (uint32_t)((n + 15u) & ~15u); if (heap_cur > heap_peak) heap_peak = heap_cur; nalloc++; return (void *)p; }
static void *hrealloc(void *p, size_t n, void *u) { void *q = hmalloc(n, u); if (p) { for (size_t i = 0; i < n; i++) ((char *)q)[i] = ((char *)p)[i]; } return q; }  /* spike only: over-copies, never frees */
static void hfree(void *p, void *u) { (void)p; (void)u; }
#define DRFLAC_MALLOC(sz) hmalloc((sz), 0)
#define DRFLAC_REALLOC(p, sz) hrealloc((p), (sz), 0)
#define DRFLAC_FREE(p) hfree((p), 0)
#define DRFLAC_ASSERT(e) ((void)0)
#define DRFLAC_COPY_MEMORY(d, s, n) __builtin_memcpy((d), (s), (n))
#define DRFLAC_ZERO_MEMORY(p, n) __builtin_memset((p), 0, (n))
#define DRFLAC_MOVE_MEMORY(d, s, n) __builtin_memmove((d), (s), (n))
#include "dr_flac.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define R_AUDIO  0xF0000124u
#define RING_WIN ((volatile uint32_t *)0xF0010000u)
static uint32_t wix;
static void sink(const int16_t *src, uint32_t frames)
{
    volatile uint32_t *win = RING_WIN; uint32_t w = wix;
    for (uint32_t i = 0; i < frames; i++) {
        int32_t l = src[i * 2], r = src[i * 2 + 1];
        win[w] = ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
        if (++w == 32768u) w = 0u;
    }
    wix = w; REG(R_AUDIO) = w;
}
static uint32_t cur;
static size_t on_read(void *u, void *dst, size_t n) { (void)u; uint32_t got = hread(24u + cur, dst, (uint32_t)n); cur += got; return got; }
static drflac_bool32 on_seek(void *u, int off, drflac_seek_origin o) { (void)u; if (o == DRFLAC_SEEK_CUR) cur += (uint32_t)off; else cur = (uint32_t)off; return 1; }
static int16_t pcm[128];
int main(void)
{
    uint32_t hdr[6]; hread(0, hdr, 24);
    drflac *f = drflac_open(on_read, on_seek, 0, 0, 0);
    if (!f) { hputs("OPEN FAIL\n"); return 1; }
    uint32_t total = 0, want = hdr[5];
    while (total < want) {
        uint32_t n = want - total < 64u ? want - total : 64u;
        drflac_uint64 got = drflac_read_pcm_frames_s16(f, n, pcm);
        if (!got) break;
        sink(pcm, (uint32_t)got); total += (uint32_t)got;
    }
    hputs("frames "); hputu(hdr[3]); hputs(" samples/ch "); hputu(total); hputs(" heap "); hputu(heap_peak - ((uint32_t)&_heap_start)); hputs(" allocs "); hputu(nalloc); hnl();
    return total == want ? 0 : 2;
}
