/* Player 2 Halcyon EQ (the engine is tau_halcyon.sv, MMIO 0x178-0x180): the selector (OFF, the eight built-in presets, the user presets of tau-assets.bin's PRST section, CUSTOM = the page's own six
 * controls), applied inside a gain dip so the change of filter and preamp is not a step in the listener's ears. The portable parts are fw/halcyon_*.h (loader, tables, curve, user-preset reader, all
 * host-tested in the old tree); the selection rules are cores/halcyon_sel_core.h. All of this is cold code and cold data (the tables are about 24 KB). */
#ifndef P2_HALCYON_H
#define P2_HALCYON_H
#include <stdint.h>
extern uint8_t halcyon_sel;          /* 0 off, 1.. presets, 200 custom */
extern uint8_t halcyon_saved;        /* the value for persist id 16 (never custom) */
extern uint8_t halcyon_user_n;       /* user presets loaded from tau-assets.bin */
extern uint8_t halcyon_present;      /* the bitstream has the engine */
void        halcyon_init(void);                          /* boot probe */
const char *halcyon_name(void);                          /* what the selection is called ("OFF", a preset, a user preset, "CUSTOM", "NO UNIT") */
void        halcyon_cycle(void);                         /* Y: the next preset, after the last OFF */
void        halcyon_restore(uint32_t saved);             /* a saved or Core Settings value arrives */
/* The Halcyon page (ui/halcyon_view.c): the six controls (warmth, bass, vocal, punch, sibilance, air; sibilance 0..5, the rest -5..+5) apply LIVE, a preset step is a recall inside a gain dip. */
#define HALCYON_NGRID 40                                    /* points of the response curve, 20 Hz to 20 kHz on a log axis */
extern uint8_t halcyon_cmp;                                 /* 1 = the engine is out of the path (the A/B compare) */
int32_t     halcyon_ctl(uint32_t i);
void        halcyon_ctl_set(uint32_t i, int32_t v);         /* clamped to the control's range, applied live, selection becomes CUSTOM */
void        halcyon_step(int dir);                          /* L1/R1: the next / previous preset (never OFF) */
void        halcyon_compare(void);                          /* X: engine out of the path, or back */
void        halcyon_reset(void);                            /* Y: all six controls to zero (flat) */
void        halcyon_page_open(void);                        /* takes the controls of the selected preset */
int32_t     halcyon_curve_get(int16_t out[HALCYON_NGRID]);  /* the response of the six controls in 1/64 dB; returns the preamp's give-back in 1/8 dB */
void        halcyon_prst_load(const uint8_t *sec, uint32_t n, int staged, int (*prove)(void));      /* the PRST section (already found and CRC-checked by the container reader) */
#endif
