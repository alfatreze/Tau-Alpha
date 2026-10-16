/* Host check of the preset bookkeeping in fw2/ui/meters.c: the modules are stubs that record the last preset they were told. Prints lines "name value". */
#include <stdio.h>
#include <stdint.h>
#include "../fw2/ui/meters.c"
uint32_t host_rd(uint32_t a) { (void)a; return 0; }
void host_wr(uint32_t a, uint32_t v) { (void)a; (void)v; }
uint16_t th_role[TR_COUNT]; uint32_t mp3play_paused_now(void) { return 0; } caps_t caps;
static uint32_t last[8];
#define STUB(m) void meter_##m##_tick(const mtr_in_t *i) { (void)i; }
STUB(winamp_bars) STUB(winamp_scope) STUB(chladni) STUB(layered_wave) STUB(vu_master)
#define PS(m, k) void meter_##m##_preset(uint32_t p) { last[k] = p; } const char *meter_##m##_preset_name(uint32_t p) { (void)p; return #m; }
PS(winamp_bars, 0) PS(winamp_scope, 1) PS(chladni, 2) PS(layered_wave, 3) PS(vu_master, 4)
void gfx_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
int main(void)
{
    meters_init();
    meters_preset_select(3, 6); meters_preset_select(2, 9); meters_preset_select(9, 1);          /* 9 is past Chladni's 4 presets: the first; meter 9 does not exist */
    printf("of3 %u of2 %u last3 %u last2 %u\n", meters_preset_of(3), meters_preset_of(2), last[3], last[2]);
    meters_select(2); for (int i = 0; i < 5; i++) meters_preset_next();
    printf("chladni_after5 %u last2 %u name %s\n", meters_preset_of(2), last[2], meters_preset_name());
    meters_select(4); meters_preset_next(); meters_preset_next(); meters_preset_next();
    printf("vu_wrap %u\n", meters_preset_of(4));
    return 0;
}
