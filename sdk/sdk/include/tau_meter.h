/* tau_meter.h: everything a Tau Alpha meter may use. ABI 1.2.  MIT licence.
 *
 * A meter is one freestanding C file (or several) built for a bare RV32IM core and loaded by the player into one 64 KiB window when it is selected. It reaches the player ONLY through the
 * host table (mtr_host_api_t) below: no libc, no player symbol, no absolute address, no register. `tau-meter test` checks exactly that.
 *
 * The smallest meter:
 *
 *     #include <tau_meter.h>
 *     static void draw(const mtr_in_t *in) { mtr_rect(in->x, in->y, in->w, in->h, in->bg); }
 *     TAU_METER(draw)
 *
 * The player calls `draw` about once per completed spectrum window (about 38 times a second at full detail). Draw only inside the rectangle (in->x, in->y, in->w, in->h).
 * Every call a meter makes through the macros below is counted: the count is what the player compares with the meter's `cmds` ceiling (meter.json "p2": {"cmds": N}).
 * The table in docs/HOST_TABLE.md is generated from this file (tools: `tau-meter docs`); keep the one-line comments after each field: they are the reference.
 * Versioning: ABI major.minor. A minor only appends (a host-table entry or an mtr_in_t field); a pack declares the minor it NEEDS in meter.json ("abi": "1.0"). */
#ifndef TAU_METER_H
#define TAU_METER_H
#include <stdint.h>
#include <stddef.h>

#define TAU_METER_ABI_MAJOR 1u
#define TAU_METER_ABI_MINOR 2u                 /* what this header describes; a meter's "abi" in meter.json is the minor it needs (0, 1 or 2) */
#define MTR_BANDS 16u                          /* spectrum bands in mtr_in_t.spec */
#define MTR_STATS 4u                           /* words in the stats array the meter may publish */
#define MTR_MAX_PARAMS 48u                     /* parameters per meter */

/* The inputs of one tick. Read only: a write into it (or into anything the host passed) is a conformance failure. */
typedef struct {
    const uint8_t *spec;          /* [1.0] MTR_BANDS bands, 0..255, after the player's ballistics (fast attack, slow release) */
    uint32_t frame;               /* [1.0] ticks since the meter was entered */
    uint16_t dt_ms;               /* [1.0] ms since the previous tick */
    uint16_t x, y, w, h;          /* [1.0] the rectangle the meter owns, in screen pixels; it never draws outside it */
    uint16_t bg;                  /* [1.0] the background colour (RGB565) to erase with */
    uint8_t force;                /* [1.0] 1 = repaint the whole rectangle and drop every redraw cache (the context changed: new meter, new box, resumed) */
    uint8_t paused;               /* [1.0] playback is paused or stopped: levels fall to zero */
    uint32_t peak_l, peak_r;      /* [1.0] the loudest sample of each channel since the previous tick, 0..24575 (3/4 of full scale) */
    uint8_t degrade;              /* [1.0] 0 full; 1 and 2: the player skips windows (every 2nd, every 4th), a meter may also reduce detail; the meter is switched off at 3 */
    const int8_t *wave;           /* [1.0] optional signed waveform columns; NULL in the player today: build one from peak_l and peak_r */
    uint16_t rms_l, rms_r;        /* [1.1] RMS of each channel over the picture's window, same scale as the peaks (0..24575); needs stats_ok */
    int16_t corr;                 /* [1.1] L/R correlation, Q8: -256 opposite phase, 0 unrelated, +256 identical; 0 when stats_ok is 0 */
    uint8_t centroid;             /* [1.1] spectral centroid, 0..255: the level-weighted mean band */
    uint8_t crest;                /* [1.1] peak / RMS, Q4 (16 = 1.0); 0 when stats_ok is 0 */
    uint8_t flux;                 /* [1.1] spectral flux: the summed rise of the bands since the previous picture, at most 255 */
    uint8_t onset;                /* [1.1] 1 when this picture holds a hit (a flux spike against its own running mean) */
    uint8_t stats_ok;             /* [1.1] 1 when the hardware statistics block is present (rms_l, rms_r, corr and crest are real) */
} mtr_in_t;

/* The host table. Every call is made through a function pointer the player filled in. Coordinates are pixels of the 400 x 360 screen; colours are RGB565. */
typedef struct {
    uint32_t         abi;                                                   /* [1.0] the player's ABI: major << 8 | minor */
    void           (*rect)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);   /* [1.0] fill a rectangle (one engine command); the player clips it around fullscreen exclusions */
    const uint16_t  *role;                                                  /* [1.0] the theme colours, MTR_ROLE_COUNT entries indexed by MTR_ROLE_*: read-only */
    const uint16_t  *params;                                                /* [1.0] this meter's live parameter values, in meter.json's order: read-only */
    uint32_t        *stats;                                                 /* [1.0] MTR_STATS words the meter may publish: [0] draw commands of the last tick (TAU_METER fills it); may be NULL */
    uint32_t       (*text)(uint32_t x, uint32_t y, const char *s, uint16_t fg, uint16_t bg);   /* [1.0] 1x text, full cells (8 x 16 or the theme font); returns the x after the last glyph */
    void           (*rect_stash)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t colour);   /* [1.0] the same fill, not clipped to the visible frame: draws into the off-screen stash plane */
    int            (*blend_ready)(void);                                    /* [1.0] 1 when the bitstream has the pipelined blend */
    void           (*blend_on)(uint32_t a);                                 /* [1.0] source weight a/256 for the blits that follow */
    void           (*blend_blit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);   /* [1.0] a blit blended with the weight set by blend_on */
    void           (*blend_off)(void);                                      /* [1.0] back to plain copies */
    int            (*mb_write)(uint32_t pixel_addr, uint32_t two_pixels);   /* [1.0] the SDRAM mailbox, two pixels a word; 0 ok, -1 timeout */
    int            (*mb_read)(uint32_t pixel_addr, uint32_t *two_pixels);   /* [1.0] read the mailbox; 0 ok, -1 timeout */
    void           (*fence)(void);                                          /* [1.0] wait until every queued draw command has executed (bounded by the player) */
    void           (*blit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);       /* [1.0] a copy inside the buffer being drawn into */
    void           (*blit_abs)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);   /* [1.0] stash to stash copy */
    void           (*sblit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h, uint32_t scale);   /* [1.0] a scaled copy from the stash into the drawing buffer */
    void           (*bar)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit, uint16_t fg, uint16_t bg);   /* [1.0] a bar column in one command: the bottom `lit` rows in fg, the rest in bg */
    void           (*scroll_down)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d);                      /* [1.0] the box moves down d rows (a few copies, not a redraw) */
    void           (*clut_load)(uint32_t start, const uint16_t *colors, uint32_t n);                                 /* [1.0] palette entries start..: load yours before your cblit calls (other users overwrite it) */
    void           (*cblit)(uint32_t sx, uint32_t sy, uint32_t dx, uint32_t dy, uint32_t w, uint32_t h);              /* [1.0] palette indices from the stash at (sx, sy) through the palette to the drawing buffer; w <= 127 */
    void           (*scroll_left)(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t d);                      /* [1.2] the box moves left d columns */
} mtr_host_api_t;

/* The theme roles (indices into api->role). */
enum {
    MTR_ROLE_BG_TOP = 0, MTR_ROLE_BG_BOTTOM, MTR_ROLE_SURFACE, MTR_ROLE_SURFACE_TRACK, MTR_ROLE_TEXT_PRIMARY, MTR_ROLE_TEXT_SECONDARY,
    MTR_ROLE_ACCENT, MTR_ROLE_ON_ACCENT, MTR_ROLE_ACCENT2, MTR_ROLE_OK, MTR_ROLE_WARN, MTR_ROLE_DANGER,
    MTR_ROLE_BASE, MTR_ROLE_CHROME, MTR_ROLE_PILL, MTR_ROLE_ERROR, MTR_ROLE_FAINT, MTR_ROLE_SPLASH_BG, MTR_ROLE_SPLASH_BAR, MTR_ROLE_FS_RED, MTR_ROLE_FS_TRACK,
    MTR_ROLE_COUNT
};

typedef uint32_t (*mtr_pack_entry_fn)(const mtr_in_t *in, const mtr_host_api_t *api);

/* ---- the convenience layer: counted calls through the table the player gave this tick. ---- */
extern const mtr_host_api_t *tau_meter_api;      /* set by TAU_METER before your tick runs */
extern uint32_t tau_meter_cmds;                  /* draw commands issued so far this tick */
#define mtr_rect(x, y, w, h, c)            (tau_meter_cmds++, tau_meter_api->rect((x), (y), (w), (h), (c)))
#define mtr_text(x, y, s, fg, bg)          (tau_meter_cmds++, tau_meter_api->text((x), (y), (s), (fg), (bg)))
#define mtr_bar(x, y, w, h, lit, fg, bg)   (tau_meter_cmds++, tau_meter_api->bar((x), (y), (w), (h), (lit), (fg), (bg)))
#define mtr_scroll_down(x, y, w, h, d)     (tau_meter_cmds++, tau_meter_api->scroll_down((x), (y), (w), (h), (d)))
#define mtr_scroll_left(x, y, w, h, d)     (tau_meter_cmds++, tau_meter_api->scroll_left((x), (y), (w), (h), (d)))   /* needs "abi": "1.2" */
#define mtr_blit(sx, sy, dx, dy, w, h)     (tau_meter_cmds++, tau_meter_api->blit((sx), (sy), (dx), (dy), (w), (h)))
#define mtr_rect_stash(x, y, w, h, c)      (tau_meter_cmds++, tau_meter_api->rect_stash((x), (y), (w), (h), (c)))
#define mtr_fence()                        tau_meter_api->fence()
#define mtr_param(i)                       (tau_meter_api->params[(i)])
#define mtr_role(r)                        (tau_meter_api->role[(r)])

/* TAU_METER(tick) defines the entry point the player calls: it records the table, counts your draw commands, publishes the count in stats[0] and returns it. */
#define TAU_METER(tick_fn)                                                                                                   \
    const mtr_host_api_t *tau_meter_api; uint32_t tau_meter_cmds;                                                            \
    __attribute__((section(".text.mtr_pack_entry"), used))                                                                   \
    uint32_t mtr_pack_entry(const mtr_in_t *in, const mtr_host_api_t *api)                                                   \
    { tau_meter_api = api; tau_meter_cmds = 0u; tick_fn(in); if (api->stats) api->stats[0] = tau_meter_cmds; return tau_meter_cmds; }

/* Persistent state. Scratch memory is only 4 KiB for every meter together: keep static data small. A meter with a history may keep it in PSRAM with TAU_PSTATE (zeroed when
 * the meter is selected, up to the rest of the 64 KiB window): PSRAM takes WHOLE 32-bit word reads and writes only, so declare it as uint32_t. */
#define TAU_PSTATE __attribute__((section(".pstate")))

#endif
