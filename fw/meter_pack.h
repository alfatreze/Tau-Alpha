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

#define MTR_PACK_ABI 6u

/* Where things live. These addresses are part of the ABI: a pack is linked for them, so they are fixed constants, not firmware symbols.
 *   scratch: the on-chip meter scratch. The shipping 192 KB RAM layout leaves a heap gap whose top 4 KB (0x26800..0x27800, just under the ID3 landing zone) is
 *            reserved for it when the firmware is built with TAU_PACKS (fw/link.ld asserts that it fits). Whichever meter is active has its working state there.
 *   slots:   MTR_PACK_SLOTS pack slots of MTR_PACK_SLOT_SIZE bytes in the PSRAM code window, after the cold image (which is far below 0x40000). A slot is executed at
 *            its 0x24xx_xxxx alias and written through the 0xA4xx_xxxx data alias. A meter id has one fixed slot (mtr_pack_slot_of()).
 * Host tests override the bases (the simulator has no PSRAM window). */
#ifndef MTR_PACK_SCRATCH_ORG
#define MTR_PACK_SCRATCH_ORG  0x00026800u
#endif
#define MTR_PACK_SCRATCH_SIZE 0x1000u
#ifndef MTR_PACK_SLOT_BASE
#define MTR_PACK_SLOT_BASE    0x24840000u          /* instruction alias; the data alias is the same offset from 0xA4800000 */
#endif
#define MTR_PACK_SLOT_SIZE    0x10000u
#define MTR_PACK_SLOTS        5u
#define MTR_PACK_METER_LAYERED_WAVE 16u            /* the VIZ_* id of the meter (fw/meter_gen_enum.h) */
#define MTR_PACK_METER_WINAMP_BARS  12u
#define MTR_PACK_METER_WINAMP_SCOPE 13u
#define MTR_PACK_METER_VU_MASTER    15u
#define MTR_PACK_METER_CHLADNI      14u
/* The slot a meter's pack goes in, or -1 if that meter cannot be a pack. Slot 0 = Layered Wave, slot 1 = Winamp Bars, slot 2 = Winamp Scope, slot 3 = VU Master, slot 4 = Chladni. */
static inline int mtr_pack_slot_of(uint32_t meter_id) { return meter_id == MTR_PACK_METER_LAYERED_WAVE ? 0 : meter_id == MTR_PACK_METER_WINAMP_BARS ? 1 : meter_id == MTR_PACK_METER_WINAMP_SCOPE ? 2 : meter_id == MTR_PACK_METER_VU_MASTER ? 3 : meter_id == MTR_PACK_METER_CHLADNI ? 4 : -1; }

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
    const uint32_t  *hs_base;                                                 /* the word base of the buffer this call draws into (a direct draw: the displayed buffer; a composed figure: the idle back buffer) */
    int            (*held)(void);                                             /* the screen belongs to an overlay or fullscreen figure: draw nothing now */
    int            (*afford)(void);                                           /* the audio FIFO has room for this meter's cost now (the yield test); 0 = skip this time */
    uint32_t       (*cpu_pct)(void);                                          /* whole-system CPU load, percent (a one-second average) */
    void           (*toast)(const char *msg);                                 /* a short on-screen message */
    int            (*mb_write)(uint32_t word_addr, uint32_t data);            /* one SDRAM mailbox write of two adjacent 16-bit words; 0 = ok, -1 = timeout */
    int            (*mb_read)(uint32_t word_addr, uint32_t *out);             /* ...and the read */
    void           (*blit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);                                  /* the engine's rectangle copy                      */
    void           (*sblit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t scx, uint32_t scy);    /* ...and its scaled copy */
    void           (*wait)(void);                                             /* until the engine can take a command                */
    void           (*fence)(void);                                            /* until the engine has executed everything queued    */
    void           (*set_bases)(uint32_t src_base, uint32_t dst_base);       /* the sticky source and destination bases of blit-mode commands */
    void           (*set_color)(uint16_t fg, uint16_t bg);                     /* text colours for the next fb_char / text_clipped                */
    void           (*ch)(uint32_t x, uint32_t y, char c, uint32_t sx, uint32_t sy);   /* one glyph (sizes are the firmware's TS_* enum: 0 = 1x)   */
    uint32_t       (*text_clipped)(uint32_t x, uint32_t y, const char *s, uint32_t sx, uint32_t sy, uint32_t max_w);   /* a string clipped to max_w advance */
    char          *(*dec)(char *p, uint32_t v);                               /* append v in decimal, return the end                             */
    uint32_t       (*cell)(uint32_t size);                                    /* height of a text row at that size                              */
    const uint8_t   *grad;                                                    /* non-zero when the meter is drawn over the player screen's gradient (HM_GRAD): the scope then restores or blends that gradient; zero = flat in->bg */
    const uint8_t   *fullscreen;                                              /* non-zero while the fullscreen figure is up (the scope then erases column by column) */
    void           (*bg_restore)(uint32_t x, uint32_t y, uint32_t w, uint32_t h);               /* repaint the player's gradient background over a box */
    int            (*bg_blend)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t bg_alpha);   /* fade a box toward that background (the scope trail); 0 = not done, erase instead */
    void           (*scope_note)(int blended, uint32_t x, uint32_t y);                          /* the firmware's own trail diagnostics (Info rows) */
} mtr_host_api_t;

typedef uint32_t (*mtr_pack_entry_fn)(const mtr_in_t *in, const mtr_host_api_t *api);
#endif
