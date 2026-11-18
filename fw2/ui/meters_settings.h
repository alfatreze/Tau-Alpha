/* fw2/ui/meters_settings.h -- ONE interface for what a meter's Configure page edits (release 0.7.5, workstream W1; D-M25).
 *
 * Why it exists: workstream W2 replaces the storage (a save file in the Pocket's own save slot, one record per meter) and W1 must not have to change again when it does. Every reader and writer of a meter's saved
 * settings goes through these four functions and nothing else: ui/meters.c (the registry) calls them when a parameter is edited, when a meter is selected and when the Configure page of a meter is opened.
 *
 * WHAT A RECORD IS: the settings of ONE meter, found by its identity: id_hash (the 32-bit hash of the 128-bit id of its manifest) and params_rev (the revision of its parameter list: a record made for another revision is
 * never applied, it would put values into the wrong parameters). It is SPARSE: `preset` (the preset the values started from: the preset FILE's id + 1 when the meter runs on preset files, else the position in its own list)
 * and the entries (parameter index, value) of the parameters that DIFFER from that preset's values. Everything else comes from the preset, so a changed preset file changes the untouched parameters, as a user expects.
 *
 * THE SCHEMA ARGUMENTS: `max[i]` is the largest value parameter i can have (a colour 65535, a switch 1, a choice its option count - 1, a number its maximum) and `np` the parameter count. put skips an entry whose index is
 * not below np or whose value is above its maximum; get returns 0 for a record that does not fit the schema (a damaged one). The backend may use them to size what it stores.
 *
 * THE BACKEND TODAY (as built since 0.6.x): the pool of TWO tunings (cores/tune_core.h) in the persisted settings words, keyed by the low 16 bits of id_hash and the low 4 bits of params_rev (two meters that share them
 * run from their defaults: the registry's collision rule). KNOWN LIMITS until W2: only two meters keep their settings across a restart; tuning a third evicts the one saved least recently; a record holds at most the
 * entries that fit 85 bits (six parameters of 8 bits). The backend is the four mset_pool_* functions at the bottom, implemented in ui/meters.c (the pool is persisted from there by appearance_get/set, which are
 * unchanged); meters_settings.c holds the format. W2 adds a second backend behind get/put/drop and the pool functions stay as the migration's reader.
 * Cold code (the Configure path only; no card access). */
#ifndef P2_METERS_SETTINGS_H
#define P2_METERS_SETTINGS_H
#include <stdint.h>
#ifdef P2_HOST
#define MS_STATE                                                      /* a host build has no such section */
#else
#define MS_STATE __attribute__((section(".psram_state")))            /* PSRAM state (word accesses): a variable of cold code costs hot RAM otherwise, and hot RAM is at its floor */
#endif
#define MS_MAX_ENTRIES 40u            /* the largest parameter count of a meter (DYN_MAX_P) */
typedef struct {
    uint8_t preset;                   /* the anchor: preset file id + 1, or the position in the meter's own list (0 = the first / DEFAULT) */
    uint8_t n;                        /* entries */
    uint8_t idx[MS_MAX_ENTRIES];      /* parameter index, ascending */
    uint16_t val[MS_MAX_ENTRIES];     /* its value */
} meter_settings_t;
/* Stores the settings of the meter (id_hash, params_rev). Returns how many entries were kept (0 = none: any record of this meter is dropped, which is what "nothing differs from the preset" means). The record replaces the
 * meter's previous one. Entries are taken in index order; those that do not fit the schema or the backend's room are left out (the rest go back to the preset after a restart). */
uint32_t meter_settings_put(uint32_t id_hash, uint32_t params_rev, const meter_settings_t *s, const uint16_t *max, uint32_t np);
/* Reads them: 1 = a valid record exists for this meter and revision (`out` is filled), 0 = none (or one that does not fit the schema). */
int      meter_settings_get(uint32_t id_hash, uint32_t params_rev, const uint16_t *max, uint32_t np, meter_settings_t *out);
/* The preset a stored record started from, without checking its entries: 1 = a record exists (*preset is its anchor). The registry selects that preset FIRST, because the entries only apply to it. */
int      meter_settings_preset(uint32_t id_hash, uint32_t params_rev, uint32_t *preset);
void     meter_settings_drop(uint32_t id_hash, uint32_t params_rev);
/* ---- the pool backend (ui/meters.c): the persisted tuning words ---- */
int             mset_pool_find(uint32_t key);                         /* the slot (0 or 1) of the record with this tune_key, -1 when there is none */
const uint32_t *mset_pool_words(uint32_t slot);                       /* the four words of a slot */
void            mset_pool_store(uint32_t key, const uint32_t *w);    /* the record of `key` replaces its slot, else takes an empty one, else the one saved least recently */
void            mset_pool_drop(uint32_t key);
#endif
