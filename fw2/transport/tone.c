#include "tone.h"
#include "../aout/aout.h"
#include "../platform/mmio.h"
#include "../generated/sine.h"

#ifndef TONE_BUG
#define TONE_BUG 0
#endif
#define BLOCK 512u
#define TONE_HZ 1000u
#define PHASE_INC(rate) ((uint32_t)(((uint64_t)TONE_HZ << 32) / (rate)))     /* constants only: folded by the compiler */

tone_t tone;
tl_t   tone_tl;
gov_t  tone_gov;
static int cymo_present_;
static int16_t buf[BLOCK];

static void gen_block(uint32_t n)
{
    const uint32_t inc = tone.rate == 44100u ? PHASE_INC(44100u) : PHASE_INC(48000u);
    uint32_t ph = tone.phase;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t idx = ph >> 24, frac = (ph >> 16) & 0xFFu;
        int32_t a = p2_sine[idx], b = p2_sine[idx + 1u];
        buf[i] = (int16_t)((a + (((b - a) * (int32_t)frac) >> 8)) >> 4);      /* -24 dBFS: safe at any headphone volume */
        ph += inc;
    }
    tone.phase = ph;
}

void tone_init(int cymo_present)
{
    cymo_present_ = cymo_present;
    tone.rate = 44100u; tone.seg_pairs = 12u * 44100u;
    aout_set_rate(tone.rate, cymo_present_);
    aout_gain_unity();
    aout_snap_t s = aout_snap();
    tl_init(&tone_tl, s.cnt);
    gov_init(&tone_gov, AOUT_USABLE);
}

static void restart_segments(void)
{
    aout_flush();
    aout_snap_t s = aout_snap();                 /* the FIFO is empty and the ring dropped: heard == cnt */
#if TONE_BUG != 5
    tl_flush(&tone_tl, s.cnt);                   /* the timeline restarts at the push count the flush left */
#endif
    tone.seg_open = 0; tone.seg_left = 0;
}

void tone_key(uint32_t edge)
{
    if (edge & KEY_A) { restart_segments(); tone.playing = 1; tone.held = 0; aout_hold(0); }
    if (edge & KEY_B) { tone.held = !tone.held; aout_hold(tone.held); }
    if (edge & KEY_X) { if (tone.playing) restart_segments(); }
    if (edge & KEY_Y) {
        tone.rate = tone.rate == 44100u ? 48000u : 44100u; tone.seg_pairs = 12u * tone.rate;
        restart_segments();
        aout_set_rate(tone.rate, cymo_present_);
    }
}

void tone_pump(void)
{
    if (!tone.playing) return;
    for (uint32_t k = 0; k < 4u; k++) {
        aout_snap_t s = aout_snap();
        uint32_t fill = aout_fill(&s);
        tone.gov_mode = (uint32_t)gov_update(&tone_gov, fill);
        if (!gov_decode_wanted(&tone_gov, fill)) break;
        if (!tone.seg_open) {
            tl_seg_t *g = tl_open(&tone_tl, ++tone.gen, 0u, tone.seg_pairs, 0u, 1u, 1u, 0u);
            if (!g) break;                                                  /* the queue is full: wait for the heard side to pop */
            tone.seg_open = 1; tone.seg_left = tone.seg_pairs; tone.src_pos = 0;
#if TONE_BUG == 8
            tone.seg_left--;                                                 /* every segment is one pair short */
#endif
        }
        uint32_t n = tone.seg_left < BLOCK ? tone.seg_left : BLOCK;
        if (aout_room() < n) { tone.stalls++; break; }
        gen_block(n);
        uint32_t acc = aout_put(buf, n, 0);
#ifdef P2_HOST_LABEL
        P2_HOST_LABEL(tone.gen, tone.src_pos, acc);
#endif
        tl_put(&tone_tl, acc);
        tone.src_pos += acc; tone.seg_left -= acc; tone.blocks++;
        if (!tone.seg_left) {
#if TONE_BUG == 6
            tone.phase = 0;                                                      /* the phase restarts at every segment: a click at every boundary */
#endif
            if (tl_close(&tone_tl)) tone.acct_bad++;
            tone.seg_open = 0;
        }
    }
}

void tone_heard(void)
{
    aout_snap_t s = aout_snap();
    uint32_t h = aout_heard(&s);
#if TONE_BUG != 7
    tl_advance(&tone_tl, h);
#endif
    const tl_seg_t *g = tl_current(&tone_tl, h);
    tone.cur_valid = g != 0;
    if (g) { tone.cur_gen = g->gen; tone.cur_pos = tl_pos(g, h); }
}
