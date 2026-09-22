/* ================================================================== COLD IMAGE (TAU_COLD, Phase G1)
 * Read-only data that is only needed occasionally (meter previews, help text) lives in PSRAM at 0xA4800000 instead of on-chip RAM.
 * It is loaded at boot from the data slot file tau-cold.bin (tools/pack_cold.py), checked (size, layout id, CRC) and proven
 * before first use. If anything is wrong the cold features switch OFF (the previews and help show a plain fallback); playback
 * is unaffected. */
#if TAU_COLD
#include "cold_core.h"

#define COLD_SLOT_ID 6u
extern char _cold_start, _cold_end, _cold_img_size;   /* _cold_img_size: data + padding + code, as one image */
/* Patched into the ROM by tools/pack_cold.py: an image built for another layout is refused. */
static volatile uint32_t tau_cold_id = 0xC01DC01Du;
static uint8_t  cold_state;         /* 0 not tried, 1 loaded, 2 off */
#define COLD_E_PROBE     17u        /* the cold-code probe call did not return the expected value */
#define COLD_E_NOFETCH   18u        /* the bitstream has no instruction fetch from PSRAM */
static uint8_t  cold_err;
static uint32_t cold_ms, cold_bytes;

static int cold_rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t len)
{
    (void)ctx; (void)dst;
    return target_read_slot(COLD_SLOT_ID, off, TAG_OFF, len);
}

#if TAU_COLD_CODE
/* Cold CODE (Phase G2/G4): functions marked COLD_TEXT are linked at the instruction alias 0x24xx_xxxx and run from PSRAM through
 * the I-cache. They may only be called after cold_code_ok is set (see cold_boot_load); hot code reaches them through
 * COLD_READY() checks, never blindly. Nothing that runs per audio frame may be cold. */
#define COLD_TEXT __attribute__((section(".cold_text"), noinline))
static uint8_t cold_code_ok;
#define COLD_READY() (cold_code_ok != 0u)
COLD_TEXT static uint32_t cold_probe(uint32_t a, uint32_t b) { return a * b + 1u; }

#if TAU_DIAG_TESTS
/* Diagnostic Build: functions for Diagnostics > Tests > COLD CODE TEST (same shapes as the simulation firmware sim/fw_ifetch). */
static uint32_t cold_hot_twice(uint32_t x) { return x * 2u; }        /* BRAM: cold code calls back into hot code */
COLD_TEXT static uint32_t cold_add(uint32_t a, uint32_t b) { return a + b; }
COLD_TEXT static uint32_t cold_loop(uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 1; i <= n; i++) s += i * 3u;
    return s;
}
COLD_TEXT static uint32_t cold_calls_hot(uint32_t x) { return cold_hot_twice(x) + 1u; }
/* 3,000 instructions (12 KB): three times the 4 KiB I-cache, so every run refills all of it. */
__asm__(".pushsection .cold_text,\"ax\",@progbits\n"
        ".globl cold_big\n.type cold_big,@function\ncold_big:\n"
        ".rept 3000\n addi a0, a0, 1\n.endr\n ret\n.size cold_big, .-cold_big\n.popsection\n");
extern uint32_t cold_big(uint32_t x);
#endif
#endif

static int cold_psram_prove(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG(0x80000088u) != 0x50535231u) return 0;
    if (!((REG(0x800000A0u) >> 16) & 1u)) return 0;
    volatile uint32_t *w0 = (volatile uint32_t *)&_cold_start;
    volatile uint32_t *w1 = (volatile uint32_t *)((uintptr_t)&_cold_start + (uintptr_t)&_cold_img_size - 4u);
    for (uint32_t k = 0; k < 2u; k++) {
        *w0 = pat[k]; *w1 = ~pat[k];
        if (*w0 != pat[k] || *w1 != ~pat[k]) return 0;
    }
    return 1;
}

static void cold_boot_load(void)
{
    cold_state = 2u; cold_err = 0u;
    uint32_t size = (uint32_t)(uintptr_t)&_cold_img_size;
    if (!size) { cold_state = 1u; return; }                          /* nothing cold in this build */
    if (!target_read_slot(COLD_SLOT_ID, 0u, TAG_OFF, 1u)) { cold_err = COLD_E_NOFILE; return; }
    if (TAG_SIZE < LIB_WIN) { cold_err = COLD_E_SIZE; return; }
    uint32_t t0 = cycles();
    int e = cold_psram_prove() ? cold_load(cold_rd, 0, tagbuf, (volatile uint8_t *)&_cold_start, 0x100000u, size, tau_cold_id)
                               : COLD_E_PSRAM;
    cold_ms = (cycles() - t0) / (CLK_HZ / 1000u);
    if (e) { cold_err = (uint8_t)e; return; }
    cold_bytes = size;
    cold_state = 1u;
#if TAU_COLD_CODE
    /* Cold CODE: only when the bitstream has instruction fetch from PSRAM (IF_CFG bit 0), and only after a probe call has
     * come back right. A bitstream without the feature would fetch the alias as an aliased copy of RAM. */
    if (!(REG(0x800000B8u) & 1u)) { cold_err = COLD_E_NOFETCH; return; }
    if (cold_probe(6u, 7u) != 43u) { cold_err = COLD_E_PROBE; return; }
    cold_code_ok = 1u;
#endif
}
#define cold_ready() (cold_state == 1u)
#else
#define cold_ready() 1
#endif
#if !TAU_COLD_CODE
#define COLD_READY() 0
#endif
