/* Helios, the Player 2 UI layer (docs/features/HELIOS_SPEC.md): dirty regions flushed beam-aware, one wrap rule. The logic is cores/helios_core.h (host-tested); this file is the hardware side:
 * the video line counter (R_SCAN, bit 9 = the bitstream has it) decides when a region's rows are safe to draw. Without the counter every region draws at once. */
#ifndef P2_HELIOS_H
#define P2_HELIOS_H
#include <stdint.h>
#include "../cores/helios_core.h"
void     helios_init(void);                                  /* once at boot, after platform_probe */
uint8_t  helios_region(hl_redraw_fn fn, uint32_t y0, uint32_t y1);   /* setup time; y1 = HL_WHOLE for a full repaint */
void     helios_dirty(uint8_t id);
void     helios_dirty_mask(uint32_t mask);
void     helios_flush(void);
void     helios_flush_now(void);                             /* draws every dirty region at once (inside a frame the view layer began) */                                 /* once per loop pass */
extern hl_t helios;                                          /* its counters (drawn / deferred) feed the Info page */
extern uint32_t helios_beam;                                 /* 1 when the bitstream has a beam counter */
#endif
