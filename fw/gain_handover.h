/* fw/gain_handover.h -- who applies the gain (B-653): the firmware (before the FIFO) or the hardware stage (after it), and how to hand it from one to the other without a click.
 *
 * The two owners sit at opposite ends of the 23-46 ms FIFO. While the stage owns the gain the firmware pushes UNSCALED samples; while the firmware owns it the stage is a unity pass-through.
 * Flip the owner and everything already in the FIFO is wrong for the new owner (full scale played with no gain, or scaled twice). At maximum volume the two are the same, which is why only the
 * very first toggle of a test at full volume ever popped. A seamless flip is therefore not possible without discarding that content, so the hand-over is done under a mute:
 *   1. mute AT THE FIFO OUTPUT with the stage, whichever side owns the gain now (a software owner first hands the stage a unity pass-through: snap to unity, no step);
 *   2. wait for the stage's ramp to reach silence (5 ms; 8 ms waited), the FIFO still holds 23+ ms so the decoder is not starved;
 *   3. flush the FIFO (about 46 ms of music is dropped, inaudible because it is already muted);
 *   4. flip the owner: to hardware, the target is the volume and the stage ramps up with its fade; to software, the stage is disabled and the software fade-in starts.
 * Pure logic over four callbacks so sim/test_gain_handover.py can run the real code and check the order of the writes. Cold code: a Diagnostics toggle, never in the playback path. */
#ifndef GAIN_HANDOVER_H
#define GAIN_HANDOVER_H
#include <stdint.h>
#include "pcm_push.h"

#define GH_UNITY 32768u
#define GH_CTRL_SNAP 2u        /* R_GAIN_CTRL bit 1: gain = target now */
#define GH_CTRL_FADE 4u        /* bit 2: start the discontinuity fade */
#define GH_MUTE_WAIT_MS 8u

typedef struct {
    void (*ctrl)(uint32_t v);          /* write R_GAIN_CTRL */
    void (*target)(uint32_t v);        /* write R_GAIN_TARGET */
    void (*wait_ms)(uint32_t ms);
    void (*flush)(void);               /* pcm_flush(): empties the FIFO and arms the software fade (which depends on who owns the gain NOW) */
} gh_ops_t;

static inline void gh_handover(const gh_ops_t *o, pcm_vol_t *v, uint32_t *fade_left, uint32_t fade_samples, uint32_t ctrl_base, uint32_t on)
{
    if ((uint32_t)v->hw == (on ? 1u : 0u)) return;
    if (!v->hw) { o->target(GH_UNITY); o->ctrl(ctrl_base | GH_CTRL_SNAP); }     /* the stage takes over an unscaled-by-hardware path at exactly unity */
    o->target(0u);
    o->wait_ms(GH_MUTE_WAIT_MS);
    o->flush();
    if (on) {
        v->hw = 1u; *fade_left = 0u;
        o->target((uint32_t)v->target);
        o->ctrl(ctrl_base | GH_CTRL_FADE);
    } else {
        v->hw = 0u; v->cur = v->target; *fade_left = fade_samples;
        o->ctrl(0u);
    }
}
#endif
