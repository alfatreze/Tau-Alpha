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

#define MTR_PACK_ABI 4u

/* Where things live. These addresses are part of the ABI: a pack is linked for them, so they are fixed constants, not firmware symbols.
 *   scratch: the on-chip meter scratch. The shipping 192 KB RAM layout leaves a heap gap whose top 1 KB (0x27400..0x27800, just under the ID3 landing zone) is
 *            reserved for it when the firmware is built with TAU_PACKS (fw/link.ld asserts that it fits). Whichever meter is active has its working state there.
 *   slots:   MTR_PACK_SLOTS pack slots of MTR_PACK_SLOT_SIZE bytes in the PSRAM code window, after the cold image (which is far below 0x40000). A slot is executed at
 *            its 0x24xx_xxxx alias and written through the 0xA4xx_xxxx data alias. A meter id has one fixed slot (mtr_pack_slot_of()).
 * Host tests override the bases (the simulator has no PSRAM window). */
#ifndef MTR_PACK_SCRATCH_ORG
#define MTR_PACK_SCRATCH_ORG  0x00027400u
#endif
#define MTR_PACK_SCRATCH_SIZE 0x400u
#ifndef MTR_PACK_SLOT_BASE
#define MTR_PACK_SLOT_BASE    0x24840000u          /* instruction alias; the data alias is the same offset from 0xA4800000 */
#endif
#define MTR_PACK_SLOT_SIZE    0x10000u
#define MTR_PACK_SLOTS        4u
#define MTR_PACK_METER_LAYERED_WAVE 16u            /* the VIZ_* id of the meter (fw/meter_gen_enum.h) */
#define MTR_PACK_METER_WINAMP_BARS  12u
#define MTR_PACK_METER_WINAMP_SCOPE 13u
/* The slot a meter's pack goes in, or -1 if that meter cannot be a pack. Slot 0 = Layered Wave, slot 1 = Winamp Bars, slot 2 = Winamp Scope. */
static inline int mtr_pack_slot_of(uint32_t meter_id) { return meter_id == MTR_PACK_METER_LAYERED_WAVE ? 0 : meter_id == MTR_PACK_METER_WINAMP_BARS ? 1 : meter_id == MTR_PACK_METER_WINAMP_SCOPE ? 2 : -1; }

#define MTR_PACK_STATS 4u
typedef struct {
    uint32_t         abi;                                                     /* MTR_PACK_ABI                                                   */
    uint32_t         clk_hz;                                                  /* the CPU clock the cycles() counter runs at (it differs between builds), for time budgets in cycles */
    void           (*fb_rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);   /* the draw engine's rectangle fill     */
    void           (*rect_clip)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);  /* the same, clipped around the fullscreen overlay rects (a meter drawn straight onto the screen uses this one) */
    void           (*bar_clip)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg);   /* OP_BAR column, clipped likewise; only if bar_ready() */
    int            (*bar_ready)(void);                                        /* the bitstream has the bar opcode (probed once by the host) */
    uint32_t       (*cycles)(void);                                           /* free-running CPU cycle counter                                 */
    int            (*psram_ready)(void);                                      /* the PSRAM window has been proven (the pack's own state lives there) */
    const uint16_t  *accent;                                                  /* the current accent colour (RGB565)                             */
    const uint16_t  *role;                                                    /* theme roles, TR_COUNT entries (fw/theme.h)                     */
    uint8_t         *force;                                                   /* the "repaint everything" request flag; the pack clears it once it has drawn */
    const uint16_t  *params;                                                  /* this meter's current setting values (the order of its manifest) */
    uint32_t        *stats;                                                   /* MTR_PACK_STATS words the pack may publish for Info (Info > LW COST, METER DRAW): [0] draw commands of the last tick, [1] CPU cycles spent issuing them, [2] cycles spent reading its PSRAM history, [3] its draw stride; 0 = none. May be NULL. */
    const uint8_t   *grad;                                                    /* non-zero when the meter is drawn over the player screen's gradient (HM_GRAD): the scope then restores or blends that gradient; zero = flat in->bg */
    const uint8_t   *fullscreen;                                              /* non-zero while the fullscreen figure is up (the scope then erases column by column) */
    void           (*bg_restore)(uint32_t x, uint32_t y, uint32_t w, uint32_t h);               /* repaint the player's gradient background over a box */
    int            (*bg_blend)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t bg_alpha);   /* fade a box toward that background (the scope trail); 0 = not done, erase instead */
    void           (*scope_note)(int blended, uint32_t x, uint32_t y);                          /* the firmware's own trail diagnostics (Info rows) */
} mtr_host_api_t;

typedef uint32_t (*mtr_pack_entry_fn)(const mtr_in_t *in, const mtr_host_api_t *api);
#endif
