/* Host harness for fw2/cores/counters_core.h: a fixed registry over real variables; prints the exported entries and some lookups (sim/test_p2_counters.py). */
#include <stdio.h>
#include <string.h>
#include "counters_core.h"
static volatile uint8_t  c_u8 = 200;
static volatile uint16_t c_u16 = 65535;
static volatile uint32_t c_u32 = 3000000000u;
static volatile int32_t  c_i32 = -2147483647 - 1;
static volatile uint32_t c_long = 4294967295u;
static volatile uint32_t c_pairs = 48672;
static volatile uint32_t c_last = 7;
static volatile uint32_t c_hex = 0x4D50331Au;
static const char c_text[] = "0.6.0-dev.126+b31f522.dirty FLAC 44.1K";   /* 38 characters */
static const cnt_t T[] = {
    { "RING FILL",     CK_U32, CU_PAIRS,   &c_pairs },
    { "A8",            CK_U8,  CU_NONE,    &c_u8 },
    { "B16",           CK_U16, CU_NONE,    &c_u16 },
    { "BIG U32",       CK_U32, CU_NONE,    &c_u32 },
    { "NEG I32",       CK_I32, CU_NONE,    &c_i32 },
    { "LONG VALUE",    CK_U32, CU_PERMILLE, &c_long },
    { "LAST",          CK_U32, CU_MS,      &c_last },
    { "TEXT",          CK_STR, CU_NONE,    c_text },
    { "CORE VERSION",  CK_HEX32, CU_NONE,  &c_hex },
};
static void emit(void *ctx, const uint8_t *e, uint32_t n)
{
    (void)ctx; printf("E %u ", e[0]);
    const uint8_t *z = memchr(e + 1, 0, n - 1); printf("%.*s|%.*s\n", (int)(z - e - 1), e + 1, (int)(n - (z - e) - 1), z + 1);
}
int main(void)
{
    printf("N %u\n", cnt_export(T, sizeof T / sizeof T[0], emit, 0));
    printf("F %d %d %d\n", cnt_find(T, 9, "LAST"), cnt_find(T, 9, "LA"), cnt_find(T, 9, "NOPE"));
    return 0;
}
