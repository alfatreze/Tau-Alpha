/* stdin, one command a line:
 *   "T"                 -> the table: SF_COUNT lines "word shift width min max def"
 *   "u w0..w31"         -> unpack: the SF_COUNT values
 *   "p w0..w31 v0..vN"  -> pack: the 32 words
 *   "g w0..w31"         -> 1 when the words are the legacy layout (layout version 0), else 0
 *   "l w0..w31"         -> the values a LEGACY register file holds (every other field at its default)
 *   "m w0..w31"         -> the 32 words of the migrated register file
 *   "r w0..w31"         -> the same for a register file a migration already half wrote (sf_migrate_resume)
 *   "O"                 -> the write order (words, in order)
 *   "S"                 -> the seven shipped meters: "index id_hash params_rev" lines (fw2/cores/shipped_meter_ids.h)
 *   "M idx np max0..N"  -> load the parameter maxima of shipped meter idx (the test fixture the migration needs to keep layout-1 tunings; a meter never loaded has none)
 *   m and r use the loaded maxima; "m0 w0..w31" / "r0 w0..w31" are the same WITHOUT any (every tuning is dropped: the entry points that existed before layout 2) */
#include <stdio.h>
#include "settings_core.h"
static uint16_t fx_max[16][64]; static uint32_t fx_np[16];
static const uint16_t *fx_of(uint32_t meter, uint32_t *np) { if (meter >= 16u || !fx_np[meter]) return 0; *np = fx_np[meter]; return fx_max[meter]; }
int main(void)
{
    char c; unsigned x;
    while (scanf(" %c", &c) == 1) {
        if (c == 'M') { unsigned i, n; scanf("%u %u", &i, &n); fx_np[i] = n; for (unsigned k = 0; k < n; k++) { scanf("%u", &x); fx_max[i][k] = (uint16_t)x; } continue; }
        if (c == 'S') { for (uint32_t i = 0; i < SHIPPED_METERS_N; i++) printf("%u %u %u\n", i, shipped_meter_hash[i], shipped_meter_rev[i]); continue; }
        if ((c == 'm' || c == 'r')) { int ch = getchar(); if (ch == '0') { uint32_t w[SF_WORDS], o[SF_WORDS]; for (uint32_t i = 0; i < SF_WORDS; i++) { scanf("%u", &x); w[i] = x; } if (c == 'm') sf_migrate_words(w, o); else sf_migrate_resume(w, o); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", o[i]); printf("\n"); continue; } ungetc(ch, stdin); }
        uint32_t w[SF_WORDS], v[SF_COUNT];
        if (c == 'O') { for (uint32_t i = 0; i < sizeof sf_write_order; i++) printf("%u ", sf_write_order[i]); printf("\n"); continue; }
        if (c == 'T') { for (uint32_t i = 0; i < SF_COUNT; i++) printf("%u %u %u %u %u %u\n", sf_table[i].word, sf_table[i].shift, sf_table[i].width, sf_table[i].min, sf_table[i].max, sf_table[i].def); continue; }
        for (uint32_t i = 0; i < SF_WORDS; i++) { scanf("%u", &x); w[i] = x; }
        if (c == 'u') { sf_unpack(w, v); for (uint32_t i = 0; i < SF_COUNT; i++) printf("%u ", v[i]); printf("\n"); }
        else if (c == 'g') printf("%d\n", sf_is_legacy(w));
        else if (c == 'l') { sf_legacy_unpack(w, v); for (uint32_t i = 0; i < SF_COUNT; i++) printf("%u ", v[i]); printf("\n"); }
        else if (c == 'r') { uint32_t o[SF_WORDS]; sf_migrate_resume_t(w, o, fx_of); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", o[i]); printf("\n"); }
        else if (c == 'm') { uint32_t o[SF_WORDS]; sf_migrate_words_t(w, o, fx_of); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", o[i]); printf("\n"); }
        else { for (uint32_t i = 0; i < SF_COUNT; i++) { scanf("%u", &x); v[i] = x; } sf_pack(w, v); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", w[i]); printf("\n"); }
    }
    return 0;
}
