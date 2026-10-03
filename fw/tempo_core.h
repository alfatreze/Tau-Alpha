/* fw/tempo_core.h -- the tempo funnel (Cymo C7 T2, B-557): decoded PCM in, stretched PCM out. Portable and host-testable.
 *
 * Between the decoder and the audio FIFO: decoded frames are cut into 64-sample chunks, each chunk is written to a staging ring (in the firmware: the PSRAM window, so
 * the stretcher's small on-chip footprint is kept) and fed to the tempo core v2 (fw/wsola_core.h), and after every chunk the funnel steps the stretcher as long as
 * it can, handing each output pair to TEMPO_PUSH (in the firmware: cymo_push(), which blocks on a full FIFO and so paces everything at the native rate).
 * Stepping after every chunk keeps the core's ring inside the span a step reads (docs/features/CYMO_TEMPO_INTEGRATION.md section 4).
 *
 * The includer provides: TEMPO_PS_L and TEMPO_PS_R (volatile uint32_t pointers to the two planar staging rings, TEMPO_RING/2 words each), TEMPO_PUSH(l, r) (returns 0 to
 * abort, as cymo_push() does on a pending reload) and optionally TEMPO_METER(l, r, n) (n output pairs of one hop, for the meters).
 * Staging is word-wide (two samples per word): 32-bit accesses are what the PSRAM window is proven for. */
#ifndef TEMPO_CORE_H
#define TEMPO_CORE_H
#include "wsola_core.h"

#define TEMPO_RING  32768u            /* samples per channel in the staging ring (a power of two): 64 KB a channel */
#define TEMPO_CHUNK 64u

typedef struct {
    ws2_t    ws;
    uint8_t  on;                      /* started and usable */
    uint8_t  have_odd;                /* one decoded sample per channel is waiting for its partner in the word */
    int16_t  odd_l, odd_r;
    uint32_t written;                 /* samples written to the staging ring and fed to the core */
    int16_t  out_l[WS_MAX_HS], out_r[WS_MAX_HS];   /* the current hop */
} tempo_t;

static inline void tempo_start(tempo_t *t, uint32_t fs, uint8_t ch, uint32_t speed_q8)
{
    ws2_init(&t->ws, fs, ch, speed_q8);
    t->on = 1u; t->have_odd = 0u; t->written = 0u;
}

static inline void tempo_stop(tempo_t *t) { t->on = 0u; }

/* the reader the core calls: copy n samples of channel ch starting at absolute sample lo out of the staging ring */
static inline void tempo_rd(void *ctx, uint32_t ch, uint32_t lo, uint32_t n, int16_t *dst)
{
    (void)ctx;
    volatile uint32_t *ring = ch ? TEMPO_PS_R : TEMPO_PS_L;
    uint32_t i = lo;
    while (n) {
        const uint32_t idx = i & (TEMPO_RING - 1u);
        const uint32_t w = ring[idx >> 1];
        if (idx & 1u) { *dst++ = (int16_t)(w >> 16); i++; n--; }
        else {
            *dst++ = (int16_t)w; i++; n--;
            if (n) { *dst++ = (int16_t)(w >> 16); i++; n--; }
        }
    }
}

/* Feed m <= TEMPO_CHUNK samples per channel (r may be null for mono): complete pairs go to the ring and to the core, an odd last sample waits for the next call. */
static inline void tempo_feed(tempo_t *t, const int16_t *l, const int16_t *r, uint32_t m)
{
    int16_t tl[TEMPO_CHUNK + 1u], tr[TEMPO_CHUNK + 1u];
    uint32_t k = 0;
    if (t->have_odd) { tl[0] = t->odd_l; tr[0] = t->odd_r; k = 1; t->have_odd = 0u; }
    for (uint32_t i = 0; i < m; i++) { tl[k] = l[i]; tr[k] = r ? r[i] : (int16_t)0; k++; }
    if (k & 1u) { t->odd_l = tl[k - 1u]; t->odd_r = tr[k - 1u]; t->have_odd = 1u; k--; }
    const uint32_t base = t->written >> 1;           /* the word index counts on; every access is wrapped, because a chunk can straddle the end of the ring */
    for (uint32_t i = 0; i < k; i += 2u) {
        const uint32_t wi = (base + (i >> 1)) & (TEMPO_RING / 2u - 1u);
        TEMPO_PS_L[wi] = (uint32_t)(uint16_t)tl[i] | ((uint32_t)(uint16_t)tl[i + 1u] << 16);
        if (t->ws.core.ch == 2u) TEMPO_PS_R[wi] = (uint32_t)(uint16_t)tr[i] | ((uint32_t)(uint16_t)tr[i + 1u] << 16);
    }
    ws2_feed(&t->ws, tl, tr, k);
    t->written += k;
}

/* Step the stretcher while it can; push every output pair. Returns 0 if TEMPO_PUSH asked to abort. */
static inline int tempo_drain(tempo_t *t)
{
    while (ws2_ready(&t->ws)) {
        const uint32_t h = ws2_step(&t->ws, tempo_rd, 0, t->out_l, t->out_r);
        const int st = t->ws.core.ch == 2u;
        for (uint32_t i = 0; i < h; i++)
            if (!TEMPO_PUSH(t->out_l[i], st ? t->out_r[i] : t->out_l[i])) return 0;
#ifdef TEMPO_METER
        TEMPO_METER(t->out_l, st ? t->out_r : (const int16_t *)0, h);
#endif
    }
    return 1;
}

/* One decoded frame, interleaved L,R (or mono when stereo is 0), n samples in total as the MP3 decoder reports them. Returns 0 if it was aborted. */
static inline int tempo_frame(tempo_t *t, const int16_t *pcm, uint32_t n, int stereo)
{
    int16_t cl[TEMPO_CHUNK], cr[TEMPO_CHUNK];
    const uint32_t pairs = stereo ? n / 2u : n;
    for (uint32_t pos = 0; pos < pairs; pos += TEMPO_CHUNK) {
        const uint32_t m = pairs - pos < TEMPO_CHUNK ? pairs - pos : TEMPO_CHUNK;
        if (stereo) { for (uint32_t i = 0; i < m; i++) { cl[i] = pcm[2u * (pos + i)]; cr[i] = pcm[2u * (pos + i) + 1u]; } }
        else        { for (uint32_t i = 0; i < m; i++) cl[i] = pcm[pos + i]; }
        tempo_feed(t, cl, stereo ? cr : (const int16_t *)0, m);
        if (!tempo_drain(t)) return 0;
    }
    return 1;
}
#endif
