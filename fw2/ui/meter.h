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
#ifndef P2_MPD_T
#define P2_MPD_T
/* a parameter the Configure page can edit: type 0 number, 1 choice, 2 on/off, 3 colour (a raw RGB565 value); shown only when the parameter `when_idx` has the value `when_val` (255 = always) */
typedef struct { const char *label; uint8_t type, hidden; uint16_t min, max, step; uint8_t nval; const char *const *vals; uint8_t when_idx; uint16_t when_val; } mpd_t;
#endif
typedef struct {
    const char *name; void (*tick)(const mtr_in_t *in);
    uint8_t npre; void (*set_pre)(uint32_t); const char *(*pre_name)(uint32_t);
    uint16_t *mv; const mpd_t *pd; uint8_t np; void (*apply)(void);
    uint8_t compose;                          /* 1: the meter repaints its whole box each tick, so it is drawn into the idle buffer and copied onto the shown one (no wipe is ever seen) */         /* the live parameters, their descriptors (the manifest's order) and the call that makes the module re-read them */
} meter_def_t;
extern const meter_def_t meter_table[];
extern const uint32_t meter_count;
void        meters_init(void);
void        meters_enter(uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* a new rectangle (or a new meter): clear it, drop every cache, skip the window in progress */
void        meters_tick(int allowed);          /* once per loop pass: draws when a new spectrum window is complete (allowed = 0 while the governor has shed the UI) */
void        meters_select(uint32_t idx);       /* a saved or menu choice (an index outside the table means the first meter) */
void        meters_next(int dir);              /* X: the next / previous meter */
uint32_t    meters_cur(void);
uint32_t    meters_preset_of(uint32_t meter);          /* the selected preset of a meter */
void        meters_preset_select(uint32_t meter, uint32_t p);   /* a saved or menu choice (out of range = the first); repaints when it is the meter on screen */
void        meters_preset_next(void);
uint32_t    meters_npre(uint32_t meter);               /* presets of a meter: the built-in ones, or the METR file's */
uint32_t    meters_metr_load(const uint8_t *src, uint32_t n);   /* the METR section of tau-assets.bin: replaces the presets of the meters it carries (returns how many); applies each boot preset */                  /* Select+X: the next preset of the current meter */
const char *meters_preset_name(void);
void        meters_params_changed(uint32_t meter);   /* the Configure page edited meter `meter`: re-read, repaint, call the preset CUSTOM */
uint32_t    meters_custom(uint32_t meter);           /* 1 when the parameters were edited since the preset was chosen */                  /* the current meter's current preset */
const char *meters_name(void);
void        meters_tune_save(uint32_t meter);       /* the parameters of `meter` were edited: store what differs from its preset (one meter at a time) */
void        meters_tune_words(uint32_t *w);         /* the four persisted words of the stored tuning */
void        meters_tune_apply(const uint32_t *w);   /* persisted words changed (boot, Core Settings): apply the tuning when it belongs to the preset now selected */
uint32_t    meters_payload(uint32_t meter, uint8_t *out);   /* the SR_T_METERCFG entry of the meter as it is now (see report export) */
#endif
