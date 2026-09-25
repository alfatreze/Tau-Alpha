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
 * COLD_READY() checks, never blindly.
 *
 * G4 step 4 (TAU_G4 >= 3, B-199/B-200/B-201): a cold function on the PER-AUDIO-FRAME path is fine
 * if its worst-case cost is small relative to the per-frame budget -- measured, not assumed. This
 * corrects the original Phase G1 wording ("nothing that runs per audio frame may be cold"), written
 * a full day before the blit engine existed, against a total unknown. Three real hardware
 * measurements (PHASE_F_SPEC.md sections 4.2-4.3) settled it: a small I-cache-resident probe under
 * ordinary playback (3,309 cycles worst-case), the same probe under a forced full-eviction-every-
 * call worst case simulating browse-while-playing (3,067 cycles -- no thrashing amplification found),
 * and a deliberately always-cold 12 KB probe (94,801 cycles, matching the 31.6-cycles/word model to
 * within 1 cycle) -- all three PASS, zero late underruns, against a ~1,578,947-cycle (26.3 ms)
 * budget. `ui_draw_dynamic()` (fw/player.c) is the first real use of this, kept behind its own G4
 * tier rather than folded into TAU_G4>=2 because it is qualitatively different from every other G4
 * step: those only ever run from menus/settings (rare, recoverable); this runs continuously during
 * ordinary playback. Anything ELSE considered for this path should get the same measurement
 * treatment first, not be assumed safe by analogy. */
#define COLD_TEXT __attribute__((section(".cold_text"), noinline))
static uint8_t cold_code_ok;
#define COLD_READY() (cold_code_ok != 0u)
COLD_TEXT static uint32_t cold_probe(uint32_t a, uint32_t b) { return a * b + 1u; }

/* Free-running accumulator, read and reset by fw/suite.inc's CT_COLDFRAME -- always declared (not
 * just under TAU_COLD_FRAME_PROBE) because TAU_G4>=3's real conversion below uses it directly too,
 * turning CT_COLDFRAME from a one-off synthetic-probe experiment into a permanent regression guard
 * on the real ui_draw_dynamic_cold(), the same role CT_BLT plays for the blit engine's own SDRAM
 * traffic. Same "shared plain globals, no locking, single-threaded main loop" convention every
 * other Check accumulator here already uses. */
static uint32_t coldframe_calls, coldframe_cyc_min = 0xFFFFFFFFu, coldframe_cyc_max, coldframe_cyc_sum;
static void coldframe_record(uint32_t dt)
{
    coldframe_calls++;
    coldframe_cyc_sum += dt;
    if (dt < coldframe_cyc_min) coldframe_cyc_min = dt;
    if (dt > coldframe_cyc_max) coldframe_cyc_max = dt;
}

#if TAU_DIAG_TESTS || TAU_COLD_FRAME_PROBE
/* cold_big moved out from under TAU_DIAG_TESTS alone (B-199): it is also the deliberate worst-case
 * probe for the per-audio-frame cold-code measurement (PHASE_F_SPEC.md section 4.2), which needs it
 * without pulling in the rest of the Diagnostics > Tests > COLD CODE TEST machinery below. */
/* 3,000 instructions (12 KB): three times the 4 KiB I-cache, so every run refills all of it. */
__asm__(".pushsection .cold_text,\"ax\",@progbits\n"
        ".globl cold_big\n.type cold_big,@function\ncold_big:\n"
        ".rept 3000\n addi a0, a0, 1\n.endr\n ret\n.size cold_big, .-cold_big\n.popsection\n");
extern uint32_t cold_big(uint32_t x);
#endif

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
#endif

#if TAU_COLD_FRAME_PROBE
/* B-199 (PHASE_F_SPEC.md section 4.2): per-audio-frame cold-code cost measurement. 100 instructions
 * (~400 B) -- well inside the 4 KiB / 128-line I-cache, the "small, expected to stay resident"
 * bound this measurement is meant to answer, distinct from cold_big's deliberate 3x-oversize
 * worst case above. Neither probe does real work; only the fetch/dispatch cost is being measured,
 * exactly like cold_big's own existing convention. */
__asm__(".pushsection .cold_text,\"ax\",@progbits\n"
        ".globl cold_frame_small\n.type cold_frame_small,@function\ncold_frame_small:\n"
        ".rept 100\n addi a0, a0, 1\n.endr\n ret\n.size cold_frame_small, .-cold_frame_small\n.popsection\n");
extern uint32_t cold_frame_small(uint32_t x);

/* Called once per ui_draw_dynamic() invocation (fw/player.c) when this macro is on -- the same
 * call frequency (~38 Hz, FL_UI_PERIOD) the real "meters go cold" question is about. Picks the
 * probe via COLDFRAME_BIG (a separate build-time toggle) so both bounds can be measured without
 * two firmware variants sharing a single macro.
 *
 * B-199 section 4.3: COLDFRAME_EVICT simulates the browse-while-playing worst case -- some OTHER
 * cold function (a settings/library redraw) having just run immediately before this meter draw --
 * without driving real UI navigation. cold_big() is called untimed and its result discarded purely
 * to force a full I-cache eviction (it is 3x the cache's own capacity by construction, so this is
 * unconditional, not probabilistic); only the probe call that follows is timed. This is a harsher
 * interleaving than real browsing produces (real browsing does not evict on every single tick),
 * making it a genuine upper bound, not a realistic-frequency simulation. */
static void coldframe_tick(void)
{
    if (!COLD_READY()) return;   /* same fail-safe as every other cold call site: no cold, no probe */
#if COLDFRAME_EVICT
    (void)cold_big(0xEEu);   /* untimed, discarded -- see the comment above */
#endif
    uint32_t t0 = cycles();
#if COLDFRAME_BIG
    (void)cold_big(t0);
#else
    (void)cold_frame_small(t0);
#endif
    coldframe_record(cycles() - t0);
}
#else
static void coldframe_tick(void) {}
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
static void coldframe_tick(void) {}   /* B-199: no cold code in this build at all, so nothing to measure */
#endif
