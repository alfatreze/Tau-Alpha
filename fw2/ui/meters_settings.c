#pragma GCC optimize ("Os")
#include "meters_settings.h"
#include "../cores/tune_core.h"
/* The settings interface over the pool of two tunings (see meters_settings.h for the contract). The format is cores/tune_core.h's, unchanged: the persisted words are bit for bit what the older code wrote, so a card
 * written by 0.7.1 is read by this and the other way round. */
MS_STATE static const uint16_t *ms_max; MS_STATE static uint32_t ms_np, ms_key;
static const uint16_t *ms_max_of(uint32_t key, uint32_t *np) { if (key != ms_key || !ms_max) return 0; *np = ms_np; return ms_max; }       /* tune_unpack asks for the schema of the key it reads */
uint32_t meter_settings_put(uint32_t id_hash, uint32_t params_rev, const meter_settings_t *s, const uint16_t *max, uint32_t np)
{
    const uint32_t key = tune_key(id_hash, params_rev);
    uint8_t idx[MS_MAX_ENTRIES]; uint16_t val[MS_MAX_ENTRIES]; uint32_t n = 0;
    for (uint32_t k = 0; k < s->n && k < MS_MAX_ENTRIES; k++) if (s->idx[k] < np && s->val[k] <= max[s->idx[k]]) { idx[n] = s->idx[k]; val[n] = s->val[k]; n++; }
    uint32_t w[TUNE_WORDS];
    const uint32_t kept = tune_build(key, s->preset, idx, val, n, max, w);
    if (!kept) { mset_pool_drop(key); return 0u; }
    mset_pool_store(key, w);
    return kept;
}
int meter_settings_get(uint32_t id_hash, uint32_t params_rev, const uint16_t *max, uint32_t np, meter_settings_t *out)
{
    const uint32_t key = tune_key(id_hash, params_rev); const int sl = mset_pool_find(key);
    out->n = 0; out->preset = 0;
    if (sl < 0) return 0;
    ms_max = max; ms_np = np; ms_key = key;
    uint32_t mt = 0, pr = 0; uint8_t ix[TUNE_MAX]; uint16_t vl[TUNE_MAX];
    const uint32_t n = tune_unpack(mset_pool_words((uint32_t)sl), ms_max_of, &mt, &pr, ix, vl);
    if (!n || mt != key) return 0;
    out->preset = (uint8_t)pr; out->n = (uint8_t)n;
    for (uint32_t k = 0; k < n; k++) { out->idx[k] = ix[k]; out->val[k] = vl[k]; }
    return 1;
}
int meter_settings_preset(uint32_t id_hash, uint32_t params_rev, uint32_t *preset)
{
    const int sl = mset_pool_find(tune_key(id_hash, params_rev));
    if (sl < 0) return 0;
    uint8_t b[16]; uint32_t pos = 28u; tune_from_words(mset_pool_words((uint32_t)sl), b); *preset = tune_get(b, &pos, 7u);
    return 1;
}
void meter_settings_drop(uint32_t id_hash, uint32_t params_rev) { mset_pool_drop(tune_key(id_hash, params_rev)); }
