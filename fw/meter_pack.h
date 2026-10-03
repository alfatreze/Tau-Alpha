/* Meter pack ABI (docs/features/meters/METER_PACKS.md): what a loadable meter module and the firmware agree on.
 *
 * A pack is one meter's drawing code, compiled as a freestanding blob that is linked at a fixed address (its slot in the PSRAM code window), carries
 * its own copy of the few compiler helper routines it needs, and reaches the firmware ONLY through the table below. It never names a firmware symbol,
 * so it does not depend on where the ROM put anything: a pack built against ABI N works with every firmware that speaks ABI N, however the ROM
 * changes around it. Bump MTR_PACK_ABI whenever mtr_in_t or mtr_host_api_t changes (sim/test_meter_pack.py fails until the fingerprint is updated).
 *
 * The entry point is  uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api): the same contract as a built-in meter's tick function;
 * the return value is the number of draw commands issued (0 = nothing drawn). The firmware owns every input, the settings values and the draw engine;
 * a pack owns only its own state (in its .data/.bss inside the slot) and the drawing. */
#ifndef TAU_METER_PACK_H
#define TAU_METER_PACK_H
#include <stdint.h>
#include "meter.h"

#define MTR_PACK_ABI 1u

typedef struct {
    uint32_t         abi;                                                     /* MTR_PACK_ABI                                                   */
    void           (*fb_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);   /* the draw engine's rectangle fill     */
    uint32_t       (*cycles)(void);                                           /* free-running CPU cycle counter                                 */
    int            (*psram_ready)(void);                                      /* the PSRAM window has been proven (the pack's own state lives there) */
    const uint16_t  *accent;                                                  /* the current accent colour (RGB565)                             */
    const uint16_t  *role;                                                    /* theme roles, TR_COUNT entries (fw/theme.h)                     */
    uint8_t         *force;                                                   /* the "repaint everything" request flag; the pack clears it once it has drawn */
    const uint16_t  *params;                                                  /* this meter's current setting values (the order of its manifest) */
} mtr_host_api_t;

typedef uint32_t (*mtr_pack_entry_fn)(const mtr_in_t *in, const mtr_host_api_t *api);
#endif
