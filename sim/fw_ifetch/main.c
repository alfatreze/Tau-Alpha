/* Phase G2 firmware-in-the-loop test: run code from PSRAM through the instruction alias on the real VexRiscv. */
#include <stdint.h>

#define REG(a)        (*(volatile uint32_t *)(a))
#define CONSOLE       0x80000000u
#define R_CYCLES      0x8000000Cu
#define IF_N          0x800000B0u      /* instruction beats served from PSRAM  */
#define IF_CYC        0x800000B4u      /* cycles the fetch stage waited        */
#define IF_CFG        0x800000B8u      /* bit0 feature present, write = clear  */
#define PS_ID         0x80000088u
#define WIN           ((volatile uint32_t *)0xA4800000u)   /* data window onto the cold region */
#define COLD          __attribute__((section(".cold"), noinline))

static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void hex_(uint32_t v) { for (int i = 28; i >= 0; i -= 4) putc_("0123456789ABCDEF"[(v >> i) & 15]); }
static void line_(const char *tag, uint32_t v) { puts_(tag); putc_(' '); hex_(v); putc_('\n'); }

extern char _cold_vma, _cold_end;
extern uint32_t _cold_lma[];

/* ---- cold functions (linked at 0x2480_0000) ------------------------------------------------------------------ */
uint32_t hot_twice(uint32_t x) { return x * 2u; }          /* BRAM: cold code calls back into hot code */

COLD uint32_t cold_add(uint32_t a, uint32_t b) { return a + b; }

COLD uint32_t cold_loop(uint32_t n)                        /* small loop: fits the I-cache after the first fills */
{
    uint32_t s = 0;
    for (uint32_t i = 1; i <= n; i++) s += i * 3u;
    return s;
}

COLD uint32_t cold_calls_hot(uint32_t x) { return hot_twice(x) + 1u; }

COLD uint32_t cold_mixed(uint32_t n)                       /* data-window reads between instruction fetches (arbitration) */
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += WIN[0x2000u + (i & 63u)] ^ i;
    return s;
}

/* About 12 KiB of straight-line code: three times the I-cache, so every run refills. */
__asm__(".section .cold,\"ax\",@progbits\n"
        ".globl cold_big\n.type cold_big,@function\ncold_big:\n"
        ".rept 3000\n addi a0, a0, 1\n.endr\n ret\n.size cold_big, .-cold_big\n.text\n");
extern uint32_t cold_big(uint32_t x);

/* Straight-line code with a PSRAM data-window load every 9 instructions: instruction line fills and data loads compete for the
 * controller all the way through (the pipeline prefetches the next line while a load is in flight). a2 = window base. */
__asm__(".section .cold,\"ax\",@progbits\n"
        ".globl cold_interleave\n.type cold_interleave,@function\ncold_interleave:\n"
        ".rept 500\n lw a3, 0(a1)\n add a0, a0, a3\n addi a1, a1, 4\n nop\n nop\n nop\n nop\n nop\n nop\n.endr\n ret\n"
        ".size cold_interleave, .-cold_interleave\n.text\n");
extern uint32_t cold_interleave(uint32_t acc, const volatile uint32_t *p);

int main(void)
{
    uint32_t fails = 0;
    puts_("G2 START\n");
    line_("CFG", REG(IF_CFG));
    if (!(REG(IF_CFG) & 1u) || REG(PS_ID) != 0x50535231u) { puts_("G2 NOFEATURE\n"); return 1; }

    /* load the cold image into PSRAM through the data window (uncached, word stores) */
    volatile uint32_t *d = WIN;
    const uint32_t *s = _cold_lma;
    uint32_t words = ((uint32_t)(uintptr_t)&_cold_end - (uint32_t)(uintptr_t)&_cold_vma) / 4u;
    for (uint32_t i = 0; i < words; i++) d[i] = s[i];
    for (uint32_t i = 0; i < words; i++) if (d[i] != s[i]) fails++;         /* read back through the data window */
    line_("WORDS", words);
    line_("READBACK_FAILS", fails);

    /* data words the mixed test reads */
    for (uint32_t i = 0; i < 64u; i++) WIN[0x2000u + i] = 0x9E3779B9u * (i + 1u);
    for (uint32_t i = 0; i < 500u; i++) WIN[0x2100u + i] = 0x01000193u * (i + 7u);

    REG(IF_CFG) = 1u;                                         /* clear the counters */
    uint32_t r;
    uint32_t n0 = REG(IF_N);
    r = cold_add(40u, 2u);            line_("ADD", r);            if (r != 42u) fails++;
    uint32_t n1 = REG(IF_N);
    line_("BEATS_FIRST_CALL", n1 - n0);                           /* at least one 8-beat line fill */
    if (n1 - n0 < 8u) fails++;
    r = cold_loop(100u);              line_("LOOP", r);           if (r != 3u * 5050u) fails++;
    r = cold_calls_hot(21u);          line_("CALLSHOT", r);       if (r != 43u) fails++;
    uint32_t nb = REG(IF_N);
    r = cold_add(1u, 1u);             if (r != 2u) fails++;       /* cached now: no new fills */
    uint32_t na = REG(IF_N);
    line_("BEATS_CACHED_CALL", na - nb);
    if (na != nb) fails++;
    uint32_t exp = 0;
    for (uint32_t i = 0; i < 200u; i++) exp += (0x9E3779B9u * ((i & 63u) + 1u)) ^ i;
    r = cold_mixed(200u);             line_("MIXED", r);          if (r != exp) fails++;
    {   /* 500 window loads interleaved with 4.5 KB of cold code */
        uint32_t want = 5u;
        for (uint32_t i = 0; i < 500u; i++) want += WIN[0x2100u + i];
        uint32_t got = cold_interleave(5u, &WIN[0x2100u]);
        line_("INTERLEAVE", got);
        if (got != want) fails++;
    }
    uint32_t nbig0 = REG(IF_N);
    uint32_t t0 = REG(R_CYCLES);
    r = cold_big(7u);                 line_("BIG1", r);           if (r != 3007u) fails++;
    uint32_t t1 = REG(R_CYCLES);
    uint32_t nbig1 = REG(IF_N);
    r = cold_big(9u);                 line_("BIG2", r);           if (r != 3009u) fails++;
    uint32_t t2 = REG(R_CYCLES);
    uint32_t nbig2 = REG(IF_N);
    line_("BIG1_BEATS", nbig1 - nbig0);                           /* about 12 KiB / 4 = 3000 words */
    line_("BIG2_BEATS", nbig2 - nbig1);
    line_("BIG1_CYCLES", t1 - t0);
    line_("BIG2_CYCLES", t2 - t1);
    if (nbig1 - nbig0 < 2900u || nbig2 - nbig1 < 2900u) fails++;  /* the cache cannot hold it: both runs refill */
    line_("IF_CYC", REG(IF_CYC));
    line_("FAILS", fails);
    puts_(fails ? "G2 FAIL\n" : "G2 PASS\n");
    return 0;
}
