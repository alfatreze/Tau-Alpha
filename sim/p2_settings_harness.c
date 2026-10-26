/* stdin, one command a line:
 *   "T"                 -> the table: SF_COUNT lines "word shift width min max def"
 *   "u w0..w31"         -> unpack: the SF_COUNT values
 *   "p w0..w31 v0..vN"  -> pack: the 32 words
 *   "g w0..w31"         -> 1 when the words are the legacy layout (layout version 0), else 0
 *   "l w0..w31"         -> the values a LEGACY register file holds (every other field at its default)
 *   "m w0..w31"         -> the 32 words of the migrated register file
 *   "r w0..w31"         -> the same for a register file a migration already half wrote (sf_migrate_resume)
 *   "O"                 -> the write order (words, in order) */
#include <stdio.h>
#include "settings_core.h"
int main(void)
{
    char c; unsigned x;
    while (scanf(" %c", &c) == 1) {
        uint32_t w[SF_WORDS], v[SF_COUNT];
        if (c == 'O') { for (uint32_t i = 0; i < sizeof sf_write_order; i++) printf("%u ", sf_write_order[i]); printf("\n"); continue; }
        if (c == 'T') { for (uint32_t i = 0; i < SF_COUNT; i++) printf("%u %u %u %u %u %u\n", sf_table[i].word, sf_table[i].shift, sf_table[i].width, sf_table[i].min, sf_table[i].max, sf_table[i].def); continue; }
        for (uint32_t i = 0; i < SF_WORDS; i++) { scanf("%u", &x); w[i] = x; }
        if (c == 'u') { sf_unpack(w, v); for (uint32_t i = 0; i < SF_COUNT; i++) printf("%u ", v[i]); printf("\n"); }
        else if (c == 'g') printf("%d\n", sf_is_legacy(w));
        else if (c == 'l') { sf_legacy_unpack(w, v); for (uint32_t i = 0; i < SF_COUNT; i++) printf("%u ", v[i]); printf("\n"); }
        else if (c == 'r') { uint32_t o[SF_WORDS]; sf_migrate_resume(w, o); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", o[i]); printf("\n"); }
        else if (c == 'm') { uint32_t o[SF_WORDS]; sf_migrate_words(w, o); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", o[i]); printf("\n"); }
        else { for (uint32_t i = 0; i < SF_COUNT; i++) { scanf("%u", &x); v[i] = x; } sf_pack(w, v); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", w[i]); printf("\n"); }
    }
    return 0;
}
