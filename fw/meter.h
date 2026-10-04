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
 * unrelated to this file -- that one is the config contract; this one is the draw contract.
 *
 * FRAMEWORK RULE (mandatory for every meter, every mode): a meter is never called directly; the host calls helios_meter() (fw/player.c), which applies the CPU
 * budget of fw/meter_policy.h (duty cap from the measured cost of the last draw, audio-FIFO yield, headroom-aware) before the tick runs. A tick may therefore
 * be skipped: it must keep all its state in its own statics (not assume one call per frame) and take elapsed time from in->dt_ms, which carries the time
 * of any skipped calls (clamped to MP_MAX_DT_MS). A forced repaint (in->force) is never skipped. */
#ifndef TAU_METER_H
#define TAU_METER_H
#include <stdint.h>

/* The technical readout a meter may show about the stream and the machine (the MASTER VU info overlay). The HOST measures it (once per second, only while the
 * meter asks for it: it reads decoder counters and the SDRAM busy counter, which a meter must never touch) and the meter only draws it. 0xFF in a percent = not
 * available on this build (no decode profiler, no SDRAM busy counter). */
typedef struct {
    uint32_t        sec;         /* the second these figures belong to: changes once a second, the meter's refresh latch */
    uint32_t        hz;          /* sample rate of the track                                         */
    uint16_t        kbps;        /* bitrate, 0 = unknown                                             */
    uint8_t         flac;        /* 1 = FLAC (bps below valid), 0 = MP3                              */
    uint8_t         bps;         /* FLAC bit depth                                                   */
    const char     *encoder;     /* encoder tag, "" if none (host string, valid for the call)        */
    uint8_t         cpu_pct;     /* whole-system non-idle time                                       */
    uint8_t         dec_pct;     /* audio decoder stages only, percent of realtime                   */
    uint8_t         sdr_pct;     /* SDRAM port busy                                                  */
} mtr_info_t;

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
    uint8_t         paused;      /* playback is paused: a meter holds or dims, it never reads the player global */
    const uint8_t  *env;         /* UI_WAVE_N entries: rolling amplitude envelope, 0..h, oldest first (the host shifts it once per tick) */
    uint8_t         energy;      /* mean of the spectrum bands, 0..255 (mtr_energy), computed once per frame by the host */
    uint8_t         silent;      /* 1 = digital silence: zero peak and every band zero (mtr_silent) */
    uint8_t         stats_ok;    /* 1 = the hardware audio-statistics block is built in and has produced a window (the six fields below are valid) */
    uint16_t        rms_l, rms_r;   /* sqrt of the mean square over the last window, 0..32768 (the same scale as a sample)   */
    int16_t         corr_q8;     /* stereo correlation, -256 (opposite) .. 0 (unrelated) .. +256 (identical channels)        */
    uint16_t        crest_q8;    /* peak / rms of the louder channel in Q8 (256 = square wave, ~362 = sine)                  */
    uint16_t        centroid_q8; /* spectral centre of mass in band index Q8 (0 .. 15*256); 0 in silence; always valid       */
    uint16_t        clip_l, clip_r;   /* samples at full scale since the firmware last cleared the counters (saturating)    */
    const uint8_t  *env_pk;      /* the matching peak-hold marker per entry, sinking toward env by the host           */
    const mtr_info_t *info;      /* stream and machine readout for an info overlay; refreshed by the host only while the meter shows one (its sec is 0 until then) */
} mtr_in_t;

/* ---- Mutable meter state lives in PSRAM (docs/features/meters/METER_MODULE_SPEC.md, "Mutable meter state lives in PSRAM", D-M14) ----
 * A meter's long-lived mutable buffers (history rings, accumulators -- anything beyond a few hundred bytes that is not touched per pixel) are
 * declared MTR_PSRAM, which the linker places in the .psram_state region (fw/link.ld, 0xA4009000..0xA400FFFF). That keeps scarce on-chip RAM for
 * the audio path. The rules that make this safe are in the spec; the short version a module must follow:
 *   - call mtr_psram_ready() before the FIRST store; if it returns 0 the module draws nothing (or a flat background) and returns;
 *   - uncached window = ~32 cycles per read, ~26 per write [HW, KB-040]: never scatter-read it per pixel, never shift a buffer through it.
 *     Use a ring (an index moves, not the bytes) and unwrap what a frame needs into a small hot scratch with WORD reads;
 *   - word-aligned 32-bit accesses only (declare the storage as uint32_t); a sub-word store is a read-modify-write on the word.
 * Host harnesses (golden-frame tests) do not define MTR_PSRAM_FW: there MTR_PSRAM is empty and the readiness check is a constant 1, so the very same
 * module source compiles and is compared against its JS twin with plain arrays. */
#ifdef MTR_PSRAM_FW
#define MTR_PSRAM __attribute__((section(".psram_state")))
extern uint32_t __psram_state_start[], __psram_state_end[];
#define MTR_PS_ID      0x80000088u     /* PSRAM expansion ID register (same proof art.inc's art_psram_prove() uses) */
#define MTR_PS_CFG     0x800000A0u     /* bit 16 = the CPU window is built into this bitstream */
#define MTR_PS_ID_VAL  0x50535231u
static uint8_t mtr_ps_state;           /* 0 untested, 1 proven, 2 failed -- shared by every meter, proven once per boot */

/* Runs once, before any meter has stored anything into .psram_state (every user calls mtr_psram_ready() first), so overwriting the first and
 * last word of the region with test patterns destroys nothing: each meter initialises its state after this returns. */
COLD_FN3 static int mtr_psram_prove(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG(MTR_PS_ID) != MTR_PS_ID_VAL) return 0;
    if (!((REG(MTR_PS_CFG) >> 16) & 1u)) return 0;
    volatile uint32_t *w0 = (volatile uint32_t *)__psram_state_start;
    volatile uint32_t *w1 = (__psram_state_end > __psram_state_start) ? (volatile uint32_t *)__psram_state_end - 1 : w0;
    for (uint32_t k = 0; k < 2u; k++) {
        *w0 = pat[k]; *w1 = ~pat[k];
        if (w1 == w0) { if (*w0 != ~pat[k]) return 0; }
        else if (*w0 != pat[k] || *w1 != ~pat[k]) return 0;
    }
    return 1;
}
COLD_FN3 static int mtr_psram_ready_fn(void)
{
    if (!mtr_ps_state) mtr_ps_state = mtr_psram_prove() ? 1u : 2u;
    return mtr_ps_state == 1u;
}
#define mtr_psram_ready() mtr_psram_ready_fn()
#else
#define MTR_PSRAM
#define mtr_psram_ready() 1
#endif

#endif

/* fig_rect/fig_bar: see fw/player.c. Without the firmware's clipping wrappers they are the plain primitives. */
#ifndef fig_rect
#define fig_rect fb_rect
#define fig_bar  fb_bar
#endif
