/* Meter pack ABI for Player 2 (docs/features/meters/METER_PACKS.md): what a loadable meter and the firmware agree on.
 *
 * A pack is one meter's drawing code, compiled freestanding and linked at a fixed address (its slot in the PSRAM code window). It reaches the firmware ONLY through the table below and never names a firmware
 * symbol, so a pack built against ABI N works with every firmware that speaks ABI N however the ROM changes around it. The table is a thin layer over gfx_* (fw2/gfx/gfx.h); a function is added to it only
 * when a meter that is a pack needs it, and every change to the table or to mtr_in_t bumps MTR_PACK_ABI (sim/test_p2_meter_pack.py fails until the fingerprint is updated).
 * The entry point is  uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api): the same contract as a built-in meter's tick; the return value is the number of draw commands issued. */
#ifndef P2_METER_PACK_H
#define P2_METER_PACK_H
#include <stdint.h>
#include "meter.h"
#include "../cores/meter_pack_core.h"      /* MTR_PACK_ABI, the slot and scratch addresses */

typedef struct {
    uint32_t         abi;                                                   /* MTR_PACK_ABI */
    void           (*rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);   /* gfx_rect: the draw engine's rectangle fill (clipped around the fullscreen exclusions by the host) */
    const uint16_t  *role;                                                  /* the theme roles, TR_COUNT entries (ui/theme.h); the pack reads them through its own UI_* macros */
    const uint16_t  *params;                                                /* this meter's live setting values, in its manifest's order */
    uint32_t        *stats;                                                 /* MTR_PACK_STATS words the pack may publish: [0] draw commands of the last tick (the host fills the rest); may be NULL */
} mtr_host_api_t;

typedef uint32_t (*mtr_pack_entry_fn)(const mtr_in_t *in, const mtr_host_api_t *api);
#endif
