/* Player 2 meters (spec section 11; the meter module framework of docs/features/meters/METER_MODULE_SPEC.md): a registry of modules, each a tick function that draws its picture inside the rectangle it is given
 * from the inputs it is given (mtr_in_t). The host (meters.c) owns the inputs (the hardware spectrum bank with its ballistics, the clock, pause), the selection (X cycles) and the redraw cadence (one tick
 * per completed spectrum window). The meters set of the previous player: Winamp bars and scope, Chladni, VU master, Layered Wave (the inherited HarpMudd meters are not ported). */
#ifndef P2_METER_H
#define P2_METER_H
#include <stdint.h>
#define MTR_BANDS 16u
typedef struct {
    const uint8_t *spec;          /* MTR_BANDS bands, 0..255, after the shared ballistics */
    uint32_t frame;               /* ticks since the meter was entered */
    uint16_t dt_ms;               /* ms since the previous tick */
    uint16_t x, y, w, h;          /* the rectangle the meter owns; it never draws outside it */
    uint16_t bg;                  /* the background colour to erase with */
    uint8_t force;                /* 1 = repaint the whole rectangle and drop every redraw cache (the context changed) */
    uint8_t paused;               /* playback is paused or stopped: levels fall to zero */
    uint32_t peak_l, peak_r;      /* the loudest sample of each channel since the previous tick, scaled by 3/4 of headroom (0..24575) */
    const int8_t *wave;           /* optional: signed waveform columns (a meter that needs one builds it from peak_l/peak_r otherwise); null in the host */
} mtr_in_t;
typedef struct { const char *name; void (*tick)(const mtr_in_t *in); } meter_def_t;
extern const meter_def_t meter_table[];
extern const uint32_t meter_count;
void        meters_init(void);
void        meters_enter(uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* a new rectangle (or a new meter): clear it, drop every cache, skip the window in progress */
void        meters_tick(int allowed);          /* once per loop pass: draws when a new spectrum window is complete (allowed = 0 while the governor has shed the UI) */
void        meters_select(uint32_t idx);       /* a saved or menu choice (an index outside the table means the first meter) */
void        meters_next(int dir);              /* X: the next / previous meter */
uint32_t    meters_cur(void);
const char *meters_name(void);
#endif
