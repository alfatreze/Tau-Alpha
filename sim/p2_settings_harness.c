/* stdin: "u w0 .. w31" -> unpack: prints the SF_COUNT values; "p w0 .. w31 v0 .. v(SF_COUNT-1)" -> pack: prints the 32 words */
#include <stdio.h>
#include "settings_core.h"
int main(void)
{
    char c; unsigned x;
    while (scanf(" %c", &c) == 1) {
        uint32_t w[SF_WORDS], v[SF_COUNT];
        for (uint32_t i = 0; i < SF_WORDS; i++) { scanf("%u", &x); w[i] = x; }
        if (c == 'u') { sf_unpack(w, v); for (uint32_t i = 0; i < SF_COUNT; i++) printf("%u ", v[i]); printf("\n"); }
        else { for (uint32_t i = 0; i < SF_COUNT; i++) { scanf("%u", &x); v[i] = x; } sf_pack(w, v); for (uint32_t i = 0; i < SF_WORDS; i++) printf("%u ", w[i]); printf("\n"); }
    }
    return 0;
}
