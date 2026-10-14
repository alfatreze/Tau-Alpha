/* B-741 firmware-in-the-loop for sim/tb_pcm_ring_soc.v: the real VexRiscv drives the PCM ring reader through the real mp3_soc, PSRAM arbiter, tau_psram_async controller and
 * strict chip model. It (1) enables the ring, then puts 2500 distinct pairs into a 64-pair ring through the uncached PSRAM window (data first, then the write index), waiting while the
 * ring is full, and also writes a poison value to R_AUDIO every iteration (ignored while the ring is on); (2) stops the FIFO drain (rate 0) so the FIFO stays at the reader's threshold
 * and the ring keeps unread pairs, puts 10 marked pairs and flushes at once (the unread ones must never reach the FIFO); (3) puts 5 more pairs that must be delivered. The testbench
 * checks the stream the FIFO received. */
#include <stdint.h>
#define REG(a)    (*(volatile uint32_t *)(a))
#define CONSOLE   0x80000000u
#define R_AUDIO   0x80000008u
#define R_PCM_ST  0x80000034u
#define R_PCM_RATE 0x80000038u
#define R_RING_BASE 0x80000184u
#define R_RING_SIZE 0x80000188u
#define R_RING_WPTR 0x8000018Cu
#define R_RING_RIDX 0x80000190u
#define R_RING_CTRL 0x80000194u
#define R_RING_CNT  0x80000198u
#define WIN_BASE_WORD 8192u
#define RING 64u
#define WIN ((volatile uint32_t *)(uintptr_t)(0xA4000000u + 4u * WIN_BASE_WORD))
static void putc_(char c) { REG(CONSOLE) = (uint32_t)(uint8_t)c; }
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void putn(uint32_t v) { char b[11]; int i = 0; if (!v) b[i++] = '0'; while (v) { b[i++] = (char)('0' + v % 10u); v /= 10u; } while (i) putc_(b[--i]); }
static uint32_t w;
static void put(uint32_t word)
{
    while (((w + 1u) % RING) == REG(R_RING_RIDX)) { }       /* full: the reader frees a slot when it pushes a pair */
    WIN[w] = word;
    w = (w + 1u) % RING;
    REG(R_RING_WPTR) = w;
}
int main(void)
{
    puts_("RING BOOT\n");
    if (!(REG(R_RING_CTRL) >> 31)) { puts_("RING NOFEATURE\n"); for (;;) { } }
    REG(R_PCM_RATE) = 3435974u;                              /* 48 kHz at 60 MHz */
    REG(R_RING_BASE) = WIN_BASE_WORD; REG(R_RING_SIZE) = RING; REG(R_RING_WPTR) = 0; w = 0;
    REG(R_RING_CTRL) = 1u;
    for (uint32_t n = 0; n < 2500u; n++) {
        put((n * 0x9E3779B1u) ^ 0x5A5A1234u);
        REG(R_AUDIO) = 0xDEADBEEFu;                          /* must be ignored: the FIFO is fed by the ring */
    }
    puts_("RING PUT1 DONE\n");
    REG(R_PCM_RATE) = 0u;                                    /* the FIFO stops draining: it fills to the reader's threshold and the ring keeps unread pairs */
    for (volatile uint32_t d = 0; d < 20000u; d++) { }
    for (uint32_t k = 0; k < 10u; k++) put(0xF1A50000u | k); /* never to be delivered ... */
    REG(R_PCM_ST) = 1u;                                      /* ... the flush drops them (and the FIFO) */
    for (volatile uint32_t d = 0; d < 20000u; d++) { }
    REG(R_PCM_RATE) = 3435974u;
    for (uint32_t k = 0; k < 5u; k++) put(0xC0DE0000u | k);  /* delivered */
    for (volatile uint32_t d = 0; d < 400000u; d++) { }
    puts_("RING CNT "); putn(REG(R_RING_CNT)); putc_('\n');
    puts_("RING END\n");
    for (;;) { }
}
