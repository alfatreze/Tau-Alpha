/* argv: file. Reads the file as a METR section and prints, for each of the five meters (their real descriptors from the generated header), "id npre def" then each preset "NAME v0 v1 ..." (clamped). */
#include <stdio.h>
#include <stdlib.h>
#include "../fw2/ui/meter.h"
#include "../fw2/generated/meter_presets.h"
#include "../fw2/generated/lw_presets.h"
#include "metr_core.h"
static uint8_t buf[16384];
static void conv(const mpd_t *pd, uint32_t np, metr_p_t *o) { for (uint32_t i = 0; i < np; i++) { o[i].min = pd[i].min; o[i].max = pd[i].max; o[i].kind = pd[i].type; o[i].enum_n = pd[i].nval; o[i].wide = (pd[i].type == 3u || (pd[i].type == 0u && pd[i].max > 255u)); } }
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb"); uint32_t n = (uint32_t)fread(buf, 1, sizeof buf, f); fclose(f);
    printf("check %d\n", metr_check(buf, n));
    if (metr_check(buf, n)) return 0;
    static const struct { uint32_t id; const mpd_t *pd; uint32_t np; } M[5] = { { 12, mpd_bars, MPD_BARS_N }, { 13, mpd_scope, MPD_SCOPE_N }, { 14, mpd_chl, MPD_CHL_N }, { 15, mpd_vu, MPD_VU_N }, { 16, mpd_lw, MPD_LW_N } };
    for (int m = 0; m < 5; m++) {
        metr_p_t ps[40]; conv(M[m].pd, M[m].np, ps); metr_e_t e;
        if (!metr_find(buf, n, M[m].id, ps, M[m].np, &e)) { printf("%u none\n", M[m].id); continue; }
        printf("%u %u %u\n", M[m].id, e.npre, e.def);
        for (uint32_t p = 0; p < e.npre; p++) {
            char nm[16]; uint16_t v[40]; metr_name(buf, &e, p, nm); metr_values(buf, &e, p, ps, M[m].np, v);
            printf("%s", nm); for (uint32_t i = 0; i < M[m].np; i++) printf(" %u", v[i]); printf("\n");
        }
    }
    return 0;
}
