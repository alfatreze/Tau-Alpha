#!/usr/bin/env python3
"""Cymo C3 slice 2 host model: the hardware gain stage (docs/features/CYMO_GAIN_STAGE_DESIGN.md, B-615). Integer-exact reference for src/fpga/core/tau_gain_stage.sv.

Per output sample pair (one `tick` of pcm_fifo's fractional-rate strobe), in this order, which is exactly fw/pcm_push.h's pcm_gain_apply() moved to the FIFO output:
  1. ramp:  cur moves toward target by at most `ramp` (default 149; the target is capped at unity 32768: the stage only attenuates)
  2. volume: y = (x * cur + 16384) >> 15              (round to nearest; unity is exact)
  3. fade:   if fade_left != 0:  g = (fade_total - fade_left) >> shift ; y = (y * g + 128) >> 8
             fade_left decrements only when the tick delivered a real sample (`adv`: FIFO primed and not empty), so priming and underrun gaps do not eat the fade.
  A flush or FADE_NOW sets fade_left = fade_total = 256 << shift (shift 3 = 2048 samples, the firmware's FADE_SAMPLES; g reaches 255 on the last sample).
  Both channels are produced together and published in the same clock (no torn pair).
"""
UNITY = 32768
RAMP_DEFAULT = 149
SHIFT_DEFAULT = 3


def rnd(v, n):
    return (v + (1 << (n - 1))) >> n


class GainStage:
    def __init__(self, ramp=RAMP_DEFAULT, shift=SHIFT_DEFAULT):
        self.ramp = ramp
        self.shift = shift
        self.cur = UNITY
        self.target = UNITY
        self.fade_left = 0
        self.out = (0, 0)

    @property
    def fade_total(self):
        return 256 << self.shift

    def set_target(self, t):
        self.target = min(t, UNITY)

    def snap(self):
        self.cur = self.target

    def fade_now(self):                 # flush and the software FADE_NOW pulse both do this
        self.fade_left = self.fade_total

    def tick(self, xl, xr, adv):
        """One sample tick: xl/xr are the FIFO output registers AFTER this tick's update; returns the published pair."""
        if self.cur != self.target:
            d = self.target - self.cur
            d = max(-self.ramp, min(self.ramp, d))
            self.cur += d
        yl, yr = rnd(xl * self.cur, 15), rnd(xr * self.cur, 15)
        if self.fade_left:
            g = (self.fade_total - self.fade_left) >> self.shift
            yl, yr = rnd(yl * g, 8), rnd(yr * g, 8)
            if adv:
                self.fade_left -= 1
        self.out = (yl, yr)
        return self.out
