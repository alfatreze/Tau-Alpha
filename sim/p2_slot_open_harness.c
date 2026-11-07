/* Host model of the APF target channel around fw2/stream/slot.c (the real file, compiled in): what slot_open_path() hands to APF for a path (BUG-001, docs/issues/001).
 * stdin: the 0190 response template as 256 hex bytes (what APF would write for a slot that already holds a file), then the path as hex bytes. stdout: "rc <return> err <slot_err>", then the 0192
 * parameter words the firmware wrote, as 256 hex bytes. The model completes every command at once; TGT_GET writes the template into the response area, TGT_OPEN just records the parameters. */
#define P2_HOST 1
#include <stdint.h>
static uint32_t reg_rd(uint32_t a); static void reg_wr(uint32_t a, uint32_t v);
#define REG_RD(a) reg_rd(a)
#define REG_WR(a, v) reg_wr((a), (v))
#include "../fw2/platform/mmio.h"
#include <stdio.h>
#include <string.h>
static uint32_t dt[512], dt_addr, tgt_seq, tgt_id, cyc, tpl[64];
static uint32_t reg_rd(uint32_t a) { if (a == R_CYCLES) return cyc += 100; if (a == R_DT_DATA) return dt[dt_addr & 511u]; if (a == R_TGT_GO) return (tgt_seq & 0xFFu) << 8; return 0; }
static void reg_wr(uint32_t a, uint32_t v)
{
    if (a == R_DT_ADDR) dt_addr = v; else if (a == R_DT_DATA) dt[dt_addr & 511u] = v;
    else if (a == R_TGT_ID) tgt_id = v;
    else if (a == R_TGT_GO) { if (v == 2u) memcpy(&dt[64], tpl, sizeof tpl); tgt_seq++; }
}
#include "../fw2/stream/slot.c"
static int hexin(uint8_t *out, int max) { int n = 0; unsigned v; while (n < max && scanf("%2x", &v) == 1) out[n++] = (uint8_t)v; return n; }
int main(void)
{
    uint8_t t[256], p[512]; char path[512];
    if (hexin(t, 256) != 256) return 2;
    for (int w = 0; w < 64; w++) tpl[w] = ((uint32_t)t[w*4] << 24) | ((uint32_t)t[w*4+1] << 16) | ((uint32_t)t[w*4+2] << 8) | t[w*4+3];
    int n = hexin(p, 511); memcpy(path, p, (size_t)n); path[n] = 0;
    int rc = slot_open_path(SLOT_MEDIA, path);
    printf("rc %d err %u\n", rc, slot_err);
    for (int w = 0; w < 64; w++) { uint32_t v = dt[128 + w]; printf("%02x%02x%02x%02x", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255); }
    printf("\n");
    return 0;
}
