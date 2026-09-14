// Tau Phase 1 SDRAM mailbox diagnostic firmware.
//
// This is a deliberately separate, developer-only ROM.  It exercises only
// SDRAM word addresses at and above 0x80000 (1 MiB in byte addressing), leaving
// the visible framebuffer and off-screen artwork stash untouched.  It does not
// change the player firmware or the linker map.

#include <stdint.h>
#include "font_metrics.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define R_CYCLES     0x8000000Cu
#define R_STAT0      0x80000010u
#define R_STAT1      0x80000014u
#define R_STAT2      0x80000018u
#define R_STAT3      0x8000001Cu
#define R_TGT_ID     0x80000020u
#define R_TGT_GO     0x80000030u
#define R_INPUT      0x8000003Cu
#define R_VERSION    0x80000040u
#define R_FB_ADDR    0x80000048u
#define R_FB_SIZE    0x8000004Cu
#define R_FB_COLOR   0x80000050u
#define R_FB_GO      0x80000054u
#define R_SDR_ADDR   0x80000074u
#define R_SDR_DATA   0x80000078u
#define R_SDR_CTRL   0x8000007Cu
#define R_SDR_RDATA  0x80000080u
#define R_SDR_STATUS 0x80000084u

#define EXPECT_VERSION 0x4D503316u
#define CLK_HZ         60000000u

/* APF target command selector. GETFILE for the already-loaded firmware slot
 * is read-only.  It is used only as a boot heartbeat: the Pocket developer
 * log records the command, independently of the framebuffer path. */
#define FW_SLOT_ID   1u
#define TGT_GETFILE  2u

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
#define UI_TRACK  0x18E3u

#define KEY_A (1u << 4)

// Controller addresses are 16-bit word addresses.  The first 1 MiB is reserved
// for framebuffer ownership, so CPU diagnostics begin at 1 MiB / 2.
#define TEST_BASE_WORD 0x00080000u
#define TEST_LAST_WORD 0x000FFFFEu
#define SDR_TIMEOUT    (CLK_HZ / 2u)

static uint32_t fb_color_shadow = 0xFFFFFFFFu;
static uint32_t tests_run;
static uint32_t failures;
static uint32_t first_fail_addr;
static uint32_t first_fail_expected;
static uint32_t first_fail_actual;
static uint32_t timed_out;

static inline uint32_t cycles(void) { return REG(R_CYCLES); }
static inline void fb_wait(void) { while (REG(R_FB_GO) & 1u) { } }

static void fb_set_color(uint16_t fg, uint16_t bg)
{
    uint32_t value = ((uint32_t)bg << 16) | fg;
    if (value != fb_color_shadow) {
        REG(R_FB_COLOR) = value;
        fb_color_shadow = value;
    }
}

static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                    uint16_t color)
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

static uint32_t advance(char ch)
{
    return font_adv[glyph(ch) - FONT_FIRST];
}

static void fb_text(uint32_t x, uint32_t y, const char *text,
                    uint16_t fg, uint16_t bg)
{
    fb_set_color(fg, bg);
    while (*text && x + FONT_CELL_W <= FB_W) {
        fb_wait();
        REG(R_FB_ADDR) = y * FB_STRIDE + x;
        REG(R_FB_GO) = FB_OP_CHAR | ((glyph(*text) & 0x7Fu) << 3);
        x += advance(*text++);
    }
}

static char hex_digit(uint32_t value)
{
    value &= 15u;
    return (char)(value < 10u ? ('0' + value) : ('A' + value - 10u));
}

static void hex8(char *out, uint32_t value)
{
    for (uint32_t i = 0; i < 8u; ++i)
        out[i] = hex_digit(value >> (28u - i * 4u));
    out[8] = 0;
}

static char *append_dec(char *out, uint32_t value)
{
    char reverse[10];
    uint32_t count = 0;
    do {
        reverse[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value);
    while (count) *out++ = reverse[--count];
    *out = 0;
    return out;
}

static int wait_idle(void)
{
    uint32_t started = cycles();
    while (REG(R_SDR_STATUS) & 1u) {
        if ((uint32_t)(cycles() - started) > SDR_TIMEOUT) {
            timed_out = 1u;
            return 0;
        }
    }
    return 1;
}

/* APF-visible, read-only checkpoint. The Pocket developer log records every
 * GETFILE request. Wait for its sequence change so successive checkpoints are
 * never collapsed by the toggle-based command bridge. */
static int target_heartbeat(void)
{
    uint32_t seq = (REG(R_TGT_GO) >> 8) & 0xFFu;
    uint32_t started;

    REG(R_TGT_ID) = FW_SLOT_ID;
    REG(R_TGT_GO) = TGT_GETFILE;
    started = cycles();
    while (((REG(R_TGT_GO) >> 8) & 0xFFu) == seq) {
        if ((uint32_t)(cycles() - started) > SDR_TIMEOUT) return 0;
    }
    return 1;
}

static int issue(uint32_t word_addr, uint32_t data, uint32_t byte_en,
                 uint32_t write)
{
    if (!wait_idle()) return 0;
    REG(R_SDR_ADDR) = word_addr;
    REG(R_SDR_DATA) = data;
    REG(R_SDR_CTRL) = 1u | (write ? 2u : 0u) | ((byte_en & 15u) << 2);

    // sys_busy is asserted on the clock after the MMIO start pulse.  Waiting a
    // fixed handful of system clocks before polling prevents an immediate
    // status read from mistaking "not accepted yet" for completion.  The
    // bridge itself remains the completion authority after this guard.
    uint32_t guard = cycles();
    while ((uint32_t)(cycles() - guard) < 128u) { }
    return wait_idle();
}

static int write32(uint32_t word_addr, uint32_t value, uint32_t byte_en)
{
    return issue(word_addr, value, byte_en, 1u);
}

static int read32(uint32_t word_addr, uint32_t *value)
{
    if (!issue(word_addr, 0u, 15u, 0u)) return 0;
    *value = REG(R_SDR_RDATA);
    return 1;
}

static void record_failure(uint32_t addr, uint32_t expected, uint32_t actual)
{
    if (!failures) {
        first_fail_addr = addr;
        first_fail_expected = expected;
        first_fail_actual = actual;
    }
    ++failures;
}

static void expect_value(uint32_t addr, uint32_t expected)
{
    uint32_t actual = 0u;
    ++tests_run;
    if (!read32(addr, &actual)) {
        record_failure(addr, expected, 0xDEAD0001u);
    } else if (actual != expected) {
        record_failure(addr, expected, actual);
    }
}

static void write_expect(uint32_t addr, uint32_t value)
{
    if (!write32(addr, value, 15u)) {
        ++tests_run;
        record_failure(addr, value, 0xDEAD0002u);
        return;
    }
    expect_value(addr, value);
}

static void partial_expect(uint32_t addr, uint32_t value, uint32_t byte_en,
                           uint32_t expected)
{
    if (!write32(addr, value, byte_en)) {
        ++tests_run;
        record_failure(addr, expected, 0xDEAD0003u);
        return;
    }
    expect_value(addr, expected);
}

static void draw_running(uint32_t step, const char *label)
{
    fb_rect(20, 177, 360, 10, UI_TRACK);
    fb_rect(20, 177, step * 90u, 10, UI_ACCENT);
    fb_rect(20, 204, 360, 18, UI_BG);
    fb_text(20, 204, label, UI_DIM, UI_BG);
}

static void draw_version_mismatch(uint32_t actual)
{
    char hex[9];
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 38, "TAU SDRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
    fb_text(28, 78, "RTL VERSION MISMATCH", UI_RED, UI_PANEL);
    fb_text(28, 120, "EXPECTED", UI_DIM, UI_PANEL);
    hex8(hex, EXPECT_VERSION);
    fb_text(220, 120, hex, UI_WHITE, UI_PANEL);
    fb_text(28, 146, "ACTUAL", UI_DIM, UI_PANEL);
    hex8(hex, actual);
    fb_text(220, 146, hex, UI_RED, UI_PANEL);
    fb_text(28, 306, "REBUILD OR REINSTALL RBF", UI_DIM, UI_PANEL);
}

static void run_tests(void)
{
    static const uint32_t patterns[4] = {
        0x00000000u, 0xFFFFFFFFu, 0xAAAAAAAAu, 0x55555555u
    };
    static const uint32_t anchors[12] = {
        0x00080000u, 0x00080002u, 0x000800FEu, 0x00080100u,
        0x000807FEu, 0x00080800u, 0x00081FFEu, 0x00082000u,
        0x00087FFEu, 0x00088000u, 0x0008FFFEu, 0x00090000u
    };
    tests_run = failures = first_fail_addr = 0u;
    first_fail_expected = first_fail_actual = timed_out = 0u;

    draw_running(0u, "FIXED PATTERNS");
    for (uint32_t a = 0; a < 12u && !timed_out; ++a)
        for (uint32_t p = 0; p < 4u && !timed_out; ++p)
            write_expect(anchors[a], patterns[p]);

    draw_running(1u, "WALKING ONES AND ZEROS");
    for (uint32_t bit = 0; bit < 32u && !timed_out; ++bit) {
        write_expect(TEST_BASE_WORD + 0x400u, 1u << bit);
        if (!timed_out) write_expect(TEST_BASE_WORD + 0x400u, ~(1u << bit));
    }

    draw_running(2u, "ADDRESS AS DATA");
    // Write the whole sparse set before reading any of it back.  A write/read
    // pair at each address would miss address-line aliases because the aliased
    // location would still contain the value most recently written to it.
    for (uint32_t i = 0; i < 64u && !timed_out; ++i) {
        uint32_t addr = TEST_BASE_WORD + i * 0x1FFEu;
        if (addr > TEST_LAST_WORD) addr = TEST_LAST_WORD - (i & 0x1Eu);
        if (!write32(addr, 0xA5000000u ^ addr, 15u))
            record_failure(addr, 0xA5000000u ^ addr, 0xDEAD0002u);
    }
    for (uint32_t i = 0; i < 64u && !timed_out; ++i) {
        uint32_t addr = TEST_BASE_WORD + i * 0x1FFEu;
        if (addr > TEST_LAST_WORD) addr = TEST_LAST_WORD - (i & 0x1Eu);
        expect_value(addr, 0xA5000000u ^ addr);
    }

    draw_running(3u, "BYTE ENABLES");
    uint32_t addr = TEST_BASE_WORD + 0x600u;
    write_expect(addr, 0x11223344u);
    if (!timed_out) partial_expect(addr, 0x000000AAu, 0x1u, 0x112233AAu);
    if (!timed_out) partial_expect(addr, 0x0000BB00u, 0x2u, 0x1122BBAAu);
    if (!timed_out) partial_expect(addr, 0x00CC0000u, 0x4u, 0x11CCBBAAu);
    if (!timed_out) partial_expect(addr, 0xDD000000u, 0x8u, 0xDDCCBBAAu);
    if (!timed_out) partial_expect(addr, 0x0000BEEFu, 0x3u, 0xDDCCBEEFu);
    if (!timed_out) partial_expect(addr, 0xCAFE0000u, 0xCu, 0xCAFEBEEFu);

    draw_running(4u, "COMPLETE");
    REG(R_STAT0) = failures ? 0x53444641u : 0x53445041u; // SDFA / SDPA
    REG(R_STAT1) = failures;
    REG(R_STAT2) = first_fail_addr;
    REG(R_STAT3) = first_fail_actual;
}

static void draw_result(void)
{
    char number[12], hex[9];
    uint16_t color = failures ? UI_RED : UI_GREEN;
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 38, "TAU SDRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
    fb_text(28, 72, failures ? "FAIL" : "PASS", color, UI_PANEL);

    char *end = append_dec(number, tests_run);
    (void)end;
    fb_text(28, 110, "READBACK CHECKS", UI_DIM, UI_PANEL);
    fb_text(220, 110, number, UI_WHITE, UI_PANEL);

    end = append_dec(number, failures);
    (void)end;
    fb_text(28, 136, "FAILURES", UI_DIM, UI_PANEL);
    fb_text(220, 136, number, color, UI_PANEL);

    if (failures) {
        hex8(hex, first_fail_addr);
        fb_text(28, 180, "FIRST WORD ADDR", UI_DIM, UI_PANEL);
        fb_text(220, 180, hex, UI_WHITE, UI_PANEL);
        hex8(hex, first_fail_expected);
        fb_text(28, 206, "EXPECTED", UI_DIM, UI_PANEL);
        fb_text(220, 206, hex, UI_WHITE, UI_PANEL);
        hex8(hex, first_fail_actual);
        fb_text(28, 232, timed_out ? "TIMEOUT CODE" : "ACTUAL", UI_DIM, UI_PANEL);
        fb_text(220, 232, hex, color, UI_PANEL);
    } else {
        fb_text(28, 190, "TEST REGION ABOVE 1 MIB", UI_DIM, UI_PANEL);
        fb_text(28, 216, "FIXED WALK ADDRESS LANES", UI_DIM, UI_PANEL);
    }
    fb_text(28, 306, "A  RUN AGAIN", UI_DIM, UI_PANEL);
}

static void draw_initial(void)
{
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 38, "TAU SDRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
    fb_text(28, 78, "PHASE 1 MAILBOX TEST", UI_WHITE, UI_PANEL);
    fb_text(28, 112, "SAFE REGION 1-2 MIB", UI_DIM, UI_PANEL);
    fb_text(28, 138, "PLAYER DATA UNCHANGED", UI_DIM, UI_PANEL);
    draw_running(0u, "STARTING");
}

int main(void)
{
    /* The tiny diagnostic reaches its first MMIO writes far earlier than the
     * player. Give the SDRAM-backed framebuffer time to leave reset before
     * sending its first command. */
    uint32_t warmup = cycles();
    while ((uint32_t)(cycles() - warmup) < CLK_HZ / 2u) { }

    /* This preserves a post-reset CPU heartbeat in the Pocket developer log.
     * It is read-only and its result is not needed to enter the diagnostic. */
    (void)target_heartbeat();

    /* Draw before the RTL version interlock so a mismatch cannot turn into an
     * unexplained black screen. */
    draw_initial();
    uint32_t version = REG(R_VERSION);
    if (version != EXPECT_VERSION) {
        REG(R_STAT0) = 0xBAD00016u;
        REG(R_STAT1) = version;
        draw_version_mismatch(version);
        for (;;) { }
    }

    uint32_t pause = cycles();
    while ((uint32_t)(cycles() - pause) < CLK_HZ / 4u) { }
    run_tests();
    draw_result();

    uint32_t old_keys = 0u;
    for (;;) {
        uint32_t keys = REG(R_INPUT) & 0xFFFFu;
        if ((keys & KEY_A) && !(old_keys & KEY_A)) {
            draw_initial();
            run_tests();
            draw_result();
        }
        old_keys = keys;
    }
}
