/* Meter draw contract (docs/features/meters/METER_MODULE_SPEC.md section 3): the input struct every
 * meter module's tick function reads. No dependency on player.c internals -- a module includes this
 * and nothing else to know its input shape. The host (today: a handful of call sites in player.c/
 * fullscreen.inc/settingsui.inc; a real fw/meter_host.inc that owns all of this is a later step,
 * docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md section 5 item 4) builds one of these per
 * display frame and passes it in; a module never reaches past it for its inputs.
 *
 * This header intentionally does NOT yet include mtr_desc_t, the mtr_bar/mtr_rect/... primitive
 * wrappers, or an open/close lifecycle (section 3's fuller contract) -- this is the first, narrowest
 * step (Helios review item 1): get the input shape real and used by the meters that already have
 * generated config, without also inventing the descriptor table, cost counting or the host layer in
 * the same pass. fw/meter_module.h (mtr_data_t/mtr_param_t, the PARAMETER half) already exists and is
 * unrelated to this file -- that one is the config contract; this one is the draw contract. */
#ifndef TAU_METER_H
#define TAU_METER_H
#include <stdint.h>

typedef struct {                 /* everything a meter may read, refreshed once per display frame */
    const uint8_t  *spec;        /* SPEC_BANDS bands, 0..255, after the shared ballistics (spec_lvl) */
    const int8_t   *wave;        /* WAVE_COLS signed samples (wav_v)                                */
    uint32_t        peak, peak_l, peak_r;   /* headroom-scaled, as MTR_HEADROOM already gives them   */
    uint32_t        frame;       /* display frame counter                                           */
    uint16_t        dt_ms;       /* ms since the last tick (best-effort: the fixed ~38 Hz UI cadence,
                                     not a measured delta -- nothing in this codebase measures one yet) */
    uint16_t        x, y, w, h;  /* the rect the meter owns; it never draws outside it               */
    uint16_t        bg;          /* flat background when the host says so                            */
    const uint16_t *role;        /* th_role[], TR_COUNT entries (fw/theme.h); never hard-code colours */
    uint8_t         force;       /* 1 = repaint the whole rect and drop every redraw cache            */
} mtr_in_t;

#endif
