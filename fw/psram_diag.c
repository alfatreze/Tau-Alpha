// Tau PSRAM diagnostic firmware (P2, B-004).
//
// Developer-only ROM, paired with an RBF built with TAU_PSRAM_PROBE. It talks to
// the mailbox in src/fpga/core/tau_psram_probe.sv (MMIO 0x88..0xA8, see
// docs/MMIO_ALLOCATION.md); it never touches the SDRAM CPU window, the player
// data, or any card file. Per die (4 dies: chip = die>>1, die select = die&1):
//   1. address lines   one word per address bit, written then all read back
//   2. byte lanes      full word, lane masks, no-lane write
//   3. hash fill       2^PSRAM_FILL_LOG2 words (1 MiB by default) write, verify,
//                      CRC of the data read back (independently reproducible)
//   4. anchors         fixed words incl. the last legal word before the guard
//   5. guard word      read and write of 3FFFFFh's CPU word must be REFUSED with
//                      no chip access; the sticky guard flag must set
// Window mode (B-016): L1 runs the same tests through the CPU's uncached PSRAM window
// (loads and stores at 0xA4000000..), cross-checks it against the mailbox, measures the
// per-access cost and records a PSW1 record; R1 repeats that as a soak until a mode key
// is pressed.
// A = run at default timing, X = read sample +1 clock, Y = read sample +2 clocks,
// B = run with the slow-timing dials (+3 read/+3 write clocks). The read-sample index
// of the build (T_ACC, read back from PS_CFG[15:8]) plus the read extra is the
// effective index, shown and recorded (margin experiment, B-012). The result is drawn and published through interact.json (16 words,
// 31-bit encoding, decode with tools/decode_tau_diag_log.py --interact --psram).

#include <stdint.h>
#include "font_metrics.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define R_CYCLES     0x8000000Cu
#define R_INPUT      0x8000003Cu
#define R_VERSION    0x80000040u
#define R_FB_ADDR    0x80000048u
#define R_FB_SIZE    0x8000004Cu
#define R_FB_COLOR   0x80000050u
#define R_FB_GO      0x80000054u
#define R_SET_IDX    0x8000006Cu
#define R_SET_DAT    0x80000070u
#define PS_ID        0x80000088u
#define PS_ADDR      0x8000008Cu
#define PS_WDATA     0x80000090u
#define PS_CTRL      0x80000094u
#define PS_STATUS    0x80000098u
#define PS_RDATA     0x8000009Cu
#define PS_CFG       0x800000A0u
#define PS_LAST      0x800000A4u
#define PS_COUNT     0x800000A8u
#define PS_WCOUNT    0x800000ACu
#define WIN_BASE     0xA4000000u        /* uncached PSRAM CPU window */
#define EXPECT_PS_WID 0x50535731u       /* "PSW1": window record magic */

#define EXPECT_VERSION 0x4D503317u   /* unchanged: the expansion window is additive */
#define EXPECT_PS_ID   0x50535231u

#ifndef CLK_HZ
#define CLK_HZ 60000000u
#endif
#ifndef PSRAM_FILL_LOG2
#define PSRAM_FILL_LOG2 18u          /* 2^18 words = 1 MiB per die */
#endif
#ifndef PSRAM_WARMUP
#define PSRAM_WARMUP (CLK_HZ / 2u)
#endif
#ifndef PSRAM_PUBLISH_WAIT
#define PSRAM_PUBLISH_WAIT (CLK_HZ / 100u)
#endif
#define PS_TIMEOUT (CLK_HZ / 100u)   /* 10 ms: an op takes well under 1 us */
#define FILL_WORDS (1u << PSRAM_FILL_LOG2)

#define GUARD_OFF   0x1FFFFFu        /* CPU-word offset inside a die that holds 3FFFFFh */
#define LAST_LEGAL  0x1FFFFEu

#define ST_BUSY     (1u << 0)
#define ST_DONE     (1u << 1)
#define ST_TIMEOUT  (1u << 2)
#define ST_GUARD    (1u << 3)
#define ST_CE       (1u << 4)
#define ST_GHIT     (1u << 5)
#define ST_WLO      (1u << 6)
#define ST_WHI      (1u << 7)

#define FB_W      400u
#define FB_H      360u
#define FB_STRIDE 512u
#define FB_OP_RECT 1u
#define FB_OP_CHAR 2u
#define UI_BG     0x0862u
#define UI_PANEL  0x2945u
#define UI_WHITE  0xFFFFu
#define UI_DIM    0x94B2u
#define UI_ACCENT 0xFC65u
#define UI_GREEN  0x4F49u
#define UI_RED    0xF800u
#define KEY_A (1u << 4)
#define KEY_B (1u << 5)
#define KEY_X (1u << 6)
#define KEY_Y (1u << 7)
#define KEY_L1 (1u << 8)
#define KEY_R1 (1u << 9)

static uint32_t fb_color_shadow = 0xFFFFFFFFu;
static uint32_t timed_out, checks, fails, run_no, slow_mode, run_mode, t_acc;
static uint32_t die_fail[4], die_crc[4], die_checks[4];
static uint32_t first_word, first_exp, first_act, first_test, have_first;
static uint32_t end_status, end_count, last_reg, last_rdata;
static uint32_t words[16];
static uint32_t via_win, win_present, chain, cross_fails, cum_checks, cum_fails, passes, win_guard_ok;
static uint32_t rd_min, rd_avg, rd_max, wr_min, wr_avg, wr_max, win_ops;

static inline uint32_t cycles(void) { return REG(R_CYCLES); }
static inline void fb_wait(void) { while (REG(R_FB_GO) & 1u) { } }

static void delay(uint32_t n)
{
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < n) { }
}

static void fb_set_color(uint16_t fg, uint16_t bg)
{
    uint32_t value = ((uint32_t)bg << 16) | fg;
    if (value != fb_color_shadow) { REG(R_FB_COLOR) = value; fb_color_shadow = value; }
}

static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    if (!w || !h) return;
    fb_wait();
    REG(R_FB_ADDR) = y * FB_STRIDE + x;
    REG(R_FB_SIZE) = (h << 9) | w;
    fb_set_color(color, color);
    REG(R_FB_GO) = FB_OP_RECT;
}

static uint32_t glyph(char ch)
{
    uint32_t c = (unsigned char)ch;
    return (c < FONT_FIRST || c > FONT_LAST) ? (uint32_t)' ' : c;
}
static uint32_t advance(char ch) { return font_adv[glyph(ch) - FONT_FIRST]; }

static void fb_text(uint32_t x, uint32_t y, const char *text, uint16_t fg, uint16_t bg)
{
    fb_set_color(fg, bg);
    while (*text && x + FONT_CELL_W <= FB_W) {
        fb_wait();
        REG(R_FB_ADDR) = y * FB_STRIDE + x;
        REG(R_FB_GO) = FB_OP_CHAR | ((glyph(*text) & 0x7Fu) << 3);
        x += advance(*text++);
    }
}

static char *put_s(char *o, const char *s) { while (*s) *o++ = *s++; *o = 0; return o; }
static char *put_hex(char *o, uint32_t v, uint32_t digits)
{
    for (uint32_t i = 0; i < digits; ++i) {
        uint32_t n = (v >> ((digits - 1u - i) * 4u)) & 15u;
        *o++ = (char)(n < 10u ? '0' + n : 'A' + n - 10u);
    }
    *o = 0; return o;
}
static char *put_dec(char *o, uint32_t v)
{
    char r[10]; uint32_t n = 0;
    do { r[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *o++ = r[--n];
    *o = 0; return o;
}

/* ---- deterministic data: reproducible in Python (tools/decode_tau_diag_log.py) */
static uint32_t hash_word(uint32_t a)
{
    uint32_t x = a * 0x9E3779B1u + 0x7F4A7C15u;
    x ^= x >> 15; x *= 0x85EBCA6Bu; x ^= x >> 13;
    return x;
}
static uint32_t crc_step(uint32_t crc, uint32_t w)
{
    crc ^= w;
    return ((crc << 5) | (crc >> 27)) ^ 0x9E3779B9u;
}

/* ---- mailbox access ---------------------------------------------------- */
static int ps_wait(void)
{
    uint32_t t = cycles();
    while (REG(PS_STATUS) & ST_BUSY) {
        if ((uint32_t)(cycles() - t) > PS_TIMEOUT) { timed_out = 1u; return 0; }
    }
    return 1;
}
static int ps_op(uint32_t we, uint32_t word, uint32_t data, uint32_t be)
{
    if (!ps_wait()) return 0;
    REG(PS_ADDR) = word;
    REG(PS_WDATA) = data;
    REG(PS_CTRL) = 1u | (we << 1) | (be << 2);
    return ps_wait();
}
static int ps_write(uint32_t word, uint32_t data) { return ps_op(1u, word, data, 0xFu); }
static int ps_read(uint32_t word, uint32_t *v)
{
    if (!ps_op(0u, word, 0u, 0xFu)) return 0;
    *v = REG(PS_RDATA);
    return 1;
}

/* ---- access shims: the same tests run through the mailbox or the CPU window ---- */
static inline volatile uint32_t *winp(uint32_t word)
{
    return (volatile uint32_t *)(uintptr_t)(WIN_BASE + (word << 2));
}
static int acc_write(uint32_t word, uint32_t data)
{
    if (!via_win) return ps_write(word, data);
    *winp(word) = data;
    return 1;
}
static int acc_read(uint32_t word, uint32_t *v)
{
    if (!via_win) return ps_read(word, v);
    *v = *winp(word);
    return 1;
}
/* Byte-lane write. Through the window each enabled lane is its own byte store. */
static void acc_write_be(uint32_t word, uint32_t data, uint32_t be)
{
    if (!via_win) { (void)ps_op(1u, word, data, be); return; }
    if (be == 0xFu) { *winp(word) = data; return; }
    for (uint32_t i = 0; i < 4u; ++i)
        if (be & (1u << i))
            *(volatile uint8_t *)(uintptr_t)(WIN_BASE + (word << 2) + i) = (uint8_t)(data >> (8u * i));
}

static void note_fail(uint32_t die, uint32_t test, uint32_t word, uint32_t exp, uint32_t act)
{
    fails++; die_fail[die]++;
    if (!have_first) {
        have_first = 1u; first_test = test; first_word = word; first_exp = exp; first_act = act;
    }
}
static void expect(uint32_t die, uint32_t test, uint32_t word, uint32_t exp)
{
    uint32_t v = 0u;
    checks++; die_checks[die]++;
    if (!acc_read(word, &v)) { note_fail(die, test, word, exp, 0xDEAD0000u); return; }
    if (v != exp) note_fail(die, test, word, exp, v);
}
static void put(uint32_t die, uint32_t word, uint32_t data)
{
    if (!acc_write(word, data)) note_fail(die, 0xFu, word, data, 0xDEAD0001u);
}

static void draw_status(const char *label)
{
    fb_rect(28, 318, 344, 18, UI_PANEL);
    fb_text(28, 318, label, UI_DIM, UI_PANEL);
}

static void test_die(uint32_t d)
{
    uint32_t base = d << 21;
    char label[24], *q;
    q = put_s(label, "DIE "); q = put_dec(q, d); put_s(q, " RUNNING");
    draw_status(label);

    /* 1. address lines: CPU word bit k = PSRAM word bit k+1, k = 0..20 (the
     * all-ones word is the guard, so k stops at 19 for the highest legal). */
    for (uint32_t k = 0; k < 21u; ++k) {
        uint32_t off = 1u << k;
        if (off == 0u || (off & 0x1FFFFFu) == GUARD_OFF) continue;
        put(d, base + off, (base + off) ^ 0xA5A50000u);
    }
    for (uint32_t k = 0; k < 21u; ++k) {
        uint32_t off = 1u << k;
        expect(d, 1u, base + off, (base + off) ^ 0xA5A50000u);
    }

    /* 2. byte lanes */
    uint32_t lw = base + 0x200u;
    put(d, lw, 0x11223344u);
    acc_write_be(lw, 0xAABBCCDDu, 0x5u); expect(d, 2u, lw, 0x11BB33DDu);
    acc_write_be(lw, 0x55667788u, 0xAu); expect(d, 2u, lw, 0x55BB77DDu);
    acc_write_be(lw, 0xFFFFFFFFu, 0x0u); expect(d, 2u, lw, 0x55BB77DDu);
    acc_write_be(lw, 0x000000EEu, 0x1u); expect(d, 2u, lw, 0x55BB77EEu);

    /* 3. hash fill, verify and CRC of the data read back */
    q = put_s(label, "DIE "); q = put_dec(q, d); put_s(q, " FILL");
    draw_status(label);
    for (uint32_t i = 0; i < FILL_WORDS; ++i) put(d, base + i, hash_word(base + i));
    uint32_t crc = 0u;
    for (uint32_t i = 0; i < FILL_WORDS; ++i) {
        uint32_t v = 0u, e = hash_word(base + i);
        checks++; die_checks[d]++;
        if (!acc_read(base + i, &v)) { note_fail(d, 3u, base + i, e, 0xDEAD0000u); v = 0u; }
        else if (v != e) note_fail(d, 3u, base + i, e, v);
        crc = crc_step(crc, v);
        chain = crc_step(chain, v);
    }
    die_crc[d] = crc;

    /* 4. anchors, incl. the last legal word before the guard */
    static const uint32_t off_a[4] = { 0u, 1u, FILL_WORDS - 1u, LAST_LEGAL };
    for (uint32_t i = 0; i < 4u; ++i) put(d, base + off_a[i], 0xC0DE0000u + (d << 8) + i);
    for (uint32_t i = 0; i < 4u; ++i) expect(d, 4u, base + off_a[i], 0xC0DE0000u + (d << 8) + i);

    /* 5. guard: refused, RDATA 0, GUARD status set, neighbour untouched */
    if (via_win) {
        /* Through the window a guard-word access must ACK (no bus error), read 0, ignore
         * the store, and set the sticky flag. */
        uint32_t v1 = 0xFFFFFFFFu, v2 = 0xFFFFFFFFu;
        checks += 2u; die_checks[d] += 2u;
        v1 = *winp(base + GUARD_OFF);
        *winp(base + GUARD_OFF) = 0x13572468u;
        v2 = *winp(base + GUARD_OFF);
        if (v1 != 0u) { note_fail(d, 5u, base + GUARD_OFF, 0u, v1); win_guard_ok = 0u; }
        if (v2 != 0u) { note_fail(d, 5u, base + GUARD_OFF, 0u, v2); win_guard_ok = 0u; }
    } else
    for (uint32_t w = 0; w < 2u; ++w) {
        uint32_t v = 0xFFFFFFFFu;
        checks++; die_checks[d]++;
        if (!ps_op(w, base + GUARD_OFF, 0x13572468u, 0xFu)) { note_fail(d, 5u, base + GUARD_OFF, 0u, 0xDEAD0000u); continue; }
        uint32_t st = REG(PS_STATUS);
        v = REG(PS_RDATA);
        if (!(st & ST_GUARD)) note_fail(d, 5u, base + GUARD_OFF, ST_GUARD, st & 0xFFu);
        else if (!w && v != 0u) note_fail(d, 5u, base + GUARD_OFF, 0u, v);
    }
    expect(d, 5u, base + LAST_LEGAL, 0xC0DE0000u + (d << 8) + 3u);
}

static uint32_t set_read(uint32_t idx) { REG(R_SET_IDX) = idx; return REG(R_SET_DAT); }

static void publish(void)
{
    uint32_t mask = 0u;
    for (uint32_t i = 0; i < 15u; ++i) {
        REG(R_SET_IDX) = i;
        REG(R_SET_DAT) = words[i] & 0x7FFFFFFFu;
        mask |= (words[i] >> 31) << i;
    }
    REG(R_SET_IDX) = 15u;
    REG(R_SET_DAT) = mask;
    delay(PSRAM_PUBLISH_WAIT);              /* let the toggle cross to clk_74a */
    (void)set_read(0u);
}

static void build_record(void)
{
    uint32_t sum = EXPECT_PS_ID;
    words[0]  = EXPECT_PS_ID;
    words[1]  = (run_no & 0xFFFFu) | (slow_mode << 16) | ((run_mode & 7u) << 17) |
                (PSRAM_FILL_LOG2 << 24);
    words[2]  = checks;
    words[3]  = fails;
    words[4]  = (end_status & 0xFFu) | (timed_out << 8) | ((t_acc & 0xFFu) << 16);
    for (uint32_t i = 0; i < 4u; ++i) words[5 + i] = die_crc[i];
    words[9]  = (die_fail[0] & 0xFFu) | ((die_fail[1] & 0xFFu) << 8) |
                ((die_fail[2] & 0xFFu) << 16) | ((die_fail[3] & 0xFFu) << 24);
    words[10] = (first_test << 24) | (first_word & 0x7FFFFFu);
    words[11] = first_exp;
    words[12] = first_act;
    words[13] = end_count;
    for (uint32_t i = 0; i < 14u; ++i) sum ^= words[i];
    words[14] = sum;
    words[15] = 0u;
}

static void draw_result(void)
{
    char line[40], *q;
    uint32_t bad = fails || timed_out || (end_status & (ST_CE | ST_TIMEOUT)) || !(end_status & ST_GHIT);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 26, "TAU PSRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
    q = put_s(line, "RUN "); q = put_dec(q, run_no);
    q = put_s(q, run_mode == 3u ? "  SLOW +3/+3" : run_mode == 2u ? "  READ +2" :
                 run_mode == 1u ? "  READ +1" : "  DEFAULT TIMING");
    fb_text(28, 52, line, UI_WHITE, UI_PANEL);
    for (uint32_t d = 0; d < 4u; ++d) {
        q = put_s(line, "D"); q = put_dec(q, d);
        q = put_s(q, die_fail[d] ? " FAIL " : " PASS ");
        q = put_dec(q, die_fail[d]); q = put_s(q, " ");
        q = put_hex(q, die_crc[d], 8);
        fb_text(28, 82 + 24u * d, line, die_fail[d] ? UI_RED : UI_GREEN, UI_PANEL);
    }
    q = put_s(line, "CHECKS "); q = put_dec(q, checks);
    q = put_s(q, " FAIL "); q = put_dec(q, fails);
    fb_text(28, 184, line, fails ? UI_RED : UI_WHITE, UI_PANEL);
    q = put_s(line, "TO "); q = put_dec(q, (end_status >> 2) & 1u);
    q = put_s(q, " CE "); q = put_dec(q, (end_status >> 4) & 1u);
    q = put_s(q, " GHIT "); q = put_dec(q, (end_status >> 5) & 1u);
    q = put_s(q, " W "); q = put_dec(q, (end_status >> 6) & 1u);
    q = put_dec(q, (end_status >> 7) & 1u);
    fb_text(28, 208, line, (end_status & (ST_CE | ST_TIMEOUT)) ? UI_RED : UI_DIM, UI_PANEL);
    q = put_s(line, "LAST "); q = put_hex(q, last_reg, 8);
    q = put_s(q, " RD "); q = put_hex(q, last_rdata, 8);
    fb_text(28, 232, line, UI_DIM, UI_PANEL);
    q = put_s(line, "OPS "); q = put_dec(q, end_count);
    q = put_s(q, "  IDX "); q = put_dec(q, t_acc + run_mode);
    if (have_first) {
        q = put_s(q, " FT"); q = put_dec(q, first_test);
    }
    fb_text(28, 256, line, UI_DIM, UI_PANEL);
    if (have_first) {
        q = put_s(line, "W "); q = put_hex(q, first_word, 6);
        q = put_s(q, " E "); q = put_hex(q, first_exp, 8);
        fb_text(28, 280, line, UI_RED, UI_PANEL);
        q = put_s(line, "ACTUAL "); q = put_hex(q, first_act, 8);
        fb_text(28, 300, line, UI_RED, UI_PANEL);
    }
    fb_text(28, 318, bad ? "FAIL" : "PASS", bad ? UI_RED : UI_GREEN, UI_PANEL);
    fb_text(90, 318, "A X Y B RUN MODES", UI_DIM, UI_PANEL);
}

static void run_once(uint32_t mode)
{
    uint32_t slow = (mode == 3u);
    run_mode = mode;
    slow_mode = slow;
    t_acc = (REG(PS_CFG) >> 8) & 0xFFu;
    checks = fails = timed_out = have_first = 0u;
    first_word = first_exp = first_act = first_test = 0u;
    for (uint32_t i = 0; i < 4u; ++i) die_fail[i] = die_crc[i] = die_checks[i] = 0u;
    REG(PS_CTRL) = 0x40u;                      /* clear sticky flags and WAIT samples */
    REG(PS_CFG) = (mode & 3u) | (slow ? 0x30u : 0x00u);   /* read extra = mode, write +3 only in mode 3 */
    run_no++;
    for (uint32_t d = 0; d < 4u && !timed_out; ++d) test_die(d);
    end_status = REG(PS_STATUS);
    last_reg = REG(PS_LAST);
    last_rdata = REG(PS_RDATA);
    end_count = REG(PS_COUNT);
    REG(PS_CFG) = 0u;
    build_record();
    draw_result();
    publish();
}

/* ---- window suite (B-016) ----------------------------------------------------- */
static uint32_t cum_cross, soak_mode;

/* Mailbox and window must see the same memory: write through one, read through the
 * other, both directions (the A-092 discriminator pattern). A mailbox pass with a
 * window failure points at the bus wrapper or the CPU return path, not the chip. */
static void cross_check(void)
{
    for (uint32_t d = 0; d < 4u && !timed_out; ++d) {
        uint32_t base = d << 21;
        for (uint32_t i = 0; i < 64u; ++i) {
            uint32_t w = base + 0x300u + i;
            uint32_t a = hash_word(w ^ 0x5A5A5A5Au), b = ~a, v = 0u;
            *winp(w) = a;
            checks++; die_checks[d]++;
            if (!ps_read(w, &v)) { note_fail(d, 6u, w, a, 0xDEAD0000u); cross_fails++; }
            else if (v != a)     { note_fail(d, 6u, w, a, v); cross_fails++; }
            (void)ps_write(w, b);
            checks++; die_checks[d]++;
            v = *winp(w);
            if (v != b) { note_fail(d, 6u, w, b, v); cross_fails++; }
        }
    }
}

/* Net clk_sys cycles per uncached window access (A-094 method: the empty cycles()
 * pair is measured and subtracted): 256 stores then 256 loads on die 0. */
static void measure_cost(void)
{
    uint32_t ovh = 0xFFFFFFFFu, sink = 0u;
    for (uint32_t i = 0; i < 16u; ++i) {
        uint32_t t0 = cycles(), t1 = cycles();
        if ((uint32_t)(t1 - t0) < ovh) ovh = t1 - t0;
    }
    for (uint32_t pass = 0; pass < 2u; ++pass) {
        uint32_t mn = 0xFFFFFFFFu, mx = 0u, sum = 0u;
        for (uint32_t i = 0; i < 256u; ++i) {
            uint32_t w = 0x180000u + i * 97u, t0, t1, v = 0u;
            if (pass == 0u) { t0 = cycles(); *winp(w) = 0xC0570000u + i; t1 = cycles(); }
            else            { t0 = cycles(); v = *winp(w);               t1 = cycles(); sink ^= v; }
            uint32_t c = (uint32_t)(t1 - t0);
            c = (c > ovh) ? (c - ovh) : 0u;
            if (c < mn) mn = c;
            if (c > mx) mx = c;
            sum += c;
        }
        if (pass == 0u) { wr_min = mn; wr_max = mx; wr_avg = sum / 256u; }
        else            { rd_min = mn; rd_max = mx; rd_avg = sum / 256u; }
    }
    if (sink == 0x13579BDFu) checks += 0u;          /* keep the loads observable */
}

static void build_record_win(void)
{
    uint32_t sum = EXPECT_PS_WID;
    words[0]  = EXPECT_PS_WID;
    words[1]  = (passes & 0xFFFFu) | ((soak_mode ? 5u : 4u) << 17) | (PSRAM_FILL_LOG2 << 24);
    words[2]  = cum_checks;
    words[3]  = cum_fails;
    words[4]  = (end_status & 0xFFu) | (timed_out << 8) | ((t_acc & 0xFFu) << 16) |
                (win_guard_ok << 24) | ((cum_cross == 0u) << 25);
    words[5]  = chain;
    words[6]  = ((rd_max & 0xFFFFu) << 16) | (rd_avg & 0xFFFFu);
    words[7]  = ((wr_max & 0xFFFFu) << 16) | (wr_avg & 0xFFFFu);
    words[8]  = cum_cross;
    words[9]  = (die_fail[0] & 0xFFu) | ((die_fail[1] & 0xFFu) << 8) |
                ((die_fail[2] & 0xFFu) << 16) | ((die_fail[3] & 0xFFu) << 24);
    words[10] = (first_test << 24) | (first_word & 0x7FFFFFu);
    words[11] = first_exp;
    words[12] = first_act;
    words[13] = win_ops;
    for (uint32_t i = 0; i < 14u; ++i) sum ^= words[i];
    words[14] = sum;
    words[15] = 0u;
}

static void draw_result_win(void)
{
    char line[40], *q;
    uint32_t bad = fails || timed_out || cross_fails || !win_guard_ok ||
                   (end_status & (ST_CE | ST_TIMEOUT)) || !(end_status & ST_GHIT);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 26, "TAU PSRAM CPU WINDOW", UI_ACCENT, UI_PANEL);
    q = put_s(line, soak_mode ? "SOAK PASS " : "WINDOW RUN ");
    q = put_dec(q, passes);
    q = put_s(q, "  IDX "); q = put_dec(q, t_acc);
    fb_text(28, 52, line, UI_WHITE, UI_PANEL);
    for (uint32_t d = 0; d < 4u; ++d) {
        q = put_s(line, "D"); q = put_dec(q, d);
        q = put_s(q, die_fail[d] ? " FAIL " : " PASS ");
        q = put_dec(q, die_fail[d]); q = put_s(q, " ");
        q = put_hex(q, die_crc[d], 8);
        fb_text(28, 82 + 24u * d, line, die_fail[d] ? UI_RED : UI_GREEN, UI_PANEL);
    }
    q = put_s(line, "CHECKS "); q = put_dec(q, cum_checks);
    q = put_s(q, " FAIL "); q = put_dec(q, cum_fails);
    fb_text(28, 184, line, cum_fails ? UI_RED : UI_WHITE, UI_PANEL);
    q = put_s(line, "TO "); q = put_dec(q, (end_status >> 2) & 1u);
    q = put_s(q, " CE "); q = put_dec(q, (end_status >> 4) & 1u);
    q = put_s(q, " GHIT "); q = put_dec(q, (end_status >> 5) & 1u);
    q = put_s(q, " X "); q = put_dec(q, cum_cross);
    q = put_s(q, win_guard_ok ? " GD OK" : " GD BAD");
    fb_text(28, 208, line, (bad && !fails) ? UI_RED : UI_DIM, UI_PANEL);
    q = put_s(line, "RD "); q = put_dec(q, rd_avg); q = put_s(q, "/"); q = put_dec(q, rd_max);
    q = put_s(q, "  WR "); q = put_dec(q, wr_avg); q = put_s(q, "/"); q = put_dec(q, wr_max);
    q = put_s(q, " CYC");
    fb_text(28, 232, line, UI_DIM, UI_PANEL);
    q = put_s(line, "WIN OPS "); q = put_dec(q, win_ops);
    if (have_first) { q = put_s(q, " FT"); q = put_dec(q, first_test); }
    fb_text(28, 256, line, UI_DIM, UI_PANEL);
    if (have_first) {
        q = put_s(line, "W "); q = put_hex(q, first_word, 6);
        q = put_s(q, " E "); q = put_hex(q, first_exp, 8);
        fb_text(28, 280, line, UI_RED, UI_PANEL);
        q = put_s(line, "ACTUAL "); q = put_hex(q, first_act, 8);
        fb_text(28, 300, line, UI_RED, UI_PANEL);
    }
    fb_text(28, 318, bad ? "FAIL" : "PASS", bad ? UI_RED : UI_GREEN, UI_PANEL);
    fb_text(90, 318, "AXYB MAILBOX  L WIN  R SOAK", UI_DIM, UI_PANEL);
}

static void run_window_once(uint32_t reset_cum)
{
    via_win = 1u; run_mode = 4u; slow_mode = 0u;
    t_acc = (REG(PS_CFG) >> 8) & 0xFFu;
    checks = fails = timed_out = have_first = 0u;
    first_word = first_exp = first_act = first_test = 0u;
    for (uint32_t i = 0; i < 4u; ++i) die_fail[i] = die_crc[i] = die_checks[i] = 0u;
    chain = 0u; cross_fails = 0u; win_guard_ok = 1u;
    if (reset_cum) { cum_checks = cum_fails = cum_cross = passes = 0u; }
    REG(PS_CTRL) = 0x40u;                       /* clear sticky flags and WAIT samples */
    REG(PS_CFG) = 0u;                           /* build default read timing */
    run_no++;
    for (uint32_t d = 0; d < 4u && !timed_out; ++d) test_die(d);
    cross_check();
    measure_cost();
    end_status = REG(PS_STATUS);
    last_reg = REG(PS_LAST);
    last_rdata = REG(PS_RDATA);
    win_ops = REG(PS_WCOUNT);
    cum_checks += checks; cum_fails += fails; cum_cross += cross_fails; passes++;
    if (!(end_status & ST_GHIT)) win_guard_ok = 0u;
    build_record_win();
    draw_result_win();
    publish();
    via_win = 0u;
}

static void draw_message(const char *l1, const char *l2, uint32_t v)
{
    char line[24];
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 38, "TAU PSRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
    fb_text(28, 82, l1, UI_RED, UI_PANEL);
    fb_text(28, 112, l2, UI_WHITE, UI_PANEL);
    put_hex(line, v, 8);
    fb_text(28, 142, line, UI_WHITE, UI_PANEL);
}

int main(void)
{
    delay(PSRAM_WARMUP);                       /* framebuffer leaves reset */
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 38, "TAU PSRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
    fb_text(28, 78, "STARTING", UI_WHITE, UI_PANEL);

    uint32_t version = REG(R_VERSION);
    if (version != EXPECT_VERSION) {
        draw_message("RTL VERSION MISMATCH", "RBF DOES NOT MATCH ROM", version);
        for (;;) { }
    }
    uint32_t id = REG(PS_ID);
    if (id != EXPECT_PS_ID) {
        draw_message("NO PSRAM PROBE IN RBF", "NEED TAU_PSRAM_PROBE BUILD", id);
        for (;;) { }
    }
    /* Hold-off: the controller keeps both chips idle for 200 us after reset. */
    if (!ps_wait()) {
        draw_message("PSRAM NEVER BECAME IDLE", "CONTROLLER STUCK", REG(PS_STATUS));
        for (;;) { }
    }
    win_present = (REG(PS_CFG) >> 16) & 1u;
    run_once(0u);
    uint32_t old = 0u;
    for (;;) {
        uint32_t keys = REG(R_INPUT) & 0xFFFFu;
        if ((keys & KEY_A) && !(old & KEY_A)) run_once(0u);
        else if ((keys & KEY_X) && !(old & KEY_X)) run_once(1u);
        else if ((keys & KEY_Y) && !(old & KEY_Y)) run_once(2u);
        else if ((keys & KEY_B) && !(old & KEY_B)) run_once(3u);
        else if (win_present && (keys & KEY_L1) && !(old & KEY_L1)) { soak_mode = 0u; run_window_once(1u); }
        else if (win_present && (keys & KEY_R1) && !(old & KEY_R1)) {
            soak_mode = 1u;
            run_window_once(1u);
            /* soak: repeat until a mode key is pressed; counters are cumulative */
            while (!(REG(R_INPUT) & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_L1)))
                run_window_once(0u);
            soak_mode = 0u;
            keys = REG(R_INPUT) & 0xFFFFu;
        }
        old = keys;
    }
}
