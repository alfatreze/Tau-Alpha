#include "aout.h"
#include "../platform/mmio.h"

#define RING_BASE_W 0x1C0000u            /* PSRAM word address of ring[0]: byte +0x700000 of the window (spec section 12.1) */
#ifndef P2_RING_WINDOW
#define P2_RING_WINDOW ((volatile uint32_t *)(uintptr_t)(0xA4000000u + 4u * RING_BASE_W))
#endif
#ifndef AOUT_BUG
#define AOUT_BUG 0
#endif

aout_stats_t aout_stats;
static uint32_t w;                       /* the write index: the one writer of this quantity */
static uint32_t ctrl_hold;
static uint32_t cymo_on = 0xFFu, rate_hz;
static int dry_prev, under_on, startup = 1;
static uint32_t under_t0;

int aout_init(void)
{
    volatile uint32_t *win = P2_RING_WINDOW;
    if (!(REG_RD(R_RING_CTRL) >> 31)) return 1;
    win[0] = 0x5A5AC3C3u; win[AOUT_RING_PAIRS - 1u] = 0xA5A53C3Cu; win[1] = 0x12345678u;       /* the window must answer before the hardware is trusted with it */
    if (win[0] != 0x5A5AC3C3u || win[AOUT_RING_PAIRS - 1u] != 0xA5A53C3Cu || win[1] != 0x12345678u) return 2;
    REG_WR(R_RING_CTRL, 0u);                                    /* disabled: the reader follows the write index */
    REG_WR(R_RING_BASE, RING_BASE_W); REG_WR(R_RING_SIZE, AOUT_RING_PAIRS); REG_WR(R_RING_WPTR, 0u);
    for (uint32_t n = 0; n < 1000u && REG_RD(R_RING_RIDX) != 0u; n++) { }
    w = 0; ctrl_hold = 0;
    aout_stats.ring_min = 0xFFFFFFFFu; aout_stats.ring_max = 0;
    REG_WR(R_RING_CTRL, 1u);                                    /* enable, hold clear */
    return 0;
}

void aout_set_rate(uint32_t hz, int cymo_present)
{
    rate_hz = hz;
    REG_WR(R_PCM_RATE, hz == 44100u ? RATE_INC(44100u) : RATE_INC(48000u));
    if (cymo_present) {
        uint32_t want = (hz == 44100u) ? 4u : 0u;               /* the resampler is live only for 44.1 kHz at 1.00x */
        if (want != cymo_on) { cymo_on = want; REG_WR(R_CYMO_CTRL, want); }
    }
}

void aout_gain_unity(void)
{
    REG_WR(R_GAIN_TARGET, 32768u);
    REG_WR(R_GAIN_CTRL, 1u | (3u << 4) | 2u);                   /* enabled, fade length 2048 samples, SNAP: gain = target now */
}

void aout_mute_wait(void)
{
    REG_WR(R_GAIN_TARGET, 0u);
    uint32_t t0 = cycles();
    while ((uint32_t)(cycles() - t0) < CLK_HZ / 125u) { }          /* 8 ms: the ramp from full scale takes 5 */
}
void aout_fade_in(void)
{
    REG_WR(R_GAIN_TARGET, 32768u);
    REG_WR(R_GAIN_CTRL, 1u | (3u << 4) | 4u);                      /* enabled, fade length kept, FADE_NOW: ramp to the target */
}

aout_snap_t aout_snap(void)
{
    aout_snap_t s;
    uint32_t c1, c2;
    do {                                                        /* the reader pushes one pair per request: retry until the count did not move */
        c1 = REG_RD(R_RING_CNT);
        s.fifo = REG_RD(R_PCM_ST) & 0xFFFu;
        s.ridx = REG_RD(R_RING_RIDX);
        c2 = REG_RD(R_RING_CNT);
    } while (c1 != c2);
    s.cnt = c1; s.wptr = w;
    return s;
}

uint32_t aout_room(void)
{
    uint32_t ridx = REG_RD(R_RING_RIDX);
    uint32_t fill = w >= ridx ? w - ridx : w + AOUT_RING_PAIRS - ridx;
#if AOUT_BUG == 3
    return fill + 1u >= AOUT_RING_PAIRS ? 0u : AOUT_RING_PAIRS - 1u - fill;          /* no margin: the unread tail the meters and the FIFO need can be overwritten */
#else
    return fill + AOUT_RING_MARGIN + 1u >= AOUT_RING_PAIRS ? 0u : AOUT_RING_PAIRS - 1u - AOUT_RING_MARGIN - fill;
#endif
}

uint32_t aout_put(const int16_t *pcm, uint32_t pairs, int stereo)
{
    const uint32_t t0 = cycles();
    volatile uint32_t *win = P2_RING_WINDOW;
    uint32_t room = aout_room(), n = pairs < room ? pairs : room;
    for (uint32_t i = 0; i < n; i++) {
        int32_t l = pcm[stereo ? 2u * i : i], r = stereo ? pcm[2u * i + 1u] : l;
        win[w] = ((uint32_t)(uint16_t)(int16_t)r << 16) | (uint32_t)(uint16_t)(int16_t)l;
#if AOUT_BUG == 2
        w = (w + 1u) % (AOUT_RING_PAIRS + 1u);                  /* the index wraps one slot late */
#else
        if (++w == AOUT_RING_PAIRS) w = 0u;
#endif
    }
#if AOUT_BUG == 4
    /* the index is never published: the reader never sees the pairs */
#else
    REG_WR(R_RING_WPTR, w);                                     /* once per block, after the data */
#endif
    aout_stats.put_blocks++; aout_stats.put_pairs += n;
    if (n < pairs) aout_stats.put_stalls++;
    uint32_t d = cycles() - t0; if (d > aout_stats.put_max_cyc) aout_stats.put_max_cyc = d;
#ifdef P2_HOST_ON_PUT
    P2_HOST_ON_PUT(pcm, n, stereo);
#endif
    return n;
}

void aout_flush(void)
{
    REG_WR(R_PCM_ST, 1u);                                       /* the hardware drops the unread pairs and the FIFO (one write) */
    aout_stats.flushes++;
    startup = 1;                                                /* the FIFO is empty until the reader has refilled it: not an underrun, and not the producer being late */
    under_on = 0; dry_prev = 0;
}

void aout_hold(int on)
{
    ctrl_hold = on ? 1u : 0u;
    REG_WR(R_RING_CTRL, 1u | (ctrl_hold << 1));
}

void aout_poll(int playing)
{
    aout_snap_t s = aout_snap();
    uint32_t fill = aout_fill(&s);
    if (fill < aout_stats.ring_min) aout_stats.ring_min = fill;
    if (fill > aout_stats.ring_max) aout_stats.ring_max = fill;
    if (s.fifo) startup = 0;
    /* The reader fell behind: the FIFO stayed empty for more than 2 ms although the ring held pairs (a refill takes under 1 ms, so a shorter gap is the normal moment after a flush or a resume). */
    int under = (s.fifo == 0 && fill > 0 && !ctrl_hold);
    if (!under) under_on = 0;
    else if (!under_on) { under_on = 1; under_t0 = cycles(); }
    else if (under_on == 1 && (uint32_t)(cycles() - under_t0) > CLK_HZ / 500u) { under_on = 2; aout_stats.under_ring++; }
    /* The producer fell behind: both empty while playing, after the stream had started. */
    int dry = (playing && !ctrl_hold && !startup && s.fifo == 0 && fill == 0);
    if (dry && !dry_prev) aout_stats.dry++;
    dry_prev = dry;
}
