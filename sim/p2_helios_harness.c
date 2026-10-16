/* Host harness for fw2/cores/helios_core.h. Commands on stdin:
 *   S vc y0 y1      -> prints 1/0: hl_rows_safe(beam_ok=1, vc, y0, y1)
 *   N y0 y1 vc      -> prints 1/0 with no beam counter in the bitstream (always 1)
 *   W idx count up  -> hl_wrap
 *   R               -> region scenario, prints a trace */
#include <stdio.h>
#include "helios_core.h"
static int log_n, log_[64];
static void r0(void) { log_[log_n++] = 0; }
static void r1(void) { log_[log_n++] = 1; }
static void r2(void) { log_[log_n++] = 2; }
static hl_t h;
static uint32_t cur_vc = 0; static int cur_beam = 1;
static int safe(void *c, uint32_t y0, uint32_t y1) { (void)c; return hl_rows_safe(cur_beam, cur_vc, y0, y1); }
static void flush_print(const char *tag, uint32_t vc)
{
    cur_vc = vc; log_n = 0; uint32_t n = hl_flush(&h, safe, 0);
    printf("%s n=%u [", tag, n);
    for (int i = 0; i < log_n; i++) printf("%d%s", log_[i], i + 1 < log_n ? "," : "");
    printf("] drawn=%u deferred=%u\n", h.drawn, h.deferred);
}
int main(void)
{
    char c; unsigned a, b, d;
    while (scanf(" %c", &c) == 1) {
        if (c == 'S') { if (scanf("%u %u %u", &a, &b, &d) == 3) printf("%d\n", hl_rows_safe(1, a, b, d)); }
        else if (c == 'N') { if (scanf("%u %u %u", &a, &b, &d) == 3) printf("%d\n", hl_rows_safe(0, d, a, b)); }
        else if (c == 'W') { if (scanf("%u %u %u", &a, &b, &d) == 3) printf("%u\n", hl_wrap(a, b, (int)d)); }
        else if (c == 'R') {
            hl_t z = {0}; h = z;
            uint8_t i0 = hl_add(&h, r0, 0, 100), i1 = hl_add(&h, r1, 200, 300), i2 = hl_add(&h, r2, 0, HL_WHOLE);
            printf("ids %u %u %u\n", i0, i1, i2);
            flush_print("clean", 10);
            hl_dirty(&h, i0); hl_dirty(&h, i1); hl_dirty(&h, i2);
            flush_print("beam in region 1 (row 250)", HL_VOFF + 250);
            flush_print("beam moved past region 1", HL_VOFF + 320);
            flush_print("again", HL_VOFF + 320);
            hl_dirty_mask(&h, 0x5u);
            flush_print("mask 0b101 in blanking", 2);
            for (int k = 0; k < 20; k++) hl_add(&h, r0, 0, 1);
            printf("full %u\n", hl_add(&h, r0, 0, 1));
        }
    }
    return 0;
}
