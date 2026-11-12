/* fw2/cores/shipped_meter_ids.h -- THE ONE PLACE that maps the table index of the seven shipped meters (the index that layout 0 and layout 1 of the persisted settings stored) to the identity a meter has in the external-meters design
 * (docs/features/meters/EXTERNAL_METERS_SPEC.md section 5): its `id_hash` (the first 32 bits of the 128-bit id) and `params_rev` (the low 4 bits are part of a saved tuning's key).
 *
 * The id_hash values are the FINAL ids of P2.0 (the first 8 hex digits of `id` in meters/<key>/meter.json; sim/test_persist_layout.py checks this table against the manifests). The params_rev values are still
 * PLACEHOLDERS (the real ones come with the TOC in P2.2; one is above 15 on purpose: only the low 4 bits are part of a key). Line format X(index, id_hash, params_rev): tools/persist_layout.py reads it by regular expression. Order = the manifests' p2 slot (tools/player2/gen_meters_p2.py): winamp_bars, winamp_scope, chladni, layered_wave, vu_master, waterfall_grid, sound_map. */
#ifndef SHIPPED_METER_IDS_H
#define SHIPPED_METER_IDS_H
#include <stdint.h>
#define SHIPPED_METERS_N 7u
#define SHIPPED_METER_LIST(X) \
    X(0, 0x7DDDFE63u, 1u)  /* winamp_bars    */ \
    X(1, 0x143ACB80u, 1u)  /* winamp_scope   */ \
    X(2, 0x7471CC21u, 2u)  /* chladni        */ \
    X(3, 0x58128034u, 1u)  /* layered_wave   */ \
    X(4, 0x0D8C03D3u, 3u)  /* vu_master      */ \
    X(5, 0x17E60776u, 1u)  /* waterfall_grid */ \
    X(6, 0x0C82D9F0u, 17u) /* sound_map      */
#define SHIPPED_HASH_X(i, h, r) h,
#define SHIPPED_REV_X(i, h, r) r,
static const uint32_t shipped_meter_hash[SHIPPED_METERS_N] = { SHIPPED_METER_LIST(SHIPPED_HASH_X) };   /* the whole 32-bit id_hash */
static const uint8_t shipped_meter_rev[SHIPPED_METERS_N] = { SHIPPED_METER_LIST(SHIPPED_REV_X) };      /* params_rev */
#endif
