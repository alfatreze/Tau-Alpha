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
    uint8_t degrade;              /* 0 full; 1 and 2: the audio's lead is shrinking, the host already skips windows (every 2nd, every 4th): a meter may also reduce detail (fewer cells, coarser steps) with mtr_degrade_div(); the meters are off at 3 */
    const int8_t *wave;           /* optional: signed waveform columns (a meter that needs one builds it from peak_l/peak_r otherwise); null in the host */
    /* ABI 1.1: audio statistics (fw2/cores/audio_stats_core.h). rms and corr need the hardware block (stats_ok = 1); centroid, crest, flux and onset are always there. All 0 while paused or silent. */
    uint16_t rms_l, rms_r;        /* RMS of each channel over the picture's window, on the same scale as peak_l/peak_r (0..24575) */
    int16_t corr;                 /* L/R correlation, Q8: -256 opposite phase, 0 unrelated, +256 identical; 0 when stats_ok is 0 */
    uint8_t centroid;             /* spectral centroid, 0..255: the level-weighted mean band */
    uint8_t crest;                /* peak / RMS, Q4 (16 = 1.0); 0 when stats_ok is 0 */
    uint8_t flux;                 /* spectral flux: the summed rise of the bands since the previous picture, capped at 255 */
    uint8_t onset;                /* 1 when this picture holds a hit (a flux spike against its own running mean) */
    uint8_t stats_ok;             /* 1 when the hardware statistics block is present (rms_l, rms_r, corr and crest are real) */
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
    uint32_t cmp;                             /* when the meter is composed: 0 never, 1 always, or a parameter gate (low 16 bits; tools/player2/gen_meters_p2.py cmp_code): it repaints its whole box each tick, so it is drawn into the idle buffer and copied onto the shown one (no wipe, no tear is ever seen) */
} meter_def_t;
/* What drawing costs, measured around every tick (one writer: meters_tick; the counter registry publishes it on Info and in the reports). Cycles of the 66.667 MHz counter. */
typedef struct {
    uint32_t ticks;                 /* ticks drawn */
    uint32_t degraded;              /* windows not drawn because the degrade level asked for fewer pictures */
    uint32_t shed;                  /* spectrum windows that had to wait because the governor shed the UI (counted once each; the window is drawn when the UI is allowed again) */
    uint32_t last_cyc;              /* the last tick, including the compose copy */
    uint32_t compose_cyc;           /* the last compose copy onto the shown buffer (0 for a meter that draws directly) */
    uint32_t cmds_last;             /* draw-engine commands the last tick issued */
    uint32_t cmds_max;              /* the most commands one drawn tick has issued since boot (forced repaints included) */
    uint32_t over;                  /* drawn ticks that exceeded the meter's command ceiling (each raised its own degrade level; cores/meter_budget_core.h) */
    uint32_t overruns;              /* ticks of a pack over its cycle limit (cores/pack_strike_core.h) */
    uint32_t struck;                /* packs disabled for the session after three overruns */
    uint32_t rows;                  /* the visible rows the last tick wrote: first << 16 | one past the last (0 when it drew nothing visible) */
    uint32_t direct;                /* ticks drawn straight onto the shown buffer (not composed, no idle-buffer frame), forced repaints excluded */
    uint32_t stats_win;             /* hardware statistics windows read (one per drawn picture while the block is present) */
    uint32_t stats_n;               /* samples in the last window */
    uint16_t stats_ovf;             /* windows that overflowed the block's 131,072-sample sums */
    uint16_t stats_rms;             /* the last window's RMS, mean of both channels, on the peaks' scale (0..24575) */
    int32_t  stats_corr;            /* the last window's L/R correlation, Q8 (-256..256) */
    uint32_t exposed;               /* of those, the ticks that began while the beam was inside the rows they wrote (seen half old, half new for a frame) */
    uint32_t max_cyc[8];            /* the slowest tick of each meter since boot (indexed like meter_table; METER_MAX_STATS bounds the table) */
} meter_stats_t;
extern meter_stats_t meter_stats;
extern const meter_def_t meter_table[];                 /* the built-in rows (0..METER_N-1) */
extern uint32_t meter_count;                            /* the rows in use: a pack-only meter's row (METER_N..) exists only while its pack is installed with a sound descriptor */
int         meters_present(uint32_t row);               /* the row has a meter (rows in between may be absent) */
const meter_def_t *meters_def(uint32_t row);            /* the row's definition (a pack-only row's Configure descriptors are copied to hot RAM on first use and stay until another pack-only row is asked for) */
uint32_t    meters_list_count(void);                    /* the METER list: the meters that are there, in row order */
uint32_t    meters_list_row(uint32_t i);                /* list position -> row (the saved meter index) */
uint32_t    meters_list_index(uint32_t row);            /* row -> list position */
uint32_t    meters_thumb_len(uint32_t row);             /* a pack-only meter's preview, read from its descriptor: stream length, byte, palette entry (0 for a built-in row: its preview is in the firmware) */
int         meters_thumb_rle(uint32_t row, uint32_t i, uint8_t *v);
int         meters_thumb_col(uint32_t row, uint32_t c, uint16_t *fixed, uint8_t *a, uint8_t *b, uint8_t *t, uint8_t *n);
void        meters_init(void);
void        meters_enter(uint32_t x, uint32_t y, uint32_t w, uint32_t h);   /* a new rectangle (or a new meter): clear it, drop every cache, skip the window in progress */
void        meters_tick(int allowed);          /* once per loop pass: draws when a new spectrum window is complete (allowed = 0 while the governor has shed the UI) */
void        meters_select(uint32_t idx);       /* a saved or menu choice (an index outside the table means the first meter) */
void        meters_next(int dir);              /* X: the next / previous meter */
uint32_t    meters_cur(void);
uint32_t    meters_self_degrade(uint32_t meter);   /* the meter's own degrade level 0..2 (it issued more draw commands than its ceiling; 0 when it stays inside) */
uint32_t    meters_preset_of(uint32_t meter);          /* the selected preset of a meter */
void        meters_preset_select(uint32_t meter, uint32_t p);   /* a saved or menu choice (out of range = the first); repaints when it is the meter on screen */
void        meters_preset_next(void);
uint32_t    meters_npre(uint32_t meter);               /* presets of a meter: the built-in ones, or the METR file's */
#ifdef P2_PACKS
void        meters_packs_load(const uint8_t *p, uint32_t n);    /* the MPKB section of tau-assets.bin: loadable meters (a PACKS build) */
void        meters_pack_info(uint32_t *running, uint32_t *first_err, uint32_t *file_err, uint32_t *seen);   /* Info: packs running (bit per meter), first refusal code, the bundle's result (0xFF: no bundle), packs in the file */
uint32_t    meters_pack_state(uint32_t m);                       /* 0 = meter m runs as a pack, 0xFF = no pack, else the refusal code */
#endif
uint32_t    meters_metr_load(const uint8_t *src, uint32_t n);   /* the METR section of tau-assets.bin: replaces the presets of the meters it carries (returns how many); applies each boot preset */                  /* Select+X: the next preset of the current meter */
const char *meters_preset_name(void);
void        meters_params_changed(uint32_t meter);   /* the Configure page edited meter `meter`: re-read, repaint, call the preset CUSTOM */
uint32_t    meters_custom(uint32_t meter);           /* 1 when the parameters were edited since the preset was chosen */                  /* the current meter's current preset */
const char *meters_name(void);
void        meters_tune_save(uint32_t meter);       /* the parameters of `meter` were edited: store what differs from its preset (one meter at a time) */
void        meters_tune_words(uint32_t *w);         /* the eight persisted words of the tuning pool (two slots of four) */
uint32_t    meters_tune_newer(void);                /* which slot was saved last (0 or 1): the other is evicted next */
void        meters_tune_apply(const uint32_t *w, uint32_t newer);   /* persisted words changed (boot): apply each slot's tuning when it belongs to the preset now selected */
uint32_t    meters_payload(uint32_t meter, uint8_t *out);   /* the SR_T_METERCFG entry of the meter as it is now (see report export) */
#endif
