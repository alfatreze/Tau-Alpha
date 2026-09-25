/* Native harness for fw/chladni_core.h (sim/test_chladni_core.py). Not part of any firmware build.
 *   plane Rx Ry hw_q8 K m n w s ...        one tile of levels, one line per row
 *   run <preset 0|1> [seconds-per-line]    stdin: 16 band levels per meter tick; prints one line per figure update:
 *                                          tick trig K rects modes...   (rects = draw commands for the tile) */
#define CHL_COUNT 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../fw/chladni_core.h"

static uint32_t g_rects; static uint32_t g_rx;
static void count_cb(uint32_t j, const uint8_t *lvl, void *ud)
{
    (void)j; (void)ud;
    uint32_t i = 0;
    while (i < g_rx) {
        if (!lvl[i]) { i++; continue; }
        uint8_t v = lvl[i]; while (i < g_rx && lvl[i] == v) i++;
        g_rects++;
    }
}
static void print_cb(uint32_t j, const uint8_t *lvl, void *ud)
{
    (void)j; (void)ud;
    for (uint32_t i = 0; i < g_rx; i++) putchar('0' + lvl[i]);
    putchar('\n');
}

int main(int argc, char **argv)
{
    static int16_t ring[3 * CHL_MAX_RX], half[CHL_HALF_N], cxm[CHL_MAX_K * CHL_MAX_RX], cxn[CHL_MAX_K * CHL_MAX_RX];
    static uint8_t row[CHL_MAX_RX];
    if (argc >= 2 && (!strcmp(argv[1], "plane") || !strcmp(argv[1], "planefold"))) {
        int fold = !strcmp(argv[1], "planefold");
        uint32_t Rx = atoi(argv[2]), Ry = atoi(argv[3]), hw = atoi(argv[4]), K = atoi(argv[5]);
        chl_mode_t md[CHL_MAX_K];
        for (uint32_t k = 0; k < K; k++) {
            md[k].m = atoi(argv[6 + 4 * k]); md[k].n = atoi(argv[7 + 4 * k]);
            md[k].w = atoi(argv[8 + 4 * k]); md[k].s = atoi(argv[9 + 4 * k]);
        }
        g_rx = Rx;
        chl_macs = 0;
        chl_render(md, K, Rx, Ry, hw, ring, fold ? half : 0, cxm, cxn, row, print_cb, 0);
        fprintf(stderr, "macs %u\n", chl_macs);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "cos")) {
        for (uint32_t i = 0; i < 1024; i++) printf("%d\n", (int)chl_cos(i));
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "field")) {          /* field Rx Ry K m n w s ...: raw Q14 rows */
        uint32_t Rx = atoi(argv[2]), Ry = atoi(argv[3]), K = atoi(argv[4]);
        chl_mode_t md[CHL_MAX_K];
        for (uint32_t k = 0; k < K; k++) {
            md[k].m = atoi(argv[5 + 4 * k]); md[k].n = atoi(argv[6 + 4 * k]);
            md[k].w = atoi(argv[7 + 4 * k]); md[k].s = atoi(argv[8 + 4 * k]);
            for (uint32_t i = 0; i < Rx; i++) {
                cxm[k * Rx + i] = (int16_t)chl_cos(chl_phase(md[k].m, Rx, i));
                cxn[k * Rx + i] = (int16_t)chl_cos(chl_phase(md[k].n, Rx, i));
            }
        }
        for (uint32_t j = 0; j < Ry; j++) {
            chl_field_row(md, K, j, Ry, Rx, Rx, cxm, cxn, ring);
            for (uint32_t i = 0; i < Rx; i++) printf("%d%c", ring[i], i + 1 < Rx ? ' ' : '\n');
        }
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "run")) {
        const chl_preset_t *p = &chl_presets[atoi(argv[2]) % CHL_PRESET_N];
        chl_state_t st; chl_init(&st, &p->c);
        g_rx = p->Rx;
        unsigned lv[16]; uint8_t l8[16];
        uint32_t tick = 0, upd_ms = 0, ms = 0, since = 0;
        while (1) {
            int ok = 1;
            for (int b = 0; b < 16; b++) { if (scanf("%u", &lv[b]) != 1) { ok = 0; break; } l8[b] = lv[b] > 255 ? 255 : lv[b]; }
            if (!ok) break;
            ms = tick * 26u + (tick / 3u);           /* 26.3 ms ticks */
            int trig = chl_detect(&st, &p->c, l8, ms);
            since++;
            if (since >= p->div) {
                since = 0;
                uint32_t dt = ms - upd_ms; upd_ms = ms;
                chl_update(&st, &p->c, l8, dt ? dt : 1);
                chl_mode_t md[CHL_MAX_K]; uint32_t K = chl_select(&st, &p->c, md);
                g_rects = 0;
                chl_macs = 0;
                chl_render(md, K, p->Rx, p->Ry, chl_hw_q8(&st, &p->c, p->Rx), ring, p->c.fold ? half : 0, cxm, cxn, row, count_cb, 0);
                printf("%u %d %u %u %u", tick, trig, K, g_rects, chl_macs);
                for (uint32_t k = 0; k < K; k++) printf(" %u,%u,%d,%d", md[k].m, md[k].n, md[k].w, md[k].s);
                putchar('\n');
            } else if (trig) printf("%u 1 0 0 trig-between-updates\n", tick);
            tick++;
        }
        return 0;
    }
    fprintf(stderr, "usage\n");
    return 2;
}
