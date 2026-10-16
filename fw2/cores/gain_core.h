/* fw2/cores/gain_core.h -- Player 2 gain target (spec 7.2): the hardware gain stage's Q15 target is volume (a dB taper) x ReplayGain, capped. Pure arithmetic over the proven
 * fw/replaygain.h and fw/pcm_push.h tables, fed from the library details record (flags bits 4..7 = track gain, album gain, track peak, album peak present). Host-tested
 * (sim/test_p2_gain.py). The per-track part (factor, cap) is computed once when the track is opened and applied when it is HEARD; the volume part changes with the keys. */
#ifndef GAIN_CORE_H
#define GAIN_CORE_H
#include <stdint.h>
#include "../../fw/replaygain.h"
#include "../../fw/pcm_push.h"
#ifndef BUG
#define BUG 0
#endif

typedef struct { uint32_t factor, cap; } gain_trk_t;      /* a track's ReplayGain factor (Q15, 32768 = 0 dB) and its largest allowed total gain (Q15) */

static inline gain_trk_t gain_track(uint32_t mode, uint32_t det_flags, int32_t tg_cdb, int32_t ag_cdb, uint32_t tp_q15, uint32_t ap_q15, uint32_t boost_ok)
{
    rg_t r; rg_clear(&r);
#if BUG == 1
    r.have = (uint8_t)(det_flags >> 3 & 15u);                       /* the flag bits shifted by one */
#else
    r.have = (uint8_t)((det_flags >> 4) & 15u);
#endif
    r.track_cdb = tg_cdb; r.album_cdb = ag_cdb; r.track_peak = tp_q15; r.album_peak = ap_q15;
    gain_trk_t g;
    g.factor = rg_pick_factor(mode, &r);
#if BUG == 2
    g.cap = RG_BOOST_MAX;                                           /* the peak cap is ignored */
#else
    g.cap = rg_cap_q15(rg_pick_peak(mode, &r), boost_ok);
#endif
    return g;
}
static inline uint32_t gain_target(uint32_t volume, const gain_trk_t *g)
{
#if BUG == 3
    return (uint32_t)rg_target(pcm_vol_target(volume), g->factor, 0xFFFFFFFFu);
#else
    return (uint32_t)rg_target(pcm_vol_target(volume), g->factor, g->cap);
#endif
}
#endif
