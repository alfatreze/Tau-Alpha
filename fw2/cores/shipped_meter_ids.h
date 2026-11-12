/* fw2/cores/shipped_meter_ids.h -- THE ONE PLACE that maps the table index of the seven shipped meters (the index that layout 0 and layout 1 of the persisted settings stored) to the identity a meter has in the external-meters design
 * (docs/features/meters/EXTERNAL_METERS_SPEC.md section 5): its `id_hash` (the first 32 bits of the 128-bit id) and `params_rev` (the low 4 bits are part of a saved tuning's key).
 *
 * *** PLACEHOLDER VALUES (P2.3, 2026-11-12) *** P2.0 generates the real ids of the seven meters and OVERWRITES THIS FILE (keep the X(index, id_hash, params_rev) line format: tools/persist_layout.py and the tests
 * read the table from here by regular expression, and nothing else in the repository repeats these numbers). The placeholders are deliberately fake-looking, some with bit 31 set (only the low 31 bits are persisted),
 * and one params_rev above 15 (only the low 4 bits are part of a key). Order = the manifests' p2 slot (tools/player2/gen_meters_p2.py): winamp_bars, winamp_scope, chladni, layered_wave, vu_master, waterfall_grid, sound_map. */
#ifndef SHIPPED_METER_IDS_H
#define SHIPPED_METER_IDS_H
#include <stdint.h>
#define SHIPPED_METERS_N 7u
#define SHIPPED_METER_LIST(X) \
    X(0, 0xFA000001u, 1u)  /* winamp_bars    */ \
    X(1, 0x7A000002u, 1u)  /* winamp_scope   */ \
    X(2, 0xFA000003u, 2u)  /* chladni        */ \
    X(3, 0x7A000004u, 1u)  /* layered_wave   */ \
    X(4, 0xFA000005u, 3u)  /* vu_master      */ \
    X(5, 0x7A000006u, 1u)  /* waterfall_grid */ \
    X(6, 0xFA000007u, 17u) /* sound_map      */
#define SHIPPED_HASH_X(i, h, r) h,
#define SHIPPED_REV_X(i, h, r) r,
static const uint32_t shipped_meter_hash[SHIPPED_METERS_N] = { SHIPPED_METER_LIST(SHIPPED_HASH_X) };   /* the whole 32-bit id_hash */
static const uint8_t shipped_meter_rev[SHIPPED_METERS_N] = { SHIPPED_METER_LIST(SHIPPED_REV_X) };      /* params_rev */
#endif
