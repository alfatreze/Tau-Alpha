/* Host check of the preset bookkeeping in fw2/ui/meters.c: the modules are stubs that record the last preset they were told. Prints lines "name value". */
#include <stdio.h>
#include <stdint.h>
static uint32_t psram_mem[2048];
#define METR_PSRAM psram_mem
#include "../fw2/ui/meters.c"
uint32_t host_rd(uint32_t a) { (void)a; return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } uint32_t mp3play_degrade_now(void) { return 0; } caps_t caps;
static uint32_t last[METER_N];
#define IDX_X(key, nm, id, lc, UC, np, cmp) idx_##key,
enum { METER_LIST(IDX_X) };
#define STUB_X(key, nm, id, lc, UC, np, cmp) \
    void meter_##key##_tick(const mtr_in_t *i) { (void)i; } uint16_t meter_##key##_mv[MPD_##UC##_N]; void meter_##key##_apply(void) { } \
    void meter_##key##_preset(uint32_t p) { last[idx_##key] = p; } const char *meter_##key##_preset_name(uint32_t p) { (void)p; return #key; }
METER_LIST(STUB_X)
int gfx_compose_begin(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; return 0; }
void gfx_compose_present(uint32_t x, uint32_t y, uint32_t w, uint32_t h) { (void)x; (void)y; (void)w; (void)h; }
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static uint8_t fbuf[8192];
int main(int argc, char **argv)
{
    meters_init();
    if (argc > 1) {                                                   /* METR mode: load the file, then print every user preset of every meter: "id npre name v0 v1 ..." */
        FILE *f = fopen(argv[1], "rb"); uint32_t n = (uint32_t)fread(fbuf, 1, sizeof fbuf, f); fclose(f);
        printf("loaded %u\n", meters_metr_load(fbuf, n));
        for (uint32_t m = 0; m < meter_count; m++) {
            printf("meter %u npre %u boot %u\n", m, meters_npre(m), meters_preset_of(m));
            for (uint32_t p = 0; p < meters_npre(m); p++) {
                meters_select(m); meters_preset_select(m, p);
                printf("%s", meters_preset_name()); for (uint32_t i = 0; i < meter_table[m].np; i++) printf(" %u", meter_table[m].mv[i]); printf("\n");
            }
        }
        return 0;
    }
    meters_preset_select(3, 6); meters_preset_select(2, 9); meters_preset_select(9, 1);          /* 9 is past Chladni's 4 presets: the first; meter 9 does not exist */
    printf("of3 %u of2 %u last3 %u last2 %u\n", meters_preset_of(3), meters_preset_of(2), last[3], last[2]);
    meters_select(2); for (int i = 0; i < 5; i++) meters_preset_next();
    printf("chladni_after5 %u last2 %u name %s\n", meters_preset_of(2), last[2], meters_preset_name());
    meters_select(4); meters_preset_next(); meters_preset_next(); meters_preset_next();
    printf("vu_wrap %u\n", meters_preset_of(4));
    return 0;
}
