/* Differential fuzz of the overlay prototype against SEPARATE buffers. Three toy "meters" each keep long-lived state in their own scratch and draw a checksum from it:
 * the reference build gives each its own buffer (what the firmware has today); the overlay build shares one region and must give the same checksum for every random
 * sequence of mode switches and ticks, PROVIDED each meter re-initialises on claim (which is the rule being tested). usage: harness <seed> <steps> <mode> ; mode 0 = reference, 1 = overlay;
 * a meter that forgets to init (-DMETER_SKIPS_INIT) is the bug class this exists to catch: with poisoning the overlay build then differs from the reference at once. */
#include <stdio.h>
#include <stdlib.h>
#include "overlay.h"
typedef struct { int16_t ring[600]; uint32_t head; } chl_t;     OVL_SIZE_CHECK(chl_t);
typedef struct { int16_t y[256]; uint32_t k; } scope_t;         OVL_SIZE_CHECK(scope_t);
typedef struct { uint8_t row[400]; int16_t band[3][16]; uint32_t t; } lw_t;   OVL_SIZE_CHECK(lw_t);
static chl_t REF_C; static scope_t REF_S; static lw_t REF_L;
static void init_c(void *r) { memset(r, 0, sizeof(chl_t)); }
static void init_s(void *r) { memset(r, 0, sizeof(scope_t)); }
static void init_l(void *r) { memset(r, 0, sizeof(lw_t)); }
static uint32_t lcg = 1; static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }
static uint32_t tick_c(chl_t *c, uint32_t in) { c->ring[c->head % 600] = (int16_t)in; c->head++; uint32_t s = 0; for (int i = 0; i < 600; i += 7) s += (uint16_t)c->ring[i]; return s; }
static uint32_t tick_s(scope_t *s, uint32_t in) { s->y[s->k & 255] = (int16_t)in; s->k++; uint32_t a = 0; for (int i = 0; i < 256; i += 5) a += (uint16_t)s->y[i]; return a; }
static uint32_t tick_l(lw_t *l, uint32_t in) { l->row[l->t % 400] = (uint8_t)in; l->band[in % 3][in % 16] = (int16_t)in; l->t++; uint32_t a = 0; for (int i = 0; i < 400; i += 11) a += l->row[i]; for (int j = 0; j < 16; j++) a += (uint16_t)l->band[1][j]; return a; }
int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    lcg = (uint32_t)atoi(argv[1]); long steps = atol(argv[2]); const int overlay = atoi(argv[3]);
    uint32_t sum = 0; int cur = 0;
    for (long i = 0; i < steps; i++) {
        uint32_t r = rnd();
        if ((r & 15u) == 0) cur = (int)(rnd() % 3u);              /* a meter switch */
        const uint32_t in = rnd();
        if (!overlay) {
            if (cur == 0 && i == 0) { init_c(&REF_C); init_s(&REF_S); init_l(&REF_L); }
            /* the reference re-inits a meter when it is switched TO, exactly like the overlay rule */
        }
        static int last = -1;
        if (cur != last) {
            if (overlay) {
                ovl_claim(cur == 0 ? OVL_CHLADNI : cur == 1 ? OVL_SCOPE : OVL_LAYERED,
#ifdef METER_SKIPS_INIT
                          cur == 2 ? 0 :
#endif
                          cur == 0 ? init_c : cur == 1 ? init_s : init_l);
            } else {
                if (cur == 0) init_c(&REF_C); else if (cur == 1) init_s(&REF_S); else init_l(&REF_L);
            }
            last = cur;
        }
        if (overlay) sum += cur == 0 ? tick_c((chl_t *)ovl_ptr(OVL_CHLADNI), in) : cur == 1 ? tick_s((scope_t *)ovl_ptr(OVL_SCOPE), in) : tick_l((lw_t *)ovl_ptr(OVL_LAYERED), in);
        else         sum += cur == 0 ? tick_c(&REF_C, in) : cur == 1 ? tick_s(&REF_S, in) : tick_l(&REF_L, in);
        sum = sum * 31u + (uint32_t)i;
    }
    printf("%u %u\n", sum, overlay ? OVL.violations : 0u);
    return 0;
}
