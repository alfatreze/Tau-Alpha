// Tau SDRAM diagnostic firmware.
//
// This is a deliberately separate, developer-only ROM. The Phase 1 build uses
// controller word addresses at and above 0x80000 (1 MiB in byte addressing);
// the Phase 2 build uses the uncached CPU window beginning at physical 2 MiB.
// Both leave the visible framebuffer/artwork area untouched and neither alters
// player firmware or the linker map.

#include <stdint.h>
#include "font_metrics.h"

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

#define R_CYCLES     0x8000000Cu
#define R_STAT0      0x80000010u
#define R_STAT1      0x80000014u
#define R_STAT2      0x80000018u
#define R_STAT3      0x8000001Cu
#define R_TGT_ID     0x80000020u
#define R_TGT_OFF    0x80000024u
#define R_TGT_ADR    0x80000028u
#define R_TGT_LEN    0x8000002Cu
#define R_TGT_GO     0x80000030u
#define R_DT_ADDR    0x80000060u
#define R_DT_DATA    0x80000064u
#define R_INPUT      0x8000003Cu
#define R_VERSION    0x80000040u
#define R_FB_ADDR    0x80000048u
#define R_FB_SIZE    0x8000004Cu
#define R_FB_COLOR   0x80000050u
#define R_FB_GO      0x80000054u
#define R_SET_IDX    0x8000006Cu
#define R_SET_DAT    0x80000070u
#define R_SDR_ADDR   0x80000074u
#define R_SDR_DATA   0x80000078u
#define R_SDR_CTRL   0x8000007Cu
#define R_SDR_RDATA  0x80000080u
#define R_SDR_STATUS 0x80000084u

#define EXPECT_VERSION 0x4D503317u
#ifndef CLK_HZ
#define CLK_HZ         60000000u
#endif

/* APF target command selector. GETFILE for the already-loaded firmware slot
 * is read-only.  It is used only as a boot heartbeat: the Pocket developer
 * log records the command, independently of the framebuffer path. */
#define FW_SLOT_ID   1u
#define LOG_SLOT_ID  5u
#define TGT_GETFILE  2u
#define TGT_READ     0u
#define TGT_OPENFILE 1u
#define TGT_WRITE    3u
#define TGT_FLUSH    4u

/* A 64-byte binary record lives in mf_datatable words 200..215. This range is
 * above the APF slot-size table (0..63), the filename command buffers
 * (64..191), and settings (192..199), but below Pocket's build metadata at
 * 224. APF copies it to the dedicated nonvolatile diagnostic slot; the ROM
 * never writes any music, playlist, artwork, or settings slot. */
#define LOG_DT_WORD  200u
#define LOG_WORDS    16u
/* mf_datatable is bridged at 0xF8002000 (word 64 == 0xF8002100 in
 * core_game.vh). A-082..A-087 wrongly used 0xF8000000, so 0184 sourced from
 * outside the datatable and 0180 landed outside it. */
#define DT_BRIDGE_BASE 0xF8002000u
#define LOG_BRIDGE_ADDR (DT_BRIDGE_BASE + LOG_DT_WORD * 4u)
#define LOG_READ_DT_WORD 216u
#define LOG_READ_BRIDGE_ADDR (DT_BRIDGE_BASE + LOG_READ_DT_WORD * 4u)
/* These are the existing APF command buffers in core_game.vh.  The first 64
 * words remain APF's slot-size table, so a 0190 response may only use the
 * 64..127 buffer and its 0192 input must use 128..191. */
#define DT_RESP_WORD 64u
#define DT_PARAM_WORD 128u
#define DT_STRUCT_WORDS 64u

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

// The CPU-window build uses byte addresses in the uncached Phase 2 aperture.
// It begins at physical 2 MiB, preserving the framebuffer guard and the
// 1–2 MiB area already exercised by the Phase 1 mailbox diagnostic.
#ifdef TAU_CPU_WINDOW_DIAG
#define TEST_BASE_WORD 0xA0200000u
#define TEST_LAST_WORD 0xA02FFFFCu
#define TEST_ADDR_STRIDE 0x00001FFCu
#else
// Controller addresses are 16-bit words; Phase 1 begins at 1 MiB / 2.
#define TEST_BASE_WORD 0x00080000u
#define TEST_LAST_WORD 0x000FFFFEu
#define TEST_ADDR_STRIDE 0x00001FFEu
#endif
#define SDR_TIMEOUT    (CLK_HZ / 2u)

// The Phase 2 ROM first proves the established MMIO -> mux -> CDC bridge path
// at the same physical 2 MiB address. This creates a useful hardware boundary:
// a failure here means the new shared owner-mux composition is broken; a later
// CPU-window stall instead isolates the adapter/CPU-bus side. It is a single
// destructive word within the already-owned 2-3 MiB diagnostic region.
#ifdef TAU_CPU_WINDOW_DIAG
#define PREFLIGHT_SDR_WORD_ADDR 0x00100000u
#define PREFLIGHT_PATTERN       0x43505550u
#endif

static uint32_t fb_color_shadow = 0xFFFFFFFFu;
static uint32_t tests_run;
static uint32_t failures;
static uint32_t first_fail_addr;
static uint32_t first_fail_expected;
static uint32_t first_fail_actual;
static uint32_t timed_out;
static uint32_t diagnostic_run;
#ifdef TAU_LOG_COMMAND_PROBE
/* Pocket-visible evidence for A-081. D=target command completed, T=local
 * timeout, -=not attempted. Error is the APF target result code. */
static uint32_t log_open_state, log_open_err;
static uint32_t log_ready_state, log_ready_err, log_ready_tries;
static uint32_t log_write_state, log_write_err;
static uint32_t log_flush_state, log_flush_err;
static uint32_t log_read_state, log_read_err, log_read_data;
#ifdef TAU_LOG_TABLE_PROBE
/* A-090: slot 5's size from APF's {id,size} table (stride 2 from word 0)
 * before and after 0184, an XOR/rotate hash of words 0..63 to prove the table
 * is unchanged, the flush duration, and a second readback after the flush. */
static uint32_t log_tbl_size_before, log_tbl_size_after;
static uint32_t log_tbl_hash_before, log_tbl_hash_after;
static uint32_t log_flush_cycles;
static uint32_t log_read2_state, log_read2_err, log_read2_data;
#endif
#ifdef TAU_LOG_SOURCE_PROBE
/* A-087: datatable words 200..203 sampled after dt_write and immediately
 * before 0184, to separate a missing payload from an APF address fault. */
static uint32_t log_src[4];
#endif
#endif

#ifdef TAU_LOG_INTERACT_PROBE
/* A-091: the 64-byte record is published through APF's interact.json persist
 * channel (16 words at 0x20000000, APF-stored to Settings/<core>/Interact/
 * _core/interact_persist.json).  No data slot, 0184 or 0188 is used.  APF
 * stores these values as SIGNED int32, so words 0..14 carry only their low 31
 * bits and word 15 carries the withheld top bits (bit i = top bit of word i). */
static uint32_t set_readback[4];   /* words 0, 7, 12, 15 as read back */
#if defined(TAU_DISCRIMINATOR_PROBE) || defined(TAU_LATENCY_PROBE)
static uint32_t disc_words[16];    /* A-092/A-094 raw result words */
#endif
#endif

static inline uint32_t cycles(void) { return REG(R_CYCLES); }
static inline void dt_write(uint32_t word, uint32_t value)
{
    REG(R_DT_ADDR) = word;
    REG(R_DT_DATA) = value;
}
static inline uint32_t dt_read(uint32_t word)
{
    REG(R_DT_ADDR) = word;
    return REG(R_DT_DATA);
}
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

/* One APF target command, using the sequence counter rather than the sticky
 * completion level.  `state` is 1 for an answered command, 2 for local
 * timeout; the Pocket error code is returned separately. */
static int target_cmd(uint32_t selector, uint32_t slot, uint32_t offset,
                      uint32_t address, uint32_t length,
                      uint32_t timeout, uint32_t *state, uint32_t *error)
{
    uint32_t seq = (REG(R_TGT_GO) >> 8) & 0xFFu;
    uint32_t started;
    REG(R_TGT_ID) = slot;
    REG(R_TGT_OFF) = offset;
    REG(R_TGT_ADR) = address;
    REG(R_TGT_LEN) = length;
    REG(R_TGT_GO) = selector;
    started = cycles();
    while (((REG(R_TGT_GO) >> 8) & 0xFFu) == seq) {
        if ((uint32_t)(cycles() - started) > timeout) {
            *state = 2u;
            *error = 0u;
            return 0;
        }
    }
    *state = 1u;
    *error = (REG(R_TGT_GO) >> 2) & 7u;
    return *error == 0u;
}

/* A deferred nonvolatile slot is declared in data.json and bound by the
 * instance, but A-083 proves that this alone does not make it the active APF
 * file for 0184/0180.  Ask APF for its own descriptor, copy it unchanged into
 * the separate 0192 parameter buffer, then open that same descriptor.  This
 * deliberately mirrors the production playlist path rather than inventing an
 * undocumented 0192 struct layout. */
static int open_log_slot(void)
{
    uint32_t state = 0u, error = 0u;
    if (!target_cmd(TGT_GETFILE, LOG_SLOT_ID, 0u, 0u, 0u, SDR_TIMEOUT,
                    &state, &error)) {
#ifdef TAU_LOG_COMMAND_PROBE
        log_open_state = state;
        log_open_err = error;
#endif
        return 0;
    }
    for (uint32_t i = 0; i < DT_STRUCT_WORDS; ++i)
        dt_write(DT_PARAM_WORD + i, dt_read(DT_RESP_WORD + i));
    (void)target_cmd(TGT_OPENFILE, LOG_SLOT_ID, 0u, 0u, 0u, SDR_TIMEOUT,
                     &state, &error);
#ifdef TAU_LOG_COMMAND_PROBE
    log_open_state = state;
    log_open_err = error;
#endif
    return state == 1u && error == 0u;
}

static int flush_result(void);

/* `0192` completion means the Pocket accepted the request, not that the new
 * file is ready for its next transaction.  The production player has the same
 * observed boundary and uses repeated reads roughly 30 ms apart.  Require two
 * successful 0180 responses here before the diagnostic writes; each failed
 * attempt has a short 100 ms deadline so this diagnostic remains bounded. */
static int wait_log_slot_ready(void)
{
    uint32_t stable = 0u;
#ifdef TAU_LOG_COMMAND_PROBE
    log_ready_state = log_ready_err = log_ready_tries = 0u;
#endif
    for (uint32_t tries = 1u; tries <= 16u; ++tries) {
        uint32_t state = 0u, error = 0u;
        int ok = target_cmd(TGT_READ, LOG_SLOT_ID, 0u, LOG_READ_BRIDGE_ADDR,
                            4u, CLK_HZ / 10u, &state, &error);
#ifdef TAU_LOG_COMMAND_PROBE
        log_ready_state = state;
        log_ready_err = error;
        log_ready_tries = tries;
#endif
        if (ok) {
            if (++stable == 2u) return 1;
        } else {
            stable = 0u;
        }
        {
            uint32_t until = cycles() + CLK_HZ / 32u;
            while ((int32_t)(cycles() - until) < 0) { }
        }
    }
    return 0;
}

#ifdef TAU_LOG_TABLE_PROBE
static void sample_slot_table(uint32_t *size, uint32_t *hash)
{
    uint32_t h = 0u;
    *size = 0xFFFFFFFFu;                 /* id 5 not found */
    for (uint32_t w = 0u; w < 64u; ++w) {
        uint32_t v = dt_read(w);
        h = ((h << 1) | (h >> 31)) ^ v;
        if (!(w & 1u) && v == LOG_SLOT_ID) *size = dt_read(w + 1u);
    }
    *hash = h;
}
#endif

/* Write the terminal result.  The readback probes intentionally defer flush:
 * a timed-out 0188 can leave the single-command bridge occupied, making the
 * following 0180 result inconclusive.  A-086 therefore validates the write
 * first, then flushes only after that read has had a fair turn. */
#ifdef TAU_LOG_INTERACT_PROBE
static uint32_t set_read(uint32_t idx)
{
    REG(R_SET_IDX) = idx;
    return REG(R_SET_DAT);
}

static void publish_interact(const uint32_t *words)
{
    uint32_t mask = 0u;
    for (uint32_t i = 0; i < 15u; ++i) {
        REG(R_SET_IDX) = i;
        REG(R_SET_DAT) = words[i] & 0x7FFFFFFFu;
        mask |= (words[i] >> 31) << i;
    }
    REG(R_SET_IDX) = 15u;
    REG(R_SET_DAT) = mask;
    /* The CPU write crosses to clk_74a on a toggle; allow it to land before
     * reading the same words back. */
    uint32_t until = cycles() + CLK_HZ / 100u;
    while ((int32_t)(cycles() - until) < 0) { }
    set_readback[0] = set_read(0u);
    set_readback[1] = set_read(7u);
    set_readback[2] = set_read(12u);
    set_readback[3] = set_read(15u);
}
#endif

static int save_result(uint32_t stage)
{
    uint32_t words[LOG_WORDS];
    uint32_t checksum = 0x544C4F47u; /* "TLOG" */
    uint32_t command_state = 0u, command_error = 0u;

    words[0]  = 0x544C4F47u;             /* TLOG */
    words[1]  = 0x00010040u;             /* schema 1, 64 bytes */
    words[2]  = stage | (failures ? 0x00000100u : 0u) |
                (timed_out ? 0x00000200u : 0u);
    words[3]  = EXPECT_VERSION;
    words[4]  = ++diagnostic_run;
    words[5]  = cycles();
    words[6]  = tests_run;
    words[7]  = failures;
    words[8]  = first_fail_addr;
    words[9]  = first_fail_expected;
    words[10] = first_fail_actual;
    words[11] = REG(R_STAT0);
    for (uint32_t i = 0; i < 12u; ++i) checksum ^= words[i];
    words[12] = checksum;
    words[13] = 0u;
    words[14] = 0u;
    words[15] = 0u;
#ifdef TAU_LOG_INTERACT_PROBE
    (void)command_state; (void)command_error;
#if defined(TAU_DISCRIMINATOR_PROBE) || defined(TAU_LATENCY_PROBE)
    publish_interact(disc_words);
#else
    publish_interact(words);
#endif
    return 1;
#endif
    for (uint32_t i = 0; i < LOG_WORDS; ++i) dt_write(LOG_DT_WORD + i, words[i]);
#ifdef TAU_LOG_TABLE_PROBE
    sample_slot_table(&log_tbl_size_before, &log_tbl_hash_before);
#endif
#ifdef TAU_LOG_SOURCE_PROBE
    for (uint32_t i = 0; i < 4u; ++i) log_src[i] = dt_read(LOG_DT_WORD + i);
#endif

#ifdef TAU_LOG_COMMAND_PROBE
    log_write_state = log_write_err = log_flush_state = log_flush_err = 0u;
#endif
    if (!target_cmd(TGT_WRITE, LOG_SLOT_ID, 0u, LOG_BRIDGE_ADDR,
                    LOG_WORDS * 4u, SDR_TIMEOUT, &command_state, &command_error)) {
#ifdef TAU_LOG_COMMAND_PROBE
        log_write_state = command_state;
        log_write_err = command_error;
#endif
        return 0;
    }
#ifdef TAU_LOG_COMMAND_PROBE
    log_write_state = command_state;
    log_write_err = command_error;
#endif
#ifdef TAU_LOG_TABLE_PROBE
    sample_slot_table(&log_tbl_size_after, &log_tbl_hash_after);
#endif
#ifdef TAU_LOG_READBACK_PROBE
    return 1;
#else
    return flush_result();
#endif
}

static int flush_result(void)
{
    uint32_t command_state = 0u, command_error = 0u;
#ifdef TAU_LOG_TABLE_PROBE
    uint32_t flush_started = cycles();
    int flush_ok = target_cmd(TGT_FLUSH, LOG_SLOT_ID, 0u, 0u, 0u,
                              CLK_HZ * 10u, &command_state, &command_error);
    log_flush_cycles = cycles() - flush_started;
    if (!flush_ok) {
#else
    if (!target_cmd(TGT_FLUSH, LOG_SLOT_ID, 0u, 0u, 0u,
                    SDR_TIMEOUT, &command_state, &command_error)) {
#endif
#ifdef TAU_LOG_COMMAND_PROBE
        log_flush_state = command_state;
        log_flush_err = command_error;
#endif
        return 0;
    }
#ifdef TAU_LOG_COMMAND_PROBE
    log_flush_state = command_state;
    log_flush_err = command_error;
#endif
    return 1;
}

#ifdef TAU_LOG_READBACK_PROBE
/* Read the first result word back from slot 5 before core exit. Words 216..223
 * are unused and lie below Pocket's build metadata at 224; this four-byte
 * transfer cannot overlap the result record at 200..215. */
static void read_result_back(void)
{
    log_read_state = log_read_err = log_read_data = 0u;
    dt_write(LOG_READ_DT_WORD, 0u);
    if (target_cmd(TGT_READ, LOG_SLOT_ID, 0u, LOG_READ_BRIDGE_ADDR, 4u,
                   SDR_TIMEOUT, &log_read_state, &log_read_err))
        log_read_data = dt_read(LOG_READ_DT_WORD);
}
#endif

#ifdef TAU_LOG_TABLE_PROBE
static void read_result_back2(void)
{
    log_read2_state = log_read2_err = log_read2_data = 0u;
    dt_write(LOG_READ_DT_WORD, 0u);
    if (target_cmd(TGT_READ, LOG_SLOT_ID, 0u, LOG_READ_BRIDGE_ADDR, 4u,
                   SDR_TIMEOUT, &log_read2_state, &log_read2_err))
        log_read2_data = dt_read(LOG_READ_DT_WORD);
}
#endif

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
#ifdef TAU_CPU_WINDOW_DIAG
    /* Keep the exact store width visible to the CPU Wishbone byte enables.
     * The smoke test invokes full-word stores through this helper and byte/
     * halfword stores through the dedicated helpers below. */
    if (byte_en != 15u) return 0;
    *(volatile uint32_t *)(uintptr_t)word_addr = value;
    return 1;
#else
    return issue(word_addr, value, byte_en, 1u);
#endif
}

static int read32(uint32_t word_addr, uint32_t *value)
{
#ifdef TAU_CPU_WINDOW_DIAG
    *value = *(volatile uint32_t *)(uintptr_t)word_addr;
    return 1;
#else
    if (!issue(word_addr, 0u, 15u, 0u)) return 0;
    *value = REG(R_SDR_RDATA);
    return 1;
#endif
}

#ifdef TAU_CPU_WINDOW_DIAG
static int write8(uint32_t addr, uint8_t value)
{
    *(volatile uint8_t *)(uintptr_t)addr = value;
    return 1;
}

static int write16(uint32_t addr, uint16_t value)
{
    *(volatile uint16_t *)(uintptr_t)addr = value;
    return 1;
}
#endif

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
#ifdef TAU_CPU_WINDOW_DIAG
    int ok;
    if (byte_en == 1u) ok = write8(addr, (uint8_t)value);
    else if (byte_en == 2u) ok = write8(addr + 1u, (uint8_t)(value >> 8));
    else if (byte_en == 4u) ok = write8(addr + 2u, (uint8_t)(value >> 16));
    else if (byte_en == 8u) ok = write8(addr + 3u, (uint8_t)(value >> 24));
    else if (byte_en == 3u) ok = write16(addr, (uint16_t)value);
    else if (byte_en == 12u) ok = write16(addr + 2u, (uint16_t)(value >> 16));
    else ok = 0;
    if (!ok) {
        ++tests_run;
        record_failure(addr, expected, 0xDEAD0003u);
        return;
    }
#else
    if (!write32(addr, value, byte_en)) {
        ++tests_run;
        record_failure(addr, expected, 0xDEAD0003u);
        return;
    }
#endif
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
#ifdef TAU_CPU_WINDOW_DIAG
    fb_text(28, 38, "TAU CPU SDRAM TEST", UI_ACCENT, UI_PANEL);
#else
    fb_text(28, 38, "TAU SDRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
#endif
    fb_text(28, 78, "RTL VERSION MISMATCH", UI_RED, UI_PANEL);
    fb_text(28, 120, "EXPECTED", UI_DIM, UI_PANEL);
    hex8(hex, EXPECT_VERSION);
    fb_text(220, 120, hex, UI_WHITE, UI_PANEL);
    fb_text(28, 146, "ACTUAL", UI_DIM, UI_PANEL);
    hex8(hex, actual);
    fb_text(220, 146, hex, UI_RED, UI_PANEL);
    fb_text(28, 306, "REBUILD OR REINSTALL RBF", UI_DIM, UI_PANEL);
}

#ifdef TAU_CPU_WINDOW_DIAG
static void draw_preflight_failure(uint32_t actual)
{
    char hex[9];
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 38, "TAU CPU SDRAM TEST", UI_ACCENT, UI_PANEL);
    fb_text(28, 78, "MAILBOX PREFLIGHT FAIL", UI_RED, UI_PANEL);
    fb_text(28, 112, "MUX OR CDC BRIDGE PATH", UI_DIM, UI_PANEL);
    fb_text(28, 138, "CPU WINDOW NOT ATTEMPTED", UI_DIM, UI_PANEL);
    fb_text(28, 180, "ACTUAL", UI_DIM, UI_PANEL);
    hex8(hex, actual);
    fb_text(220, 180, hex, UI_RED, UI_PANEL);
    fb_text(28, 306, "REBUILD REQUIRED", UI_DIM, UI_PANEL);
}

#ifdef TAU_CPU_WINDOW_READBACK_DIAG
static void draw_cpu_preflight_readback_failure(uint32_t actual)
{
    char hex[9];
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
    fb_text(28, 38, "TAU CPU SDRAM TEST", UI_ACCENT, UI_PANEL);
    fb_text(28, 78, "CPU PREFLIGHT READ FAIL", UI_RED, UI_PANEL);
    fb_text(28, 112, "MAILBOX WROTE 2 MIB", UI_DIM, UI_PANEL);
    fb_text(28, 138, "CPU READS SAME WORD", UI_DIM, UI_PANEL);
    fb_text(28, 180, "EXPECTED", UI_DIM, UI_PANEL);
    hex8(hex, PREFLIGHT_PATTERN);
    fb_text(220, 180, hex, UI_WHITE, UI_PANEL);
    fb_text(28, 206, "ACTUAL", UI_DIM, UI_PANEL);
    hex8(hex, actual);
    fb_text(220, 206, hex, UI_RED, UI_PANEL);
    fb_text(28, 306, "REBUILD REQUIRED", UI_DIM, UI_PANEL);
}
#endif

static int mailbox_preflight(void)
{
    uint32_t actual = 0u;
    timed_out = 0u;
    if (!issue(PREFLIGHT_SDR_WORD_ADDR, PREFLIGHT_PATTERN, 15u, 1u) ||
        !issue(PREFLIGHT_SDR_WORD_ADDR, 0u, 15u, 0u)) {
        draw_preflight_failure(timed_out ? 0xDEAD0001u : 0xDEAD0002u);
        return 0;
    }
    actual = REG(R_SDR_RDATA);
    if (actual != PREFLIGHT_PATTERN) {
        draw_preflight_failure(actual);
        return 0;
    }
    return 1;
}
#endif

#ifdef TAU_DISCRIMINATOR_PROBE
/* A-092: separate the proven mailbox path from the CPU window.  Byte offset
 * `off` from 2 MiB is halfword address 0x100000 + off/2 for the mailbox.
 * Distinct patterns (not only 0/FFFFFFFF) expose lag, swap or shift.
 * Raw results are published as 15 words (`disc_words`), no matrix is run. */
#define DISC_BASE_HW  0x00100000u
#define DISC_A        0x1000u
#define DISC_B        0x2000u
#define DISC_C        0x3000u
#define DISC_D        0x4000u

static int mb_write(uint32_t off, uint32_t value)
{
    return issue(DISC_BASE_HW + (off >> 1), value, 15u, 1u);
}
static int mb_read(uint32_t off, uint32_t *value)
{
    if (!issue(DISC_BASE_HW + (off >> 1), 0u, 15u, 0u)) return 0;
    *value = REG(R_SDR_RDATA);
    return 1;
}
static uint32_t cpu_rd(uint32_t off)
{
    return *(volatile uint32_t *)(uintptr_t)(TEST_BASE_WORD + off);
}
static void cpu_wr(uint32_t off, uint32_t value)
{
    *(volatile uint32_t *)(uintptr_t)(TEST_BASE_WORD + off) = value;
}

static void run_discriminator(void)
{
    uint32_t fail = 0u, v;
    for (uint32_t i = 0; i < 16u; ++i) disc_words[i] = 0u;
    disc_words[0] = 0x44534331u;                          /* "DSC1" */
    /* w1/w2: mailbox write, mailbox read; then the CPU reads it */
    fail |= (uint32_t)!mb_write(DISC_A, 0x12345678u) << 1;
    v = 0u; fail |= (uint32_t)!mb_read(DISC_A, &v) << 2; disc_words[1] = v;
    disc_words[2] = cpu_rd(DISC_A);
    /* w3..w7: CPU write of a distinctive word between marked neighbours */
    fail |= (uint32_t)!mb_write(DISC_B - 4u, 0x0DEFACEDu) << 3;
    fail |= (uint32_t)!mb_write(DISC_B + 4u, 0x0BADF00Du) << 4;
    cpu_wr(DISC_B, 0xA5C33C5Au);
    v = 0u; fail |= (uint32_t)!mb_read(DISC_B, &v) << 5; disc_words[3] = v;
    disc_words[4] = cpu_rd(DISC_B);
    disc_words[5] = cpu_rd(DISC_B);
    disc_words[6] = cpu_rd(DISC_B + 4u);
    disc_words[7] = cpu_rd(DISC_B - 4u);
    /* w8: mailbox all-ones, CPU read */
    fail |= (uint32_t)!mb_write(DISC_C, 0xFFFFFFFFu) << 6;
    disc_words[8] = cpu_rd(DISC_C);
    /* w9..w12: CPU all-ones then zero, checked from both sides */
    cpu_wr(DISC_D, 0xFFFFFFFFu);
    v = 0u; fail |= (uint32_t)!mb_read(DISC_D, &v) << 7; disc_words[9] = v;
    disc_words[10] = cpu_rd(DISC_D);
    cpu_wr(DISC_D, 0u);
    disc_words[11] = cpu_rd(DISC_D);
    v = 0u; fail |= (uint32_t)!mb_read(DISC_D, &v) << 8; disc_words[12] = v;
    disc_words[13] = fail;                                /* failed mailbox ops */
    disc_words[14] = cpu_rd(DISC_A);                      /* w2 again, at the end */
    tests_run = 14u;
    failures = 0u;
}
#endif

#ifdef TAU_LATENCY_PROBE
/* A-094: cost of the uncached SDRAM CPU window, measured with the 60 MHz core
 * cycle counter while scanout keeps running.  Region 2-3 MiB, N ops per test.
 * disc_words: 0 "LAT1", 1 N, 2 empty-loop total, 3 seq write total, 4 seq read
 * total, 5 stride-0x1000 write total, 6 stride-0x1000 read total, 7 write+read
 * same word total, 8 read-modify-write total, 9 min read cycles, 10 max read
 * cycles, 11 max write cycles, 12 stride-0x40000 read total (bank/row hop),
 * 13 data mismatches, 14 0. */
#define LAT_N 256u
static void run_latency(void)
{
    volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)TEST_BASE_WORD;
    uint32_t t0, t1, i, mism = 0u, rmin = 0xFFFFFFFFu, rmax = 0u, wmax = 0u;
    volatile uint32_t sink = 0u;
    for (i = 0; i < 16u; ++i) disc_words[i] = 0u;
    disc_words[0] = 0x4C415431u;                                   /* "LAT1" */
    disc_words[1] = LAT_N;
    t0 = cycles();
    for (i = 0; i < LAT_N; ++i) sink = i;                          /* loop cost */
    disc_words[2] = cycles() - t0;
    t0 = cycles();
    for (i = 0; i < LAT_N; ++i) base[i] = 0x51000000u ^ i;
    disc_words[3] = cycles() - t0;
    t0 = cycles();
    for (i = 0; i < LAT_N; ++i) sink ^= base[i];
    disc_words[4] = cycles() - t0;
    for (i = 0; i < LAT_N; ++i) {                                  /* per-op */
        uint32_t a = cycles();
        uint32_t v = base[i];
        uint32_t d = cycles() - a;
        if (v != (0x51000000u ^ i)) ++mism;
        if (d < rmin) rmin = d;
        if (d > rmax) rmax = d;
    }
    t0 = cycles();
    for (i = 0; i < LAT_N; ++i) base[i * 0x400u] = i;              /* 4 KiB stride */
    disc_words[5] = cycles() - t0;
    t0 = cycles();
    for (i = 0; i < LAT_N; ++i) if (base[i * 0x400u] != i) ++mism;
    disc_words[6] = cycles() - t0;
    t0 = cycles();
    for (i = 0; i < LAT_N; ++i) { base[0x100u] = i; sink ^= base[0x100u]; }
    disc_words[7] = cycles() - t0;
    t0 = cycles();
    for (i = 0; i < LAT_N; ++i) base[i] = base[i] + 1u;
    disc_words[8] = cycles() - t0;
    for (i = 0; i < LAT_N; ++i) {                                  /* write max */
        uint32_t a = cycles();
        base[0x200u + i] = i;
        uint32_t d = cycles() - a;
        if (d > wmax) wmax = d;
    }
    disc_words[9] = rmin;
    disc_words[10] = rmax;
    disc_words[11] = wmax;
    t0 = cycles();
    for (i = 0; i < 4u; ++i) base[i * 0x10000u] = i;               /* 256 KiB stride */
    for (i = 0; i < 4u; ++i) sink ^= base[i * 0x10000u];
    disc_words[12] = cycles() - t0;
    disc_words[13] = mism;
    (void)sink;
    tests_run = LAT_N; failures = 0u;
}
#endif

static void run_tests(void)
{
#ifdef TAU_DISCRIMINATOR_PROBE
    run_discriminator();
    return;
#endif
#ifdef TAU_LATENCY_PROBE
    run_latency();
    return;
#endif
    static const uint32_t patterns[4] = {
        0x00000000u, 0xFFFFFFFFu, 0xAAAAAAAAu, 0x55555555u
    };
    static const uint32_t anchors[12] = {
        0x000u, 0x004u, 0x0FCu, 0x100u,
        0x7FCu, 0x800u, 0x1FFCu, 0x2000u,
        0x7FFCu, 0x8000u, 0xFFFCu, 0x10000u
    };
    tests_run = failures = first_fail_addr = 0u;
    first_fail_expected = first_fail_actual = timed_out = 0u;

    draw_running(0u, "FIXED PATTERNS");
    for (uint32_t a = 0; a < 12u && !timed_out; ++a)
        for (uint32_t p = 0; p < 4u && !timed_out; ++p)
            write_expect(TEST_BASE_WORD + anchors[a], patterns[p]);

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
        uint32_t addr = TEST_BASE_WORD + i * TEST_ADDR_STRIDE;
        if (addr > TEST_LAST_WORD) addr = TEST_LAST_WORD - (i & 0x1Eu);
        if (!write32(addr, 0xA5000000u ^ addr, 15u))
            record_failure(addr, 0xA5000000u ^ addr, 0xDEAD0002u);
    }
    for (uint32_t i = 0; i < 64u && !timed_out; ++i) {
        uint32_t addr = TEST_BASE_WORD + i * TEST_ADDR_STRIDE;
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
#ifdef TAU_LATENCY_PROBE
    {
        char line[28], *q;
        static const char *const names[] = {
            "LOOP ONLY   ", "SEQ WRITE   ", "SEQ READ    ", "STRIDE4K WR ",
            "STRIDE4K RD ", "WR+RD SAME  ", "READ-MOD-WR ", "" };
        fb_rect(12, 18, 376, 324, UI_PANEL);
        fb_text(28, 30, "TAU SDRAM LATENCY A094", UI_ACCENT, UI_PANEL);
        fb_text(28, 50, "CYCLES PER OP AT 60 MHZ", UI_DIM, UI_PANEL);
        for (uint32_t k = 0; k < 7u; ++k) {
            uint32_t total = disc_words[2u + k];
            q = line;
            for (const char *n = names[k]; *n; ++n) *q++ = *n;
            q = append_dec(q, total / LAT_N);
            *q++ = '.';
            q = append_dec(q, ((total % LAT_N) * 10u) / LAT_N);
            *q = 0;
            fb_text(28, 76u + k * 22u, line, UI_WHITE, UI_PANEL);
        }
        q = line; *q++ = 'R'; *q++ = 'D'; *q++ = ' '; *q++ = 'M'; *q++ = 'I'; *q++ = 'N'; *q++ = ' ';
        q = append_dec(q, disc_words[9]); *q++ = ' '; *q++ = 'M'; *q++ = 'A'; *q++ = 'X'; *q++ = ' ';
        q = append_dec(q, disc_words[10]); *q = 0;
        fb_text(28, 236, line, UI_WHITE, UI_PANEL);
        q = line; *q++ = 'W'; *q++ = 'R'; *q++ = ' '; *q++ = 'M'; *q++ = 'A'; *q++ = 'X'; *q++ = ' ';
        q = append_dec(q, disc_words[11]); *q++ = ' '; *q++ = 'B'; *q++ = 'A'; *q++ = 'D'; *q++ = ' ';
        q = append_dec(q, disc_words[13]); *q = 0;
        fb_text(28, 258, line, disc_words[13] ? UI_RED : UI_WHITE, UI_PANEL);
        fb_text(28, 290, "QUIT TO SAVE  A RUN AGAIN", UI_DIM, UI_PANEL);
        return;
    }
#endif
#ifdef TAU_DISCRIMINATOR_PROBE
    {
        char line[16];
        fb_rect(12, 18, 376, 324, UI_PANEL);
        fb_text(28, 30, "TAU SDRAM DISCRIMINATOR", UI_ACCENT, UI_PANEL);
        for (uint32_t i = 0; i < 16u; ++i) {
            uint32_t col = i < 8u ? 28u : 208u;
            uint32_t y = 62u + (i & 7u) * 22u;
            line[0] = hex_digit(i); line[1] = ' ';
            hex8(&line[2], i == 15u ? set_readback[3] : disc_words[i]);
            fb_text(col, y, line, UI_WHITE, UI_PANEL);
        }
        fb_text(28, 250, "W1 MB  W2 CPU  W3 MB RD OF CPU WR", UI_DIM, UI_PANEL);
        fb_text(28, 270, "W4 W5 CPU RD  W6 W7 NEIGHBOURS", UI_DIM, UI_PANEL);
        fb_text(28, 290, "W8 MB WR CPU RD  W9 W10 FFFF  W11 W12 ZERO", UI_DIM, UI_PANEL);
        fb_text(28, 320, "QUIT TO SAVE  A RUN AGAIN", UI_DIM, UI_PANEL);
        return;
    }
#endif
    fb_rect(12, 18, 376, 324, UI_PANEL);
#ifdef TAU_CPU_WINDOW_DIAG
    fb_text(28, 38, "TAU CPU SDRAM TEST", UI_ACCENT, UI_PANEL);
#else
    fb_text(28, 38, "TAU SDRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
#endif
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
#ifdef TAU_CPU_WINDOW_DIAG
        fb_text(28, 180, "FIRST BYTE ADDR", UI_DIM, UI_PANEL);
#else
        fb_text(28, 180, "FIRST WORD ADDR", UI_DIM, UI_PANEL);
#endif
        fb_text(220, 180, hex, UI_WHITE, UI_PANEL);
        hex8(hex, first_fail_expected);
        fb_text(28, 206, "EXPECTED", UI_DIM, UI_PANEL);
        fb_text(220, 206, hex, UI_WHITE, UI_PANEL);
        hex8(hex, first_fail_actual);
        fb_text(28, 232, timed_out ? "TIMEOUT CODE" : "ACTUAL", UI_DIM, UI_PANEL);
        fb_text(220, 232, hex, color, UI_PANEL);
    } else {
#ifdef TAU_CPU_WINDOW_DIAG
        fb_text(28, 190, "CPU WINDOW 2-3 MIB", UI_DIM, UI_PANEL);
        fb_text(28, 216, "WORD BYTE HALFWORD LANES", UI_DIM, UI_PANEL);
#else
        fb_text(28, 190, "TEST REGION ABOVE 1 MIB", UI_DIM, UI_PANEL);
        fb_text(28, 216, "FIXED WALK ADDRESS LANES", UI_DIM, UI_PANEL);
#endif
    }
#ifdef TAU_LOG_COMMAND_PROBE
    {
        char line[16] = "OPEN ? ERR ?";
        /* Keep all lifecycle evidence visible on one screen.  An answered
         * command with a nonzero APF result is red, not a misleading pass. */
        line[5] = ' '; line[6] = log_open_state == 1u ? 'D' :
                              (log_open_state == 2u ? 'T' : '-');
        line[7] = ' '; line[8] = 'E'; line[9] = 'R'; line[10] = 'R';
        line[11] = ' '; line[12] = hex_digit(log_open_err); line[13] = 0;
        fb_text(28, 248, line,
                (log_open_state == 1u && log_open_err == 0u) ? UI_WHITE : UI_RED,
                UI_PANEL);
        line[0] = 'R'; line[1] = 'E'; line[2] = 'A'; line[3] = 'D'; line[4] = 'Y';
        line[5] = ' '; line[6] = log_ready_state == 1u ? 'D' :
                              (log_ready_state == 2u ? 'T' : '-');
        line[7] = ' '; line[8] = '#';
        line[9] = (char)('0' + ((log_ready_tries / 10u) % 10u));
        line[10] = (char)('0' + (log_ready_tries % 10u)); line[11] = 0;
        fb_text(28, 264, line, log_ready_state == 1u ? UI_WHITE : UI_RED, UI_PANEL);
        line[0] = 'W'; line[1] = 'R'; line[2] = 'I'; line[3] = 'T'; line[4] = 'E';
        line[5] = ' '; line[6] = log_write_state == 1u ? 'D' :
                              (log_write_state == 2u ? 'T' : '-');
        line[7] = ' '; line[8] = 'E'; line[9] = 'R'; line[10] = 'R';
        line[11] = ' '; line[12] = hex_digit(log_write_err); line[13] = 0;
        fb_text(28, 280, line,
                (log_write_state == 1u && log_write_err == 0u) ? UI_WHITE : UI_RED,
                UI_PANEL);
        line[0] = 'F'; line[1] = 'L'; line[2] = 'U'; line[3] = 'S'; line[4] = 'H';
        line[5] = ' '; line[6] = log_flush_state == 1u ? 'D' :
                              (log_flush_state == 2u ? 'T' : '-');
        line[7] = ' '; line[8] = 'E'; line[9] = 'R'; line[10] = 'R';
        line[11] = ' '; line[12] = hex_digit(log_flush_err); line[13] = 0;
        fb_text(28, 296, line,
                (log_flush_state == 1u && log_flush_err == 0u) ? UI_WHITE : UI_RED,
                UI_PANEL);
#ifdef TAU_LOG_READBACK_PROBE
        {
            char read_line[27] = "READ ? ERR ? DATA 00000000";
            read_line[5] = log_read_state == 1u ? 'D' :
                           (log_read_state == 2u ? 'T' : '-');
            read_line[11] = hex_digit(log_read_err);
            hex8(&read_line[18], log_read_data);
            fb_text(28, 312, read_line,
                    (log_read_state == 1u && log_read_err == 0u) ? UI_WHITE : UI_RED,
                    UI_PANEL);
        }
#ifdef TAU_LOG_TABLE_PROBE
        {
            char a[28], b[28];
            uint32_t ms = log_flush_cycles / (CLK_HZ / 1000u);
            a[0] = 'T'; a[1] = '5'; a[2] = ' '; a[3] = 'B'; a[4] = ' ';
            hex8(&a[5], log_tbl_size_before); a[13] = ' '; a[14] = 'A'; a[15] = ' ';
            hex8(&a[16], log_tbl_size_after); a[24] = ' ';
            a[25] = log_tbl_hash_before == log_tbl_hash_after ? '=' : '!'; a[26] = 0;
            fb_text(28, 328, a, log_tbl_hash_before == log_tbl_hash_after ?
                    UI_WHITE : UI_RED, UI_PANEL);
            b[0] = 'F'; b[1] = ' ';
            b[2] = (char)('0' + ((ms / 10000u) % 10u));
            b[3] = (char)('0' + ((ms / 1000u) % 10u));
            b[4] = (char)('0' + ((ms / 100u) % 10u));
            b[5] = (char)('0' + ((ms / 10u) % 10u));
            b[6] = (char)('0' + (ms % 10u));
            b[7] = 'M'; b[8] = 'S'; b[9] = ' '; b[10] = 'R'; b[11] = '2';
            b[12] = ' '; b[13] = log_read2_state == 1u ? 'D' :
                                 (log_read2_state == 2u ? 'T' : '-');
            b[14] = ' '; hex8(&b[15], log_read2_data); b[23] = 0;
            fb_text(28, 340, b, (log_read2_state == 1u && log_read2_err == 0u) ?
                    UI_WHITE : UI_RED, UI_PANEL);
        }
#endif
#ifdef TAU_LOG_SOURCE_PROBE
        {
            char src_line[20];
            src_line[0] = 'S'; src_line[1] = ' ';
            hex8(&src_line[2], log_src[0]); src_line[10] = ' ';
            hex8(&src_line[11], log_src[1]);
            fb_text(28, 328, src_line, UI_WHITE, UI_PANEL);
            src_line[0] = 'S'; src_line[1] = ' ';
            hex8(&src_line[2], log_src[2]); src_line[10] = ' ';
            hex8(&src_line[11], log_src[3]);
            fb_text(28, 340, src_line, UI_WHITE, UI_PANEL);
        }
#endif
#else
        fb_text(28, 306, "A  RUN AGAIN", UI_DIM, UI_PANEL);
#endif
    }
#elif defined(TAU_LOG_INTERACT_PROBE)
    {
        char line[24];
        fb_text(28, 248, "PUBLISHED 16 WORDS", UI_WHITE, UI_PANEL);
        line[0] = 'W'; line[1] = '0'; line[2] = ' ';
        hex8(&line[3], set_readback[0]);
        fb_text(28, 264, line, UI_WHITE, UI_PANEL);
        line[0] = 'W'; line[1] = '7'; line[2] = ' ';
        hex8(&line[3], set_readback[1]);
        fb_text(28, 280, line, UI_WHITE, UI_PANEL);
        line[0] = 'W'; line[1] = 'C'; line[2] = ' ';
        hex8(&line[3], set_readback[2]);
        fb_text(28, 296, line, UI_WHITE, UI_PANEL);
        line[0] = 'W'; line[1] = 'F'; line[2] = ' ';
        hex8(&line[3], set_readback[3]);
        fb_text(28, 312, line, UI_WHITE, UI_PANEL);
        fb_text(28, 328, "QUIT TO SAVE  A RUN AGAIN", UI_DIM, UI_PANEL);
    }
#else
    fb_text(28, 306, "A  RUN AGAIN", UI_DIM, UI_PANEL);
#endif
}

static void draw_initial(void)
{
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_rect(12, 18, 376, 324, UI_PANEL);
#ifdef TAU_CPU_WINDOW_DIAG
    fb_text(28, 38, "TAU CPU SDRAM TEST", UI_ACCENT, UI_PANEL);
    fb_text(28, 78, "PHASE 2 UNCACHED WINDOW", UI_WHITE, UI_PANEL);
    fb_text(28, 112, "SAFE REGION 2-3 MIB", UI_DIM, UI_PANEL);
    fb_text(28, 138, "CPU LOAD STORE LANES", UI_DIM, UI_PANEL);
#else
    fb_text(28, 38, "TAU SDRAM DIAGNOSTIC", UI_ACCENT, UI_PANEL);
    fb_text(28, 78, "PHASE 1 MAILBOX TEST", UI_WHITE, UI_PANEL);
    fb_text(28, 112, "SAFE REGION 1-2 MIB", UI_DIM, UI_PANEL);
    fb_text(28, 138, "PLAYER DATA UNCHANGED", UI_DIM, UI_PANEL);
#endif
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

    /* Make the fixed diagnostic result file the active slot before either the
     * initial run or a repeat.  A-083 showed that the instance binding alone
     * is not sufficient for target read/write commands. */
#ifndef TAU_LOG_INTERACT_PROBE
    (void)open_log_slot();
    (void)wait_log_slot_ready();
#endif

    /* Draw before the RTL version interlock so a mismatch cannot turn into an
     * unexplained black screen. */
    draw_initial();
    uint32_t version = REG(R_VERSION);
    if (version != EXPECT_VERSION) {
        REG(R_STAT0) = 0xBAD00016u;
        REG(R_STAT1) = version;
        draw_version_mismatch(version);
        (void)save_result(1u);             /* version interlock */
        for (;;) { }
    }

    uint32_t pause = cycles();
    while ((uint32_t)(cycles() - pause) < CLK_HZ / 4u) { }
#ifdef TAU_CPU_WINDOW_DIAG
    draw_running(0u, "MAILBOX PREFLIGHT");
    if (!mailbox_preflight()) {
        (void)save_result(2u);             /* mailbox preflight */
        for (;;) { }
    }
    draw_running(0u, "MAILBOX OK CPU WINDOW NEXT");
    pause = cycles();
    while ((uint32_t)(cycles() - pause) < CLK_HZ) { }
#ifdef TAU_CPU_WINDOW_READBACK_DIAG
    {
        uint32_t actual = 0u;
        draw_running(0u, "CPU READS PREFLIGHT WORD");
        if (!read32(TEST_BASE_WORD, &actual) || actual != PREFLIGHT_PATTERN) {
            draw_cpu_preflight_readback_failure(actual);
            first_fail_addr = TEST_BASE_WORD;
            first_fail_expected = PREFLIGHT_PATTERN;
            first_fail_actual = actual;
            failures = 1u;
            (void)save_result(3u);         /* CPU preflight readback */
            for (;;) { }
        }
    }
#endif
#endif
    run_tests();
#if defined(TAU_LOG_COMMAND_PROBE) || defined(TAU_LOG_INTERACT_PROBE)
    (void)save_result(4u);                 /* show target write/flush outcome */
#ifdef TAU_LOG_READBACK_PROBE
    read_result_back();
    (void)flush_result();
#ifdef TAU_LOG_TABLE_PROBE
    read_result_back2();
#endif
#endif
    draw_result();
#else
    draw_result();
    (void)save_result(4u);
#endif

    uint32_t old_keys = 0u;
    for (;;) {
        uint32_t keys = REG(R_INPUT) & 0xFFFFu;
        if ((keys & KEY_A) && !(old_keys & KEY_A)) {
            draw_initial();
#ifndef TAU_LOG_INTERACT_PROBE
            (void)open_log_slot();
            (void)wait_log_slot_ready();
#endif
            run_tests();
#if defined(TAU_LOG_COMMAND_PROBE) || defined(TAU_LOG_INTERACT_PROBE)
            (void)save_result(4u);
#ifdef TAU_LOG_READBACK_PROBE
            read_result_back();
            (void)flush_result();
#ifdef TAU_LOG_TABLE_PROBE
            read_result_back2();
#endif
#endif
            draw_result();
#else
            draw_result();
            (void)save_result(4u);
#endif
        }
        old_keys = keys;
    }
}
