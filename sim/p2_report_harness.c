/* Builds the Player 2 report record from a fixed counter table with the REAL fw2 code and prints it as hex (sim/test_p2_report.py). The table mirrors the real one in shape:
 * numbers, signed, hex, a long text, units, and enough rows (60) to need a real grid. */
#include <stdio.h>
#include <string.h>
#include "../fw2/diagnostics/report_record.h"
static volatile uint32_t u32[60]; static volatile int32_t neg = -12345; static volatile uint32_t hx = 0x4D50331Au; static volatile uint8_t b8 = 1;
static const char txt[] = "0.6.0-dev.132+abcdef0.dirty FLAC 44.1K";
static cnt_t T[60];
int main(void)
{
    uint32_t n = 0;
    T[n++] = (cnt_t){ "FIRMWARE", CK_STR, CU_NONE, txt };
    T[n++] = (cnt_t){ "CORE VERSION", CK_HEX32, CU_NONE, &hx };
    T[n++] = (cnt_t){ "NEG", CK_I32, CU_PAIRS, &neg };
    T[n++] = (cnt_t){ "FLAG", CK_U8, CU_NONE, &b8 };
    for (int i = 0; n < 60; i++) { u32[i] = 1000u * (uint32_t)(i + 1) + 7u; snprintf(T[n].name, CNT_NAME_MAX, "ROW %02d", i); T[n].kind = CK_U32; T[n].unit = (i % 3) ? CU_PAIRS : CU_MS; T[n].src = &u32[i]; n++; }
    static uint8_t rec[4096];
    uint32_t len = report_record_build(rec, sizeof rec, T, n);
    printf("%u\n", len);
    for (uint32_t i = 0; i < len; i++) printf("%02x", rec[i]);
    printf("\n");
    return 0;
}
