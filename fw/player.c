        /* The playlist-switch fields (pl_sw_*) are gone for good along with legacy playlist mode
         * itself, not just the display row: that investigation shipped in v1.2.0, and the row's
         * width was needed for the throughput and heap figures the FLAC decision turns on. tk_hist
         * is still recorded (it covers ordinary track loads too, not only a playlist switch). */
// =============================================================================
// Stage 3 -- real MP3 playback.
//
// Pipeline:
//   SD card --(APF 0180, arbitrary offset)--> MP3 ring buffer in RAM
//           --(Helix fixed-point decode)-----> PCM frame (1152 samples)
//           --(burst push)-------------------> pcm_fifo, drained in HARDWARE
//                                              at the file's true sample rate
//
// The hardware FIFO is what makes this possible at all: a decode call blocks
// for ~1.1M cycles (~22 ms) while the DAC needs a sample every ~21 us, so
// firmware can never feed the DAC directly. It pushes a frame at a time and
// hardware handles timing.
//
// Refill policy: top the MP3 ring up whenever it drops below half, and only
// while the PCM FIFO still has a cushion. Analogue's docs warn the first access
// to a slot walks the FAT cluster chain and that the seek cache is dropped when
// switching slots, so reads can occasionally be slow -- the PCM FIFO is what
// absorbs that jitter.
// =============================================================================

#include <stdint.h>
/* B-333 (RAM shrink to 192 KB, docs/RAM_SHRINK_192K_PLAN.md): a 192 KB link compiles this file for SIZE (-Os, about 5.5 KB of hot code saved)
 * and keeps the audio-critical functions at -O2 (HOT_O2). Every other build is unchanged and byte-identical. The decoders (Helix, flac.c)
 * are separate translation units and stay -O2. */
#if TAU_RAM_192K_FW
#pragma GCC optimize ("Os")
#define HOT_O2 __attribute__((optimize("O2")))
#else
#define HOT_O2
#endif
/* The shrink's other hot-RAM savings, all under the same switch so the normal 256 KB builds stay byte-identical until the 192 KB
 * bitstream ships (a 192 KB firmware also runs on today's 256 KB bitstream, so it can be tested there first):
 *   COLD_SR   a function moved to PSRAM (cold code); its hot callers are guarded with SR_READY() (the cold image is loaded).
 *             "No cold image, nothing works" is the owner's decision (2026-09-25); these paths simply do nothing without it.
 *   DIV64     64-bit division through udiv64() instead of libgcc's __udivdi3 (1,128 B). */
#if TAU_RAM_192K_FW
#define COLD_SR __attribute__((section(".cold_text"), noinline))
#define SR_READY() (cold_code_ok != 0u)
#define DIV64(n, d) udiv64((n), (d))
#else
#define COLD_SR
#define SR_READY() 1u
#define DIV64(n, d) ((n) / (d))
#endif
#include "mp3dec.h"
#include "font_metrics.h"
/* Up here, not down beside the FLAC glue where it used to sit. The diagnostic
 * row is ~800 lines ABOVE that point and reads flac_order, and C would have
 * taken the undeclared name as an error -- the same ordering trap that once
 * dropped the IO_BENCH readout with no warning at all. Nothing in these two
 * headers depends on anything in this file. */
#include <stdlib.h>                /* malloc/free -- fw/alloc.c provides them */
#include "flac.h"
#if MP3_PROFILE
#include "mp3_profile.h"
#endif

#define REG(a)      (*(volatile uint32_t *)(uintptr_t)(a))

#define R_AUDIO     0x80000008u
#define R_CYCLES    0x8000000Cu
#define R_STAT0     0x80000010u
#define R_STAT1     0x80000014u
#define R_STAT2     0x80000018u
#define R_STAT3     0x8000001Cu
#define R_TGT_ID    0x80000020u
#define R_TGT_OFF   0x80000024u
#define R_TGT_ADR   0x80000028u
#define R_TGT_LEN   0x8000002Cu
#define R_TGT_GO    0x80000030u
#define R_PCM_ST    0x80000034u
#define R_PCM_RATE  0x80000038u
#define R_INPUT     0x8000003Cu
#define R_VERSION   0x80000040u
#define R_RELOAD    0x80000044u
#define R_FB_ADDR   0x80000048u
#define R_FB_SIZE   0x8000004Cu   /* {h[17:9], w[8:0]} */
#define R_FB_COLOR  0x80000050u   /* {bg[31:16], fg[15:0]} */
#define R_FB_GO     0x80000054u   /* W: {sy[13:12],sx[11:10],glyph[9:3],op[1:0]} */
#define R_FB_STALL  0x80000058u   /* R: clk cycles spent with the draw FIFO full */
#define R_SLOT_SZ   0x8000005Cu   /* R: size of the file APF reported at reload */
#define R_DT_ADDR   0x80000060u   /* W: datatable word address                  */
#define R_DT_DATA   0x80000064u   /* R/W: datatable word at that address        */
#define R_EQ        0x80000068u   /* R/W: EQ preset index, 0 = FLAT (bypass)    */
#define R_SET_IDX   0x8000006Cu   /* W:   persistent settings word index, 0..7  */
#define R_SET_DAT   0x80000070u   /* R: value APF wrote  W: value we publish    */
#define R_SDR_ADDR  0x80000074u
#define R_SDR_DATA  0x80000078u
#define R_SDR_CTRL  0x8000007Cu
#define R_SDR_RDATA 0x80000080u
#define R_SDR_STATUS 0x80000084u
#define R_BLT_IDX   0x800000C0u   /* Phase F: sticky blit-engine field select (W) */
#define R_BLT_DATA  0x800000C4u   /* Phase F: sticky blit-engine field value  (W) */
#define R_SDR_BUSY  0x800000BCu   /* Phase F B7: SDRAM port-busy cycles, free-running (0 if TAU_SDRAM_BUSY is off) */
#define R_CLUT_IDX  0x800000C8u   /* Phase F B8: sticky CLUT index (W), 0-255 */
#define R_DBG_MARK  0x800000D0u   /* B-186: CPU-side checkpoint, read live by TAU_ISSP's DBGM probe -- see fw/suite.inc's bt_crumb(). Harmless write if TAU_ISSP isn't built. */
#define R_CLUT_DATA 0x800000CCu   /* Phase F B8: CLUT entry at that index (W), RGB565; index auto-increments */
#define R_RC_IDX    0x800000D4u   /* B11: corner-cut LUT entry select (W), 0-15 -- see fw/rc_lut.h */
#define R_RC_DATA   0x800000D8u   /* B11: corner-cut LUT entry value (W), 0-31 (5 bits) at the index above */
#define R_SPEC_IDX  0x800000DCu   /* B-263: spectrum bank -- write the band index 0..15 */
#define R_SPEC_DATA 0x800000E0u   /* read: the window mean |band| of band SPEC_IDX (20 bits) */
#define R_WAVE_CTL  0x800000ECu   /* B-283: level/scope block -- write: bit 0 clear peaks, bit 1 arm a scope capture, [11:8] = samples per column - 1 */
#define R_WAVE_IDX  0x800000F0u   /* write: scope column 0..255 to present at R_WAVE_DATA */
#define R_WAVE_DATA 0x800000F4u   /* read: {min[31:16], max[15:0]} (signed 16-bit each) of the selected column of the last capture */
#define R_WAVE_PK   0x800000F8u   /* read: {max |R| [31:16], max |L| [15:0]} since the last clear */
#define R_WAVE_ST   0x800000FCu   /* read: bit 0 = the block is built in, bit 1 = a capture is running, bit 2 = trigger timed out */
#define WAVE_HW_COLS 256u
#define R_TEXT_MODE 0x80000114u   /* theme/gamma: write bit 0 = 1 selects the light-polarity text weight table; read bit 31 = the bitstream has it, bit 0 = current */
#define R_POLY_CTL  0x80000100u   /* B-292 MP3 window unit: write: bit 0 clear history, bit 1 go (compute the pending slot) */
#define R_POLY_PUSH 0x80000104u   /* write: one FDCT32 output word in push order, 64 per slot (channel 0's 32, then channel 1's) */
#define R_POLY_IDX  0x80000108u   /* write: PCM word 0..31 to present at R_POLY_OUT */
#define R_POLY_OUT  0x8000010Cu   /* read: {R sample [31:16], L sample [15:0]} of the last computed slot, Helix's own interleave */
#define R_POLY_ST   0x80000110u   /* read: bit 0 = built in, bit 1 = busy, bits 31:16 = slots computed */
/* B-307: redirect FDCT32's captured output words to the MP3 window unit from inside Subband() itself
 * (third_party/libhelix-mp3/real/subband.c, gated by the same macro). Off by default -- byte-identical
 * to the unmodified decoder; no build target defines this yet (docs/MP3_FILTERBANK_KERNEL_DESIGN.md
 * section 6 step 4 is not shipped in any build). fw/mp3_poly_hw.inc implements fw/mp3_poly_hw.h, which
 * subband.c (a separate translation unit) declares extern. */
#ifndef TAU_POLY_FW
#define TAU_POLY_FW 0
#endif
#if TAU_POLY_FW
#include "mp3_poly_hw.inc"
#endif
#define R_LPC_CFG       0x80000120u   /* B-368/B-369/B-370: FLAC LPC unit -- write: [5:0] order (1-32), [11:6] shift (0-31), once per subframe */
#define R_LPC_COEF_IDX  0x80000124u   /* write: [4:0] coefficient index 0-31 (0 = most-recent-paired tap) */
#define R_LPC_COEF_DATA 0x80000128u   /* write: signed 16-bit coefficient at that index; index auto-increments */
#define R_LPC_WARM_IDX  0x8000012Cu   /* write: [4:0] warm-up/history index 0-31, same convention as COEF_IDX */
#define R_LPC_WARM_DATA 0x80000130u   /* write: signed 32-bit warm-up sample at that index; index auto-increments */
#define R_LPC_RESIDUAL  0x80000134u   /* write: next residual, starts one reconstruction */
#define R_LPC_SAMPLE    0x80000138u   /* read: the reconstructed sample -- this read is itself the ack that clears STATUS bit 2 */
#define R_LPC_STATUS    0x8000013Cu   /* read: bit 0 = built in, bit 1 = busy, bit 2 = done */
#define R_I2S_DIAG_MINMAX 0x80000140u /* B-467: [15:0] min / [31:16] max interval ever (clk_sys cycles) between real I2S DAC-domain sample updates, free-running since reset */
#define R_I2S_DIAG_CNT    0x80000144u /* count of update events since reset */
#define R_I2S_DIAG_SUM    0x80000148u /* sum of measured intervals since reset (average via delta / delta-count) */
#define R_I2S_DIAG_ST     0x8000014Cu /* bit 0 = built in (I2S_DIAG_ENABLE) */
#define R_CYMO_CTRL   0x80000150u /* B-471/B-476: write: bit0 clear (pulse), bit1 start (pulse, test-only), bit2 LIVE_ENABLE (STICKY -- hands the real audio path to the resampler) */
#define R_CYMO_PUSH   0x80000154u /* write: {push_r[31:16],push_l[15:0]} + one push_we pulse (self-test only, the live audio path never uses this) */
#define R_CYMO_OUT    0x80000158u /* read: {out_r[31:16],out_l[15:0]} -- this read is itself the ack that clears STATUS bit 2 (self-test only) */
#define R_CYMO_STATUS 0x8000015Cu /* read: bit0 built in, bit1 busy, bit2 done, bit3 pop_req, bit4 live_en */
#define R_CYMO_DIAG   0x80000160u /* read: [15:0] saturating count of consumes with no fresh push since the last one (B-492) */
/* Redirect fw/flac.c's LPC reconstruction to the hardware unit (docs/research/FLAC_LPC_KERNEL_DESIGN.md).
 * Off by default -- byte-identical to the unmodified decoder; no build target defines this yet (no
 * Quartus fit or hardware test exists for TAU_LPC yet, section 7 item 5). fw/flac_lpc_hw.inc implements
 * fw/flac_lpc_hw.h, which flac.c (a separate translation unit) declares extern. */
#ifndef TAU_LPC_FW
#define TAU_LPC_FW 0
#endif
#if TAU_LPC_FW
#include "flac_lpc_hw.inc"
#endif
#define R_SPEC_ST   0x800000E4u   /* read: bit 0 = the bank is built into this bitstream, bits 31:16 = windows completed */
#define R_SCAN      0x800000E8u   /* B-267 Helios beam position: bit 9 = present (TAU_BEAM bitstream), bits 8:0 = video line counter */
#define R_VBLANK    0x800000D0u   /* Helios/Talos H0: bit 0 = vblank status, CDC'd from clk_vid; 0 when TAU_VBLANK is off */
#define R_DBUF_CPU  0x80000118u   /* Helios/Talos H2 (B-340): write bit 0 = which buffer plain RECT/CHAR/COPY commands target; read bit 31 = built in, bit 0 = echo */
#define R_DBUF_DISP 0x8000011Cu   /* write bit 0 = 1 requests a flip (applied at the next vblank); read bit 31 = built in, bit 1 = flip still pending, bit 0 = buffer currently displayed */
#define SDR_CLK_HZ  100000000u    /* clk_sdram, for R_SDR_BUSY deltas -- see docs/MMIO_ALLOCATION.md 0xBC */

/* Target command selector, written to R_TGT_GO bits [1:0]. */
#define TGT_READ     0u   /* 0180 */
#define TGT_OPENFILE 1u   /* 0192 */
#define TGT_GETFILE  2u   /* 0190 */
#define TGT_WRITE    3u   /* 0184 */

/* R_RELOAD bits -- see mp3_soc.v. Bit 4 (the legacy PLAYLIST-slot-reloaded bit, RL_PL_RELOAD =
 * 0x10) is no longer read by firmware: legacy playlist mode is gone, so nothing here ever
 * acknowledges it, but the RTL bit itself is untouched (this is a firmware-only removal). */
#define RL_PENDING   1u
#define RL_READY     2u   /* allcomplete rose since the reload notification   */
#define RL_AC_NOW    4u   /* allcomplete level right now                      */
#define RL_AC_FELL   8u   /* allcomplete fell since the reload notification   */

#define FB_OP_RUN   0u
#define FB_OP_RECT  1u
#define FB_OP_CHAR  2u
#define FB_OP_COPY  3u
#define FB_OP_BLIT  4u   /* Phase F B1 */
#define FB_OP_BAR   5u   /* Phase F B6 */
#define FB_OP_SBLIT 6u   /* Phase F B4 */
#define FB_OP_CBLIT 7u   /* Phase F B8 */
/* B11: opcode 8 needed a 4th cmd_op bit, which mp3_soc.v adds by pulling in
 * R_FB_GO's bit 14 (`fb_cmd_op <= {dDAT_MOSI[14], dDAT_MOSI[2:0]}`) rather
 * than shifting the existing 3-bit field -- so this is a bit position, not a
 * plain small integer like every opcode above it. */
#define FB_OP_RRECT (1u << 14)   /* Phase F B11 */

/* Album-art panel. The image is decoded ONCE into an off-screen SDRAM stash
 * (row 400+, past the 360 visible rows) and then blitted into place with a
 * single COPY per animation step -- which is what makes sliding affordable at
 * all. Re-sending ~9000 pixels from the CPU every step would have starved the
 * decoder outright. */
/* The panel is a MOUNT, not a bare image: a rounded grey plate carrying the
 * card's tone, with the cover inset inside it and a hairline between the two.
 * Baked into the stash as one picture, so sliding it is still a single COPY --
 * the frame costs nothing per animation step.
 *
 *   ART_W/ART_H  the whole mount (what gets blitted)
 *   ART_PAD      grey border around the cover
 *   ART_IMG      the cover itself
 */
#define ART_PAD  0u                /* 2026-09-26: the design (Figma, shared at 4x) has the cover itself at 128 px with rounded corners, no plate */
#define ART_IMG  128u              /* cover: 128 px, the size the pre-converted covers (TAU_ART_TIMG) are made at */
#define ART_W    (ART_IMG + 2u * ART_PAD)
#define ART_H    (ART_IMG + 2u * ART_PAD)
/* Now-playing UI pass 1 (Figma node 163:57, first Helios-era layout): the art
 * panel moved from the right (flush with the meter's baseline) to a static
 * top-left mount beside the title block, matching the new design. ART_X is a
 * literal, not UI_MARGIN, because UI_MARGIN is defined much later in this
 * file and this block is used before it -- keep the two in sync by hand if
 * either ever changes (same pre-existing constraint the old ART_X had). */
#define ART_X    8u                 /* design: cover at (8, 8) */
#define ART_Y    8u                 /* fixed: no longer tracks the meter */
#define ART_STASH_Y 360u           /* off-screen: rows 360..399 are unused, and starting here keeps the stash + thumbnails + cover plane clear of the Chladni plane at 984 */

/* R_PCM_ST layout -- MUST match mp3_soc.v:
 *   {13'd0, underrun, full, empty, 4'd0, level[11:0]}
 * i.e. level = bits 11:0, empty = 16, full = 17, underrun = 18.
 * These were originally coded as bits 13/14, which silently read as constant 0:
 * the "wait while full" check never waited (pcm_fifo drops pushes when full, so
 * samples vanished) and underruns were never reported. Keep in sync with RTL.
 * Moved up here (was down near their first original use) because the UI code
 * below now calls pcm_underrun() earlier in the file than that was -- C needs
 * the declaration, and macros need their #define, visible before first use. */
#define PCM_LEVEL(s)  ((s) & 0xFFFu)
#define PCM_EMPTY(s)  (((s) >> 16) & 1u)
#define PCM_FULL(s)   (((s) >> 17) & 1u)
#define PCM_UNDER(s)  (((s) >> 18) & 1u)

/* sysio.c -- heap high-water mark. Displayed so "is it leaking?" is answered
 * by a number instead of an argument: load_track() frees the old Helix
 * instance before allocating the new one, so newlib should hand back the same
 * ~34 KB block every time and this should sit flat no matter how many tracks
 * are loaded. If it climbs per reload, something genuinely is not being freed. */
extern unsigned int arena_limit(void);
#define ARENA_LIMIT (arena_limit())

/* B-338: clk_sys 60 -> 66.667 MHz (TAU_CLK66_FW, docs/HARPMUDD_UPSTREAM_1.5_REVIEW.md section 1). Every deadline in this file already goes
 * through CLK_HZ (as `n * CLK_HZ` / `n / CLK_HZ`), so this one constant is the whole port -- except the wrap-period comments below, which
 * are cosmetic (2^32/CLK_HZ: 71.6 s at 60 MHz, 64.4 s at 66.667 MHz) and are NOT all individually corrected; the wrap-safe
 * `(int32_t)(cycles() - deadline) >= 0` idiom they document is itself frequency-independent. Mutually exclusive with TAU_RAM_192K_FW for now
 * (the RTL is too, B-338), matching the boot interlock below. */
#if TAU_CLK66_FW
#define CLK_HZ      66666667u
#else
#define CLK_HZ      60000000u   /* clk_sys; UI timing needs it before playback does */
#endif

/* Free-running cycle counter. Up here because the UI uses it for its own
 * timing (marquee, paused-state throttle) well before the playback code does. */
/* 64-bit unsigned division without libgcc's __udivdi3 (1,128 B of hot RAM, B-333). The dividends here are sample counts and bit counts
 * (up to about 2^40), the divisors 32-bit; none of these run per audio frame, so a plain shift-subtract loop is fine. d must be non-zero
 * (every call site already guards it, as the / operator would have trapped or returned garbage). */
__attribute__((noinline, unused)) static uint64_t udiv64(uint64_t n, uint64_t d)
{
    uint64_t q = 0, r = 0;
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= (uint64_t)1 << i; }
    }
    return q;
}

static inline uint32_t cycles(void) { return REG(R_CYCLES); }

static inline uint32_t pcm_level(void)    { return PCM_LEVEL(REG(R_PCM_ST)); }
static inline int      pcm_underrun(void) { return PCM_UNDER(REG(R_PCM_ST)); }

/* Must match CORE_VERSION in mp3_soc.v. The .rom reloads in seconds but the
 * bitstream needs a ~6 min compile, so flashing firmware onto stale RTL is easy
 * and its symptoms (dead peripheral, silent audio, unresponsive buttons) look
 * exactly like logic bugs. Checking here turns that into an obvious signal. */
#define EXPECT_VERSION 0x4D503317u   /* rev 23: target data-slot flush     */
/* B-333: the 192 KB RAM-shrink bitstream (TAU_RAM_192K) reports rev 24. A firmware linked for 256 KB (every normal build) accepts only
 * rev 23, so it is REFUSED on the 192 KB bitstream instead of silently running on 64 KB less RAM; a firmware linked for 192 KB
 * (RAM_192K=1, -DTAU_RAM_192K_FW) accepts both (a 192 KB image also runs on the 256 KB bitstream). */
#define EXPECT_VERSION_192K 0x4D503318u
#define EXPECT_VERSION_CLK66 0x4D503319u
/* B-347: the combined bitstream (TAU_RAM_192K + TAU_CLK66) reports rev 26. A firmware built assuming BOTH contracts (RAM_192K_FW and
 * CLK66_FW both set) must accept ONLY rev 26 -- deliberately stricter than the single-feature branches below, which each also accept
 * the plain rev 23 baseline. Running combined-assumption firmware (192 KB link + 66.667 MHz cycle counting) on a single-feature or
 * baseline bitstream must be refused, not silently tolerated, since either mismatch (wrong RAM size or wrong cycle rate) corrupts
 * silently rather than crashing. This branch is checked first, before the single-feature `#elif`s, so it can never fall through to
 * their more permissive checks. */
#define EXPECT_VERSION_192K_CLK66 0x4D50331Au
#if TAU_RAM_192K_FW && TAU_CLK66_FW
#define VERSION_OK(v) ((v) == EXPECT_VERSION_192K_CLK66)
#elif TAU_RAM_192K_FW
#define VERSION_OK(v) ((v) == EXPECT_VERSION || (v) == EXPECT_VERSION_192K)
#elif TAU_CLK66_FW
#define VERSION_OK(v) ((v) == EXPECT_VERSION || (v) == EXPECT_VERSION_CLK66)
#else
#define VERSION_OK(v) ((v) == EXPECT_VERSION)
#endif

/* Shown on the splash. This is the PRODUCT version, not the RTL/firmware
 * contract above -- they answer different questions and must not be conflated.
 * Keep it in step with the status line in README.md; nothing enforces that. */
#define APP_VER "0.6.0"

/* The Diagnostic Build switch. One macro for everything that must not be in the shipped release: the
 * Check and its QR report (fw/suite.inc), the Tests and Stress pages, the SDRAM stress pump, soak and
 * HUD, and the diagnostic menus. Off in `release`; on in `player-library-diagnostic` and
 * `player-library-diagnostic-profile` (fw/build.sh). Needs the same RBF features as the release. */
#ifndef TAU_DIAGNOSTIC
#define TAU_DIAGNOSTIC 0
#endif

/* Developer-only: drive the Phase 2 CPU-window (uncached alias 0xA0100000) from
 * the stress pump instead of the Phase 1 MMIO mailbox. Requires an RBF built
 * with TAU_PHASE2_WINDOW; the pump refuses to start without a window preflight. */
/* Phase F B7: whether this bitstream has the SDRAM port-busy-cycle counter (R_SDR_BUSY,
 * MMIO 0xBC) wired to something other than a hardwired 0 -- see docs/MMIO_ALLOCATION.md.
 * A firmware build flag, not a hardware probe: the counter has no ready-detect of its own
 * (unlike PSRAM's PS_ID or cold code's IF_CFG), so this must be set to match the bitstream
 * actually installed, the same convention TAU_PHASE2_WINDOW already uses for its own
 * hardware-support macro. */
#ifndef TAU_SDRAM_BUSY
#define TAU_SDRAM_BUSY 0
#endif
/* P5: place the album-art accumulator (art_acc, 11,040 B) in PSRAM behind the uncached CPU
 * window at 0xA4000000 instead of BRAM. Needs the P4 bitstream (PSRAM window); art_prove()
 * checks for it before the first store and turns cover art off (no BRAM fallback) if it is
 * absent or fails. Off by default. */
#ifndef TAU_ART_TIMG
#define TAU_ART_TIMG 0       /* B-285: the TIM1 cover reader (docs/COVER_TIMG_READER.md); off unless built with ART_TIMG=1 */
#endif
#ifndef TAU_ART_PSRAM
#define TAU_ART_PSRAM 0
#endif
#if TAU_ART_PSRAM
#define ART_PSRAM __attribute__((section(".psram")))
#else
#define ART_PSRAM
#endif
/* A-115: in-app settings (Start opens it; the Start stop function is removed in such a
 * build). Off by default so the standard build stays byte-identical until it is versioned. */
/* Media library (spec docs/MEDIA_LIBRARY_0.4_SPEC.md): browse and play from a host-built index in PSRAM.
 * Off by default; needs the PSRAM window bitstream (proven at boot, feature off if absent). */
/* Phase G1 (docs/PHASE_G_SPEC.md): read-only data that is only needed occasionally lives in PSRAM (section .cold_data,
 * loaded from tau-cold.bin at boot). Off: COLD_DATA is empty and the data stays in the ROM image. */
#define COLD_DATA __attribute__((section(".cold_data")))
/* Phase G4: cold CODE run from PSRAM (needs TAU_COLD and a bitstream with PSRAM_IFETCH_ENABLE; checked at boot). */
/* A-118/A-119: Diagnostics group in the settings menu. TAU_DIAG_INFO adds the read-only Info
 * page and is part of the release-style build; TAU_DIAGNOSTIC is reserved for the on-demand
 * tests, stress pump and soak of the "Diagnostic Build" (no code behind it yet). */
/* A-132: the real meter previews in the settings menu (fw/meter_thumbs.h, about 6 KiB of ROM);
 * builds without it keep the grey placeholder. Release-style builds only: the Diagnostic Build has
 * no room for it. */
#ifndef TAU_G4
#define TAU_G4 0               /* Phase G4: library and settings UI code runs from PSRAM (cold code); needs TAU_COLD_CODE */
#endif
/* G4 step 2-3 (TAU_G4 >= 2): the playlist loader and overlay drawing and the cover-art decoder glue also run from PSRAM. The
 * attribute is spelled out here because the overlay code sits above where cold.inc defines COLD_TEXT. The picojpeg callback
 * art_need_bytes stays hot: it runs many times per cover and would refetch from PSRAM each time. */
#if TAU_G4 >= 2
#define COLD_FN2 __attribute__((section(".cold_text"), noinline))
#else
#define COLD_FN2
#endif
/* G4 step 4 (TAU_G4 >= 3, B-199..B-201): ui_draw_dynamic_cold() (below) also runs from PSRAM -- the
 * first cold code on the live playback path itself, not just menus/settings, kept as its own tier
 * rather than folded into TAU_G4>=2 for exactly that reason. See fw/cold.inc's own comment (right
 * above coldframe_record()) for the measurement that justifies this. Spelled out here for the same
 * textual-ordering reason as COLD_FN2. */
#if TAU_G4 >= 3
#define COLD_FN3 __attribute__((section(".cold_text"), noinline))
#else
#define COLD_FN3
#endif
/* True when CT_COLDFRAME (fw/suite.inc) has anything to measure -- either the synthetic-probe
 * experiment or the real ui_draw_dynamic_cold() conversion. Defined once here so suite.inc's three
 * separate gates (enum/CHK_MAX, chk_ex, the case block) can't drift out of sync with each other. */
#define TAU_COLDFRAME_ON (TAU_G4 >= 3)
#define PL_SDRAM __attribute__((section(".sdram")))

/* Framebuffer: 400x360 RGB565, one word/pixel, 512-word (page-aligned) stride.
 * See mp3_fb.sv for the full rationale. */
#define FB_W       400u
#define FB_H       360u
#define FB_STRIDE  512u

/* Glyph cell: the engine upscales the 8x8 source font 2x via EPX, so one
 * character occupies 16*scale pixels each way. The source font already leaves
 * its two right-hand columns clear, which becomes the inter-character gap --
 * cells tile edge-to-edge with no extra advance needed. */
/* Type scale. The engine scales fractionally (Bresenham), so there is a real
 * step between 16 and 32 px -- integer-only scaling forced a doubling, which
 * is far too coarse for setting a title against an artist line. */
enum { TS_1X = 0, TS_15X = 1, TS_2X = 2, TS_3X = 3 };
static const unsigned char ts_half[4] = { 2, 3, 4, 6 };   /* size = 16*n/2 */

#define FB_CELL(s)  ((FONT_CELL_H * ts_half[s]) / 2u)   /* 16 / 24 / 32 / 48 */

/* The one place a byte becomes a glyph index. The atlas holds 0x20..0x7E and
 * nothing else, so everything outside that is a space.
 *
 * This exists because the width path and the DRAW path disagreed. fb_adv()
 * substituted a space for an out-of-range byte while fb_char() masked with
 * 0x7F and sent the result to the engine -- so the first UTF-8 byte of an
 * accented or symbol character (0xE2, say) was drawn as 'b' while being
 * measured as a space. Wrong glyphs AND overlapping spacing, from one title
 * containing a character the font cannot show. Both now ask this. */
static uint32_t fb_glyph(char ch)
{
    uint32_t c = (unsigned char)ch;
    return (c < FONT_FIRST || c > FONT_LAST) ? (uint32_t)' ' : c;
}

/* Proportional advance. The engine paints the full 16-px cell, and glyphs are
 * left-aligned within it, so stepping by the ink width overwrites only the
 * previous glyph's blank padding -- proportional spacing without needing a
 * transparent blit. */
static uint32_t fb_adv(char ch, uint32_t sx)
{
    unsigned char c = (unsigned char)ch;
    return ((uint32_t)font_adv[fb_glyph(c) - FONT_FIRST] * ts_half[sx]) / 2u;
}

/* Shadow of the engine's colour register. The parameter registers persist
 * between pushes (each push snapshots them into the FIFO), so a whole string
 * in one colour costs ONE colour write plus two writes per character. */
static uint32_t fb_color_shadow = 0xFFFFFFFFu;

static inline void fb_wait(void) { while (REG(R_FB_GO) & 1u) { } }

/* A full-screen overlay (library, settings) owns every pixel. While one is up, every
 * drawing primitive is a no-op unless the overlay itself is painting (ov_draw), so the
 * player -- meter, clock, progress bar, toasts, art -- cannot punch through it. The
 * repaint on close already redraws and invalidates all of it. */
static uint8_t lib_ui_open;          /* library browse overlay is up */
static uint8_t lib_src;              /* the current track came from the library queue */
static uint16_t lib_qn, lib_qpos;    /* the library queue: length and position */
/* What the user was playing, kept across restarts (persist words, settings.inc): lib_h = kind | id << 3 | queue position << 14,
 * lib_hb = low 31 bits of the index build id it refers to, lib_hs = the Shuffle All seed. */
static uint32_t lib_h, lib_hb, lib_hs;
enum { LIB_ST_NONE = 0, LIB_ST_OK, LIB_ST_OFF };
static uint8_t lib_state;            /* LIB_ST_*: no index / loaded / switched off by an error */
static uint8_t  lib_boot_ok;          /* B-080 evidence: did lib_boot_restore() open something at this boot */
static uint8_t lib_ui_dirty;         /* repaint the browse overlay */
static void lib_ui_draw(void);
static void lib_ui_close(void);
static void lib_ui_enter(void);
static void lib_ui_input(uint32_t *edge_p, uint32_t *fall_p, uint32_t *keys_p);
#define LIB_OVL lib_ui_open
static uint8_t set_open;           /* settings overlay is up */
#define UI_OVERLAY_UP (set_open || LIB_OVL)
static uint8_t ov_draw;            /* the overlay itself is painting */
/* Fullscreen visualiser (fw/fullscreen.inc, Select+Y): while it is up every ordinary draw call is held exactly as under an
 * overlay (the state machines keep running), and the fullscreen code draws with ov_draw set. */
static uint8_t ui_fullscreen;
static uint8_t cold_code_ok;       /* the cold image is loaded and verified (again, tentatively, in fw/cold.inc, which is included later) */
#define FB_HELD() ((UI_OVERLAY_UP || ui_fullscreen) && !ov_draw)

static void fb_set_color(uint16_t fg, uint16_t bg)
{
    uint32_t v = ((uint32_t)bg << 16) | (uint32_t)fg;
    if (v != fb_color_shadow) { fb_color_shadow = v; REG(R_FB_COLOR) = v; }
}

/* One command for the whole block -- the engine walks the rows itself. This
 * used to be a firmware loop issuing one command per row, which is what made
 * drawing expensive enough to disturb the decode budget.
 *
 * CLOBBERS the colour registers (it sets fg = bg = its fill). Any text drawn
 * afterwards needs its own fb_set_color() -- and calling fb_set_color() BEFORE
 * a clear rect leaves fg == bg, so the glyphs paint invisibly. */
static void fb_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t color)
{
    if (!w || !h || FB_HELD()) return;
    fb_wait();
    REG(R_FB_ADDR) = y * FB_STRIDE + x;
    REG(R_FB_SIZE) = (h << 9) | w;
    fb_set_color(color, color);
    REG(R_FB_GO)   = FB_OP_RECT;
}

/* Draws the full cell -- foreground AND background -- so text overwrites
 * whatever was there. Nothing needs erasing before a redraw, which removes
 * both the erase-rect and the "did I erase enough" class of bug. Call
 * fb_set_color() once for the run of characters. */
/* SDRAM -> SDRAM block move. Source rides in the colour registers because a
 * copy has no use for them. */
/* The engine stages each row in glyphbuf, which is 128 entries addressed by
 * wsrc_addr[6:0]. A copy wider than that wraps the read address while still
 * writing the full width, so it emits garbage AND runs past where the caller
 * thinks it stopped. A 246 px scroll wrote straight through into the album art,
 * which shares those scanlines. Splitting here rather than at the call sites
 * means nothing else can hit it either.
 *
 * 127, not 128: the engine latches the width as char_w <= q_w[6:0], seven bits,
 * so a span of exactly 128 truncates to ZERO and copies nothing at all. That
 * left the first span dead and only the remainder scrolling -- half the strip
 * moving and half sitting still. */
#define FB_COPY_MAX 127u

static void fb_copy_span(uint32_t sx_, uint32_t sy_, uint32_t dx, uint32_t dy,
                         uint32_t w, uint32_t h)
{
    if (FB_HELD()) return;
    uint32_t src = sy_ * FB_STRIDE + sx_;
    fb_wait();
    REG(R_FB_ADDR)  = dy * FB_STRIDE + dx;
    REG(R_FB_SIZE)  = (h << 9) | w;
    REG(R_FB_COLOR) = ((src >> 16) & 0x7u) | ((src & 0xFFFFu) << 16);
    fb_color_shadow = 0xFFFFFFFFu;      /* colour regs clobbered -- invalidate */
    REG(R_FB_GO)    = FB_OP_COPY;
}

static void fb_copy(uint32_t sx_, uint32_t sy_, uint32_t dx, uint32_t dy,
                    uint32_t w, uint32_t h)
{
    if (!w || !h) return;

    /* Overlapping copies must run away from the direction of travel, or a
     * later chunk reads source pixels an earlier one already overwrote. */
    if (dx > sx_) {
        uint32_t done = 0;
        while (done < w) {
            uint32_t n = w - done;
            if (n > FB_COPY_MAX) n = FB_COPY_MAX;
            uint32_t off = w - done - n;
            fb_copy_span(sx_ + off, sy_, dx + off, dy, n, h);
            done += n;
        }
    } else {
        uint32_t done = 0;
        while (done < w) {
            uint32_t n = w - done;
            if (n > FB_COPY_MAX) n = FB_COPY_MAX;
            fb_copy_span(sx_ + done, sy_, dx + done, dy, n, h);
            done += n;
        }
    }
}

/* Phase F B8: load N palette entries into the 256-entry CLUT, starting at
 * index 0 (R_CLUT_DATA auto-increments after each write, R_CLUT_IDX=0 resets
 * it -- see mp3_soc.v R-CLUT_IDX/R_CLUT_DATA). fb_wait() first: the CLUT is a
 * single shared table the draw engine reads asynchronously, so it must not be
 * reloaded while an earlier OP_CBLIT command is still draining the FIFO,
 * exactly the same reasoning fb_rect/fb_char already apply to fg/bg. */
static void fb_clut_load(const uint16_t *pal, uint32_t n)
{
    fb_wait();
    REG(R_CLUT_IDX) = 0u;
    for (uint32_t i = 0; i < n; i++) REG(R_CLUT_DATA) = pal[i];
}

/* Phase F B8: one draw-engine command reading a palette-index source (one
 * index per 16-bit SDRAM word, low byte) through the CLUT loaded above,
 * writing resolved RGB565 pixels to the destination -- same addressing
 * convention as fb_copy_span (source and dest are both `sy_*FB_STRIDE+sx_`
 * word offsets in the framebuffer's own grid, sticky SRC/DST BASE=0 and
 * STRIDE=512 at their power-up default, never touched here). Same
 * FB_COPY_MAX=127 constraint as fb_copy_span (char_w is a 7-bit field; a
 * width of exactly 128 truncates to 0) -- not enforced here since every
 * current caller (56px meter thumbnails) is well under it; a caller needing
 * a wider blit must split itself, same as fb_copy() does for fb_copy_span(). */
static void fb_cblit(uint32_t sx_, uint32_t sy_, uint32_t dx, uint32_t dy,
                     uint32_t w, uint32_t h)
{
    if (!w || !h || FB_HELD()) return;
    uint32_t src = sy_ * FB_STRIDE + sx_;
    fb_wait();
    REG(R_FB_ADDR)  = dy * FB_STRIDE + dx;
    REG(R_FB_SIZE)  = (h << 9) | w;
    REG(R_FB_COLOR) = ((src >> 16) & 0x7u) | ((src & 0xFFFFu) << 16);
    fb_color_shadow = 0xFFFFFFFFu;      /* colour regs clobbered -- invalidate */
    REG(R_FB_GO)    = FB_OP_CBLIT;
}

/* B-166 (Blit Test): the three opcodes nothing in this firmware has ever
 * issued before -- OP_RUN as itself (fb_rect always emits OP_RECT even for
 * h=1, so the RUN opcode has never actually been exercised), OP_BLIT (a
 * plain, non-CLUT generalised blit; fb_cblit() above only covers B8),
 * OP_BAR and OP_SBLIT (B6/B4 -- confirmed by the B-112 audit that nothing
 * had ever called either operationally). All three reuse the exact
 * addressing/register conventions already proven by fb_cblit()/fb_copy_span()
 * and blit_probe.inc's own OP_BLIT usage -- no new RTL, no new register. */

/* OP_RUN: a single-row fill, the true opcode 0 (not RECT's h=1 case). */
static void fb_run(uint32_t x, uint32_t y, uint32_t w, uint16_t color)
{
    if (!w || FB_HELD()) return;
    fb_wait();
    REG(R_FB_ADDR) = y * FB_STRIDE + x;
    REG(R_FB_SIZE) = (1u << 9) | w;
    fb_set_color(color, color);
    REG(R_FB_GO)   = FB_OP_RUN;
}

/* OP_BLIT: same addressing as fb_cblit() minus the CLUT -- a raw pixel copy
 * with independent source/dest via the sticky SRC/DST BASE+STRIDE fields
 * (left at their power-up default here, same as fb_cblit()). */
static void fb_blit(uint32_t sx_, uint32_t sy_, uint32_t dx, uint32_t dy,
                    uint32_t w, uint32_t h)
{
    if (!w || !h || FB_HELD()) return;
    uint32_t src = sy_ * FB_STRIDE + sx_;
    fb_wait();
    REG(R_FB_ADDR)  = dy * FB_STRIDE + dx;
    REG(R_FB_SIZE)  = (h << 9) | w;
    REG(R_FB_COLOR) = ((src >> 16) & 0x7u) | ((src & 0xFFFFu) << 16);
    fb_color_shadow = 0xFFFFFFFFu;
    REG(R_FB_GO)    = FB_OP_BLIT;
}

/* Helios H2's buffer 1, in the same word units fb_blit()'s own addressing uses -- matches
 * mp3_fb.sv's DBUF_BASE1 localparam exactly (1,048,576 words = 2 MiB above buffer 0). Only real use
 * so far: the Settings crossfade below, which uses buffer 1 purely as scratch render space (never
 * flips display to it) -- safe because dbuf_redraw_begin() itself already refuses to engage while
 * FB_HELD() (B-402), so H2's own front/back usage is guaranteed idle whenever Settings holds the
 * screen. */
#define DBUF_BASE1_W 1048576u

/* Sticky SRC_BASE(field 0)/DST_BASE(field 2) (docs/MMIO_ALLOCATION.md 0xC0/0xC4). Callers today:
 * Chladni's H2 buffer tracking (fw/chladni.inc, B-414) and the Settings crossfade (fw/settingsui.inc,
 * B-405/B-410); everything else (fb_blit()/fb_cblit()/fb_bar()/etc., including ui_bg_blend()'s own
 * trail blit) relies on whatever these two last left behind, since both fields are WRITE-ONLY in
 * hardware (docs/MMIO_ALLOCATION.md 0xC0/0xC4: "W") -- there is no MMIO read to check the sticky state
 * directly, only this shadow. Sets both together since every real use wants a matched pair. Not
 * fenced on entry (matching fb_blend_on()'s own precedent, B-334/B-396's own reasoning: this
 * cooperative single-threaded design has no concurrent caller who could have commands still queued
 * expecting the OLD sticky state at the moment this changes) -- the caller IS responsible for
 * fb_fence()-ing before restoring back to (0,0), so no LATER unrelated caller ever inherits a nonzero
 * base; see set_xfade_step_draw()'s own use for the concrete pattern.
 * B-434: dbg_base_src/dst mirror the last values WRITTEN here (the only writer), so a caller that
 * forgets to restore (0,0) before returning -- the exact bug class B-410/B-414 both found real
 * instances of -- is visible without JTAG: if these read nonzero right when wviz_scope_tick() issues
 * its trail blit, that blit (and ui_bg_blend()'s own destination pre-read) executes against the wrong
 * SDRAM region, which reads exactly like "the trail never fades" even though the strip itself (B-433)
 * is provably correct. */
static uint32_t dbg_base_src, dbg_base_dst;
static void fb_set_bases(uint32_t src_base, uint32_t dst_base)
{
    fb_wait();
    REG(R_BLT_IDX) = 0u; REG(R_BLT_DATA) = src_base;
    REG(R_BLT_IDX) = 2u; REG(R_BLT_DATA) = dst_base;
    dbg_base_src = src_base; dbg_base_dst = dst_base;
}

/* OP_BAR: (x, base_y) top-left, w wide, h rows total, `lit` of them lit (fg)
 * at the bottom, the rest unlit (bg) at the top -- mp3_fb.sv's own B6 field
 * convention (cmd_glyph reused as the lit-row count, clamped to h in RTL).
 *
 * B-406: RTL's own clamp (pre_bar_lit vs. pre_bar_h, mp3_fb.sv) only ever sees the value AFTER it has
 * already been truncated to 7 bits below -- `lit & 0x7Fu` WRAPS modulo 128, it does not saturate, so
 * any caller passing lit >= 128 (a real case: fullscreen visualisers' bars are up to FS_FIG_H = 323
 * rows tall) got a near-empty bar instead of a clamped-tall one every time the true value crossed a
 * multiple of 128 -- the reported "some bars flicker, particularly at fullscreen." Firmware clamped to
 * 127 as an immediate mitigation at the time (a bar this tall visibly stopped growing there instead of
 * intermittently collapsing), but that still left tall bars capped short of their real height.
 *
 * B12 (B-454): the real fix -- the field itself widened 7->9 bits in RTL (cmd_glyph_hi, a NEW field
 * claiming 2 previously-unused FIFO padding bits and 2 previously-unused R_FB_GO bits, src/fpga/core/
 * mp3_fb.sv/mp3_soc.v), enough for FS_FIG_H=323. No hardware-capability probe here (unlike
 * BLIT_READY()/RRECT_READY()/etc.): at this project's current alpha stage every firmware build is
 * already paired one-to-one with a specific fit bitstream (B-448's own --build-flags discipline), so
 * an old-bitstream fallback is unneeded complexity -- worth revisiting once betas/a real release start
 * shipping firmware that has to run against bitstreams it wasn't built alongside. */
static void fb_bar(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t lit,
                   uint16_t fg, uint16_t bg)
{
    if (!w || !h || FB_HELD()) return;
    if (lit > 511u) lit = 511u;   /* the widened field's own ceiling */
    fb_wait();
    REG(R_FB_ADDR) = y * FB_STRIDE + x;
    REG(R_FB_SIZE) = (h << 9) | w;
    fb_set_color(fg, bg);
    REG(R_FB_GO)   = FB_OP_BAR | ((lit & 0x7Fu) << 3) | (((lit >> 7) & 0x3u) << 15);   /* cmd_glyph : cmd_glyph_hi */
}

/* OP_RRECT (B11, Helios/Talos H1): (x, y) top-left, w x h the full rect,
 * radius 0..15 corners cut to `bg` -- matching fb_round_rect_on()'s own
 * fg/bg convention exactly (the main body fills with `color`, corner cuts
 * reveal `bg`): mp3_fb.sv's OP_RRECT dispatch leaves char_fg at its default
 * (q_fg, the same as OP_RECT) for the main fill and only overrides it to
 * rrect_bg (q_bg) for the corner segments. radius reuses cmd_glyph, the same
 * field BAR uses for its own lit-row count, clamped to the 16-entry
 * corner-cut LUT's own range in RTL (mp3_fb.sv: q_glyph_r). radius=0
 * degenerates to a plain rect (rrect_pending never arms), matching
 * fb_round_rect_on()'s own r=0 case for free.
 *
 * Caller MUST check RRECT_READY() first (fw/blit_probe.inc) -- the
 * bitstream currently shipped predates B11 entirely; on that hardware,
 * writing FB_OP_RRECT's bit 14 is silently dropped by the old 3-bit cmd_op
 * decode, truncating to cmd_op=0 (OP_RUN) and drawing a single 1px line
 * instead of a rounded rect, not a hang -- but a real, silent visual
 * regression if called unconditionally. */
static void fb_rrect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t radius,
                     uint16_t color, uint16_t bg)
{
    if (!w || !h || FB_HELD()) return;
    fb_wait();
    REG(R_FB_ADDR) = y * FB_STRIDE + x;
    REG(R_FB_SIZE) = (h << 9) | w;
    fb_set_color(color, bg);
    REG(R_FB_GO)   = FB_OP_RRECT | ((radius & 0x7Fu) << 3);
}

/* OP_SBLIT: same source/dest addressing as fb_blit(), but (w, h) here are
 * the DESTINATION (scaled) extent, and scale_x/scale_y (0-3 = 1x/1.5x/2x/3x,
 * CHAR's own encoding, reused verbatim -- mp3_fb.sv's cmd_sx/cmd_sy) ride in
 * R_FB_GO's sx/sy bits, the same bit positions fb_char() already uses. */
static void fb_sblit(uint32_t sx_, uint32_t sy_, uint32_t dx, uint32_t dy,
                     uint32_t w, uint32_t h, uint32_t scale_x, uint32_t scale_y)
{
    if (!w || !h || FB_HELD()) return;
    uint32_t src = sy_ * FB_STRIDE + sx_;
    fb_wait();
    REG(R_FB_ADDR)  = dy * FB_STRIDE + dx;
    REG(R_FB_SIZE)  = (h << 9) | w;
    REG(R_FB_COLOR) = ((src >> 16) & 0x7u) | ((src & 0xFFFFu) << 16);
    fb_color_shadow = 0xFFFFFFFFu;
    REG(R_FB_GO)    = FB_OP_SBLIT | ((scale_x & 3u) << 10) | ((scale_y & 3u) << 12);
}

static void fb_char(uint32_t x, uint32_t y, char ch, uint32_t sx, uint32_t sy)
{
    if (FB_HELD()) return;
    fb_wait();
    REG(R_FB_ADDR) = y * FB_STRIDE + x;
    REG(R_FB_GO)   = FB_OP_CHAR
                   | ((fb_glyph(ch) & 0x7Fu) << 3)
                   | (sx << 10)
                   | (sy << 12);
}

/* Stage 4b bring-up proof, isolated from playback deliberately: this is the
 * FIRST use of SDRAM, a new PLL clock, and a new scanout engine in this
 * project, all introduced together. Draw something simple and unmistakable
 * BEFORE building real UI art on top, so a problem points at "the new
 * subsystem" rather than being tangled up with the working playback code. */
static void fb_test_pattern(void)
{
    /* Five horizontal bands: primaries + white, easy to eyeball for correct
     * colour channel mapping (a mixed-up RGB565 packing shows up immediately
     * as e.g. "red" rendering blue) and for scanout corruption (tearing,
     * wrong-line artifacts, the "pink edge" class of scaler bug documented
     * in mp3_fb.sv). RGB565: bits [15:11]=R [10:5]=G [4:0]=B. */
    uint16_t bands[5] = {
        0xF800u,  /* red   */
        0x07E0u,  /* green */
        0x001Fu,  /* blue  */
        0xFFFFu,  /* white */
        0x0000u,  /* black */
    };
    uint32_t band_h = FB_H / 5u;
    for (int i = 0; i < 5; i++)
        fb_rect(0, (uint32_t)i * band_h, FB_W, band_h, bands[i]);
}

/* The font itself now lives in the FPGA (src/fpga/core/font_rom.v, generated
 * from the Inter typeface by tools/gen_font_rom.py) -- the CPU sends a
 * character code, not pixels. Firmware keeps only the geometry, since it still
 * has to decide what fits where. */

static uint32_t fb_text_width(const char *s, uint32_t sx)
{
    uint32_t w = 0;
    while (*s) w += fb_adv(*s++, sx);
    return w;
}

/* Clips to whole characters. This MUST be the only way text gets drawn: the
 * engine does not bounds-check, and stride=512 > FB_W=400 means a cell running
 * past column 400 lands in the memory the NEXT scanline reads -- corrupt
 * pixels elsewhere on screen, not a clean cutoff.
 *
 * The screen edge is enforced here rather than trusted to max_w, because
 * max_w is a budget measured from x and every caller would otherwise have to
 * subtract its own x correctly to stay safe. Returns where the text ended. */
/* As fb_text_clipped, plus a hard right edge the painted CELL may not cross.
 *
 * Needed because the two limits below are genuinely different sizes: max_w
 * budgets ADVANCES, but fb_char paints a whole cell, so the final glyph can
 * reach (cell - advance) px past the budget -- 12px at TS_1X and 24px at
 * TS_2X. On the info card that ran the title through the panel's right border,
 * and left the marquee painting outside the strip it erases, so the overspill
 * was never cleaned up and accumulated as it scrolled.
 *
 * The card has 8px of padding on the left and had none on the right. Passing
 * its inner edge here makes it symmetric. */
static uint32_t fb_text_boxed(uint32_t x, uint32_t y, const char *s,
                              uint32_t sx, uint32_t sy, uint32_t max_w,
                              uint32_t paint_r)
{
    uint32_t cell  = (FONT_CELL_W * ts_half[sx]) / 2u;
    uint32_t limit = x + max_w;
    if (paint_r > FB_W) paint_r = FB_W;
    while (*s) {
        uint32_t a = fb_adv(*s, sx);
        if (x + a > limit)   break;        /* out of layout budget */
        if (x + cell > paint_r) break;     /* would paint past the box */
        fb_char(x, y, *s, sx, sy);
        x += a;
        s++;
    }
    return x;
}

static uint32_t fb_text_clipped(uint32_t x, uint32_t y, const char *s,
                                uint32_t sx, uint32_t sy, uint32_t max_w)
{
    /* Two DIFFERENT limits, which is what the old single test got wrong.
     * max_w budgets ADVANCES (how much room the text may occupy), while the
     * screen edge bounds the painted CELL. Testing the cell against max_w
     * meant the final glyph needed a whole cell of budget when it only
     * advances by its ink width -- so the last character of every
     * tightly-measured string was silently dropped. */
    uint32_t cell  = (FONT_CELL_W * ts_half[sx]) / 2u;
    uint32_t limit = x + max_w;
    while (*s) {
        uint32_t a = fb_adv(*s, sx);
        if (x + a > limit) break;          /* out of layout budget */
        if (x + cell > FB_W) break;        /* would paint off-screen */
        fb_char(x, y, *s, sx, sy);
        x += a;
        s++;
    }
    return x;
}

/* Picks the largest scale whose whole string still fits, so a short title gets
 * the big treatment and a long one stays legible instead of being chopped. */
static uint32_t fb_text_fit(const char *s, uint32_t max_w, uint32_t max_scale)
{
    for (uint32_t k = max_scale; k > 0u; k--)
        if (fb_text_width(s, k) <= max_w) return k;
    return TS_1X;
}

/* Declared here (rather than down with the rest of the playback state) so the
 * UI functions just below, which reference them, don't need forward decls --
 * C requires a declaration to be visible before first use. */
static HMP3Decoder dec;
static uint32_t frames, errs, rate_set, min_level;
static uint32_t samprate;         /* set once rate_set; needed for elapsed-time display */
/* Samples per frame, taken from the decoder rather than assumed. MPEG-1 Layer
 * III is 1152; MPEG-2 and 2.5 are 576. Hardcoding 1152 ran the elapsed clock at
 * double speed on any low-sample-rate file -- which is the only thing that ever
 * made this core MPEG-1-only. Helix decodes all three, and everything else here
 * already derives from what it reports. */
static uint32_t samp_per_frame = 1152u;
/* Up here with the other UI-visible state: the progress bar needs both, and C
 * needs them declared before ui_draw_dynamic(). */
static uint32_t audio_start;             /* first byte after any ID3 tag */
static uint32_t bytes_per_sec = 16000u;  /* refined once decoding */
/* Set the moment a decoded frame disagrees with the first frame's bitrate.
 * Until then the file is CBR as far as we have seen, and for CBR the frame
 * bitrate IS the byte rate -- exactly, with nothing to measure. */
static uint8_t  vbr_seen;
static uint32_t track_kbps, track_hz;

/* Playback speed. 1.2x is the whole range: playing at N x needs N x the decode
 * throughput, and against the Stage 0 figures 1.5x already breaks on 320 kbps
 * while 2x cannot work at any bitrate. 6/5 rather than a float -- there is no
 * FPU, and this lands in the same expression as a 64-bit shift.
 *
 * Deliberately NOT persisted, so it costs no settings slot (all eight are used
 * and a ninth would mean an RTL change). Resetting to normal each launch is
 * also the right default for something engaged per-listen. See ROADMAP. */
/* Ten speeds (A-132, 1.25x back to 1.20x in A-135 after 1.25x micro-stuttered on the Pocket): rational scale factors, no FPU. The FIFO drain rate is the file's rate times
 * num/den, so the pitch follows the speed (pitch correction is a later job). The decoder needs
 * N x the throughput: about 1.2x is the limit on 320 kbps MP3 at 60 MHz (Stage 0 figures; 1.25x
 * micro-stuttered on the Pocket, A-135), and
 * 1.5x and above will underrun. Above 48 kHz / file rate the I2S path also drops samples
 * (zero-order hold at a fixed 48 kHz), which is audible. Not persisted. */
#define SPEED_N  10u
#define SPEED_1X 2u
static const uint8_t speed_num[SPEED_N] = { 17u, 19u, 1u, 11u, 6u, 13u, 3u, 7u, 2u, 5u };
static const uint8_t speed_den[SPEED_N] = { 20u, 20u, 1u, 10u, 5u, 10u, 2u,  4u, 1u, 2u };
static const char *const speed_txt[SPEED_N] = { "0.85x", "0.95x", "1.00x", "1.10x", "1.20x",
                                                "1.30x", "1.50x", "1.75x", "2.00x", "2.50x" };
static uint8_t speed_idx = SPEED_1X;

/* Drain the PCM FIFO at the file's sample rate, scaled by the current speed;
 * sound_i2s zero-order-holds up to its fixed 48 kHz. Pitch rises with speed --
 * there is no room in the decode budget for time-stretching on top.
 *
 * ONE place computes this. It was duplicated at the two points that learn the
 * sample rate, and a speed toggle has to reproduce it exactly or the pitch goes
 * wrong on the next track only.
 *
 * Nothing else needs adjusting for speed: ui_sec, the duration and the seek
 * distances are all derived from FILE POSITION, not wall clock, so they stay
 * correct by construction. The FIFO drain is the only wall-clock-domain thing
 * here. */
static void pcm_rate_apply(uint32_t hz)
{
    if (!hz) return;
    uint64_t inc = DIV64((uint64_t)hz << 32, CLK_HZ);
    if (speed_idx != SPEED_1X) inc = DIV64(inc * speed_num[speed_idx], speed_den[speed_idx]);
    REG(R_PCM_RATE) = (uint32_t)inc;
}
static uint32_t track_bytes;      /* audio length the FILE declares (Xing/VBRI) */
static uint32_t fl_first_frame;   /* absolute offset of the first audio frame */
enum { FMT_MP3 = 0, FMT_FLAC };
static uint8_t  track_fmt;
static uint8_t  size_suspect;     /* directory disagrees with the file itself   */
static uint8_t  ui_size_warned;
/* Load timing, in milliseconds, for the phases between pcm_flush() and
 * prefill(). Everything in that window runs with the FIFO empty, so whatever
 * dominates it is the hiccup. Measured rather than reasoned about: four
 * attempts at this were aimed by theory and three of them made it worse. */
static uint16_t ld_head, ld_size, ld_art, ld_pre, ld_total;
#if TAU_DIAGNOSTIC
static uint16_t chk_loads;              /* completed track loads (the Check's track-change test waits on it) */
#endif
static uint8_t  ui_ld_shown;
#define LD_MS(c) ((uint16_t)((c) / (CLK_HZ / 1000u)))
/* Total frames declared by a Xing/Info/VBRI header, 0 when the file has none.
 * Duration from size/bitrate is only right for CBR -- on a VBR file the first
 * frame's bitrate is not the file's average, which is why both the total time
 * and the progress bar were off. */
static uint32_t track_frames, track_secs;
/* How far the pending seek moves, in seconds. Was a hardcoded 5 at the point of
 * use, which made both a fine seek and an accelerating scrub impossible to
 * express -- the request said which way but never how far. */
static uint32_t seek_secs = 5u;
/* Average byte rate measured from real playback. Converges on the truth for
 * VBR files that carry no Xing header, which the first-frame bitrate cannot. */
static uint32_t meas_rate;
/* Where the current measurement window started. meas_rate has to be throughput
 * ACTUALLY DECODED, and file_pos alone is not that -- a seek moves it without
 * any time passing. Measuring from a baseline that a seek resets keeps the two
 * quantities describing the same interval. */
static uint32_t meas_pos0, meas_sec0;

/* Ring cursors live up here with the UI state: the measured-rate calculation
 * needs them, and C wants them declared before ui_draw_dynamic(). */
static uint32_t file_pos;       /* next byte offset to fetch from the file   */
static uint32_t ring_fill;      /* valid bytes currently in the ring         */
static uint32_t ring_rd;        /* read cursor within the ring               */ /* stream format, known only once decoding starts */
static uint32_t ui_info_y;            /* where chrome left room for the format line */
static uint32_t ui_last_info, ui_last_prog, ui_pause_next, ui_breath, ui_icon_next;
static uint32_t ui_arr_t, ui_wave_force, ui_accent_changed;

/* ---- preset EQ ----
 * The curve table and the names are GENERATED from the same quantised
 * coefficients the RTL filters with (tools/gen_eq_coeffs.py --curves), so
 * the shape on screen cannot drift from the shape being applied. */
#include "eq_curve.h"
static uint8_t  eq_idx;              /* 0 = FLAT = RTL bypass          */
static uint32_t ui_sec, ui_sec_acc, ui_last_frames, ui_prog_sec;
static int      ui_was_paused;
static uint32_t peak_amp;         /* max |sample| in the most recently decoded frame, 0..32767 */
/* Peaks ACCUMULATE between display frames instead of overwriting.
 *
 * meters_feed() runs once per decoded chunk and the bar history consumes at
 * half the display rate, so a plain assignment meant the last write before a
 * tick won and everything else was thrown away -- half the chunks on MP3, and
 * on FLAC an arbitrary half, because FLAC's chunks arrive BUNCHED.
 *
 * A FLAC frame is 4608 samples, 104 ms, and subframe() decodes all of channel
 * 0 emitting nothing -- a stereo pair cannot be reconstructed until channel 1
 * arrives. So ~39 ms of every frame produces no meter data at all, then four
 * chunks land close together. Taking the MAX since the last display frame
 * makes what the meter shows independent of when the decoder happened to
 * produce it, and loses no transient. */
static uint32_t peak_acc, peak_acc_l, peak_acc_r;
static uint8_t  peak_acc_any;

/* Meter headroom -- see where it is applied, below. Raise the numerator toward
 * the denominator for taller bars, lower it for shorter ones. This is the only
 * number to touch: it scales what every meter reads, and nothing else. */
#define MTR_HEADROOM_NUM 3u
#define MTR_HEADROOM_DEN 4u
/* The bar history scrolls every SECOND display frame, so it needs a max over
 * its own two-frame period -- reading peak_amp directly would discard the
 * frame in between, which is the same drop this change exists to remove, one
 * consumer further down. */
static uint32_t wave_pend;

enum { ID3_OK, ID3_NO_TAG, ID3_NO_FRAME, ID3_UNSUPPORTED_ENCODING };
static int title_status;
static uint8_t ui_warn_row;   /* draw the album/format row as a warning */
/* WHY a FLAC file was turned away, and the offending value. Mirrored because
 * `fl` is declared with the FLAC glue, ~2000 lines below the card that has to
 * name it.
 *
 * Four reasons, not one. Only the sample-rate gate used to say anything
 * useful; a 32-bit, multichannel or huge-blocksize file fell through to "THE
 * FILE COULD NOT BE READ", which is both wrong and alarming -- those files
 * read perfectly well, this core just cannot play them. flac_open fills
 * STREAMINFO before it rejects, so the real numbers are already in hand. */
enum { FLR_NONE = 0, FLR_RATE, FLR_DEPTH, FLR_CHANS, FLR_BLOCK };
static uint8_t  fl_reject_kind;
static uint32_t fl_reject_val;

/* BUG-002: 48 clipped long titles at about 45 characters; the marquee holds TITLE_MAX. */
#define TITLE_MAX 80u
static char track_title[TITLE_MAX];
static char track_artist[64];
static char track_album[64];
static char track_year[8];
static char track_trk[8];

/* Encoder identification from the LAME/Info tag extension.
 *
 * The tag sits immediately after the Xing header this core already parses, so
 * the bytes are free -- we were reading past them and throwing them away. Its
 * first nine bytes are the encoder string ("LAME3.100", "Lavf58.29"), and the
 * byte after that carries the VBR method in its low nibble.
 *
 * Empty when the file has no LAME extension, which is most non-LAME encoders
 * and every file with a bare Xing header. */
static char    track_encoder[12];
static uint8_t track_vbr_method;      /* 0 = unknown / absent */

/* Transport state. Declared up here with the other UI-visible globals rather
 * than down with poll_input(): ui_draw_dynamic() shows the play/pause state,
 * and C needs the declaration before that use. */
/* Volume as a straight percentage, 0..100, where 100 is unity gain -- the
 * loudest the stream goes without clipping. Applied as a Q8 fixed-point gain
 * recomputed once per change, because a divide per sample would be 2304
 * software divides every frame inside the decode budget. */
#define VOL_MAX   100u
#define VOL_STEP  5u
static uint32_t paused, volume = 65u;    /* overridden by the saved setting     */

/* Fade-in after ANY audio discontinuity, in samples (~46 ms at 44.1 kHz).
 *
 * Paired with pcm_fifo's glide-to-zero on underrun: the FIFO brings the held
 * output down to silence during a gap, and this ramps the new audio up from
 * silence after it. BOTH halves are required -- a fade alone starts at zero
 * while the DAC still holds the old level (the step just moves), and the glide
 * alone ends at zero and then steps up to the first full-scale sample.
 *
 * This includes RESUME from pause. Resume used to be click-free by accident:
 * the held sample was waveform-continuous with the next pushed one. The glide
 * breaks that bargain, so resume must ramp like everything else. */
#define FADE_SAMPLES 2048u
static uint32_t fade_left;
static uint8_t  under_shadow;   /* underrun already faded this flush epoch */
static uint32_t pcm_under_n;    /* underrun EDGES since boot, for the diag  */

#if TAU_DIAGNOSTIC
#include "stress_defs.inc"
#endif

/* FIRST underrun of the current track, latched with its circumstances.
 *
 * The hiccup two to three seconds in has now survived one fix aimed at the
 * size probe, and reading the code has eliminated the other two candidates:
 * probe_file_size() only runs on a seek press, and art_decode() runs inside
 * load_track() before a sample is played. Guessing a fourth time is not a
 * plan.
 *
 * The first question is not WHICH cause -- it is whether the glitch is a
 * starved decoder at all. If no underrun edge is recorded while it is plainly
 * audible, then no amount of buffer or I/O work can fix it and the fault is
 * downstream, in the fade, the glide or the decoder itself. That is a
 * different repair entirely, and this tells the two apart in one sitting.
 *
 * DEBUG BUILD ONLY -- displayed under UI_SHOW_DIAG, which ships at 0. */
static uint32_t und1_sec = 0xFFFFFFFFu;  /* second it happened, FFFF = never */
static uint8_t  und1_idle, und1_io;      /* CPU split at that instant       */
static uint8_t  und1_szp;                /* size-probe phase; 0 = not running */
static uint16_t und1_ring;               /* ring bytes ahead, in 64B units  */

/* A SECOND detector, because the first one could not see the fault.
 *
 * under_shadow is cleared only by a flush, so exactly one underrun is ever
 * counted per track -- which is why N rose by exactly one per track and why
 * songs 2 and 3 reported second ZERO. That first edge is the start-of-track
 * transient: the FIFO is empty before the first decode fills it. It is
 * expected, it is faded, and it MASKS everything after it, including the
 * audible one.
 *
 * This counts edges itself and does not consume the shadow, so the fade
 * behaviour is untouched. It ignores anything in the first second, which is
 * the transient, and latches the first REAL one after it. */
static uint8_t  und_prev;
static uint32_t und_edges;
static uint32_t und2_sec = 0xFFFFFFFFu;
static uint8_t  und2_idle, und2_io, und2_szp;
static uint16_t und2_ring;

/* CLICK detector -- measures the waveform instead of the plumbing.
 *
 * The readings rule out starvation: no underrun at two to three seconds on any
 * track, and any CPU stall long enough to hear would have emptied the FIFO and
 * shown up as one. So the samples ARE arriving on time and the fault is in
 * their VALUES: a discontinuity, which is heard as a click or a pop rather
 * than as a stutter.
 *
 * Sample-to-sample jump on the left channel. Real music at 44.1 kHz rarely
 * steps more than a few thousand between adjacent samples -- a jump of half
 * full scale is not music, it is a splice. clk_max is reported alongside so
 * the threshold can be judged against the material rather than trusted. */
#define CLK_THRESH 12000
static int32_t  clk_prev;
static uint32_t clk_n;
static uint32_t clk_sec = 0xFFFFFFFFu;   /* second of the FIRST one */
static uint32_t clk_max;                 /* largest jump seen, any time */
/* Where the CPU's second actually goes, so the hiccup can be ATTRIBUTED
 * instead of guessed at. Three outcomes, three different fixes:
 *
 *   idle high  -- the decoder finishes early and waits on a full FIFO. The
 *                 CPU is fine; a hiccup then is the ring or the SD read.
 *   io high    -- the decoder is blocked in flac_pull waiting for bytes. The
 *                 ring is too small or the reads are too slow.
 *   both ~0    -- the decoder cannot keep up. Only optimisation helps.
 *
 * Cycles, not iterations: a wait iteration and a decode iteration are not the
 * same size, and comparing counts of them would prove nothing. */
static uint32_t fl_idle_cyc, fl_io_cyc;    /* accumulating, this second     */
static uint32_t fl_rate_hz;                /* mirrors fl.rate, declared later */
static uint8_t  fl_bps_mirror;             /* mirrors fl.bps, declared later -- vu_master.inc's overlay needs it before fl exists */
static uint8_t  fl_io_pct;
static uint32_t ui_last_prof;              /* UI_SHOW_DECODE_PROFILE latch, Phase F step 1 */
static int32_t  vol_gain = 256;          /* Q8: 256 == unity */

static void vol_apply(void)
{
    vol_gain = (int32_t)(volume * 256u / 100u);
}
/* ------------------------------------------------------------- playlist ----
 * The legacy .m3u-playlist-file playback mode (state + logic in playlist.inc)
 * has been removed: the media library (fw/library.inc) is now the only way to
 * play more than one file. playlist.inc still holds the shared "open a named
 * file into a slot" primitive the library itself uses. */

/* Overlay state, declared HERE rather than with its drawing code:
 * ui_draw_chrome() repaints the overlay on top of itself and sits a
 * thousand lines earlier in the file. */
static uint8_t  set_dirty;
static int  set_input(uint32_t edge, uint32_t keys);
#if TAU_RAM_192K_FW
#define SET_INPUT(e, k) ((set_open || ((e) & KEY_START)) && cold_code_ok && set_input((e), (k)))   /* set_input is cold code: only when settings are open or Start went down */
#else
#define SET_INPUT(e, k) set_input((e), (k))
#endif
static void set_draw(void);
static void set_close(void);
static void set_open_wvizcfg_direct(void);   /* Start+X chord in poll_input(), defined in settingsui.inc */

enum { REP_OFF = 0, REP_ALL, REP_ONE };
static uint8_t  rep_mode;                /* cycles off -> all -> one -> off */

/* Screen blanking. blank_min is minutes with no button press before the screen
 * goes black; 0 disables it. Playback is untouched -- only drawing stops.
 *
 * Be honest about what this buys, because an earlier version of this comment
 * was not. The Pocket is a 1600x1440 LTPS LCD, NOT an OLED, so there is no
 * burn-in to protect against -- an LCD gets temporary image persistence at
 * worst. Nor is it a power saving: a core cannot reach the backlight (the APF
 * interface carries pixel data and sync and nothing else), so the panel stays
 * lit and the draw calls saved are a rounding error next to it.
 *
 * What it actually gives you is a dark screen in a dark room. That is a real
 * want, and it is the whole of the case for this feature.
 *
 * Set by Select+Down, not persisted. */
/* The RESUME feature (a packed settings word remembering the playback
 * position across boots) was removed along with legacy playlist mode: it only
 * ever saved a position when a track was started BY THE LEGACY PLAYLIST
 * (track_from_pl), never for a single picked file or a library track, so with
 * the playlist gone it had no remaining path to ever fire. See settings.inc
 * for the now-retired persist words SW_RETIRED_RESUME/SW_RETIRED_RESUMEON. */

static uint32_t blank_min;            /* 0 = never; set by Select+Down        */
static uint32_t blank_sec;            /* whole seconds since the last button   */
static uint32_t blank_tick;           /* cycles() deadline for the next second */
static uint8_t  screen_blank;         /* the screen is currently black         */
static uint32_t ui_mode_dirty = 1u;      /* repaint the mode icons / N-of-M   */
static uint32_t idle;                    /* nothing loaded: waiting on the user */
static uint8_t  stopped;                 /* Start: at 0:00, not merely paused  */
static uint8_t  hold_paused;             /* stay paused across a track change  */

/* Processor load for display (the playing bar's percentage, Info > CPU LOAD): the share of the last second NOT spent
 * waiting for room in the PCM FIFO. The decode loop blocks there when it is ahead of the DAC, so the blocked time is the
 * idle time (fl_idle_pct, latched once a second, capped at 99). Nothing is decoding while stopped or paused, so 0. */
static uint8_t fl_idle_pct;
static inline uint32_t ui_cpu_pct(void) { return (idle || paused) ? 0u : 100u - (uint32_t)fl_idle_pct; }
static uint32_t stop_req;

#define PL_HOLD_MS 400u                  /* Left/Right held this long = skip  */
/* 1.2x speed needs a LONGER hold than everything else.
 *
 * It used to share PL_HOLD_MS with the scrub and the art panel, and 400 ms is
 * short enough to reach by lingering on the play button -- users were ending up
 * in 1.2x without knowing they had done anything, with only the music sounding
 * fast to tell them. Two and a half times the length makes it a gesture rather
 * than a slip, and the on-screen indicator covers the case where it happens
 * anyway. Started at 1200 ms and came down to 1000 on the only authority that
 * counts for a hold, which is a thumb.
 *
 * A modifier was tried instead and does not work here: holding Select is
 * already the album-art panel, so Select+A fights a binding that fires at the
 * same threshold. */
#define SPEED_HOLD_MS 1000u              /* A held this long = 1.2x toggle    */

/* paused is a bitmask: 1 the user, 2 the OS menu. (A third bit, for a legacy
 * playlist switch in progress, was removed along with that mode.) */
static uint8_t  menu_was;         /* the OS menu was open on the last poll      */
/* helios_pending_mask (fw/helios.inc, item 4 of the Helios review) replaces the old bare
 * "pl_ui_restore" repaint flag with a per-view declared bitmask -- see that file's own comment for
 * the full history (B-349/B-350). The main loop drains it below, right before the flag it replaces
 * used to be read, since ui_chrome_paint()/ui_art_draw() aren't declared yet at helios.inc's own
 * #include point. */
/* The TRACK loads that follow a reload, three deep -- general reload diagnostics, not specific to
 * the (now removed) legacy playlist mode.
 *   G  1 the gate confirmed a new file id, 2 it fired on the cap
 *   O  1 load_track() returned a track, 0 it did not
 *   C  1 the filename CHANGED, 0 the SAME file was reopened */
static uint16_t tk_hist[3];
static uint32_t tk_prev_name;     /* FNV of track_file before the load */

static void settings_mark_dirty(void);
static uint8_t set_flush_now;
/* Set by settings_load() once APF's stored values have been taken. Nothing may
 * publish before that: settings_store() writes all eight words, so an earlier
 * write would push firmware defaults over what the user had saved. Declared
 * here rather than in settings.inc, which is included further down. */
static uint8_t settings_adopted;
static uint8_t settings_load_ok;   /* what the BOOT settings_load() returned */

/* Defined with the rest of the blanking code, below the error paths that have
 * to wake the screen before drawing to it directly. */
static void ui_blank_wake(void);

static uint8_t eq_apply;             /* preset changed: tell the RTL */

static uint32_t seek_req;                /* +1 forward, -1 back (as unsigned) */
static uint32_t soft_restart_req;  /* probe: reposition only, keeps the known-good tag */
static uint32_t reload_pending;
static int      reload_armed;      /* reload seen, waiting out the settle delay */
static uint32_t reload_at;         /* hard deadline: act even if never confirmed */
static uint32_t reload_probe_at;   /* next slot-changed check                    */
static uint32_t reload_settle;     /* blind wait when identity is unavailable     */

/* First four bytes of the file as they actually landed in RAM, captured the
 * instant the head read returns. Surfaced on screen when no tag is found,
 * because "(No ID3 Tag)" alone cannot distinguish the three real possibilities:
 *   49 44 33 ..  tag IS there  -> the parser is at fault
 *   FF Fx ..     bare MP3 sync -> file genuinely has no ID3v2 tag (not a bug)
 *   00 00 00 00  nothing       -> the read didn't land; an APF/DMA problem
 * Guessing between those is what burned two rounds on the reload bug earlier;
 * four bytes on screen settles it in one hardware test. */
static uint8_t head_bytes[4];

/* R_RELOAD snapshot taken at the moment the reload was acted on, and the file
 * size APF reported with the 008A notification. Both shown in the diagnostic
 * line so a failing reload reports its own circumstances. */
static uint32_t reload_status;
static uint32_t slot_size;

/* Previous track's head bytes and APF-reported size, for the stale-slot check
 * in read_track_head(). reload_retries counts how often that check actually
 * fired -- shown on screen, so the mitigation is observable instead of taken
 * on faith. */
/* Identity of the track we are LEAVING, captured at the moment a reload is
 * noticed and then held for the whole probe window. This is what makes
 * staleness detectable: `prev_head`/`prev_slot_size` used to be overwritten by
 * the (possibly stale) load itself, which destroyed the very reference the
 * later probes needed to compare against. */
static char     stale_ref_title[TITLE_MAX];   /* title of the track being left */
static uint32_t stale_ref_size;        /* and the size APF reported for it */
static char     last_title[TITLE_MAX];       /* title the current load settled on */
static char     track_file[64];        /* filename APF reports for the slot  */

/* A file identity that SURVIVES A POWER CYCLE, which cur_file_id does not.
 *
 * cur_file_id hashes the whole 0190 response struct -- 64 words -- and was
 * built to answer "has the slot changed yet?" by comparing two hashes taken
 * minutes apart in the same session. Nothing ever established it is stable
 * across a relaunch, and it is not: resume was rejecting every saved point
 * with a tag mismatch because the same file hashed differently on the next
 * boot. Anything in that 256-byte window that is a handle, padding, or
 * residue from an earlier command moves, and the hash moves with it.
 *
 * The filename is a property of the FILE, so it is the same on every boot.
 * slot_filename() extracts it as the longest printable-ASCII run rather than
 * trusting an offset, which is also why it is the sturdier thing to hash. */
/* (track_name_id() lived here: a filename hash tried as a cross-boot file
 * identity, measured unstable, and its gate removed. Deleted, not left to
 * rot.) */
static uint32_t cur_file_id;           /* 0190 identity of the loaded file   */
static int      slot_changed(void);    /* defined with the 0190 helpers      */
static uint32_t tk_poll_at;            /* next track-slot identity check     */
static uint32_t force_size_probe;      /* R_SLOT_SZ is stale: measure instead */
static uint8_t  seek_size_tried;       /* backstop size probe already attempted */
static uint32_t stale_ref_file_id;     /* ...and of the one being left       */
static uint32_t reload_retries;
static uint32_t tag_corrections;   /* periodic probe found a wrong tag */

/* ------------------------------------------------------------ UI layout ---
 * No album art (would need a JPEG decoder for ID3 APIC frames -- out of
 * scope), so this is a plain dark-mode layout: title/artist, a real
 * amplitude-driven level meter, elapsed time. Colours are RGB565.
 */
#include "theme.h"   /* named UI colours as roles (step 0a of the theme system); UI_PANEL etc. read th_role[] */
/* Vertical gradient endpoints. Drawn as horizontal bands rather than a true
 * per-pixel ramp: a rect is ONE engine command, so 40 bands cost 40 commands
 * where a per-row ramp would cost 360 -- and in RGB565 a 40-step ramp across
 * this small a colour range is visually smooth. No RTL change needed. */
#define UI_GRAD_TOP 0x2124u   /* neutral graphite -- the blue cast read as murky
                               * behind the card, and a grey ramp lets the teal
                               * accent be the only colour on screen */
#define UI_GRAD_BOT 0x0000u   /* black          */
#define UI_BANDS    40u

/* Accent palette, cycled with the L/R shoulder triggers. Deriving the accent
 * from the cover art was tried and dropped -- it changed on every track and
 * read as inconsistency. A colour the USER picks is stable, which is the
 * difference. Chosen to sit well on the graphite background: nothing so dark
 * it disappears, nothing so pale it competes with the white type. RGB565. */
static const uint16_t ui_palette[] = {
    /* Analogue Pocket hardware edition colours (RGB565), per HANDOFF_PROMPT_pocket_colors_theme_replacement.md.
     * The comment on each is the text colour the handoff validated on it.
     * Values are the RGB565 of the reference hex list (analogue-pocket-colors notes, B-315); the earlier table had the right red
     * field but wrong green and blue fields (SILVER read red-orange, SMOKE dark red, CLEAR yellow). */
    /* Standard Edition */
    0x18C3u,   /* BLACK           - white text */
    0xF7BEu,   /* WHITE           - black text */
    /* Glow Limited Edition */
    0x2A83u,   /* GLOW            - white text */
    /* Transparent Limited Edition (warm to cool) */
    0xE73Bu,   /* TRANS_CLEAR     - black text */
    0x8C51u,   /* TRANS_SMOKE     - white text */
    0xD1E6u,   /* TRANS_RED       - white text */
    0xE429u,   /* TRANS_ORANGE    - black text */
    0x5D0Du,   /* TRANS_GREEN     - black text */
    0x4BD4u,   /* TRANS_BLUE      - white text */
    0x9BF6u,   /* TRANS_PURPLE    - black text */
    /* Classic and GBC Limited Edition (hue sequence) */
    0xFEA0u,   /* CLASSIC_YELLOW  - black text */
    0xE429u,   /* CLASSIC_ORANGE  - black text */
    0xD1E6u,   /* CLASSIC_RED     - white text */
    0xE4B6u,   /* CLASSIC_PINK    - black text */
    0x4BD4u,   /* CLASSIC_BLUE    - white text */
    0x5D0Du,   /* CLASSIC_GREEN   - black text */
    0x6AD2u,   /* CLASSIC_INDIGO  - white text */
    0xBDF7u,   /* CLASSIC_SILVER  - black text */
    /* Aluminum Limited Edition */
    0x8C71u,   /* ALUMINUM        - white text */
};
#define UI_PALETTE_N (sizeof(ui_palette) / sizeof(ui_palette[0]))

/* Shown in the toast when L/R changes the accent -- the colour was the only
 * control that changed something without saying what it changed to.
 *
 * A parallel array is a correspondence a later edit can break in silence: add a
 * colour, forget the name, and the label reads off the end. The assert makes that
 * a build error instead of a bug someone finds on hardware. Keep the order. */
static const char *const ui_palette_name[] = {
    "BLACK", "WHITE", "GLOW",
    "TRANS_CLEAR", "TRANS_SMOKE", "TRANS_RED", "TRANS_ORANGE", "TRANS_GREEN", "TRANS_BLUE", "TRANS_PURPLE",
    "CLASSIC_YELLOW", "CLASSIC_ORANGE", "CLASSIC_RED", "CLASSIC_PINK", "CLASSIC_BLUE", "CLASSIC_GREEN",
    "CLASSIC_INDIGO", "CLASSIC_SILVER",
    "ALUMINUM",
};
_Static_assert(sizeof(ui_palette_name) / sizeof(ui_palette_name[0]) == UI_PALETTE_N,
               "ui_palette_name[] must name every color in ui_palette[]");
/* Default: WHITE (index 1). The handoff left BLACK or WHITE to the implementer; BLACK (0x0843) is darker than the UI
 * background, and the accent is also used as text and as fills that carry dark text, so a BLACK default would make the
 * first screen unreadable. interact.json's Color default is 1 to match. */
#define UI_ACCENT  0xF7BEu
static uint16_t ui_accent = UI_ACCENT;
static uint32_t ui_pal_idx = 1u;      /* WHITE, the default (see UI_ACCENT) */

/* Transient status line. Volume and seek had no on-screen confirmation at all
 * -- the only functional control feedback missing. Shown briefly, then wiped
 * back to the gradient. */
#define UI_TOAST_Y  (UI_WAVE_Y + UI_WAVE_H + 2u)
#define UI_TOAST_H  16u
#define UI_TOAST_HOLD  (CLK_HZ)            /* full brightness ~1 s   */
#define UI_TOAST_FADE  (CLK_HZ * 3u / 4u)  /* then dissolve over ~.75 s */
#define UI_TOAST_STEPS 10u
static char     ui_toast[24];
static uint32_t ui_toast_t0;               /* 0 = inactive */
static uint32_t ui_toast_step;             /* 0 = solid, UI_TOAST_STEPS = gone */
static uint32_t ui_toast_end;              /* x the last toast draw reached    */

#define UI_CARD_H  120u
#define UI_SHOW_DIAG 0        /* 1 = show A/S/T/F reload diagnostics */

/* Speed-branch instrumentation. ON by default here and NOT behind a button
 * combo, deliberately: the last diagnostic on this project never appeared
 * because its trigger was written down wrong (Select+L, when the code wanted
 * Select and L held plus Start), and the hardware round trip was wasted. A
 * readout with no way to fail to summon it cannot repeat that.
 *
 * OFF by default: flipping this to 1 brings back the speed row, which found
 * several faults here, and the 1.2x seek defect is still open. Cheaper to
 * keep than to rewrite. (The resume-diagnostic row this comment used to
 * mention alongside it was removed with the resume feature and legacy
 * playlist mode.) */
/* TEMPORARY, with IO_BENCH: shows the throughput figure. Back to 0 before
 * anything ships -- no diagnostic is ever shown to users. */
/* 0 for any build a user sees -- the standing rule is that no diagnostic ever
 * reaches one. Set to 1 to bring the D/O/U/F row back while investigating. */
#ifndef UI_SHOW_SPEED_DIAG
#define UI_SHOW_SPEED_DIAG 0
#endif
/* Phase F step 1 (docs/PHASE_F_SPEC.md section 14): per-stage decode cost,
 * MP3's H(uffman)/I(mdct)/S(ubband) plus a refresh of FLAC's R(ice), on the
 * same per-second latch as the row above. Follows MP3_PROFILE/FLAC_PROFILE
 * so a normal build (both 0) never carries it. */
#ifndef UI_SHOW_DECODE_PROFILE
#define UI_SHOW_DECODE_PROFILE 0
#endif
/* Seek instrumentation. Build with EXTRA_CFLAGS=-DUI_SHOW_SEEK_DIAG=1.
 *
 * This build CHANGES NO BEHAVIOUR. Two reasoned fixes for the post-seek clock
 * both failed on hardware, and both were backed by offline models that said
 * they would work -- the models were of the wrong thing twice over. So this
 * only shows what a correction WOULD have computed, while seeking keeps
 * working exactly as it does today. Never shipped: the flag defaults to 0. */
#ifndef UI_SHOW_SEEK_DIAG
#define UI_SHOW_SEEK_DIAG 0
#endif
#if UI_SHOW_SEEK_DIAG
static uint32_t dg_tgt, dg_at, dg_pos, dg_ui, dg_blk, dg_rate;
/* The load-path fields. Copied rather than read where the rows are drawn:
 * ui_draw_dynamic() sits above every FLAC declaration in this file. */
static uint32_t dg_size, dg_dur, dg_first, dg_pts, dg_len, dg_intent;
/* Probe telemetry. Distinguishes "the search ran and could not get close"
 * from "no probe ever came back", which are different faults with different
 * fixes -- and the second is what a file opened BY NAME rather than mounted
 * as a sized slot would produce if random access behaves differently there.
 * Playback only ever reads forward, so it would never reveal that. */
static uint8_t  dg_pn, dg_pfail, dg_prej;
static uint32_t dg_num[3];
static uint8_t  dg_n, dg_samp, dg_fe, dg_live;
#endif

/* Load-phase breakdown as a toast after every load: H head, S size probe,
 * A artwork, P prefill, T total, in ms. 1 only while investigating load time;
 * it must be 0 in anything a user sees.
 *
 * #ifndef so it can be turned on WITHOUT editing this file:
 *     EXTRA_CFLAGS=-DUI_SHOW_LOAD_TIMES=1 bash fw/build.sh
 * which is the whole point of a measurement flag -- an investigation should
 * not need a source edit that then has to be remembered and reverted. */
#ifndef UI_SHOW_LOAD_TIMES
#define UI_SHOW_LOAD_TIMES 0
#endif

/* ---- TEMPORARY: sequential SD throughput benchmark ------------------------
 *
 * The ONE measurement the FLAC decision turns on. Everything else about FLAC
 * is understood; whether the card can sustain ~112 KB/s is not, and 40 KB/s at
 * MP3's 320 kbps is the most this core has ever had to hold.
 *
 * It has to be a BURST, not an observation of normal playback: during playback
 * the refill rate is limited by DEMAND, so timing it would measure the bitrate
 * of the file and report it as a capacity figure. This reads N chunks
 * back-to-back with no decoding in between, which is the ceiling.
 *
 * Sequential on purpose. The ~480 ms the old size probe spent on ~20 reads is
 * not representative -- those were random offsets that made APF re-walk the
 * cluster chain each time.
 *
 * Lands in the tag scratch buffer, never the ring, so it cannot disturb audio.
 * Runs ONCE a session. MUST go back to 0 before shipping. */
#define IO_BENCH 0
/* Defined HERE, above every user. It lived next to REFILL_CHUNK, ~90 lines
 * BELOW the diagnostic row that tests it -- so `#if IO_BENCH` in the row saw
 * an undefined name, evaluated it as 0, and dropped the readout with no
 * warning. The benchmark ran; its result was simply never printed. */
static uint16_t io_kbps;          /* measured sustained sequential read rate */
static uint32_t io_bench_bytes;   /* ...and how much it managed to read      */

#define UI_MARGIN   16u
#define UI_TITLE_Y  30u   /* splash / LOAD FAILED only now -- see UI_NP_TITLE_Y for the player screen */
/* Scrolling amplitude history, drawn as bars -- the "waveform" element from
 * the reference art. Bars are cheap (one rect each) now that the engine owns
 * the row loop, and a rolling history reads as motion in a way a single
 * left-to-right level bar never does. */
/* ---- vertical layout knobs -------------------------------------------------
 * Every band's position lives here.
 *
 * Now-playing UI pass 1 (Figma node 163:57, first Helios-era layout,
 * 2026-09-25): art moved to a static top-left mount (ART_X/ART_Y, no longer
 * derived from the meter), the text block moved beside it (UI_TEXT_X), a
 * genre pill sits above the title (UI_GENRE_Y/H), the old solid card panel
 * behind the text is gone (text now blends against the gradient per row,
 * like every other un-carded element already does), and the meter got
 * bigger and full-width now that nothing shares its rows any more. Splash
 * and LOAD FAILED keep the OLD UI_TITLE_Y/UI_CARD_H layout untouched --
 * those are different screens, not moved by this pass.
 *
 *   art       ART_Y .. +ART_H (fixed, top-left)
 *   pill      UI_GENRE_Y .. +UI_GENRE_H
 *   title     UI_NP_TITLE_Y (text column starts at UI_TEXT_X)
 *   meter     UI_WAVE_Y .. +UI_WAVE_H
 *   toast     UI_TOAST_Y
 *   progress  UI_PROG_Y
 *   clock     UI_TIME_Y
 *   transport UI_TRANSPORT_Y
 */
#define UI_GENRE_Y   22u
#define UI_GENRE_H   18u
#define UI_NP_TITLE_Y (UI_GENRE_Y + UI_GENRE_H + 8u)   /* 46 */
#define UI_TEXT_X    (ART_X + ART_W + 16u)             /* 140 */
#define UI_WAVE_N   36u
#define UI_WAVE_Y   152u
#define UI_WAVE_H   122u
#define UI_WAVE_GAP 2u
/* Extra rows above the meter box that ui_bg_restore() and ui_wave_clear() also rebuild. Only the cassette meter (archived 2026-09-26, see
 * archive/cassette_meter/) drew above UI_WAVE_Y; nothing does now, but the range is kept so a meter that does can be added without
 * touching the two rebuilders. */
#define UI_WAVE_TOP 24u
#define UI_PROG_Y   295u
#define UI_PROG_H   6u
#define UI_TIME_Y   306u
#define UI_TRANSPORT_Y 334u   /* clock (24 px cell) ends at UI_TIME_Y + 24 = 316: a 4 px gap, no overlap (B-256) */
/* Diagnostic Build's stress readout: top right, above the EQ pill (rows 0..21), right-aligned, clear of the cover (x >= UI_TEXT_X). */
#define UI_STRESS_BAR_Y 1u
#define UI_STRESS_HUD_Y 4u
#define UI_INNER_W  (FB_W - 2u * UI_MARGIN)
/* Right edge a painted glyph CELL may not cross on the info card. The card
 * spans UI_MARGIN-8 .. UI_MARGIN-8+UI_INNER_W+16, so this leaves 8px of
 * padding on the right to match the 8px already on the left. */
#define UI_CARD_TEXT_R (UI_MARGIN + UI_INNER_W)

#define UI_SHOW_UNDERRUN 0   /* red square, top-right: audio FIFO ran dry */
#define UI_UNDERRUN_SZ 10u
#define UI_UNDERRUN_X  (FB_W - UI_MARGIN - UI_UNDERRUN_SZ)
#define UI_UNDERRUN_Y  UI_MARGIN

static uint32_t ui_last_sec   = 0xFFFFFFFFu;
static uint32_t ui_last_vu    = 0xFFFFFFFFu;
static uint32_t ui_last_pause = 0xFFFFFFFFu;
static uint32_t ui_last_stall;
static uint32_t ui_last_spd = 0xFFFFFFFFu;   /* speed-branch diag row */
static uint8_t  ui_splash_art_active;         /* authored Tau loading screen up */
/* One marquee per scrollable line. Title and artist can both overflow, and
 * they scroll independently -- a shared position would drag the shorter one
 * around for no reason. */
typedef struct {
    char     text[TITLE_MAX];
    uint32_t y, scale, on, pos, next;
} ui_marquee_t;
static ui_marquee_t ui_mq_title, ui_mq_artist;
/* Visualisations, cycled with X. The choice persists via interact.json.
 *
 * All three run off what the decoder already produces -- there are no frequency
 * bins here, so none of these is a spectrum: BARS and WATER show loudness over
 * TIME, LEVELS shows the two channels right now. */
/* APPEND ONLY -- never reorder, never insert.
 *
 * viz_mode is persisted as an INDEX, so moving an entry silently repoints
 * every user's saved meter at a different one. Same rule as the interact.json
 * ids. New modes get the next meters/NAME/meter.json index; see fw/meter_gen_enum.h
 * (generated by tools/gen_meters.py, docs/METER_MODULE_SPEC.md M0). */
#include "meter_gen_enum.h"

/* TAU_ART_TIMG (docs/COVER_TIMG_READER.md): the thumbnail stash is COMPACTED to the live meters so 128 rows are free for the cover reader's index
 * plane. The four retired enum slots have no thumbnail data, so they need no rows; every other meter's rows shift up. With the macro off the
 * mapping is the identity and nothing changes.
 *
 * VIZ_VU_MASTER is ALSO excluded from the compacted stash (not just the four retired slots): the stash budget between
 * ART_STASH_Y (360) and the Chladni plane (984, the hard end of the RTL's 19-bit/1024-row framebuffer address space --
 * there is no more room to grow into) was already down to its last 16-row margin before this meter existed, and one more
 * live METER_THUMB_H (32u) slot does not fit. set_draw_thumb() always uses the software RLE fallback (the same path an
 * old bitstream without the blit engine uses, B8's fail-safe) for this one meter instead of building a hardware-blit-cached
 * row for it, so it costs a few dozen extra fb_rect() calls on the rare occasions its Settings-list thumbnail is drawn,
 * not a stash row it doesn't have room for. */
#if TAU_ART_TIMG
#define THUMB_LIVE_SLOTS (VIZ_COUNT - 6u)
static inline uint32_t thumb_slot(uint32_t v)
{
    return v - (v > VIZ_RETIRED_LEVELS) - (v > VIZ_RETIRED_MIRROR) - (v > VIZ_RETIRED_EYE) - (v > VIZ_RETIRED_TAPE) - (v > VIZ_CHLADNI);
}
#else
#define THUMB_LIVE_SLOTS VIZ_COUNT
static inline uint32_t thumb_slot(uint32_t v) { return v; }
#endif

/* RETIRED meters keep their enum slot (viz_mode persists as an INDEX, so slots can never be reused or shifted) but have no code,
 * no thumbnail data and no row in the list: VIZ_RETIRED_TAPE (the cassette meter, archived in archive/cassette_meter/), VIZ_RETIRED_LEVELS (L/R levels) and VIZ_RETIRED_EYE
 * (magic eye) and VIZ_RETIRED_MIRROR (mirrored bars, now a layout of BARS), all removed 2026-09-26 at the owner's request. Their drawing code is in git (tag backup/pre-meter-removal-2026-09-26).
 *
 * The list (Settings > Meter, and the X button) is NOT in enum order any more: viz_order[] (fw/meter_gen_order.h,
 * each manifest's sel_index) is that order; the two functions below translate between a list row and the
 * persisted mode value, and every consumer (choice list, X cycle, restore of the saved setting) goes through them. */
#include "meter_gen_order.h"

/* BARS has two layouts (Select+X): 0 = up from the bottom, 1 = mirrored about the centre line. Mirrored Bars used to be its own meter; its
 * slot VIZ_RETIRED_MIRROR is now how the mirrored layout is SAVED (settings hold viz_mode as an index, so a saved MIRRORED BARS keeps
 * meaning "mirrored"): the setting is written as VIZ_RETIRED_MIRROR and read back as VIZ_BARS + layout 1. */
static uint8_t bars_layout;
static inline uint32_t viz_sel_to_mode(uint32_t row)  { return viz_order[row < VIZ_SEL_COUNT ? row : 0u]; }
static inline uint32_t viz_mode_to_sel(uint32_t mode) {
    for (uint32_t i = 0; i < VIZ_SEL_COUNT; i++) if (viz_order[i] == mode) return i;
    return 0u;
}
static inline int viz_selectable(uint32_t mode) { for (uint32_t i = 0; i < VIZ_SEL_COUNT; i++) if (viz_order[i] == mode) return 1; return 0; }

/* Stereo phase scope. Left against right, rotated 45 degrees so mono lands on
 * the vertical -- the standard goniometer orientation, and the reason it reads
 * at a glance: a vertical line is mono, a wide cloud is a wide mix, a
 * horizontal spread is out of phase.
 *
 * Points are captured during decode rather than read from pcm[] at draw time:
 * the buffer is refilled every frame and the UI runs on its own schedule, so
 * drawing from it directly would sample whatever happened to be there. */
#define SCOPE_N 48u
/* Frames of persistence. A vectorscope drawn as isolated dots, cleared every
 * pass, never builds into a shape -- what makes a real one readable is the
 * phosphor holding the trace while it moves. Keeping a few frames and drawing
 * the older ones dimmer costs a little more per pass but multiplies what is on
 * screen: 48 points x 4 frames reads as a continuous figure where 96 fresh dots
 * read as static. */
#define SCOPE_HIST 4u

/* Oscilloscope: a short TRIGGERED window, not the whole frame's envelope.
 *
 * Min/max across each column's slice filled almost the whole box, because a
 * column covering ~18 samples spans several cycles of anything above a few
 * hundred Hz -- the envelope of a frame is nearly always full scale. Showing a
 * brief window instead means the columns follow the wave itself, so the trace
 * is thin and you can see the shape moving.
 *
 * The window starts at a rising zero crossing so successive frames line up
 * instead of sliding; without that the trace skates sideways and reads as
 * noise. */
/* VU: two analogue meters, left and right.
 *
 * Ballistics matter more than the drawing. A real VU integrates over ~300 ms;
 * a needle tracking instantaneous peaks twitches and reads as an artefact
 * rather than a meter. Fast attack, slow decay, in a Q8 accumulator so the
 * movement is smooth at any frame rate.
 *
 * Deflection is by SIN TABLE rather than a divide per pixel: the needle is
 * drawn as a run of short segments along the angle, and 16 entries at Q12 is
 * both cheaper and smaller than the trigonometry. */
#define VU_ATT   180u             /* Q8 rise per frame toward the target */
#define VU_DEC    40u             /* ~370 ms full-scale fall: VU ballistics */
#define VU_STEPS  28u             /* segments: 1.9 px apart, so the needle
                                   * is solid. At 10 they sat 5.4 px apart and
                                   * the needle read as a dotted line. */
static uint32_t vu_l, vu_r;       /* Q8 deflection, 0..255               */
static uint8_t  vu_face;          /* the static face is on screen        */
static uint16_t vu_face_w;        /* ...and the width it was drawn for   */
static uint8_t  vu_shown_l, vu_shown_r;   /* deflection currently drawn   */

/* Needle angle, -50 to +50 degrees from vertical in 16 steps: a 100 degree
 * sweep, which is what a real VU movement travels. The first attempt built one
 * table and tried to derive both components from it by index arithmetic; the
 * values ran past Q12 unity, so the sweep came out narrow and lopsided -- the
 * 45-degree stub. Two honest tables, generated rather than hand-written. */
static const int16_t vu_sn[17] = {
    -3138, -2832, -2493, -2125, -1731, -1317,  -887,  -446,     0,
      446,   887,  1317,  1731,  2125,  2493,  2832,  3138
};
static const int16_t vu_cs[17] = {
     2633,  2959,  3250,  3502,  3712,  3879,  3999,  4072,  4096,
     4072,  3999,  3879,  3712,  3502,  3250,  2959,  2633
};

/* Interpolate between table entries. Indexing the table directly gave the
 * needle 17 positions across the sweep -- the tip jumping ~5 px at a time,
 * which reads as stepping rather than sweeping. Interpolating gives it 256, so
 * the movement is as smooth as the ballistics driving it. */
static void vu_angle(uint32_t v255, int32_t *sn, int32_t *cs)
{
    uint32_t pos = v255 * 16u;                 /* 0..4080 */
    uint32_t q   = pos / 255u;                 /* 0..16   */
    uint32_t f   = pos % 255u;
    uint32_t q1  = (q < 16u) ? q + 1u : 16u;
    *sn = vu_sn[q] + (int32_t)((vu_sn[q1] - vu_sn[q]) * (int32_t)f) / 255;
    *cs = vu_cs[q] + (int32_t)((vu_cs[q1] - vu_cs[q]) * (int32_t)f) / 255;
}


#define WAVE_COLS 64u
#define WAVE_SPAN 4u              /* samples per column -- 256 sample window */
static signed char wav_v[WAVE_COLS];
/* Capture is normalised to a fixed +-SCOPE_UNIT; the DRAW scales that onto the
 * box. Splitting it that way is what lets x and y have different extents: the
 * meter area is 246x72, so an isotropic trace can only ever be 72 px across and
 * sits as a small blob in a wide empty rectangle. Stretching x fills the space
 * and exaggerates stereo width, while mono still collapses to x = 0 and
 * out-of-phase still lies flat -- the readings that matter are preserved. */
#define SCOPE_UNIT 100
static signed char scope_x[SCOPE_HIST][SCOPE_N], scope_y[SCOPE_HIST][SCOPE_N];
static uint8_t     scope_head;   /* newest frame */
static uint8_t  viz_mode;
static uint32_t peak_l, peak_r;          /* per-channel, for LEVELS */

/* ---- LED LADDER -------------------------------------------------------
 * Two channels, nine rows, three blocks across each row.
 *
 * The row count is vertical so it does not change with the panel, but the
 * WIDTH does -- 360 px with the art hidden, 252 with it showing. One block per
 * row would be 25:1 hidden, a stack of thin lines rather than LEDs; splitting
 * each row three ways gives 7.9:1 and 5.3:1, so it reads as the same
 * instrument either way. True square LEDs across a 176 px column would need
 * roughly 340 rects a frame, which is not worth it on the audio budget; this
 * costs 54.
 *
 * Nine rows also divides the 72 px band exactly at 7 px plus a 1 px gap, and
 * gives 11% steps -- with the 3/4 meter headroom, real music sits at 4..7 of
 * 9 and never pegs. */
#define LED_ROWS 12u
#define LED_GAPV 1u
#define LED_BLKH (UI_WAVE_H / LED_ROWS - LED_GAPV)     /* 8 px, now-playing UI pass 1: was 5 px at the old UI_WAVE_H=72 */
#define SPEC_GAPX 3u                                   /* between columns */

/* ---- WINAMP-STYLE BARS/SCOPE (B-215/B-216) ------------------------------
 *
 * Classic Winamp bars/scope (research doc entry #6, priority rank #2), but
 * with a fluid easing layer sitting on top of the octave cascade's own
 * instant-attack/exponential-release smoothing (spec_lvl[], above), plus a
 * proper falling peak-cap per band -- neither VIZ_LED nor VIZ_BARS has one.
 * Tuned in the Fluid Bars Lab sandbox before being written here.
 *
 * SESSION-ONLY for now (B-216): every field below lives in plain RAM, reset
 * to a built-in preset at boot. No persistence decision has been made --
 * the obvious channel (fw/settings.inc's SW_* register) is a hardwired
 * 4-bit index already fully used with the library on, so adding a slot
 * needs an RTL change, not a firmware one. This is deliberately being
 * tried first without solving that, per the owner's own call. */
/* M2 (docs/METER_MODULE_SPEC.md): the Winamp pair's parameters, presets and live values are generated from meters/winamp_bars and
 * meters/winamp_scope (fw/meters_gen.h); state is per meter (B-234's lesson) and the values are read through MV_<METER>(name).
 * WVIZ_BANDS_MIN/MAX bound the band count (= SPEC_BANDS, the octave cascade's real band count: more would mean interpolating fake data;
 * the manifest range and a _Static_assert below keep them equal). */
#include "meter_module.h"
#include "meter.h"          /* the draw contract, docs/features/meters/METER_MODULE_SPEC.md section 3 */
#include "meters_gen.h"
#define WVIZ_BANDS_MIN 4u
#define WVIZ_BANDS_MAX 16u
_Static_assert(WVIZ_BANDS_MAX == 16u && WVIZ_BANDS_MIN == 4u, "manifest range for bands is 4..16");

/* The module whose parameters the Configure page and Select+X edit for the meter `viz`, or 0. */
static const mtr_data_t *mtr_of(uint32_t viz)
{
    for (uint32_t i = 0; i < MTR_MODULE_N; i++) if (mtr_modules[i]->viz == viz) return mtr_modules[i];
    return 0;
}

/* B-234: forces the next wviz_bars_tick()/wviz_scope_tick() call to repaint
 * everything, bypassing the per-band change cache and re-seeding scope's own
 * smoothing state, and (bars only) clearing the whole preview rect first.
 * Set whenever the drawing CONTEXT changes -- a preset applied, the mode
 * toggled, or the Configure page opened/closed -- since the change-cache and
 * the scope's temporal smoothing both assume a stable position/geometry
 * between calls, which stops being true the moment the caller moves the
 * preview to a different rect (player screen vs. Configure) or the config
 * itself jumps to different values. Without this, a band whose newly-computed
 * height happens to equal what was last drawn (very likely right after a
 * preset switch, when several fields reset toward similar values) silently
 * skips its redraw -- exactly the reported "bars invisible after choosing a
 * preset." Consumed by whichever tick function runs next, since only one of
 * the two ever executes per redraw (mode-gated by the caller). */
static uint8_t wviz_force;

#include "meter_core.h"   /* M1.5: ease, peak cap, band mapping, redraw cache (was inline in wviz_bars_tick) */
static uint8_t  wviz_disp[WVIZ_BANDS_MAX];        /* displayed height, 0..255 */
static int16_t  wviz_vel[WVIZ_BANDS_MAX];         /* spring mode velocity only */
static mtr_peak_t wviz_pk[WVIZ_BANDS_MAX];       /* peak cap state per band: level, gravity fall speed, ms of hold left */
static uint8_t  wviz_drawn[WVIZ_BANDS_MAX], wviz_peak_drawn[WVIZ_BANDS_MAX];
static int16_t  wviz_scope_y[256];          /* smoothed scope trace, signed pixel offset */
static uint8_t  wviz_scope_init;

/* ---- OCTAVE FILTER BANK -----------------------------------------------
 *
 * Eight bands of real frequency content, so the columns move independently and
 * against each other instead of all reporting one loudness.
 *
 * NOT an FFT, and not a bank of parallel band-passes -- both are far too
 * expensive here. Costed against one 26 ms meter window at 44.1 kHz:
 *
 *     FFT, 1024-point                  13.1% CPU
 *     12 parallel biquad bands         31.9%
 *     the same, subsampled 1-in-4       8.0%
 *     OCTAVE cascade, 8 bands           1.5%
 *
 * The cascade is cheap because each stage runs at HALF the rate of the one
 * before it. A one-pole low-pass splits the signal in two: what it rejects is
 * that stage's band, and what it passes is halved in rate and handed down. The
 * whole ladder costs about twice the first stage, not eight times.
 *
 * `lp += (x - lp) >> SPEC_SH` is that filter -- a subtract, a shift, an add.
 * The bands land at roughly 3.5k+, 1.7k, 880, 440, 220, 110, 55 and below,
 * which is octave spacing and what a spectrum display is meant to show.
 *
 * RUN ONLY WHILE THIS METER IS ON SCREEN. That is the whole safety argument:
 * 24-bit FLAC measures ~0% idle CPU, and the roadmap has long flagged a filter
 * bank as the one addition that could bring audio tics back. Gated on
 * viz_mode, the cost exists only while it is being looked at, and switching
 * meters is an instant way out. */
#define SPEC_OCT   8u                      /* octaves the bank covers       */
#define SPEC_BANDS (SPEC_OCT * 2u)         /* each octave split in half     */
_Static_assert(WVIZ_BANDS_MAX == SPEC_BANDS, "WVIZ_BANDS_MAX must track SPEC_BANDS");

static unsigned char spec_lvl[SPEC_BANDS];    /* published, 0..255           */
static uint8_t  wave_hw;                  /* B-283: the bitstream has the level/scope block (probed once at boot) */
static uint8_t  spec_hw;                  /* B-263: the bitstream has the hardware filter bank (probed once at boot) */
static uint8_t  text_mode_hw;             /* theme/gamma: the bitstream has the second text weight table (probed once at boot) */
static uint8_t  hw_poly;                  /* B-292: the bitstream has the MP3 window unit (probed once at boot) */
static uint8_t  hw_lpc;                   /* B-369: the bitstream has the FLAC LPC unit (probed once at boot) */
static uint8_t  hw_cymo;                  /* B-471/B-476: the bitstream has the Cymo resampler (probed once at boot) */
#define CYMO_RESAMP_READY() (hw_cymo != 0u)   /* a plain status-bit read: unlike BLIT_READY()/RRECT_READY(), this address
                                                * range is cleanly unmapped on every older bitstream (docs/MMIO_ALLOCATION.md:
                                                * "0x150-0x1FC free" before this unit existed), so there is no truncation/
                                                * aliasing risk a behavioural probe would need to rule out. */
static uint8_t  dbuf_hw;                  /* Helios/Talos H2 (B-340): the bitstream has double buffering (probed once at boot) */
#define DBUF_READY() (dbuf_hw != 0u)
#if FLAC_PROFILE
static uint32_t clz_cal_cyc;              /* B-342: measured cycles/call of __clzdi2, once at boot */
#endif
static uint32_t spec_win_seen;            /* last hardware window counter consumed */

/* B-263: the hardware bank (src/fpga/core/tau_spec_bank.sv) runs the same cascade continuously, at zero CPU cost, and
 * publishes the mean |band| of every 1024-sample window. Returns 1 and fills m[] when a NEW window is available; 0 when
 * there is nothing new, or the window rolled over while we were reading (then simply try again next frame). */
static int spec_hw_fetch(uint32_t *m)
{
    uint32_t w0 = (REG(R_SPEC_ST) >> 16) & 0xFFFFu;
    if (w0 == spec_win_seen) return 0;
    for (uint32_t b = 0; b < SPEC_BANDS; b++) { REG(R_SPEC_IDX) = b; m[b] = REG(R_SPEC_DATA) & 0xFFFFFu; }
    if (((REG(R_SPEC_ST) >> 16) & 0xFFFFu) != w0) return 0;
    spec_win_seen = w0;
    return 1;
}
/* Last drawn as a ROW COUNT, not as a level.
 *
 * This is what stopped the flicker. Comparing the 0..255 level means something
 * differs on virtually every update -- 255 steps against 12 rows -- so all 96
 * blocks were repainted continuously, overdrawing colours that had not
 * changed. Comparing rows means a band only redraws when it crosses a
 * boundary, and only the rows between the old count and the new get touched:
 * typically one or two rects instead of ninety-six.
 *
 * 0xFF is the sentinel for "the chrome repainted underneath you", as
 * everywhere else here. */
static unsigned char spec_drawn[SPEC_BANDS];

/* Per-band gain, Q4, low band first.
 *
 * MEASURED, not chosen. The first table was set by eye and every value was
 * roughly twenty times too small -- band means come out at 250..1950 in sample
 * units, which those gains turned into a `v` of 4..13 out of 255, so the meter
 * lit its bottom row and nothing else.
 *
 * tools/host/spectrum_harness.c runs this exact cascade over real music
 * decoded by the real fw/flac.c and reports each band's mean magnitude. Two
 * tracks, averaged, with each band's gain set to put that average around 150
 * of 255 -- roughly seven of twelve rows, high enough to see and with room to
 * move in both directions:
 *
 *     band        0    1    2    3    4    5    6    7
 *     mean      412  644  937 1272 1571 1684 1398  894
 *
 * uint16_t, not unsigned char: the top band needs 749 and the old type
 * silently caps at 255, which would have quietly flattened the treble end
 * while looking like a working table. */
/* Level to a logarithmic scale: 16 units per octave, so one unit is about
 * 0.4 dB. A loop rather than a clz builtin -- it runs sixteen times per meter
 * update, not per sample, so the cost is nothing and it cannot surprise the
 * linker. */
static uint32_t spec_log(uint32_t v)
{
    if (!v) return 0;
    uint32_t e = 0u, t = v;
    while (t > 1u) { t >>= 1; e++; }
    uint32_t m = (e >= 4u) ? ((v >> (e - 4u)) & 0xFu) : ((v << (4u - e)) & 0xFu);
    return (e << 4) | m;
}

/* The display window, in spec_log units.
 *
 * 30 dB of range killed the pegging but spent too much of the meter doing it:
 * at 80 units a 6 dB swing moved the bars two rows, so everything sat mid-scale
 * and the meter stopped reacting to the music. 50 units is 19 dB, which is
 * just under four rows per 6 dB -- nearly twice the original movement.
 *
 * What that costs is headroom, and the number is worth writing down: measured
 * across both tracks the loudest band reaches 238, so the top of this window
 * sits 12 units -- about 4.5 dB -- above it, and a master that much hotter will
 * touch the top again. The floor came up WITH the span rather than staying put,
 * which is what preserves that margin: the top is floor+span, so narrowing
 * alone would have spent the headroom instead of the range. On the linear scale it was 1.5 dB, so the pegging that
 * started this is still fixed; there is just no free lunch between the two.
 *
 * Narrow the span for more movement, widen it for more headroom. Those are the
 * only two things this trades. */
#define SPEC_FLOOR 200u
#define SPEC_SPAN   50u

static const uint16_t spec_gain[SPEC_BANDS] = {
    /* MEASURED for the HALF-OCTAVE cascade, low band first. Re-measured rather
     * than carried over: splitting each octave in two changes every level, and
     * the two halves are not equal -- the upper half of an octave carries less
     * than the lower in most music, so a table that reused the octave figures
     * would have leant the whole display one way.
     *
     * Two tracks averaged, each band set to land near 150 of 255. Predicted
     * from those same measurements: the busy track lights 6..11 rows across
     * the sixteen, the sparse one 2..7. */
    1428u, 411u, 788u, 266u, 506u, 228u, 411u, 255u,
    430u, 345u, 521u, 496u, 699u, 764u, 1010u, 1312u
};

/* The ladder's own palette, RGB565. Green low, amber through the middle, red
 * at the top -- fixed rather than accent-derived, because on this meter the
 * colour is information. */

/* Last drawn, so a still passage costs nothing. 0xFF is the sentinel every
 * other meter here uses for "the chrome repainted underneath you". */

static unsigned char wave[UI_WAVE_N], wave_drawn[UI_WAVE_N];
static unsigned char wave_pk[UI_WAVE_N], wave_pk_drawn[UI_WAVE_N];

/* Art panel: art_x is where it currently sits, animated toward its target.
 * FB_W means fully off the right edge. */
/* Shown by default; SELECT hides it. art_x is where the panel currently sits,
 * and FB_W means fully off the right edge. */
static uint32_t art_x = ART_X, art_shown = 1, art_ready, ui_text_w;
/* art_shown is what is on screen; art_pref is what the USER asked for.
 * Keeping them apart is what lets the choice survive a track change: a
 * track with no artwork hides the panel without forgetting that the panel
 * is wanted, so the next track that has some brings it back. */
static uint8_t  art_pref = 1, art_have;
static uint32_t art_file_id;      /* 0190 identity the stash was decoded for */
static uint32_t art_sig;          /* fingerprint of the IMAGE the stash holds  */
/* An image already known to be undecodable. Without this, a cover the core
 * cannot read is re-attempted on every track of the album -- and, worse, the
 * message explaining why appears on every one of them. */
static uint32_t art_bad_sig;
static uint8_t  art_bad;          /* a cover IS present and cannot be decoded */
static uint8_t  art_bad_code;     /* which picojpeg complaint, for the label  */

/* The panel is shown for a cover that exists but cannot be read, as well as
 * for one that decoded. Those two are worth telling apart and were not:
 * has_art went to 0 either way, art_shown followed it, and the panel vanished
 * entirely -- so a file WITH artwork the core cannot read looked exactly like
 * a file with none. That is why "no art, and no message either" was the
 * report: there was no frame to put a message in. */
#define ART_PANEL_WANTED (art_have || art_bad)
/* A track is loading on the player screen: the frame is drawn without the old track's text (ui_loading, during the
 * repaint) and the album art plate shows an animated loader with a caption (ui_loader_on, until the load ends). */
static uint8_t ui_loading, ui_loader_on, ui_loader_txt;
static uint32_t art_toggle;   /* rolling amplitude history, 0..UI_WAVE_H */
static int      ui_underrun_shown;

/* Linear blend of two RGB565s, t in 0..UI_BANDS. Per channel so the ramp keeps
 * its hue instead of sliding through grey. */
static uint16_t ui_mix(uint16_t a, uint16_t b, uint32_t t, uint32_t n)
{
    uint32_t r = (((a >> 11) & 0x1Fu) * (n - t) + ((b >> 11) & 0x1Fu) * t) / n;
    uint32_t g = (((a >> 5)  & 0x3Fu) * (n - t) + ((b >> 5)  & 0x3Fu) * t) / n;
    uint32_t bl = ((a & 0x1Fu) * (n - t) + (b & 0x1Fu) * t) / n;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

/* Top of the background ramp, tinted toward the user's accent. Recomputed
 * whenever the accent changes; the bottom stays black. */
static uint16_t ui_grad_top_c = UI_GRAD_TOP;

/* Luma the tinted top is normalised to. The ramp used to be a fixed neutral
 * graphite at luma 35, and at that brightness RGB565 has almost no room for
 * hue: measured over the twelve accents, CREAM quantises to exactly the old
 * neutral and several others land within one level of it, so a tint would have
 * been invisible on half the palette. 45 buys enough levels to tell them apart
 * while keeping the background far below the white type it sits under. */
#define UI_GRAD_LUMA th_bg_luma   /* per theme and polarity (fw/theme.h); 45 in the built-in dark themes */

/* Accent -> a dark tinted ramp top. Normalising to a FIXED luma rather than
 * scaling the accent directly is what keeps this safe: every colour lands at
 * the same brightness, so the contrast the text was tuned against does not move
 * when the user cycles the palette, and no accent can produce a background that
 * competes with the type. Then pulled halfway to neutral, because a full
 * saturation cast is what made an earlier coloured ramp read as murky behind
 * the card -- this should say "tinted", not "coloured". */
/* Set where ui_grad_set() can reach it; the stash itself is built further
 * down, once UI_WAVE_* are in scope. */
/* B-450: root cause of the Winamp Scope trail "accumulation" bug, found by reading mp3_fb.sv directly
 * after PIXHIST (B-449) showed a stuck, non-decaying pixel with real contrast against the expected
 * background -- decisive evidence the blend's DESTINATION write was landing somewhere never displayed,
 * not that the blend arithmetic itself was wrong (already proven correct by the boot self-test and the
 * live JTAG ALPHA reads, B-440/B-441/B-444). mp3_fb.sv's own module-header comment (line ~184-193,
 * unchanged, not a bug in the RTL) documents the real contract: H2's automatic per-buffer offsetting
 * (`dbuf_addr()`, keyed on `dbuf_cpu_buf`) applies ONLY to ordinary RECT/CHAR/COPY dispatch; every
 * true BLIT-mode opcode (BLIT/BAR/SBLIT/CBLIT/RRECT) is addressed purely through the firmware-set
 * sticky SRC_BASE/DST_BASE fields and is untouched by `dbuf_cpu_buf` -- "a caller wanting one of them
 * to target the back buffer sets blt_dst_base itself" (docs/features/HELIOS_SPEC.md section 5). This
 * is the EXACT bug class B-414 already found and fixed once for Chladni (fw/chladni.inc): a BLIT-mode
 * caller that never calls fb_set_bases() always lands on buffer 0, regardless of which buffer
 * `dbuf_redraw_begin()`/`dbuf_redraw_end()` (B-397) has since made the real CPU target via R_DBUF_CPU
 * -- exactly what happens on every Settings/fullscreen/Configure open-close, the owner's own reported
 * trigger. `wviz_scope_tick()`'s trail fade (`ui_bg_blend()` below) uses `fb_blit()`, a true BLIT-mode
 * opcode, and never called `fb_set_bases()` at all: it always faded buffer 0 while the RECT-class
 * trace bars (`fb_bar()`, automatically `dbuf_cpu_buf`-aware) correctly followed R_DBUF_CPU onto
 * whichever buffer was actually displayed -- new segments keep landing on the real screen, the fade
 * keeps updating a buffer nothing shows, exactly "accumulates instead of fading," and every earlier
 * per-register check (STRIP/BASES/ALPHA/DBUF, B-433/434/439/440) reads correct in isolation because
 * none of them asks whether a BLIT-mode write's TARGET buffer matches the DISPLAYED one.
 *
 * One further wrinkle this fix has to account for: `ui_bg_ready`'s lazy-built gradient strip (columns
 * UI_BG_X..UI_BG_X+UI_BG_W, rows well inside V_ACT=360) is itself built via `fb_rect()` -- RECT-class,
 * so it is ALREADY `dbuf_cpu_buf`-aware in hardware and, because its rows sit below DBUF_VISIBLE_WORDS,
 * genuinely gets a SEPARATE physical copy per H2 buffer (unlike Chladni's own CHL_PLANE_Y scratch,
 * which the module header confirms lives above V_ACT and is deliberately buffer-independent -- not
 * the same situation, do not assume both stash regions behave alike). A strip built once for buffer 0
 * is therefore just plain STALE/uninitialised content when later read back from buffer 1's copy -- a
 * one-shot `ui_bg_ready` flag with no buffer memory cannot express "stale because the ACTIVE buffer
 * changed," only "stale because the gradient changed" (`ui_grad_set()`). `ui_bg_buf` below fixes that:
 * whichever buffer the strip was last built for, invalidated (forcing a fresh `fb_rect()` build into
 * whichever buffer is now current) the moment `R_DBUF_CPU` disagrees with it. */
static uint8_t ui_bg_ready;
static uint8_t ui_bg_buf;      /* which H2 buffer (0/1) ui_bg_ready's strip actually reflects */
/* B-413: live counters for the still-unexplained Winamp Scope trail "accumulation" report -- see the
 * comment at their increment site (wviz_scope_tick()) for what each one means. */
static uint32_t dbg_scope_blend_ok, dbg_scope_blend_fail;

static void ui_grad_set(uint16_t accent)
{
    ui_bg_ready = 0;            /* the stashed background is now stale */
    uint32_t r = ((accent >> 11) & 0x1Fu) * 255u / 31u;
    uint32_t g = ((accent >> 5)  & 0x3Fu) * 255u / 63u;
    uint32_t b = (accent & 0x1Fu) * 255u / 31u;

    uint32_t l = (2126u * r + 7152u * g + 722u * b) / 10000u;
    if (!l) l = 1u;
    /* Rounded, not truncated. Flooring here cost up to a level of green on half
     * the palette, which at this brightness is a visible fraction of the whole
     * tint -- and made the built colours differ from the ones that were
     * reviewed in tools/gradient_preview.py. */
    r = (r * UI_GRAD_LUMA + l / 2u) / l;
    g = (g * UI_GRAD_LUMA + l / 2u) / l;
    b = (b * UI_GRAD_LUMA + l / 2u) / l;
    r = (r + UI_GRAD_LUMA + 1u) / 2u;     /* half-saturated */
    g = (g + UI_GRAD_LUMA + 1u) / 2u;
    b = (b + UI_GRAD_LUMA + 1u) / 2u;
    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;

    ui_grad_top_c = (uint16_t)(((r * 31u + 127u) / 255u) << 11 |
                               ((g * 63u + 127u) / 255u) << 5  |
                               ((b * 31u + 127u) / 255u));
}

/* THE colour of the background at row y. Every consumer must come through here
 * -- the ramp is dithered, so anything that reconstructs a background colour
 * by its own arithmetic will disagree with what was actually drawn and leave a
 * patch. There were nine such call sites before this existed.
 *
 * Why dither at all: 40 bands between the old top and black collapse to
 * THIRTEEN distinct RGB565 colours, each holding for ~28 rows -- ~111 panel
 * rows once the Pocket's 4x integer scale is applied, which is the visible
 * banding. Drawing more bands changes nothing, because the colour space has no
 * values in between. Alternating adjacent levels row to row lands the eye on
 * intermediate colours RGB565 cannot name. The cost is fine horizontal texture
 * at one framebuffer row -- 4 panel rows, ~2 arcminutes at arm's length against
 * a band's ~110 -- so it should fuse where a band edge cannot.
 *
 * Text is the one place this is imperfect: CHAR paints its own background in a
 * single colour, so a glyph cell gets one row's colour for all 16 of its rows.
 * The mismatch is at most one level, which is the same error the old band
 * boundaries already made when a glyph straddled one. */
static uint16_t ui_grad_at(uint32_t y)
{
    static const uint8_t thr[4] = { 1u, 5u, 3u, 7u };   /* eighths, ordered */
    if (y >= FB_H) y = FB_H - 1u;
    uint32_t den = FB_H - 1u;
    uint32_t rem = den - y;                 /* bottom is black: ideal = top*rem/den */
    uint32_t t   = thr[y & 3u];
    uint32_t lv[3] = { (uint32_t)((ui_grad_top_c >> 11) & 0x1Fu),
                       (uint32_t)((ui_grad_top_c >> 5)  & 0x3Fu),
                       (uint32_t)(ui_grad_top_c & 0x1Fu) };
    /* Bottom colour from the theme (black in the dark themes, which reduces to the original ramp exactly). The ramp runs from
     * the tinted top to the bottom; a light theme has a top DARKER than its bottom, so both directions are handled. */
    const uint16_t bc = th_role[TR_BG_BOTTOM];
    const uint32_t bt[3] = { (uint32_t)((bc >> 11) & 0x1Fu), (uint32_t)((bc >> 5) & 0x3Fu), (uint32_t)(bc & 0x1Fu) };
    for (uint32_t k = 0; k < 3u; k++) {
        if (lv[k] >= bt[k]) {
            uint32_t num  = (lv[k] - bt[k]) * rem;
            uint32_t base = num / den;
            if ((num - base * den) * 8u > t * den) base++;
            lv[k] = bt[k] + base;
        } else {
            uint32_t num  = (bt[k] - lv[k]) * y;
            uint32_t base = num / den;
            if ((num - base * den) * 8u > t * den) base++;
            lv[k] += base;
        }
    }
    return (uint16_t)((lv[0] << 11) | (lv[1] << 5) | lv[2]);
}

/* Theme step 0b (docs/THEME_SPEC.md). The accent is the user's palette pick; in the Light polarity a bright pick would vanish on a
 * light ramp, so it is scaled down to TH_LIGHT_ACC_MAX_L (hue kept). tools/gen_themes.py mirrors this to check contrast. */
static uint16_t th_accent_of(uint32_t idx)
{
    uint16_t a = ui_palette[idx];
    if (!th_pol) return a;
    uint32_t r = ((a >> 11) & 0x1Fu) * 255u / 31u, g = ((a >> 5) & 0x3Fu) * 255u / 63u, b = (a & 0x1Fu) * 255u / 31u;
    uint32_t l = (2126u * r + 7152u * g + 722u * b) / 10000u;
    if (l > TH_LIGHT_ACC_MAX_L) {
        r = r * TH_LIGHT_ACC_MAX_L / l; g = g * TH_LIGHT_ACC_MAX_L / l; b = b * TH_LIGHT_ACC_MAX_L / l;
        a = (uint16_t)((((r * 31u + 127u) / 255u) << 11) | (((g * 63u + 127u) / 255u) << 5) | ((b * 31u + 127u) / 255u));
    }
    return a;
}

/* Load the selected theme and polarity into the role table, then re-derive everything that depends on it: the accent, the tinted
 * background ramp (its luma is per theme) and, through ui_accent_changed, a full repaint of the player screen. Bg/top, accent
 * and accent-2 are not table entries (derived / user pick). */
static void th_apply(void)
{
    const th_theme_t *t = th_get(th_theme < TH_COUNT() ? th_theme : 0u);
    const uint32_t p = th_pol ? 1u : 0u;
    for (uint32_t i = 0; i < TR_COUNT; i++)
        if (i != TR_BG_TOP && i != TR_ACCENT && i != TR_ACCENT2) th_role[i] = t->role[p][i];
    th_bg_luma = t->bg_luma[p];
    if (text_mode_hw) REG(R_TEXT_MODE) = p;        /* dark text on a light ramp needs its own edge weights (tools/gen_text_gamma.py) */
    ui_accent = th_accent_of(ui_pal_idx);
    ui_grad_set(ui_accent);
    ui_accent_changed = 1u;
}

/* Relocated here (2026-09-25, B-197 meter/blit integration; moved earlier again the same day,
 * Helios/Talos H1) -- BLIT_READY()/blit_probe_ensure() were needed by ui_draw_dynamic()'s plain-
 * bars fast path further down; RRECT_READY()/rrect_probe_ensure() (B11) are needed here instead,
 * by fb_round_rect_on() immediately below, which is defined well before that fast path. All of
 * blit_probe.inc's own dependencies (REG(), cycles(), fb_wait(), FB_STRIDE, the R_FB_ and R_SDR_
 * registers) are already in scope well before this point either way -- confirmed by reading each
 * one's own definition line, not assumed. */
#include "blit_probe.inc"

/* B11 corner-cut LUT (fw/rc_lut.h, host-tested in sim/test_rc_lut.py): mp3_soc.v's rc_cut_lut_r
 * resets to all-zero and nothing wrote it before this, so every hardware OP_RRECT drew with cut=0
 * at every row -- a plain square, sharp corners, no rounding at all (owner-reported: "weird bars
 * around radio buttons" on the METER selection list, whose ring icons are fb_round_rect_on() calls
 * with real radii 4/6/9 -- confirmed from a real Pocket screenshot, not guessed). Cached by radius
 * so a page that only ever draws one radius (the common case: SET_MENU_ROW_H's selected-row highlight
 * is always radius 5) pays the 16-entry MMIO load once, not per row. */
#include "rc_lut.h"
static uint8_t rc_lut_cache_radius = RC_LUT_RADIUS_NONE;
static void rc_lut_ensure(uint32_t r)
{
    uint8_t out[RC_LUT_N];
    if (!rc_lut_prepare(r, &rc_lut_cache_radius, out)) return;
    for (uint32_t i = 0; i < RC_LUT_N; i++) {
        REG(R_RC_IDX)  = i;
        REG(R_RC_DATA) = out[i];
    }
}

/* Helios's display-list core (docs/HELIOS_SPEC.md section 4/9) -- inert until a real region is
 * converted to it, see fw/helios.inc's own header. Only depends on REG()/R_VBLANK, both already in
 * scope well before this point. */
#include "helios.inc"

/* Rounded rect. The engine has no corner primitive, so the corners are cut
 * back out afterwards with the GRADIENT's local colour at each row -- a flat
 * background colour would leave four visible notches against the ramp. r rows
 * of 2 small rects each; trivial next to a full-screen fill. */
static void fb_round_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint32_t r, uint16_t color)
{
    fb_rect(x, y, w, h, color);
    for (uint32_t i = 0; i < r; i++) {
        /* Quarter-circle by integer search: for this row's distance from the
         * corner centre, find the largest x still inside radius r. */
        uint32_t dy = r - i;
        uint32_t inner = 0;
        while ((inner + 1u) * (inner + 1u) + dy * dy <= r * r) inner++;
        uint32_t cut = r - inner;
        if (!cut) continue;
        uint16_t top = ui_grad_at(y + i);
        uint16_t bot = ui_grad_at(y + h - 1u - i);
        fb_rect(x, y + i, cut, 1, top);
        fb_rect(x + w - cut, y + i, cut, 1, top);
        fb_rect(x, y + h - 1u - i, cut, 1, bot);
        fb_rect(x + w - cut, y + h - 1u - i, cut, 1, bot);
    }
}

/* Same shape, but with the corners cut to a colour the caller names.
 *
 * fb_round_rect above cuts to ui_grad_at(), which is right for anything
 * sitting directly on the background and wrong for anything sitting on a
 * PANEL -- the corners would be four holes showing the gradient through it.
 * The playlist overlay is the case: a rounded row inside a rounded panel.
 *
 * Helios/Talos H1 (docs/HELIOS_SPEC.md section 10, B11): this is the ONE of
 * the two round-rect functions that can move to hardware as-is -- its `bg`
 * is a single flat colour for every corner, exactly what OP_RRECT's own
 * `rrect_bg` register holds. `fb_round_rect()` above CANNOT convert the same
 * way: its corner cuts read a DIFFERENT gradient colour per row
 * (`ui_grad_at(y+i)`), and OP_RRECT has no per-row background source (one
 * sticky register, not a lookup) -- B13's proposed gradient-bar CLUT read
 * would be the RTL that could someday close this gap for `OP_BAR`, but
 * nothing analogous exists for RRECT yet, so `fb_round_rect()` stays
 * software-only here, correctly, not by oversight. This function alone is
 * still the high-value case: it is what draws every selected row in every
 * list/menu/playlist/settings page (`fw/settingsui.inc`'s own list-row
 * painters), the single most-executed draw pattern this session's own
 * investigation found (section 2/15.2). */
static void fb_round_rect_on(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                             uint32_t r, uint16_t color, uint16_t bg)
{
    rrect_probe_ensure();   /* B-162's own "lazy, on first actual need, never at boot" convention */
    if (RRECT_READY()) {
        /* fb_wait() here, BEFORE the LUT is touched, not just inside fb_rrect() below: a prior
         * OP_RRECT at a DIFFERENT radius may still be mid-draw (fb_rrect() fires-and-returns, it
         * only waits for what came before IT), reading rc_cut_lut_r row by row as it goes. Two or
         * three fb_round_rect_on() calls at different radii back to back (set_disc()'s radio-button
         * ring/dot sequence, radii 9/6/4 or 10/7) were rewriting that shared LUT out from under the
         * still-executing earlier command -- owner-reported "radio button rounded issue" after the
         * B11 corner-cut fix, root-caused here, not a LUT-content bug (rc_lut_cut() itself matches
         * the proven software formula exactly, sim/test_rc_lut.py already covers it). */
        fb_wait();
        if (r) rc_lut_ensure(r);   /* fb_rrect()'s own comment: r=0 never arms rrect_pending, so the LUT is never read for it -- skip the load */
        fb_rrect(x, y, w, h, r, color, bg);
        return;
    }
    fb_rect(x, y, w, h, color);
    for (uint32_t i = 0; i < r; i++) {
        uint32_t dy = r - i;
        uint32_t inner = 0;
        while ((inner + 1u) * (inner + 1u) + dy * dy <= r * r) inner++;
        uint32_t cut = r - inner;
        if (!cut) continue;
        fb_rect(x, y + i, cut, 1, bg);
        fb_rect(x + w - cut, y + i, cut, 1, bg);
        fb_rect(x, y + h - 1u - i, cut, 1, bg);
        fb_rect(x + w - cut, y + h - 1u - i, cut, 1, bg);
    }
}

/* Now-playing UI pass 1: the art panel moved to its own static row above the
 * meter (UI_TEXT_X/UI_NP_TITLE_Y), so it no longer shares columns with the
 * waveform -- full width always, art shown or not. */
static uint32_t ui_wave_w(void)
{
    return UI_INNER_W;
}

/* A copy of the meter box's BACKGROUND, parked in the columns the scanout
 * never reads: the framebuffer's stride is 512 and only 400 is displayed, so
 * x 400..511 exists on every row and is invisible.
 *
 * Every meter erases part of the box before redrawing its content, and they
 * all did it with `bed` -- the gradient sampled once at the box's top row.
 * That is a flat slab on a ramp that falls to 62% of that value by the bottom
 * of the box, which is what showed behind the magic eye and the VU needles.
 *
 * Per-row erases would be correct and unaffordable: the mirrored bars and the
 * peak dots erase a full-height column PER BAR, 36 times a frame, so 72 rows
 * each is 2592 commands. Copying from a prepared strip is ONE command per
 * erase -- exactly what they cost today.
 *
 * Built lazily and dropped whenever the gradient changes. */
#define UI_BG_X  FB_W                  /* first off-screen column */
#define UI_BG_W  (FB_STRIDE - FB_W)    /* 112 px of invisible stride */

/* B-411 (owner-reported: Winamp Scope's trail stops fading -- "accumulates" -- specifically after
 * returning from a use_gradient=0 context, fullscreen or the Configure preview, and stays broken
 * every time until the next such transition): unlike every other draw primitive in this codebase
 * (fb_rect/fb_bar/fb_blit, and this function's own sibling ui_bg_blend() a few lines below), this had
 * no FB_HELD() guard of its own. If it ran during the brief window right as an overlay was closing --
 * exactly when this is called as ui_bg_blend()'s fallback after ui_bg_blend() itself failed because
 * FB_HELD() was still transiently true -- its fb_rect() calls silently no-op'd (each checks FB_HELD()
 * internally), leaving the off-screen gradient strip's memory untouched, but `ui_bg_ready` still got
 * set to 1 unconditionally: every later blend then faded toward whatever stale/garbage content was
 * actually sitting there instead of the real gradient, until the next transition reset the flag and
 * the same race could recur. */
/* B-450: which H2 buffer (0/1) R_DBUF_CPU currently targets, 0 on any bitstream without H2 -- the
 * SAME test `dbuf_addr()` performs in hardware for every RECT-class opcode, so both the strip build
 * below (fb_rect, already dbuf_cpu_buf-aware in RTL) and this function's callers agree on which
 * buffer is "current" without a second source of truth. */
static uint32_t ui_bg_cur_buf(void)
{
    return (DBUF_READY() && (REG(R_DBUF_CPU) & 1u)) ? 1u : 0u;
}

static void ui_bg_restore(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (!w || !h || FB_HELD()) return;
    const uint32_t buf = ui_bg_cur_buf();
    if (!ui_bg_ready || ui_bg_buf != buf) {   /* B-450: also rebuild if the ACTIVE H2 buffer changed */
        for (uint32_t yy = UI_WAVE_Y - UI_WAVE_TOP; yy < UI_WAVE_Y + UI_WAVE_H; yy++)
            fb_rect(UI_BG_X, yy, UI_BG_W, 1, ui_grad_at(yy));
        ui_bg_ready = 1;
        ui_bg_buf = (uint8_t)buf;
    }
    /* Source and destination share rows, so the ramp lines up by
     * construction and the copy is purely horizontal. fb_copy() is OP_COPY -- RECT-adjacent, already
     * dbuf_cpu_buf-aware in hardware (mp3_fb.sv p0_addr dispatch), so unlike fb_blit() below it needs
     * no explicit sticky-base handling here at all. */
    while (w) {
        uint32_t n = (w < UI_BG_W) ? w : UI_BG_W;
        fb_copy(UI_BG_X, y, x, y, n, h);
        x += n; w -= n;
    }
}

/* B-334: fade what is in a rectangle of the player's meter area toward the background: the background strip is blended over it with weight
 * `bg_alpha` (0..255 of the strip), so the old picture keeps (256 - bg_alpha)/256 of itself. Needs the blend bitstream (BLEND_READY());
 * returns 0 without drawing anything when it is not there (or an overlay holds the engine), and the caller erases the old way. */
COLD_FN3 static int ui_bg_blend(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t bg_alpha)
{
    if (!w || !h || FB_HELD()) return 0;
    blend_ensure();
    if (!BLEND_READY()) return 0;
    const uint32_t buf = ui_bg_cur_buf();
    if (!ui_bg_ready || ui_bg_buf != buf) {                  /* the same lazy strip build as ui_bg_restore() */
        for (uint32_t yy = UI_WAVE_Y - UI_WAVE_TOP; yy < UI_WAVE_Y + UI_WAVE_H; yy++)
            fb_rect(UI_BG_X, yy, UI_BG_W, 1, ui_grad_at(yy));
        ui_bg_ready = 1;
        ui_bg_buf = (uint8_t)buf;
    }
    /* B-450: fb_blit() is a true BLIT-mode opcode (mp3_fb.sv: BLIT/BAR/SBLIT/CBLIT/RRECT), addressed
     * ONLY through the firmware-programmable sticky SRC_BASE/DST_BASE fields -- it does NOT follow
     * R_DBUF_CPU automatically the way fb_rect()/fb_copy() do. Without this, the fade always targeted
     * buffer 0 regardless of which buffer was actually the CPU's current target (and displayed), while
     * the RECT-class trace bars correctly followed it -- new content landing on screen, the fade
     * updating a buffer nothing shows. Same bug class, same fix pattern as Chladni's own H2 tracking
     * (fw/chladni.inc, B-414): point both SRC_BASE and DST_BASE at the buffer this call already
     * determined the strip lives in (source = the strip's own per-buffer copy, destination = the
     * visible meter area, both the SAME currently-active buffer), then restore to (0,0) once the
     * blit has genuinely executed (fb_fence(), not merely queued -- B-412's own lesson) so no later,
     * unrelated BLIT-mode caller silently inherits a nonzero base. */
    const uint32_t base = buf ? DBUF_BASE1_W : 0u;
    fb_set_bases(base, base);
    fb_blend_on(bg_alpha > 255u ? 255u : bg_alpha);
    while (w) {
        uint32_t n = (w < UI_BG_W) ? w : UI_BG_W;
        fb_blit(UI_BG_X, y, x, y, n, h);
        x += n; w -= n;
    }
    fb_fence();
    fb_blend_off();
    fb_set_bases(0u, 0u);
    return 1;
}

/* B-433: Option A of the B-413 investigation. dbg_scope_blend_ok/fail (wviz_scope_tick()) already
 * proved the blend mechanism runs to completion and reports success every single time -- this reads
 * back the gradient strip's ACTUAL SDRAM pixel content (via the same mailbox primitive blend_probe()
 * uses) and compares it against what ui_grad_at() computes fresh for those same rows RIGHT NOW. Every
 * word in a given row should hold the same colour twice (both mailbox halves), since ui_bg_ready's
 * lazy build paints one uniform colour per row via a single fb_rect() call -- so a genuine mismatch
 * here is real corruption, not a sampling artefact. Three rows sampled (top/middle/bottom of the
 * strip's range) rather than one, since a corruption mechanism might only hit part of the strip.
 * Result: if this reads BAD, the strip itself holds wrong data despite ui_bg_ready claiming otherwise
 * (points back at a firmware-side write bug, worth another source pass); if it reads OK while the
 * trail still visibly doesn't fade, the strip is genuinely correct and the bug is in the blend
 * hardware's own datapath -- the next real step is a live JTAG read of the blend pipeline itself
 * (bl_fg/bl_bg/bl_r/blt_blend_alpha), not more firmware source-reading.
 * B-434: owner confirmed STRIP always reads OK. Before going to JTAG, also snapshot dbg_base_src/dst
 * (fb_set_bases()'s own shadow of the sticky SRC_BASE/DST_BASE fields, which are write-only in
 * hardware and can't otherwise be read back) right here -- if either is nonzero at the exact moment
 * this blend fires, the trail blit and ui_bg_blend()'s destination pre-read are silently targeting the
 * wrong SDRAM region (a real firmware bug, the same class B-410/B-414 both found), which would read
 * exactly like "the trail never fades" while the strip itself stays provably correct. */
static uint8_t  dbg_strip_bad;
static uint16_t dbg_strip_actual[3], dbg_strip_expect[3];
static uint32_t dbg_strip_base_src, dbg_strip_base_dst;
/* B-439: STRIP/BASES both check out fine even while the owner reproduces the accumulation live (Info
 * screenshot: SCOPE BG READY 173 OK 0 FAIL, STRIP OK, BASES both 0) -- every theory reachable from
 * ui_bg_blend()'s own inputs is now exhausted. Reconsidering from the H2/Helios addressing split
 * (docs/MMIO_ALLOCATION.md's DBUF_CPU row, corrected this session): RECT-class opcodes (fb_rect/
 * fb_bar/fb_copy -- what builds the strip AND draws the trace bars) automatically follow R_DBUF_CPU's
 * current buffer selection; true BLIT-class opcodes (fb_blit -- what ui_bg_blend() actually uses to
 * paint the fade) do NOT, and rely entirely on the sticky SRC_BASE/DST_BASE fields already confirmed
 * zero. If R_DBUF_CPU (the buffer new CPU draws target) and R_DBUF_DISP (the buffer actually shown)
 * ever disagree at the moment this blend fires, the trace bars (RECT-class) keep landing on the
 * correct, currently-displayed buffer -- explaining why new segments keep appearing -- while the
 * blend (BLIT-class, base 0 always) keeps updating the OTHER, undisplayed buffer, having zero visible
 * effect: exactly "new draws pile up, nothing ever visibly erases." Both registers are ordinary
 * readable MMIO (unlike the write-only sticky blit fields), so this needs no JTAG. */
static uint8_t dbg_dbuf_cpu, dbg_dbuf_disp;
/* B-440: dbg_blend_alpha (fw/blit_probe.inc) is fb_blend_on()'s own shadow of the last alpha value
 * WRITTEN to the sticky BLEND field -- snapshotted here so it reads alongside everything else this
 * investigation already checks. If this shows a plausible value derived from the configured trail %
 * (matching `(100 - trail) * 256 / 100` from wviz_scope_tick()'s own call) while the trail still
 * visibly never fades, firmware's write is correct and the bug is in hardware actually applying it;
 * if it reads something else (stuck at 0, or not matching what the trail setting implies), the bug is
 * in firmware's own alpha computation/write, not the blend datapath at all. */
static uint32_t dbg_strip_alpha;

/* B-444: JTAG polling (B-443) proved a genuine methodology dead end -- every ALPHA/register-level check
 * (execution success, strip content, bases, buffer selection, and now the alpha register itself) reads
 * correct, yet the visual symptom persists. Worked out why opportunistic sampling can never resolve
 * this further: `blend_ch()`'s 8-bit-truncated `>>8` approximation means a source/destination pair
 * differing by exactly 1 LSB in a channel can NEVER show any blend effect for ANY alpha 0-255 (the
 * arithmetic proves it: with f=b+1, dsp=b*256+alpha, which never reaches (b+1)*256 for alpha<256) --
 * and a genuinely fading pixel converges to within 1 LSB of the background in about 4-5 frames at
 * alpha=153 (40% trail). JTAG polling is far slower than that, so every live sample was always going
 * to land on an already-converged, blend-indistinguishable pixel regardless of whether the hardware is
 * healthy. This logs the ACTUAL framebuffer content of one fixed on-screen pixel (the scope box's own
 * top-left corner, `in->x`/`in->y` -- touched by every erase/blend call, only occasionally by the
 * trace's own accent bars when amplitude reaches that high) across consecutive `wviz_scope_tick()`
 * calls into a small ring buffer, entirely through the existing `blend_mb_read()` mailbox (no JTAG, no
 * new RTL) -- a real frame-by-frame decay trace instead of a single, badly-timed snapshot.
 * `dbg_strip_check()`'s own "whichever half matches" trick only works for a known-constant value (the
 * static gradient strip) -- it breaks down here, since a genuinely decaying value spends most of its
 * life NOT matching either "the old bright colour" or "the current background" exactly. (x, y) stays
 * fixed across every logged call, so whichever mailbox half is this pixel's own stays the SAME half on
 * every read (address parity never changes) -- storing the raw word and showing both halves lets the
 * real decaying half be told apart from its neighbour's unrelated content by eye by seeing which one is
 * actually trending, without needing to guess up front. */
#define DBG_PIXHIST_N 3u   /* B-445: 4 overflowed set_draw_ro()'s fixed char v[40] row buffer (10 chars/entry
                            * + "Wxxxx" = 45 bytes into 40, a real stack overflow -- crashed hardware the
                            * instant the row scrolled into view). 3 entries + the suffix fits with margin,
                            * matching STRIP's own 3-item convention. */
static uint32_t dbg_pixhist[DBG_PIXHIST_N];
static uint8_t  dbg_pixhist_pos;
static uint16_t dbg_pixhist_want;   /* what ui_grad_at() currently expects for that row, for comparison */

static void dbg_pixel_log(uint32_t x, uint32_t y)
{
    /* B-450: this exact non-decaying PIXHIST reading was the decisive evidence that the fade's write
     * was landing on the wrong H2 buffer -- keep reading whichever buffer is now the real CPU target
     * (ui_bg_cur_buf()), so a future read after this fix stays meaningful instead of silently going
     * back to reading a buffer nothing displays. */
    uint32_t addr = ui_bg_cur_buf() * DBUF_BASE1_W + y * FB_STRIDE + x, r = 0xFFFFFFFFu;
    blend_mb_read(addr, &r);   /* leaves r as the sentinel on failure -- visible as FFFF/FFFF, not a silent gap */
    dbg_pixhist_want = ui_grad_at(y);
    dbg_pixhist[dbg_pixhist_pos] = r;
    dbg_pixhist_pos = (uint8_t)((dbg_pixhist_pos + 1u) % DBG_PIXHIST_N);
}

static void dbg_strip_check(void)
{
    const uint32_t rows[3] = { UI_WAVE_Y - UI_WAVE_TOP, UI_WAVE_Y + UI_WAVE_H / 2u, UI_WAVE_Y + UI_WAVE_H - 1u };
    dbg_strip_bad = 0;
    dbg_strip_base_src = dbg_base_src; dbg_strip_base_dst = dbg_base_dst;
    dbg_dbuf_cpu = (uint8_t)(REG(R_DBUF_CPU) & 1u);
    dbg_dbuf_disp = (uint8_t)(REG(R_DBUF_DISP) & 1u);
    dbg_strip_alpha = dbg_blend_alpha;
    /* B-450: the strip now legitimately has a separate physical copy per H2 buffer (ui_bg_buf); read
     * back whichever one ui_bg_blend()/ui_bg_restore() actually built most recently, or this check
     * would report a false BAD/mismatch (reading buffer 0's now-stale copy) the instant the active
     * buffer is 1, even though the fix above is working correctly. */
    const uint32_t strip_base = ui_bg_buf ? DBUF_BASE1_W : 0u;
    for (uint32_t k = 0; k < 3u; k++) {
        uint32_t yy = rows[k];
        uint16_t want = ui_grad_at(yy);
        uint32_t addr = strip_base + yy * FB_STRIDE + UI_BG_X;
        uint32_t r = 0;
        dbg_strip_expect[k] = want;
        if (blend_mb_read(addr, &r)) { dbg_strip_bad = 1u; dbg_strip_actual[k] = 0xFFFFu; continue; }
        uint16_t lo = (uint16_t)(r & 0xFFFFu), hi = (uint16_t)(r >> 16);
        dbg_strip_actual[k] = (lo == want) ? lo : hi;    /* prefer whichever half matches, for display */
        if (lo != want && hi != want) dbg_strip_bad = 1u;
    }
}

/* Repaint the strip the art travels through, so a slide leaves the background
 * behind it rather than a smear. Only the bands crossing the panel's rows. */
static void ui_art_bg_range(uint32_t x, uint32_t w)
{
    if (!w || x >= FB_W) return;
    if (x + w > FB_W) w = FB_W - x;
    for (uint32_t y = ART_Y; y < ART_Y + ART_H && y < FB_H; y++)
        fb_rect(x, y, w, 1, ui_grad_at(y));
}

/* Every meter that CACHES part of itself on screen has to be listed here, and
 * anything that erases the meter band must call this.
 *
 * Twice now a cached face has been wiped and never come back while the moving
 * part carried on repainting itself: the VU's arc when the album art slid over
 * it, and the magic eye's envelope on a track change. Both were one missing
 * assignment at a site that already cleared the OTHER meter. A list of one
 * invites that; a list with a name is at least the place to look. */
static void ui_meter_faces_invalidate(void)
{
    vu_face   = 0;
}

/* Blit the stash to the current position, clipped at the right edge. The panel
 * enters from the right, so only its leftmost columns are on screen at first --
 * and an unclipped copy would run past column 400 into the memory the NEXT
 * scanline displays, i.e. corruption elsewhere rather than a clean cut. */
static void ui_art_draw(void)
{
    /* The panel overlaps the meter area while it slides, so it can paint
     * over a cached VU face -- which is how hiding the art left the right
     * meter's arc erased for good: the width changed once, the face was
     * redrawn once, and then the slide wiped it again. */
    ui_meter_faces_invalidate();

    if (!art_ready || art_x >= FB_W) return;
    uint32_t w = FB_W - art_x;
    if (w > ART_W) w = ART_W;
    fb_copy(0, ART_STASH_Y, art_x, ART_Y, w, ART_H);
}

/* One rect per ROW, not per band. 360 commands against 40 -- affordable
 * because this is a repaint, never a per-frame path, and the SDRAM work is
 * identical either way since the pixel count does not change. Per-row is what
 * lets ui_grad_at() dither at all. */
static void ui_gradient(void)
{
    for (uint32_t y = 0; y < FB_H; y++)
        fb_rect(0, y, FB_W, 1, ui_grad_at(y));
}

static void ui_toast_set(const char *msg, uint32_t n, const char *suffix)
{
    uint32_t i = 0;
    while (msg[i] && i < sizeof(ui_toast) - 1u) { ui_toast[i] = msg[i]; i++; }
    if (n != 0xFFFFFFFFu) {
        ui_toast[i++] = ' ';
        char t[8]; int k = 0;
        do { t[k++] = (char)('0' + n % 10u); n /= 10u; } while (n && k < 7);
        while (k && i < sizeof(ui_toast) - 1u) ui_toast[i++] = t[--k];
    }
    if (suffix) while (*suffix && i < sizeof(ui_toast) - 1u) ui_toast[i++] = *suffix++;
    ui_toast[i] = 0;
    ui_toast_t0   = cycles() | 1u;         /* never 0 -- that means inactive */
    ui_toast_step = 0xFFFFFFFFu;           /* force the first draw */
}

/* Plain text, no trailing number. */
#if UI_SHOW_SEEK_DIAG
/* Suppressed in the seek-diagnostic build: the toast band sits on diag row A,
 * and a toast landing mid-reading would corrupt the one thing this build
 * exists to show. */
static void ui_toast_msg(const char *msg) { (void)msg; }
#else
static void ui_toast_msg(const char *msg) { ui_toast_set(msg, 0xFFFFFFFFu, 0); }
#endif


/* Bytes per second of AUDIO, which is what both the duration and the seek
 * distance depend on.
 *
 * `bytes_per_sec` is the first decoded frame's bitrate. On a VBR file that is
 * not the file's average, so it made the total length wrong AND made a "5
 * second" seek move by some other amount -- the same error, surfacing twice.
 *
 * Preference order: the rate implied by a declared frame count (exact), then
 * the rate actually measured during playback (self-correcting, needs a few
 * seconds), then the first frame. */
static uint32_t ui_byte_rate(void)
{
    if (track_secs && slot_size > audio_start)
        return (slot_size - audio_start) / track_secs;

    /* CBR with no Xing/Info header: the frame bitrate IS the byte rate, and it
     * is exact. Measuring throughput instead was the whole defect.
     *
     * Three files on the test card are exactly this shape -- no frame count,
     * constant bitrate -- and they were the ONLY three where seeking went
     * wrong, at 1.2x and latently at 1.0x. meas_rate is a self-correcting
     * estimate; preferring it over a number that cannot be wrong let the seek
     * distance drift for no reason at all.
     *
     * meas_rate now serves only what it was ever for: a headerless VBR file,
     * where no constant exists to read. */
    if (!vbr_seen && bytes_per_sec) return bytes_per_sec;
    if (meas_rate) return meas_rate;
    return bytes_per_sec;
}

/* Byte rate for the SEEK LIMIT specifically. Deliberately not ui_byte_rate():
 * that falls back to meas_rate, which converges all through playback, so a limit
 * derived from it drifts a little on every repeat. The parked position then
 * never equals the newly computed one, the movement guard never fires, and the
 * flush-and-reprefill loop comes straight back -- but only on files with no
 * Xing header, since those are the ones that reach the meas_rate branch. That
 * is exactly "works on some songs and not others".
 *
 * Both inputs here are fixed at load, so this figure never moves. Same reasoning
 * as ui_total_secs() below, which was corrected for the same fault earlier. */
static uint32_t ui_seek_rate(void)
{
    if (track_secs && slot_size > audio_start)
        return (slot_size - audio_start) / track_secs;
    return bytes_per_sec;
}

static uint32_t ui_total_secs(void)
{
    if (track_secs) return track_secs;          /* Xing/VBRI: exact */
    /* Headerless: size / first-frame bitrate. BOTH inputs are fixed at load,
     * so the figure appears with the first painted clock and never moves --
     * exact for CBR, approximate for the rare headerless VBR. The earlier
     * version derived this from meas_rate, which updates all through playback;
     * that was the wandering total. */
    if (slot_size > audio_start && bytes_per_sec)
        return (slot_size - audio_start) / bytes_per_sec;
    return 0;
}

static char *ui_mmss(char *p, uint32_t sec)
{
    uint32_t m = sec / 60u, s = sec % 60u;
    if (m > 99u) m = 99u;
    *p++ = (char)('0' + (m / 10u) % 10u);
    *p++ = (char)('0' + m % 10u);
    *p++ = ':';
    *p++ = (char)('0' + s / 10u);
    *p++ = (char)('0' + s % 10u);
    return p;
}

static char *ui_dec(char *p, uint32_t v)
{
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (n) *p++ = t[--n];
    return p;
}

static char *ui_hex2(char *p, uint8_t v)
{
    static const char hx[] = "0123456789ABCDEF";
    *p++ = hx[v >> 4]; *p++ = hx[v & 15u];
    return p;
}

/* Drawn ONCE per track load (from load_track(), after title/artist are
 * parsed) -- static chrome, never touched again until the next track. */
/* Fill the stash with a placeholder until real cover art is decoded into it.
 * Deliberately drawn the same way real art will be delivered -- into the same
 * off-screen rows -- so the panel, the slide and the clipping are all exercised
 * now and the decoder simply replaces the contents later. */
static int  art_decode(uint32_t tag_len);

/* Round the art's corners by masking the STASH rather than the blit: baked in
 * once, so every slide step then draws an already-rounded image for free. The
 * corner colour is the card grey, which ties the panel to the text block --
 * and because the gradient runs vertically, a fixed colour stays correct at
 * every x the panel slides through. */
static void ui_art_round(void)
{
    /* Corners are cut with the colour of the GRADIENT at the row the mount
     * will occupy, not a flat colour -- otherwise the rounding shows up as four
     * pale wedges instead of disappearing. The gradient runs vertically, so a
     * per-row colour stays correct at every x the panel slides through. */
    const uint32_t r = 10u;
    for (uint32_t i = 0; i < r; i++) {
        uint32_t dy = r - i, inner = 0;
        while ((inner + 1u) * (inner + 1u) + dy * dy <= r * r) inner++;
        uint32_t cut = r - inner;
        if (!cut) continue;
        uint16_t top = ui_grad_at(ART_Y + i);
        uint16_t bot = ui_grad_at(ART_Y + ART_H - 1u - i);
        fb_rect(0,           ART_STASH_Y + i,              cut, 1, top);
        fb_rect(ART_W - cut, ART_STASH_Y + i,              cut, 1, top);
        fb_rect(0,           ART_STASH_Y + ART_H - 1u - i, cut, 1, bot);
        fb_rect(ART_W - cut, ART_STASH_Y + ART_H - 1u - i, cut, 1, bot);
    }
}

/* The grey plate the cover sits on, plus a hairline just inside the padding so
 * the artwork reads as mounted rather than as a hole cut in the panel. Drawn
 * BEFORE the cover, which then lands inside it. */
static void ui_art_mount(void)
{
    fb_rect(0, ART_STASH_Y, ART_W, ART_H, UI_PANEL);
}

/* B-412 (audit after B-410/B-411): same pattern as ui_bg_restore()'s own bug -- fb_rect() already
 * no-ops under FB_HELD(), but `art_ready` used to get marked regardless, so a track load that
 * happened to coincide with an overlay closing could leave art_ready=1 over a placeholder that was
 * never actually drawn. Lower-probability window than ui_bg_restore's (needs a track load and an
 * overlay transition to coincide, not every single transition) and a safer failure mode (no cover
 * shown, matching the existing "no art" state, not a corrupted one) -- fixed anyway for consistency
 * with every other draw primitive's own guard. */
static void ui_art_placeholder(void)
{
    if (FB_HELD()) return;
    /* Only the cover area -- the mount around it is already drawn. A slightly
     * darker fill than the plate so an artless track reads as an empty frame
     * rather than a solid grey slab. */
    fb_rect(ART_PAD, ART_STASH_Y + ART_PAD, ART_IMG, ART_IMG, UI_TRACK);
    art_ready = 1;
}

/* The same frame, captioned, for a cover that is present and unreadable.
 *
 * Two short lines because the panel is 92 px wide and the words are not:
 * "PROGRESSIVE" alone is 121 px. "PROG." and "JPEG" are 54 and 44, which fit
 * with room to centre them. Terse, but it is the difference between "this
 * player is broken" and "this file's cover is in a format it cannot read",
 * and the toast carries the full sentence for anyone who catches it.
 *
 * Persistent, unlike the toast. That is the point: a cover being unreadable is
 * a standing fact about the file, not an event. */
static void ui_art_reason(int progressive)
{
    if (FB_HELD()) return;   /* B-412: same reasoning as ui_art_placeholder()'s own guard just above */
    /* A boolean, not the picojpeg status: this lives well above the point
     * where art.inc pulls picojpeg.h in, so the enum is not in scope here.
     * The caller does the comparison, where it is. */
    const char *a = progressive ? "PROG." : "COVER";
    const char *b = progressive ? "JPEG"  : "ERROR";
    ui_art_placeholder();
    fb_set_color(UI_FAINT, UI_TRACK);
    uint32_t wa = fb_text_width(a, TS_1X), wb = fb_text_width(b, TS_1X);
    uint32_t cx = ART_PAD + ART_IMG / 2u;
    uint32_t cy = ART_STASH_Y + ART_PAD + ART_IMG / 2u;
    fb_text_clipped(cx - wa / 2u, cy - FB_CELL(TS_1X), a, TS_1X, TS_1X, wa + 2u);
    fb_text_clipped(cx - wb / 2u, cy + 2u,             b, TS_1X, TS_1X, wb + 2u);
    art_ready = 1;
}

/* Clear the waveform band across the FULL inner width and force every bar to
 * redraw. Used on a panel toggle: the bars change width, so leftovers from the
 * previous width would otherwise stay on screen. */
static void ui_wave_clear(void)
{
    /* The VU face is cached on screen rather than redrawn each pass, so
     * whatever erases this region has to say so -- otherwise the arc,
     * ticks and labels are wiped and never come back, while the needle
     * carries on repainting itself. Hiding the album art did exactly
     * that. */
    ui_meter_faces_invalidate();

    for (uint32_t y = UI_WAVE_Y - UI_WAVE_TOP; y < UI_WAVE_Y + UI_WAVE_H && y < FB_H; y++)
        fb_rect(UI_MARGIN, y, UI_INNER_W, 1, ui_grad_at(y));
    for (uint32_t i = 0; i < UI_WAVE_N; i++) { wave_drawn[i] = 0xFFu; wave_pk_drawn[i] = 0xFFu;
            for (uint32_t z = 0; z < SPEC_BANDS; z++) spec_drawn[z] = 0xFFu; }
}

/* Transport glyphs drawn as shapes, not characters: the font atlas is ASCII
 * 0x20-0x7E, so there is no play or pause symbol in it, and adding two glyphs
 * to a 97%-full BRAM for this would be a poor trade. Both are a handful of
 * rects, which the engine draws in one command each. */
#define UI_ICON_H 12u
#define UI_ICON_W 11u

/* Three chasing arrows rather than one pulsing arrow: the lit one advances and
 * the one behind it fades out across the step, so the motion reads as
 * direction of travel instead of just "something is on". */
#define UI_ARR_W     9u
#define UI_ARR_H     14u      /* 13 visible rows: a triangle tapers, so it has
                               * to run slightly taller than the 12px pause bars
                               * to carry the same visual weight */
#define UI_ARR_GAP   3u
#define UI_ARR_N     3u
#define UI_ARR_STEPS 10u       /* refreshes per arrow; 30 Hz -> ~330 ms each */
#define UI_ARR_TICKS (UI_ARR_N * UI_ARR_STEPS)
#define UI_ARR_TAIL  (UI_ARR_STEPS * 2u)   /* how far the glow trails behind */
#define UI_ARR_SPAN  (UI_ARR_N * (UI_ARR_W + UI_ARR_GAP))

/* One clear box big enough for EITHER symbol. The pause branch used to clear
 * only its own 12 rows while the arrows are 14, so switching to paused left
 * the bottom two rows of the arrows on screen. Sizing the wipe to the larger
 * of the two removes the whole class of leftover. */
#define UI_ICONBOX_H ((UI_ARR_H > UI_ICON_H) ? UI_ARR_H : UI_ICON_H)

static void ui_icon_arrow(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                          uint16_t c)
{
    /* Right-pointing triangle: vertical edge on the left, apex centred on the
     * right -- the standard media-player form. */
    uint32_t half = h / 2u;
    for (uint32_t i = 0; i < h; i++) {
        uint32_t d  = (i < half) ? (half - i) : (i - half);
        uint32_t ww = ((half - d) * w) / half;
        if (ww) fb_rect(x, y + i, ww, 1, c);
    }
}

/* ---- mode indicators -------------------------------------------------------
 * Drawn from rects rather than added to the font atlas: BRAM is at 97% and a
 * glyph costs a whole cell, while these are four rects each. They also need to
 * dim to "mode off" rather than disappear -- an icon that vanishes gives no
 * hint the mode exists, which is the usual complaint about hidden controls. */
#define UI_MODE_W  11u
#define UI_MODE_H  9u

/* Repeat: a rounded rectangle loop with an arrowhead on the top-right. With
 * `one` set, the middle is notched to read as "just this track". */
static void ui_icon_repeat(uint32_t x, uint32_t y, uint16_t c, int one)
{
    fb_rect(x,               y,               UI_MODE_W,     1u,        c);
    fb_rect(x,               y + UI_MODE_H-1, UI_MODE_W,     1u,        c);
    fb_rect(x,               y + 1u,          1u,            UI_MODE_H-2, c);
    fb_rect(x + UI_MODE_W-1, y + 1u,          1u,            UI_MODE_H-2, c);
    /* Arrowhead, top-right, pointing along the loop. */
    fb_rect(x + UI_MODE_W-4, y - 1u,          1u,            3u,        c);
    fb_rect(x + UI_MODE_W-3, y - 2u,          1u,            5u,        c);
    if (one) {
        /* Break the bottom edge and drop a tick in the gap: unmistakably a
         * different state at 1x, without needing a digit glyph. */
        fb_rect(x + UI_MODE_W/2u - 1u, y + UI_MODE_H-1, 3u, 1u, UI_PANEL);
        fb_rect(x + UI_MODE_W/2u,      y + UI_MODE_H-3, 1u, 3u, c);
    }
}

/* Speaker, with 0..3 waves. The volume is a MODE like repeat --
 * it persists, it changes what you hear, and until now the only sign of it was
 * a toast that had already gone by the time you wondered why the music was
 * quiet. Same 11x9 box and same 1 px construction as its neighbours.
 *
 * Drawn rather than typed because the font is ASCII 0x20..0x7E and has no
 * speaker in it.
 *
 * The waves are arcs, not bars: the tall ones have their ends pulled back a
 * pixel so they curve. The first is left straight -- it is three pixels tall,
 * where a curve is indistinguishable from a wobble, and pulling its end back
 * would put it against the cone and read as attached to it.
 *
 * Mute is an X rather than a slash across the whole icon. A slash would cross
 * the cone, and at this size that turns two recognisable shapes into one
 * unrecognisable one. */
static void ui_icon_speaker(uint32_t x, uint32_t y, uint32_t lvl, uint16_t c)
{
    fb_rect(x, y + 3u, 2u, 3u, c);                   /* the box */
    for (uint32_t i = 0; i < 3u; i++)                /* the cone, opening out */
        fb_rect(x + 2u + i, y + 2u - i, 1u, 5u + 2u * i, c);

    if (!lvl) {
        for (uint32_t i = 0; i < 5u; i++) {
            fb_rect(x + 6u + i,  y + 2u + i, 1u, 1u, c);
            fb_rect(x + 10u - i, y + 2u + i, 1u, 1u, c);
        }
        return;
    }
    for (uint32_t k = 0; k < lvl && k < 3u; k++) {
        uint32_t wx = x + 6u + 2u * k;
        uint32_t top = y + 3u - k, h = 3u + 2u * k;
        if (!k) {
            fb_rect(wx, top, 1u, h, c);
        } else {
            fb_rect(wx, top + 1u, 1u, h - 2u, c);
            fb_rect(wx - 1u, top, 1u, 1u, c);
            fb_rect(wx - 1u, top + h - 1u, 1u, 1u, c);
        }
    }
}

/* Stop: a filled square, the universal counterpart to the pause bars. */
static void ui_icon_stop(uint32_t x, uint32_t y, uint16_t c)
{
    fb_rect(x, y, UI_ICON_W, UI_ICON_H, c);
}

static void ui_icon_pause(uint32_t x, uint32_t y, uint16_t c)
{
    uint32_t bar = UI_ICON_W / 3u;
    fb_rect(x, y, bar, UI_ICON_H, c);
    fb_rect(x + UI_ICON_W - bar, y, bar, UI_ICON_H, c);
}

static void ui_marq_init(ui_marquee_t *m, const char *text,
                         uint32_t y, uint32_t scale)
{
    uint32_t i = 0;
    while (text[i] && i < sizeof(m->text) - 1u) { m->text[i] = text[i]; i++; }
    m->text[i] = 0;
    m->y     = y;
    m->scale = scale;
    m->pos   = 0;
    m->next  = cycles() + CLK_HZ;          /* hold at the start first */

    /* Scroll when the PAINTED text overruns, not when its advances do.
     *
     * fb_text_width sums advances, but fb_char paints a whole CELL -- 32 px at
     * 2x against an average advance near 22 -- so the final glyph reaches up to
     * a cell past where the advance total says the text ends. A title could
     * therefore pass the fits-check, have its last cell clipped at
     * UI_CARD_TEXT_R, and never scroll because nothing thought it overflowed.
     *
     * Measured on real titles: "Come All Ye Faithful" is 348 advance-pixels
     * against a 352 budget -- inside it by four -- yet paints to 390 against a
     * right edge of 380. "Alabama Getaway" is 312 and paints to 342, which
     * genuinely fits and correctly stays still. */
    uint32_t adv_w = fb_text_width(m->text, scale);
    uint32_t last  = i ? fb_adv(m->text[i - 1u], scale) : 0u;
    uint32_t painted = (adv_w > last) ? (adv_w - last + FB_CELL(scale))
                                      : FB_CELL(scale);
    m->on = (adv_w > ui_text_w) ||
            (UI_TEXT_X + painted > UI_CARD_TEXT_R);
}

/* One step. Repaints the whole row first, because the window that follows may
 * be shorter than what was there. */
static void ui_marq_step(ui_marquee_t *m, uint16_t fg)
{
    if (!m->on || (int32_t)(cycles() - m->next) < 0) return;
    m->next = cycles() + CLK_HZ / 3u;

    uint32_t len = 0;
    while (m->text[len]) len++;
    if (++m->pos > len) m->pos = 0;
    if (m->pos == 0) m->next = cycles() + CLK_HZ;   /* pause at the start */

    /* Erase the full paintable width, not just the layout budget: a glyph
     * cell reaches past the budget, and anything painted outside the erased
     * strip is never cleaned up -- it accumulated as the text scrolled. */
    uint16_t bg = ui_grad_at(m->y);
    fb_rect(UI_TEXT_X, m->y, UI_CARD_TEXT_R - UI_TEXT_X,
            FB_CELL(m->scale), bg);
    fb_set_color(fg, bg);
    fb_text_boxed(UI_TEXT_X, m->y, m->text + m->pos,
                  m->scale, m->scale, ui_text_w, UI_CARD_TEXT_R);
}

/* The library queue this track came from has no more entries and repeat is off: playback should stop, not restart the
 * last track. */
static int list_ended(void);

__attribute__((optimize("Os")))       /* one-off frame paint at a track change */
/* EQ pill: the currently applied EQ preset (FLAT, BASS, ...). Fixed width so
 * a shorter name never leaves the previous one's edges behind; text centred.
 * Redrawn from the chrome and again whenever the preset changes. */
#define UI_EQ_PILL_W 100u
static void ui_eq_pill(void)
{
    uint16_t pbg = ui_grad_at(UI_GENRE_Y + UI_GENRE_H / 2u);
    fb_round_rect_on(UI_TEXT_X, UI_GENRE_Y, UI_EQ_PILL_W, UI_GENRE_H, UI_GENRE_H / 2u,
                     UI_PILL_BG, pbg);
    const char *n = eq_name[eq_idx];
    uint32_t w = fb_text_width(n, TS_1X);
    if (w > UI_EQ_PILL_W - 12u) w = UI_EQ_PILL_W - 12u;
    fb_set_color(eq_idx ? ui_accent : UI_FAINT, UI_PILL_BG);
    fb_text_clipped(UI_TEXT_X + (UI_EQ_PILL_W - w) / 2u, UI_GENRE_Y + (UI_GENRE_H - FB_CELL(TS_1X)) / 2u, n, TS_1X, TS_1X, w);
}

static void ui_fs_frame(void);
/* B-333: cold code (RAM shrink); every path to it goes through ui_chrome_paint(), which needs the cold image. */
COLD_SR static void ui_draw_chrome(void)
{
    /* Draw nothing at all while blanked -- a track change must not light the
     * screen back up. ui_blank_wake() calls this again on the way out, so the
     * skipped work is simply deferred rather than lost. */
    if (screen_blank) return;
    if (ui_fullscreen) { ui_fs_frame(); return; }          /* fullscreen visualiser: its own frame (fw/fullscreen.inc) */
    ui_splash_art_active = 0u;
    ui_gradient();

    /* When there's no usable title, show the FILENAME.
     *
     * It is almost always the song name, so an untagged file reads as itself.
     * What used to be here was a DIAGNOSTIC -- "NOTAG FFFB9064 R04", the head
     * bytes and the reload status. It told three failure modes apart during
     * the reload hunt and earned its place then, but a user is not debugging
     * this core: a file that plays perfectly well was announcing itself as a
     * hex dump. Nothing internal goes on this screen any more.
     *
     * That also retires the UNICODE TAG case, which was the same mistake in
     * words -- a tag encoding the parser declines to handle is our limitation
     * to state in the README, not a caption for someone's music. The filename
     * is the better answer there too, and those files always have one. */
    char namebuf[TITLE_MAX];
    const char *title = track_title;
    if (ui_loading) title = "";                /* nothing known yet: leave it blank, do not invent a name */
    /* B-080: nothing loaded at all (a fresh boot with no history, or the library overlay closed without a pick) used
     * to fall through to the filename branch below, find no track_file either, and show "UNKNOWN TRACK" -- a
     * debugging label, not something a track-less screen should say. The chrome is drawn either way (closing the
     * library always repaints it), so this is the one place that has to carry the message. */
    else if (!track_title[0] && !track_file[0] && lib_state == LIB_ST_OK)
        title = "Select a track from your library";
    else if (!track_title[0] && !track_file[0] && lib_state != LIB_ST_OK)
        title = "Sync your library to play music";
    else if (!track_title[0]) {
        /* Last path component, extension dropped: the slot holds a full path
         * ("/Assets/tau/common/Flodown.mp3"). */
        uint32_t start = 0;
        for (uint32_t i = 0; track_file[i]; i++)
            if (track_file[i] == '/' || track_file[i] == 0x5Cu) start = i + 1u;

        uint32_t n = 0, dot = 0;
        for (uint32_t i = start; track_file[i] && n < sizeof(namebuf) - 1u; i++) {
            if (track_file[i] == '.') dot = n;      /* LAST dot, not the first */
            namebuf[n++] = track_file[i];
        }
        /* Only trim at a dot that actually looks like an extension -- a name
         * such as "Blur - 13.mp3" must not lose its number, and a leading dot
         * is not an extension at all. */
        if (dot && n - dot <= 5u) n = dot;
        namebuf[n] = 0;

        /* No tag and no name means APF told us nothing about the slot. Rare,
         * and still not the user's problem to diagnose. */
        title = n ? namebuf : "UNKNOWN TRACK";
    }

    /* Now-playing UI pass 1 (Figma node 163:57): art panel moved to a static
     * top-left mount, beside a genre pill + the title/artist/album stack
     * instead of a full-width card behind them. The solid UI_PANEL card is
     * gone -- text now blends against the actual gradient at its own row,
     * same convention every un-carded element on this screen already uses
     * (ui_grad_at(y)). ART_H > ART_W never happens, so a single rrect call
     * at ART_X,ART_Y is the whole mount when there's no cover yet; a real
     * cover overwrites it later via ui_art_draw()'s fb_copy once ready. The
     * note glyph is a plain colored rect -- a real icon asset is a follow-up
     * (docs/HELIOS_SPEC.md section 7.2), not this pass. Gated with the genre
     * pill below: the bare `player`/`player-profile` targets have no RAM
     * margin left to spend on placeholder polish (measured: pushed `player`
     * from a positive to a negative heap gap when this and the pill were
     * both unconditional). Those builds just leave the top-left corner
     * blank there, same as before this pass, until real cover-art assets or
     * a size trim make it affordable. */
    fb_round_rect(ART_X, ART_Y, ART_W, ART_H, 12u, UI_PANEL);
    fb_rect(ART_X + ART_W / 2u - 14u, ART_Y + ART_H / 2u - 14u, 28u, 28u, ui_accent);

    ui_eq_pill();

    /* One right edge for everything -- UI_CARD_TEXT_R is an absolute x
     * (UI_MARGIN + UI_INNER_W), so it stays correct as the right margin no
     * matter where the text column starts. */
    ui_text_w = (FB_W - UI_MARGIN) - UI_TEXT_X - 8u;

    /* Keep each line's text + scale so its marquee can repaint that row. */
    /* Capped at 2x rather than 3x: with album and format lines below it, a 48px
     * title cannot fit inside the card without colliding with the art row. A
     * predictable layout is worth more than the largest possible type. */
    /* Never below 1.5x. Auto-fit alone could drop a long title to 1x, which is
     * a two-step fall while every other track sits at 1.5x or 2x -- measured on
     * a real library, only the two longest titles ever got there, so they read
     * as a glitch rather than as a layout rule. The marquee already exists for
     * text that will not fit; shrinking was being tried first and never leaving
     * it anything to do. One step of size variation, then it scrolls. */
    /* Fixed at 2x. Auto-fitting meant the title changed size with its LENGTH:
     * measured across a real library, nine of eleven fitted at 2x and the two
     * longest dropped to 1.5x, so those two looked wrong rather than looking
     * fitted -- and one of them missed by eight pixels. One size for every
     * track, and ui_marq_init below turns the scroll on for anything that
     * overflows -- which is what the marquee was already for, and why it
     * almost never ran. */
    uint32_t ts = TS_2X;
    ui_marq_init(&ui_mq_title, title, UI_NP_TITLE_Y, ts);
    fb_set_color(UI_WHITE, ui_grad_at(UI_NP_TITLE_Y));
    fb_text_boxed(UI_TEXT_X, UI_NP_TITLE_Y, ui_mq_title.text, ts, ts,
                  ui_text_w, UI_CARD_TEXT_R);

    /* Artist one step down from the title, never below 1.5x -- that step only
     * exists because the engine can scale fractionally now. */
    uint32_t as = (ts > TS_15X) ? (ts - 1u) : TS_15X;
    ui_mq_artist.on = 0;      /* no artist -> no leftover scroll from the last track */
    uint32_t y = UI_NP_TITLE_Y + FB_CELL(ts) + 6u;
    if (!ui_loading && track_artist[0]) {
        fb_set_color(ui_accent, ui_grad_at(y));
        ui_marq_init(&ui_mq_artist, track_artist, y, as);
        fb_text_boxed(UI_TEXT_X, y, ui_mq_artist.text, as, as,
                      ui_text_w, UI_CARD_TEXT_R);
        y += FB_CELL(as) + 3u;
    }

    /* Format line (bitrate / sample rate). Nothing to draw yet -- neither is
     * known until the first frame decodes -- so chrome only reserves the row
     * and ui_draw_dynamic() fills it in once. It replaced the slot filename,
     * which existed to prove 0190 getfile returns real data. It does, so that
     * reconnaissance is finished; the capability it demonstrated is what
     * in-core track selection will be built on. */
    if (!ui_loading && (track_album[0] || track_year[0] || track_trk[0])) {
        char b[64], *q = b;
        if (track_trk[0]) {
            const char *t = track_trk;
            while (*t) *q++ = *t++;
            *q++ = ' '; *q++ = '-'; *q++ = ' ';
        }
        const char *a = track_album;
        while (*a && q < b + sizeof(b) - 10) *q++ = *a++;
        if (track_album[0] && track_year[0]) { *q++ = ' '; *q++ = '-'; *q++ = ' '; }
        const char *yr = track_year;
        while (*yr && q < b + sizeof(b) - 1) *q++ = *yr++;
        *q = 0;
        /* This row doubles as the warning line for a file the core will not
         * play. Grey reads as another piece of metadata; red reads as a
         * problem, which is the entire point of putting it there. */
        fb_set_color(ui_warn_row ? UI_RED : UI_DIM, ui_grad_at(y));
        fb_text_boxed(UI_TEXT_X, y, b, TS_1X, TS_1X,
                      ui_text_w, UI_CARD_TEXT_R);
        y += FB_CELL(TS_1X) + 2u;
    }

    ui_info_y    = y;
    ui_last_info = 0xFFFFFFFFu;
    und1_sec     = 0xFFFFFFFFu;   /* the latch is per TRACK, not per boot */
    und2_sec     = 0xFFFFFFFFu;
    und_edges    = 0;
    und_prev     = 0;
    clk_n        = 0;
    clk_sec      = 0xFFFFFFFFu;
    clk_max      = 0;
    clk_prev     = 0;

    /* Wave bed. Bars grow upward from the baseline, so clear the whole band
     * once here and let ui_draw_dynamic() repaint only the bars. */
    ui_wave_clear();

    /* Every one of these must be invalidated: this function repaints the whole
     * screen, so anything ui_draw_dynamic() only redraws "when it changes" has
     * just been erased and has to be considered absent. Forgetting
     * ui_last_stall is why the diagnostic line vanished after the first track
     * load and never came back. */
    /* Just re-blit whatever the stash holds. Decoding happens in load_track,
     * NOT here: ui_draw_chrome() is also called by the tag probe during
     * playback, and a decode there would fire hundreds of blocking SD reads
     * straight into the budget that keeps the PCM FIFO fed. */
    ui_art_draw();
    ui_loader_txt = 1u;            /* a repaint erased the loader caption */

    ui_last_sec   = 0xFFFFFFFFu;   /* force the first time-display redraw */
    ui_last_vu    = 0xFFFFFFFFu;
    ui_last_pause = 0xFFFFFFFFu;
    ui_last_stall = 0xFFFFFFFFu;
    ui_last_spd   = 0xFFFFFFFFu;   /* or the diag row dies on the first reload */
    /* THIRD entry to be forgotten from this list, after ui_last_stall and the
     * mode row. A toast is drawn only when its fade step CHANGES, so once
     * chrome has painted over one, ui_toast_step still says "already drawn"
     * and it never comes back.
     *
     * Every toast set around a track change was being erased: the LOADING
     * TRACK indicator -- which is why picking from the menu showed nothing at
     * all while the same toasts work fine from a button press. */
    ui_toast_step = 0xFFFFFFFFu;
    ui_last_prog  = 0xFFFFFFFFu;
    /* The mode row -- repeat, shuffle, the EQ name, N-of-M. Missing from this
     * list until now, and the second entry to be forgotten from it after
     * ui_last_stall. It survived because the flag is statically initialised to
     * 1, so the FIRST track drew fine and every reload after that came up
     * blank: the row had been erased and nothing said so. That is why the
     * indicators only appeared once one of them was pressed.
     *
     * The accent-change path knew to set this and ui_draw_chrome did not, which
     * is the real fault -- two lists of the same thing, and only one of them
     * correct. The duplicates there are gone now; this is the one place that
     * knows what a full repaint destroys. */
    ui_mode_dirty = 1u;
    ui_icon_next  = cycles();      /* transport arrows, on the next tick   */
    /* The elapsed time is NOT reset here, and used to be.
     *
     * A repaint destroys what is DRAWN, not what has elapsed -- but this zeroed
     * ui_sec itself, so every caller that repaints mid-track silently threw the
     * clock away. load_track was the only caller where that looked correct, and
     * it hid the fault from the others: an accent change, a blank wake, and now
     * closing the playlist overlay all restarted the timer at 0:00.
     *
     * Resetting a new track's clock belongs to load_track, which is the only
     * place that knows a new track started. It does it now. */
    ui_prog_sec   = 0xFFFFFFFFu;
#if TAU_DIAGNOSTIC
    /* Chrome repaints the bottom strip too; make the 1 Hz stress HUD restore
     * itself on the next main-loop pass rather than waiting for another tick. */
    stress_hud_tick = 0xFFFFFFFFu;
#endif
    /* track_kbps is NOT cleared here. It describes the STREAM, not the screen,
     * and ui_draw_chrome() is also called mid-track by the tag probe. Clearing
     * it there blanked the format line permanently: rate_set is latched after
     * the first decoded frame, so nothing ever recomputed the value, and the
     * line's draw is gated on track_kbps being non-zero. Whoever resets
     * rate_set clears it instead. */
    ui_underrun_shown = 0;
    ui_size_warned    = 0;
    ui_ld_shown       = 0;
    /* The overlay sits ON TOP of everything this function just painted. Any
     * caller that repaints mid-browse -- a track auto-advancing, a blank wake,
     * an accent change -- would otherwise leave the player showing until the
     * next main-loop pass noticed and redrew the list. That gap is the "it
     * flips to the player" flicker.
     *
     * Putting it here rather than in the main loop means EVERY repaint route
     * is covered by construction, including ones added later. */
    if (lib_ui_open) lib_ui_draw();
    else if (set_open) set_draw();

}

/* Helios's first real screen region (docs/HELIOS_SPEC.md section 4): the
 * whole now-playing chrome repaint, registered once and dispatched through
 * mark_dirty()/flush() instead of a bare function-pointer call. Several of
 * ui_draw_chrome()'s own call sites have code immediately after them that
 * depends on it having ALREADY run synchronously this same pass -- most
 * concretely the one at the overlay-close site, where the very next line is
 * `ui_art_draw()`, which paints the album art into a background this
 * function has to have repainted first, and `ui_wave_force = 1u` right
 * after that, which only makes sense once the chrome it invalidates exists.
 * Real deferred (vblank-gated) batching would need each of those sites
 * individually re-audited for that ordering assumption -- not done here, so
 * this wrapper marks the region dirty and flushes IMMEDIATELY, same timing
 * as calling ui_draw_chrome() directly, but through Helios's real
 * region/flush API rather than a bare call. Deferred flushing (the actual
 * anti-tearing benefit) is a follow-up once H0 is hardware-confirmed and
 * these call sites are individually cleared for it, not this pass. */
static uint8_t ui_chrome_region = 0xFFu;
static void ui_chrome_paint(void)
{
    if (!SR_READY()) return;          /* B-333: the chrome is cold code; no cold image, no player screen */
    if (ui_chrome_region == 0xFFu) ui_chrome_region = helios_region_register(ui_draw_chrome);
    helios_mark_dirty(ui_chrome_region);
    helios_flush();
}

/* Helios H2 (docs/features/HELIOS_SPEC.md section 5, item 7 of the architecture review): double
 * buffering for a genuine full-frame redraw (chrome, and whatever the same transition draws right
 * alongside it, e.g. album art) -- bracket every plain FB_BASE-relative draw in the redraw with
 * these two calls instead of touching R_DBUF_CPU/R_DBUF_DISP at each individual call site.
 *
 * dbuf_redraw_begin(): if this bitstream has H2 (DBUF_READY()), select the buffer NOT currently
 * displayed for the CPU write-side (R_DBUF_CPU) and return 1; otherwise return 0 and touch nothing
 * -- old-bitstream safety rests entirely on this one check, since R_DBUF_CPU/R_DBUF_DISP simply do
 * not exist on any bitstream without TAU_DBUF (mp3_soc.v decodes those offsets to something else
 * or nothing at all there). The return value is NOT optional: it must be passed to
 * dbuf_redraw_end() unchanged, so the "end" half never touches the registers either when this
 * bitstream lacks them.
 *
 * dbuf_redraw_end(active): a no-op if `active` is 0. Otherwise: fb_fence() (every draw issued
 * between begin/end has now actually EXECUTED, not merely been accepted into the queue -- B-412,
 * found auditing every sticky-field/buffer-redirect site after B-410/B-411 found the same
 * fb_wait()-where-fb_fence()-was-needed gap twice elsewhere: this used to call fb_wait(), which
 * only confirms the FIFO wasn't full at queue time, not that the hardware has drained it, so the
 * flip below could in principle land at the next vblank before ui_chrome_paint()/ui_art_draw()'s
 * own commands had finished. No ov_draw dance needed here unlike those other two fixes: fb_fence()
 * is itself gated on FB_HELD(), but dbuf_redraw_begin()'s own precondition already guarantees
 * FB_HELD() is false for this whole bracket -- neither overlay flag it checks changes while this
 * runs), request the flip, then poll the RTL's own "flip still pending" bit for up to CLK_HZ/10
 * (~100 ms) before giving up. Either way, resync
 * R_DBUF_CPU to whatever IS now actually displayed: every ordinary INCREMENTAL draw after this
 * point (concretely, the meter box, ui_meter_redraw()) must target the buffer currently on screen
 * directly, per HELIOS_SPEC.md section 5's own note that H1's beam-gated draws are not part of
 * this double-buffered bracket at all -- so leaving R_DBUF_CPU pointed at the back buffer past this
 * function's return would silently misdirect every one of them, whether or not the flip we
 * requested actually landed in time. */
static uint8_t dbuf_redraw_begin(void)
{
    /* B-399 (owner-observed on real hardware, TAU_DEV_55): FB_HELD() is checked INSIDE every
     * individual draw primitive (fb_rect()/fb_bar()/etc., all at line ~465 onward), not by this
     * bracket's own caller -- while an overlay holds the screen, ui_chrome_paint()'s underlying
     * ui_draw_chrome() silently draws NOTHING (every call inside it no-ops), but this function had
     * no way to know that and dbuf_redraw_end() would request a flip anyway, onto a back buffer
     * holding whatever stale content was left there from an earlier, unrelated redraw -- visible as
     * a brief tear/glitch that self-corrects once the next real redraw catches up. Matches exactly
     * what was reported: settings/library open-close and menu navigation (both of which set
     * UI_OVERLAY_UP while the chrome bit is still set) tearing briefly then returning to normal. */
    /* B-402: the exact same bug shape as B-399's FB_HELD() fix above, found while scoping an
     * unrelated feature request -- ui_draw_chrome() ALSO no-ops while screen_blank is set (its own
     * comment: "a track change must not light the screen back up", ui_blank_wake() re-triggers the
     * real draw on the way out), a DIFFERENT condition from FB_HELD() (screen_blank is not part of
     * UI_OVERLAY_UP/ui_fullscreen). Without this check, a chrome invalidation firing while blanked
     * -- a track change during blank is the documented, expected case -- would still flip onto
     * whatever stale content sits in the back buffer, briefly breaking the "stays black while
     * blanked" guarantee. */
    if (!DBUF_READY() || FB_HELD() || screen_blank) return 0u;
    const uint32_t cur = REG(R_DBUF_DISP) & 1u;
    REG(R_DBUF_CPU) = cur ^ 1u;
    return 1u;
}

static void dbuf_redraw_end(uint8_t active)
{
    if (!active) return;
    fb_fence();
    REG(R_DBUF_DISP) = 1u;
    const uint32_t t0 = cycles();
    while ((REG(R_DBUF_DISP) & 2u) && (uint32_t)(cycles() - t0) < CLK_HZ / 10u) { }
    REG(R_DBUF_CPU) = REG(R_DBUF_DISP) & 1u;
}

/* A load failure used to spin in `for(;;){}`, which is the worst possible
 * outcome: an I/O problem became a frozen screen with no information, and the
 * user could not even pick another file. Say what happened, keep the status
 * bytes on screen, and stay responsive so the Core menu can load a new track. */
static void poll_input(void);

/* Nothing loaded. The core no longer forces a file to be chosen before it
 * starts, so this is the first thing seen on a card with no playlist -- it has
 * to say what to do rather than look like a failure. */
/* Gradient and title only. Shown while the first track loads, so the wait is a
 * deliberate-looking screen rather than a flash of instructions that is then
 * replaced -- which read as a glitch. */
/* Splash composition: the PLAYER, with nothing loaded into it.
 *
 * A centred title on an empty field was tried and rejected, and the reason is
 * worth keeping: the player screen is a left-aligned card with a meter and a
 * transport row, so a centred splash is a second visual language for the same
 * product. Reusing the player's own skeleton -- same card geometry at the same
 * UI_TITLE_Y, the meter where the meter always is, an info row where the
 * transport sits, the progress bar on its own line -- means the boot screen and
 * the player are one design rather than two that happen to ship together.
 *
 * It also fills the frame honestly. The old splash left 209 of 360 rows
 * carrying nothing; here every band of the screen has the same job it has
 * during playback. */
/* Anchored to the BOTTOM of the card, not tucked under the title. The card is
 * UI_CARD_H to match the player's, which carries four lines; the splash has
 * two, so placing the version directly under the title leaves the lower half
 * visibly empty and the card looks unfinished. Top and bottom anchored, the
 * same space reads as deliberate. 14px inset mirrors the card's top inset. */
#define UI_SPL_VER_Y    (UI_TITLE_Y - 14u + UI_CARD_H - 14u - 16u)
#define UI_SPL_INFO_Y  262u    /* the transport row's line */

/* Authored Tau loading screen. The generated 16-colour RLE file lives in its
 * own deferred APF slot and streams through the existing 4 KB tag scratch
 * window. Embedding it in the firmware crossed the reserved DMA boundary by
 * 12.5 KB; keeping it external preserves both image quality and decoder RAM.
 * The source artwork already contains the progress-bar outline. */
#define TAU_SPLASH_SLOT_ID  4u
#define TAU_SPLASH_W        400u
#define TAU_SPLASH_H        360u
#define TAU_SPLASH_HEADER   44u
#define TAU_SPLASH_CHUNK    4096u
#define TAU_SPLASH_STATUS_X 104u
#define TAU_SPLASH_STATUS_Y 256u
#define TAU_SPLASH_STATUS_W 192u
#define TAU_SPLASH_STATUS_H 16u
#define TAU_SPLASH_BAR_X    132u
#define TAU_SPLASH_BAR_Y    283u
#define TAU_SPLASH_BAR_W    144u
#define TAU_SPLASH_BAR_H    4u
#define TAU_SPLASH_SEG_W    30u
#define TAU_SPLASH_VER_Y    334u

/* Defined with the target-command implementation later in this file. */
static int target_read_slot(uint32_t slot, uint32_t off,
                            uint32_t dst_off, uint32_t len);
extern char _tag_start;

static uint16_t tau_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t tau_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int ui_splash_asset(void)
{
    uint32_t dst = (uint32_t)(uintptr_t)&_tag_start;
    uint8_t *buf = (uint8_t *)(uintptr_t)(0xC0000000u + dst);
    uint16_t palette[16];

    if (!target_read_slot(TAU_SPLASH_SLOT_ID, 0u, dst, TAU_SPLASH_HEADER))
        return 0;
    if (buf[0] != 'T' || buf[1] != 'A' || buf[2] != 'U' || buf[3] != '1' ||
        tau_u16(buf + 4u) != TAU_SPLASH_W ||
        tau_u16(buf + 6u) != TAU_SPLASH_H)
        return 0;

    uint32_t bytes = tau_u32(buf + 8u);
    if (!bytes || (bytes & 1u) || bytes > TAU_SPLASH_W * TAU_SPLASH_H * 2u)
        return 0;
    for (uint32_t i = 0; i < 16u; i++)
        palette[i] = tau_u16(buf + 12u + i * 2u);

    uint32_t file_off = TAU_SPLASH_HEADER, pos = 0;
    while (bytes) {
        uint32_t chunk = bytes > TAU_SPLASH_CHUNK ? TAU_SPLASH_CHUNK : bytes;
        if (!target_read_slot(TAU_SPLASH_SLOT_ID, file_off, dst, chunk))
            return 0;
        for (uint32_t i = 0; i < chunk; i += 2u) {
            uint32_t left = buf[i];
            uint16_t c = palette[buf[i + 1u] & 15u];
            if (!left || pos + left > TAU_SPLASH_W * TAU_SPLASH_H)
                return 0;
            while (left) {
                uint32_t x = pos % TAU_SPLASH_W;
                uint32_t y = pos / TAU_SPLASH_W;
                uint32_t n = TAU_SPLASH_W - x;
                if (n > left) n = left;
                fb_rect(x, y, n, 1u, c);
                pos += n;
                left -= n;
            }
        }
        file_off += chunk;
        bytes -= chunk;
    }
    return pos == TAU_SPLASH_W * TAU_SPLASH_H;
}

static void ui_splash_version(void)
{
    const char *s = "TAU ALPHA " APP_VER;
    uint32_t w = fb_text_width(s, TS_1X);
    fb_rect(UI_MARGIN - 4u, TAU_SPLASH_VER_Y - 1u, w + 8u,
            FB_CELL(TS_1X) + 2u, TAU_SPLASH_BG);
    fb_set_color(UI_DIM, TAU_SPLASH_BG);
    fb_text_clipped(UI_MARGIN, TAU_SPLASH_VER_Y, s, TS_1X, TS_1X,
                    FB_W - 2u * UI_MARGIN);
}

/* Card, title and version. Shared by the static splash and the animated one so
 * they cannot drift -- they are the same screen, and previously each drew the
 * title itself. `f/den` is the title's fade position; the card and version do
 * not fade, because animating the frame draws the eye to the furniture. */
/* The parts of the splash card that never change: the panel itself and the
 * version. Split out because the fade redraws the TITLE 33 times, and this
 * used to be redrawn with it.
 *
 * That was the rest of the boot flicker. Capping the fade at 33 steps stopped
 * the title flashing thousands of times a second, but each of those 33 still
 * refilled the whole card first -- wiping the title AND the version and
 * writing them back, 33 times, with scanout free to catch either gap. The
 * version never changes at all, so it was pure churn.
 *
 * Glyph cells paint their own background, so the title can be redrawn in
 * place over itself without the panel underneath being cleared first. */
static void ui_splash_bg(void)
{
    /* Identical geometry to ui_draw_chrome()'s card, at full width since the
     * splash has no art panel to make room for. */
    fb_round_rect(UI_MARGIN - 8u, UI_TITLE_Y - 14u,
                  UI_INNER_W + 16u, UI_CARD_H, 8u, UI_PANEL);

    fb_set_color(UI_DIM, UI_PANEL);
    fb_text_clipped(UI_MARGIN, UI_SPL_VER_Y, "v" APP_VER, TS_1X, TS_1X,
                    UI_INNER_W);
}

static void ui_splash_title(uint32_t f, uint32_t den)
{
    uint32_t sc = fb_text_fit("TAU", UI_INNER_W, TS_2X);
    fb_set_color(ui_mix(UI_PANEL, ui_accent, f, den), UI_PANEL);
    fb_text_clipped(UI_MARGIN, UI_TITLE_Y, "TAU", sc, sc, UI_INNER_W);
}

static void ui_splash_card(uint32_t f, uint32_t den)
{
    ui_splash_bg();
    ui_splash_title(f, den);
}

static void ui_splash(void)
{
    /* Immediate fallback while the APF asset is being read. A missing or
     * damaged cosmetic asset must never prevent the player from starting. */
    fb_rect(0u, 0u, FB_W, FB_H, TAU_SPLASH_BG);
    ui_splash_art_active = 1u;
    if (!ui_splash_asset()) {
        ui_splash_art_active = 0u;
        ui_gradient();
        ui_splash_card(1u, 1u);
        return;
    }
    ui_splash_version();
}

/* Boot animation: a pulse sweeps the meter while the title fades up.
 *
 * Runs BEFORE the track is opened, so it has the whole CPU -- nothing is
 * decoding yet and there is no audio to protect. That is the one moment in this
 * core where drawing cost genuinely does not matter, which is why the meter can
 * be redrawn in full every frame here and nowhere else.
 *
 * Fixed length rather than "until the track loads": tying it to load time means
 * it is a different animation on every card, and a stutter if the load is
 * quick. ~0.8 s, then the splash stays put until playback is ready. */
/* Boot meter: the playback bar meter, running on nothing.
 *
 * Two earlier attempts missed for opposite reasons. The first decayed to
 * silence in 0.83 s, finishing before the playlist had loaded. The second was a
 * travelling sine -- smooth, but too regular to read as a meter.
 *
 * This one is the SAME MOTION as VIZ_BARS during playback: the history scrolls
 * one bar left per frame and a new sample enters at the right, so the boot
 * screen moves the way the player does. Mirroring it was the point; a boot
 * animation that moves differently from the thing it boots into is a second
 * visual language again.
 *
 * The incoming level is a random WALK rather than a fresh random number. White
 * noise scrolling sideways looks like static; a walk wanders, holds a level for
 * a while and then moves off it, which is what a loudness history actually
 * looks like.
 *
 * The walk is MEAN-REVERTING. A plain one clamped at both ends drifts into a
 * corner and sits there -- simulated, it spent most of its time near the floor
 * and the meter read flat and low. Biasing it upward instead just pinned it to
 * the ceiling. Pulling it gently back toward a centre gives a mean around half
 * height with excursions either way, which held across several seeds. */
#define WV_FPS    18u    /* 36 bars at 18 fps: ~2.0 s for one to cross */

/* The level is a slow BODY plus a decaying TRANSIENT, not one walk between a
 * floor and a ceiling. A single clamped walk gave a mean of 73% but a standard
 * deviation of only 5.7 px -- everything sat in a narrow band and the contour
 * read as texture rather than as music. Music is a sustained level with hits
 * punching above it and dropping back, so that is what this generates: the body
 * wanders gently, and every seventh frame or so a transient is struck somewhere
 * in the headroom left above it and then decays away.
 *
 * Simulated over 900 frames: mean 69% of height, sd 10.4, range 18..72, and the
 * ceiling is touched under 1% of the time so peaks land rather than flatten. */
#define WV_BODY   53u    /* percent of full height the body settles around */
#define WV_PULL   18u    /* body is pulled back toward it by /this per frame */
#define WV_DRIFT   5u    /* most the body may wander between samples */
#define WV_HIT     7u    /* a transient is struck about 1 frame in this many */
#define WV_DECAY   2u    /* transient keeps DECAY/4 of itself each frame */
#define WV_FLOOR   2u    /* a bar at zero reads as broken rather than as quiet */

static uint8_t  wv_on, wv_level, wv_tr;
static uint32_t wv_next, wv_rng;
static unsigned char wv_h[UI_WAVE_N];

/* One frame at env/100 of full height. The same code draws the run and the
 * settle: winding env down pulls the whole meter with it. */
static void ui_wave_frame(void)
{
    uint16_t bed = ui_grad_at(UI_WAVE_Y);
    /* Full width, NOT ui_wave_w(). That reports the narrow meter whenever
     * art_shown is set, and art_shown is initialised to 1 -- so the boot meter
     * was leaving a gap for an album-art panel that does not exist yet and
     * cannot, since no track has been opened. The panel appears when the first
     * track turns out to have artwork, and the player narrows the meter then. */
    uint32_t ww  = UI_INNER_W;

    /* Scroll left and insert at the right -- the playback meter's own shift. */
    for (uint32_t i = 0; i < UI_WAVE_N - 1u; i++) wv_h[i] = wv_h[i + 1u];

    /* Body. One RNG draw, mean-reverting toward WV_BODY. */
    wv_rng ^= wv_rng << 13; wv_rng ^= wv_rng >> 17; wv_rng ^= wv_rng << 5;
    int32_t body = (int32_t)((UI_WAVE_H * WV_BODY) / 100u);
    int32_t lv   = (int32_t)wv_level
                 + (int32_t)(wv_rng % (2u * WV_DRIFT + 1u)) - (int32_t)WV_DRIFT
                 - ((int32_t)wv_level - body) / (int32_t)WV_PULL;
    if (lv < (int32_t)WV_FLOOR)   lv = (int32_t)WV_FLOOR;
    if (lv > (int32_t)UI_WAVE_H)  lv = (int32_t)UI_WAVE_H;
    wv_level = (uint8_t)lv;

    /* Transient. Decays first, then may be re-struck anywhere in the headroom
     * ABOVE the body -- which is what keeps a hit from simply saturating when
     * the body is already high. */
    wv_tr = (uint8_t)(((uint32_t)wv_tr * WV_DECAY) / 4u);
    wv_rng ^= wv_rng << 13; wv_rng ^= wv_rng >> 17; wv_rng ^= wv_rng << 5;
    if (wv_rng % WV_HIT == 0u) {
        uint32_t head = (uint32_t)(UI_WAVE_H - wv_level);
        if (head) {
            wv_rng ^= wv_rng << 13; wv_rng ^= wv_rng >> 17; wv_rng ^= wv_rng << 5;
            uint32_t hit = wv_rng % (head + 1u);
            if (hit > wv_tr) wv_tr = (uint8_t)hit;
        }
    }

    uint32_t smp = (uint32_t)wv_level + wv_tr;
    if (smp > UI_WAVE_H) smp = UI_WAVE_H;
    wv_h[UI_WAVE_N - 1u] = (unsigned char)smp;

    for (uint32_t i = 0; i < UI_WAVE_N; i++) {
        uint32_t h   = wv_h[i];
        uint32_t x   = UI_MARGIN + (i * ww) / UI_WAVE_N;
        uint32_t xn  = UI_MARGIN + ((i + 1u) * ww) / UI_WAVE_N;
        uint32_t lit = (xn - x > UI_WAVE_GAP) ? (xn - x - UI_WAVE_GAP) : 1u;

        fb_rect(x, UI_WAVE_Y + UI_WAVE_H - h, lit, h,
                ui_mix(UI_TRACK, ui_accent, i + 1u, UI_WAVE_N));
        if (UI_WAVE_H > h) fb_rect(x, UI_WAVE_Y, lit, UI_WAVE_H - h, bed);
    }
}

static void ui_wave_anim_start(void)
{
    /* cycles(), not 0 -- ui_wave_anim_tick() compares
     * (int32_t)(cycles() - wv_next) < 0, which against 0 is just the sign of
     * the counter, so this animation was dead for the same half of every
     * 71.6 s wrap as the loading dots beside it. */
    wv_on = 1u; wv_next = cycles(); wv_rng = cycles() | 1u;
    wv_level = (uint8_t)((UI_WAVE_H * WV_BODY) / 100u);
    wv_tr    = 0;
    for (uint32_t i = 0; i < UI_WAVE_N; i++) wv_h[i] = 0;
}

/* Called from the read spin. A single compare and return unless armed, so
 * every other caller of target_read_slot() is unaffected. */
static void ui_wave_anim_tick(void)
{
    if (!wv_on) return;
    if ((int32_t)(cycles() - wv_next) < 0) return;
    wv_next = cycles() + CLK_HZ / WV_FPS;
    ui_wave_frame();
}

/* Just stop. A ~0.5 s drain was tried and removed: it is half a second added to
 * every launch to watch bars fall, and the player repaints the whole screen a
 * moment later anyway. Stopping dead costs nothing and nobody sees the frozen
 * frame for long. */
static void ui_wave_anim_stop(void)
{
    wv_on = 0;
}

static void ui_splash_anim(void)
{
    /* Loading activity is tied to the actual APF read below, so no artificial
     * minimum delay is added here.  Fast cards should remain fast. */
    ui_splash();
}

/* `reason` explains why there is nothing playing, or is NULL when the answer is
 * simply "you have not picked anything yet".
 *
 * It has to be painted HERE rather than raised as a toast. This screen is
 * ui_splash() plus three lines of instructions -- the same gradient and the same
 * title as the boot screen -- so a user whose playlist failed sees a screen they
 * cannot distinguish from the one that was already up, and reads it as a hang.
 * The toast that would have explained it never appears either: the main loop
 * does `if (idle) continue;` before it reaches ui_draw_dynamic(), so in idle
 * mode nothing draws toasts at all. A static line is the only thing that
 * survives here. */
/* ---- boot progress ---------------------------------------------------------
 * Reading the .m3u is ~19 APF commands and the CPU spends nearly all of that
 * spinning in target_read_slot()'s completion poll. Dead time, with a static
 * splash on screen and no way to tell a slow card from a hung one.
 *
 * So the wait gets a voice: a label plus three dots whose brightness sweeps
 * across them. The falloff is deliberately the SAME arithmetic the transport
 * arrows use -- rotating peak, trailing glow, 30 Hz tick -- so it reads as this
 * UI's existing language rather than a second idiom bolted on.
 *
 * Driven from inside the read poll, not from a timer. That matters: the dots
 * animate against the REAL load, so a slow card keeps them moving and they stop
 * because the work they were reporting actually finished. A fixed-duration
 * animation would be a lie that happens to look similar.
 *
 * Costs nothing. There is no audio at boot, which is the same reason
 * ui_splash_anim() can redraw a whole meter every frame here and nowhere else.
 */
/* The SAME row the playlist summary lands on, deliberately. The two are
 * sequential, never simultaneous -- the indicator runs while the .m3u is being
 * read and ui_boot_clear() wipes the row the moment it is done, which is
 * exactly when PLAYLIST / N TRACKS replaces it. One status line that changes
 * what it says, rather than two lines where one is always blank. */
#define UI_BOOT_Y     UI_SPL_INFO_Y
#define UI_DOT_N      3u
#define UI_DOT_W      7u
#define UI_DOT_GAP    6u
#define UI_DOT_STEPS  10u
#define UI_DOT_TICKS  (UI_DOT_N * UI_DOT_STEPS)
#define UI_DOT_TAIL   (UI_DOT_STEPS * 2u)   /* how far the glow trails behind */
#define UI_DOT_FLOOR  7u       /* out of 31: unlit dots dim, never vanish */

static const char *ui_boot_msg;        /* NULL = nothing in progress */
static uint32_t    ui_boot_next, ui_boot_t, ui_boot_x;

static uint16_t ui_boot_bg(void)
{
    return ui_splash_art_active ? TAU_SPLASH_BG : ui_grad_at(UI_BOOT_Y);
}

/* A 7x7 disc, one rect per row -- the way every other icon here is built,
 * because BRAM is at 97% and a glyph would cost a whole atlas cell. */
static void ui_icon_dot(uint32_t x, uint32_t y, uint16_t c)
{
    static const unsigned char inset[7] = { 2, 1, 0, 0, 0, 1, 2 };
    for (uint32_t i = 0; i < 7u; i++)
        fb_rect(x + inset[i], y + i, 7u - 2u * inset[i], 1u, c);
}

/* Builds the draw-contract input struct (fw/meter.h) for whichever meter is about to tick. Not yet a
 * real fw/meter_host.inc (docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md section 5 item 4) --
 * just the one place all 3 call sites (the player screen's own dispatch, fullscreen, the Configure
 * page preview) build the struct the same way, so adding a field later means editing one function.
 * `force` is passed in explicitly rather than read from a global here, because this codebase has TWO
 * separate "context changed" flags with different consumers -- `wviz_force` (Winamp Bars/Scope, VU
 * Master, their Configure-page preview) and `ui_wave_force`/`wf` (every meter drawn from
 * ui_draw_dynamic_cold(), including Chladni) -- and picking the wrong one here would be a real bug,
 * not a style choice. Whichever global is the right one for a given call site stays that call site's
 * job to read; this function does NOT clear it either -- clearing stays each tick function's own job,
 * unchanged. `frame` increments once per call, which is once per display tick from each of the call
 * sites -- good enough for "a counter that goes up," nothing reads it yet. */
#define MTR_DT_MS 26u   /* the fixed ~38 Hz UI cadence (FL_UI_PERIOD, defined later in this file)
                          * this project has always assumed here, not a measured per-call delta --
                          * nothing tracks a real one yet. Was wviz_bars_tick's own local `dec_ms`. */
static uint32_t mtr_frame_ctr;
static inline mtr_in_t mtr_build(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t bg, uint32_t force)
{
    mtr_in_t in;
    in.spec   = spec_lvl;
    in.wave   = wav_v;
    in.peak   = peak_amp;
    in.peak_l = peak_l;
    in.peak_r = peak_r;
    in.frame  = ++mtr_frame_ctr;
    in.dt_ms  = MTR_DT_MS;
    in.x = (uint16_t)x; in.y = (uint16_t)y; in.w = (uint16_t)w; in.h = (uint16_t)h;
    in.bg   = bg;
    in.role = th_role;
    in.force = (uint8_t)force;
    return in;
}

/* Helios review item 5 (docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md section 5): the 8
 * legacy meters below, extracted from ui_draw_dynamic_cold()'s own inline `if (viz_mode == VIZ_X)`
 * chain into the same mtr_in_t contract item 1 proved on Winamp Bars/Scope, Chladni and VU Master --
 * geometry (x/y/w/h/bg) and the audio-input fields the struct already carries (spec/wave/peak/peak_l/
 * peak_r) come from `in`, exactly like those four; a meter's OWN accumulated state that is not really
 * an "input" (wave[]/wave_pk[]'s shared envelope history, the phase scope's trail buffer, the VU
 * needle's physics) stays a file-scope static read directly, same precedent wviz_bars_tick's own
 * per-band easing arrays already set. No new mtr_in_t fields added -- meter.h's own header comment
 * says as much ("without also inventing... in the same pass"), and every one of these 8 already had a
 * working direct-global-read version, so there is nothing here that NEEDS a new field to express.
 *
 * Not hardware-tested this pass (no card mounted) -- pure code motion, verified by diffing against the
 * pre-move logic line by line and by `make test-host`/every firmware build target still linking; see
 * docs/AUDIT_TRAIL.md B-390 for the full account of what was and was not possible to verify this way. */

/* Scrolling waveform: the waterfall's COPY-scroll, but the new column is drawn MIRRORED about a
 * centre line instead of colour-coded from the bottom -- a DAW-style envelope building up left to
 * right. ~5 commands a frame, because COPY moves the whole strip for the price of one. */
static void viz_scroll_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint32_t cy = y + h / 2u;
    const uint32_t half = h / 2u - 1u;
    if (paused) return;
    fb_copy(x0 + 1u, y, x0, y, w - 1u, h);

    uint32_t a = (in->peak * half) / 32768u;
    if (a > half) a = half;

    uint32_t cx = x0 + w - 1u;
    ui_bg_restore(cx, y, 1, h);     /* clear column */
    if (a) fb_rect(cx, cy - a, 1, a * 2u + 1u,
                   ui_mix(UI_TRACK, ui_accent, a, half));
    else   fb_rect(cx, cy, 1, 1, UI_TRACK);         /* silence line */
}

/* Spectrum: eight columns of real frequency content from the octave cascade (in->spec, SPEC_BANDS
 * entries). Bass on the left, treble on the right, each moving on its own. */
static void viz_led_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    if (paused)
        for (uint32_t b = 0; b < SPEC_BANDS; b++) spec_lvl[b] = 0;

    /* The gaps between blocks show background, and the background is a per-row ramp -- a flat fill
     * is the mistake the magic eye made. Only on a repaint: the gaps never move. */
    int repaint = (spec_drawn[0] == 0xFFu);
    if (repaint) ui_bg_restore(x0, y, w, h);

    uint32_t colw  = w / SPEC_BANDS;
    uint32_t bw    = (colw > SPEC_GAPX) ? colw - SPEC_GAPX : 1u;
    uint32_t pitch = LED_BLKH + LED_GAPV;

    for (uint32_t b = 0; b < SPEC_BANDS; b++) {
        uint32_t lit  = ((uint32_t)in->spec[b] * LED_ROWS) / 256u;
        uint32_t prev = spec_drawn[b];

        /* Nothing crossed a row boundary: draw nothing at all. In ordinary music most bands are in
         * this state on most updates, which is the whole saving. */
        if (!repaint && lit == prev) continue;

        uint32_t lo = repaint ? 0u : (lit < prev ? lit : prev);
        uint32_t hi = repaint ? LED_ROWS : (lit > prev ? lit : prev);
        spec_drawn[b] = (unsigned char)lit;

        uint32_t bx = x0 + b * colw;
        for (uint32_t r = lo; r < hi; r++) {
            uint32_t by = y + h - (r + 1u) * pitch;
            uint16_t c;
            if (r < lit) {
                uint32_t half = LED_ROWS / 2u;
                c = (r < half)
                  ? ui_mix(LED_LO, LED_MIDC, r, half)
                  : ui_mix(LED_MIDC, LED_HI, r - half,
                           LED_ROWS - half);
            } else {
                c = UI_TRACK;
            }
            fb_rect(bx, by, bw, LED_BLKH, c);
        }
    }
}

/* Peak dots: only the peak-hold markers, no bars -- a row of floating dots tracing the loudness
 * contour. ~2 commands a column and the sparsest mode here. wave_pk[]/wave_pk_drawn[] are the shared
 * envelope-peak history every column-based meter reads, computed once per tick before this dispatch
 * (not part of mtr_in_t -- see this section's own header comment). */
static void viz_dots_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    for (uint32_t i = 0; i < UI_WAVE_N; i++) {
        uint32_t x   = x0 + (i * w) / UI_WAVE_N;
        uint32_t xn  = x0 + ((i + 1u) * w) / UI_WAVE_N;
        uint32_t lit = (xn - x > UI_WAVE_GAP) ? (xn - x - UI_WAVE_GAP) : 1u;
        uint32_t pk  = wave_pk[i];
        if (pk < 2u) pk = 2u;

        /* Same treatment as the mirrored bars: skip an unmoved column, and restore around the dot
         * rather than through it. */
        if (pk == wave_pk_drawn[i]) continue;
        wave_pk_drawn[i] = (unsigned char)pk;

        uint16_t c   = ui_mix(UI_TRACK, ui_accent, i + 1u, UI_WAVE_N);
        uint32_t top = y + h - pk;   /* first dot row */
        uint32_t end = y + h;        /* one past box  */
        if (top > y)
            ui_bg_restore(x, y, lit, top - y);
        if (top + 2u < end)
            ui_bg_restore(x, top + 2u, lit, end - (top + 2u));
        fb_rect(x, top, lit, 2u, c);
    }
}

/* Waterfall: scroll the whole strip one pixel left with a single COPY, then draw only the new
 * right-hand column. That is ~4 commands a frame against the bars' ~72, because COPY moves a block
 * for the price of one command -- the same primitive the album-art slide uses. Colour encodes
 * loudness, so the strip becomes a picture of the track's dynamics rather than an instantaneous
 * reading. */
static void viz_water_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    if (paused) return;
    fb_copy(x0 + 1u, y, x0, y, w - 1u, h);

    uint32_t a = (in->peak * h) / 32768u;
    if (a > h) a = h;

    /* Column drawn as three bands -- quiet bed, body, hot tip -- so loud passages read as brighter
     * AND taller. */
    uint32_t cx = x0 + w - 1u;
    ui_bg_restore(cx, y, 1, h - a);
    if (a) {
        uint16_t c = ui_mix(UI_TRACK, ui_accent, a, h);
        fb_rect(cx, y + h - a, 1, a, c);
        fb_rect(cx, y + h - a, 1, 1, UI_WHITE);
    }
}

/* VU meters: two analogue movements side by side. Geometry comes from `in` every pass rather than
 * being assumed: hiding the album art widens the box from ~246 to ~360, and a fixed layout would leave
 * the pair huddled at the left -- the same trap the waterfall fell into. */
static void viz_vu_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint32_t wf = in->force;
    /* The face -- arc, ticks, labels -- never changes, so it is drawn ONCE and left alone. Clearing
     * the whole box and repainting everything each pass is what made the L and R labels flicker: they
     * were being erased and redrawn while the panel was being scanned out. Only the needle is erased
     * and redrawn now. Width changes with the art panel, and the face geometry is derived from it, so
     * a cached face drawn at another width is stale even if nothing erased it. */
    if (wf || w != vu_face_w) vu_face = 0;

    /* Per ROW. Same fault the magic eye exposed: the box was filled with a flat colour, the gradient
     * sampled once at its top row, which is a flat slab on a ramp that falls to 62% of that value by
     * the bottom. The needles leave most of the box empty, so it shows. */
    if (!vu_face)
        for (uint32_t yy = y; yy < y + h; yy++)
            fb_rect(x0, yy, w, 1, ui_grad_at(yy));

    for (int ch = 0; ch < 2; ch++) {
        uint32_t half = w / 2u;
        uint32_t ox   = x0 + (uint32_t)ch * half;
        uint32_t pivx = ox + half / 2u;
        uint32_t pivy = y + h - 4u;
        uint32_t len  = h - 18u;
        /* Needle stops short of the ticks, so erasing it can never rub them out and they never need
         * repainting. */
        uint32_t nlen = len - 6u;

        uint32_t pkc = ch ? in->peak_r : in->peak_l;
        uint32_t tgt = (pkc * 255u) / 32768u;
        if (tgt > 255u) tgt = 255u;
        uint32_t *v = ch ? &vu_r : &vu_l;
        if (paused) tgt = 0;
        if (tgt > *v) { *v += VU_ATT; if (*v > tgt) *v = tgt; }
        else          { *v = (*v > VU_DEC) ? (*v - VU_DEC) : 0u;
                        if (*v < tgt) *v = tgt; }

        if (!vu_face) {
            for (uint32_t t = 0; t <= 80u; t++) {
                uint32_t q = (t * 16u) / 80u, f = (t * 16u) % 80u;
                uint32_t q1 = (q < 16u) ? q + 1u : 16u;
                int32_t sn = vu_sn[q] + (int32_t)((vu_sn[q1] - vu_sn[q]) * (int32_t)f) / 80;
                int32_t cs = vu_cs[q] + (int32_t)((vu_cs[q1] - vu_cs[q]) * (int32_t)f) / 80;
                int32_t ar = (int32_t)len + 4;
                int32_t ax = (int32_t)pivx + (ar * sn) / 4096;
                int32_t ay = (int32_t)pivy - (ar * cs) / 4096;
                if (ay < (int32_t)y) continue;
                if (ax < (int32_t)ox || ax + 1 >= (int32_t)(ox + half)) continue;
                /* Everything on the face is a TONE OF THE ACCENT. Fixed grey and red meant changing
                 * colour only moved the needle and the labels, and the meter looked unchanged. The
                 * peak zone is the accent at full strength against a dimmed scale, so it still reads
                 * as "the loud end" in any palette. */
                fb_rect((uint32_t)ax, (uint32_t)ay, 2, 2,
                        (t >= 60u) ? ui_accent
                                   : ui_mix(ui_grad_at((uint32_t)ay),
                                            ui_accent, 2u, 5u));
            }
            for (uint32_t t = 0; t <= 4u; t++) {
                uint32_t i = t * 4u;
                for (uint32_t d = 0; d < 4u; d++) {
                    int32_t ar = (int32_t)len - 1 - (int32_t)d;
                    int32_t ax = (int32_t)pivx + (ar * vu_sn[i]) / 4096;
                    int32_t ay = (int32_t)pivy - (ar * vu_cs[i]) / 4096;
                    if (ay < (int32_t)y) continue;
                    fb_rect((uint32_t)ax, (uint32_t)ay, 1, 1,
                            (t >= 3u) ? ui_accent
                                      : ui_mix(ui_grad_at((uint32_t)ay),
                                               ui_accent, 3u, 5u));
                }
            }
            fb_set_color(ui_accent, ui_grad_at(y + 2u));
            fb_text_clipped(ox + 6u, y + 2u, ch ? "R" : "L",
                            TS_1X, TS_1X, 16u);
        }

        uint8_t shown = ch ? vu_shown_r : vu_shown_l;
        uint8_t now   = (uint8_t)*v;
        if (vu_face && now == shown) continue;   /* nothing moved */

        /* Erase the old needle, then draw the new one. Two passes over the same geometry costs less
         * than repainting the face. The erase is skipped on the first draw after the face is laid
         * down, when there is no old needle to remove. */
        for (int pass = vu_face ? 0 : 1; pass < 2; pass++) {
            int32_t  sn, cs;
            vu_angle(pass ? now : shown, &sn, &cs);
            /* One colour throughout its travel. Flashing at the top drew the eye to the loudest
             * moments, which is the opposite of what a meter is for -- the scale already marks the
             * peak zone. The erase pass repaints the needle's own footprint in the BACKGROUND colour,
             * so with a ramp behind it that colour has to be sampled per segment -- a flat fill would
             * leave a lighter trail down the lower half of the sweep, exactly where the needle spends
             * most of its time. */
            for (uint32_t k = 2; k <= VU_STEPS; k++) {
                int32_t rr = ((int32_t)nlen * (int32_t)k) / (int32_t)VU_STEPS;
                int32_t nx = (int32_t)pivx + (rr * sn) / 4096;
                int32_t ny = (int32_t)pivy - (rr * cs) / 4096;
                if (nx < (int32_t)ox || nx >= (int32_t)(ox + half)) continue;
                if (ny < (int32_t)y) continue;
                uint32_t th = (k > VU_STEPS - 6u) ? 1u : 2u;
                fb_rect((uint32_t)nx, (uint32_t)ny, th, th,
                        pass ? ui_accent
                             : ui_grad_at((uint32_t)ny));
            }
        }
        fb_rect(pivx - 2u, pivy - 2u, 5, 5, ui_accent);
        for (uint32_t r = 0; r < 3u; r++)
            fb_rect(pivx - 1u, pivy - 1u + r, 3, 1,
                    ui_grad_at(pivy - 1u + r));

        if (ch) vu_shown_r = now; else vu_shown_l = now;
    }
    vu_face = 1;
    vu_face_w = (uint16_t)w;
}

/* Oscilloscope: one clear, then one vertical rect per column: ~65 commands, fewer than the bars. */
static void viz_wave_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const int32_t ey = (int32_t)(h / 2u) - 1;
    const uint32_t cy = y + h / 2u;

    ui_bg_restore(x0, y, w, h);
    fb_rect(x0, cy, w, 1, UI_TRACK);      /* zero line */

    if (paused) return;
    int32_t prev_y = 0;
    for (uint32_t c = 0; c < WAVE_COLS; c++) {
        uint32_t x  = x0 + (c * w) / WAVE_COLS;
        uint32_t xn = x0 + ((c + 1u) * w) / WAVE_COLS;
        uint32_t cw = (xn > x) ? (xn - x) : 1u;

        int32_t v = (in->wave[c] * ey) / SCOPE_UNIT;
        if (v >  ey) v =  ey;
        if (v < -ey) v = -ey;

        /* Span from the previous sample to this one, so the trace is continuous rather than a row of
         * disconnected marks -- and stays thin, because consecutive samples in a short window are
         * close together. */
        int32_t a = (c == 0) ? v : prev_y;
        int32_t lo = (a < v) ? a : v;
        int32_t hi = (a < v) ? v : a;
        prev_y = v;

        uint32_t top = (uint32_t)((int32_t)cy - hi);
        uint32_t ch = (uint32_t)(hi - lo) + 2u;   /* min 2 px line */
        if (top + ch > y + h) ch = y + h - top;
        uint16_t col = ui_mix(UI_TRACK, ui_accent, c + 1u, WAVE_COLS);
        fb_rect(x, top, cw, ch, col);
    }
}

/* Stereo phase scope: one rect to clear, then one per point: ~65 commands, fewer than the bars. The
 * whole trace is redrawn each pass rather than erased point by point, which would double the count
 * for no gain. scope_x[]/scope_y[]/scope_head are the trail's own accumulated history, not part of
 * mtr_in_t -- same precedent as the phase scope's own kind, wviz_scope_tick's smoothing state. */
static void viz_phase_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint32_t r  = h / 2u;          /* usable radius */
    const uint32_t cx = x0 + w / 2u;
    const uint32_t cy = y + r;

    ui_bg_restore(x0, y, w, h);

    /* Centre cross: without it a quiet passage is an empty box, and there is no way to tell "silent"
     * from "not working". */
    fb_rect(cx, y, 1, h, UI_TRACK);
    fb_rect(x0, cy, w, 1, UI_TRACK);

    if (paused) return;
    const int32_t ex = (int32_t)(w / 2u) - 2;   /* horizontal reach */
    const int32_t ey = (int32_t)r - 2;           /* vertical reach   */
    /* Oldest first, so the newest trace lands on top of the fading ones rather than under them. */
    for (uint32_t age = SCOPE_HIST; age-- > 0; ) {
        uint32_t f = (scope_head + SCOPE_HIST - age) % SCOPE_HIST;
        const signed char *sx = scope_x[f], *sy = scope_y[f];
        /* Blended from the background, so it has to be the background near where the trace actually
         * sits -- the dots cluster around the centre line. Per-dot would cost a call for each of
         * 48 x 4. */
        uint16_t c  = ui_mix(ui_grad_at(cy), ui_accent,
                             SCOPE_HIST - age, SCOPE_HIST);
        uint32_t sz = age ? 1u : 2u;     /* newest trace is fatter */
        for (uint32_t k = 0; k < SCOPE_N; k++) {
            int32_t px = (int32_t)cx + (sx[k] * ex) / SCOPE_UNIT;
            int32_t py = (int32_t)cy - (sy[k] * ey) / SCOPE_UNIT;
            if (px < (int32_t)x0 ||
                px + (int32_t)sz > (int32_t)(x0 + w)) continue;
            if (py < (int32_t)y ||
                py + (int32_t)sz > (int32_t)(y + h)) continue;
            fb_rect((uint32_t)px, (uint32_t)py, sz, sz, c);

            /* Newest trace only: drop a point midway to the next sample so the figure closes into a
             * curve instead of a dotted outline. Only the top layer gets this -- doing it on every
             * frame of history would triple the command count for detail that is fading out anyway. */
            if (!age && k + 1u < SCOPE_N) {
                int32_t qx = (int32_t)cx + (((sx[k] + sx[k+1]) / 2) * ex) / SCOPE_UNIT;
                int32_t qy = (int32_t)cy - (((sy[k] + sy[k+1]) / 2) * ey) / SCOPE_UNIT;
                if (qx >= (int32_t)x0 &&
                    qx + 1 < (int32_t)(x0 + w) &&
                    qy >= (int32_t)y &&
                    qy + 1 < (int32_t)(y + h))
                    fb_rect((uint32_t)qx, (uint32_t)qy, 1, 1, c);
            }
        }
    }
}

/* Classic bars: the default meter. Loop invariants computed once per frame instead of once per
 * changed column: the engine probe, the lit colour and the mirrored geometry. The plain-rectangle
 * fallback for a bitstream without the blit engine is gone: every bitstream that can run the cold code
 * has it (OP_BAR since B-104, in every build since alpha.1). wave[]/wave_pk[]/wave_drawn[]/
 * wave_pk_drawn[] are the shared envelope history every column-based meter reads (not part of
 * mtr_in_t, see this section's own header comment). */
static void viz_bars_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint16_t bg = in->bg;
    blit_probe_ensure();
    const uint16_t lit_c = paused ? ui_mix(UI_TRACK, ui_accent, 1u, 3u) : ui_accent;
    const uint32_t hh = h / 2u, cy = y + hh;
    for (uint32_t i = 0; i < UI_WAVE_N; i++) {
        /* Bar edges come from scaling the index across the full width, so the row always reaches its
         * right edge. */
        const uint32_t x   = x0 + (i * w) / UI_WAVE_N;
        const uint32_t xn  = x0 + ((i + 1u) * w) / UI_WAVE_N;
        const uint32_t lit = (xn - x > UI_WAVE_GAP) ? (xn - x - UI_WAVE_GAP) : 1u;
        uint32_t bh = wave[i];
        if (bars_layout) { bh = (bh * (hh - 1u)) / h; if (bh < 1u) bh = 1u; }   /* mirrored: each half is half the box */
        else if (bh < 2u) bh = 2u;                 /* always show a floor */
        /* Most bars land on the height already drawn there: skip them (one compare instead of a draw
         * command). */
        uint32_t pk = bars_layout ? bh : wave_pk[i];
        if (pk < bh) pk = bh;
        if (bh == wave_drawn[i] && pk == wave_pk_drawn[i]) continue;
        wave_drawn[i]    = (unsigned char)bh;
        wave_pk_drawn[i] = (unsigned char)pk;
        const uint16_t c = ui_mix(UI_TRACK, lit_c, i + 1u, UI_WAVE_N);       /* newest bars brightest */
        if (bars_layout) {
            /* MIRRORED: two OP_BAR per changed column. The upper half is an ordinary bar (lit rows at
             * its bottom, the centre line); the lower half is the same bar inverted -- OP_BAR always
             * lights the bottom of its span, so swap the colours and light the EMPTY part (top h rows
             * the bar colour, the rest the bed). The bed is the dim track colour. */
            fb_bar(x, y, lit, hh, bh, c, UI_TRACK);
            fb_bar(x, cy, lit, hh, hh - bh, UI_TRACK, c);
            continue;
        }
        fb_bar(x, y, lit, h, bh, c, bg);
        if (pk > bh + 1u)                       /* 1 px peak-hold marker */
            fb_rect(x, y + h - pk, lit, 1, UI_WHITE);
    }
}

/* Winamp bars/scope drawing, factored out of ui_draw_dynamic_cold()'s own
 * VIZ_WINAMP_BARS/VIZ_WINAMP_SCOPE blocks (below) so the Settings > Meter >
 * Configure page (fw/settingsui.inc, B-215/B-216) can render the SAME live,
 * audio-reactive meter at its own position instead of a second copy of this
 * logic -- one source of truth for both places. Explicit (x0, y, w, h)
 * rather than reading UI_MARGIN/UI_WAVE_Y/ww/UI_WAVE_H directly, precisely
 * so the Configure page can pin the preview wherever its own layout wants. */
COLD_FN3 static void wviz_bars_tick(const mtr_in_t *in)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint16_t bg = in->bg;
    const uint32_t force = in->force;
    uint32_t bands = MV_WINAMP_BARS(BANDS);
    if (bands < WVIZ_BANDS_MIN) bands = WVIZ_BANDS_MIN;
    if (bands > WVIZ_BANDS_MAX) bands = WVIZ_BANDS_MAX;
    uint32_t gap  = 2u;
    uint32_t colw = (w > gap * (bands - 1u)) ? (w - gap * (bands - 1u)) / bands : 1u;

    /* B-234: context just changed (preset applied, mode switched, page opened/
     * closed) -- wipe the whole preview rect once so no leftover pixels from a
     * DIFFERENT geometry or a different mode's draw survive, then force every
     * band to redraw below regardless of the change cache. */
    if (force) fb_rect(x0, y, w, h, bg);

    for (uint32_t b = 0; b < bands; b++) {
        uint32_t target = mtr_band_target(in->spec, SPEC_BANDS, bands, b);
        if (paused) target = 0u;

        uint32_t rate = (target >= wviz_disp[b]) ? MV_WINAMP_BARS(ATTACK) : MV_WINAMP_BARS(RELEASE);
        wviz_disp[b] = mtr_ease(wviz_disp[b], (uint8_t)target, MV_WINAMP_BARS(EASE), rate, &wviz_vel[b]);

        const mtr_peak_cfg_t pcfg = { MV_WINAMP_BARS(PEAK_ON), MV_WINAMP_BARS(PEAK_GRAVITY), MV_WINAMP_BARS(PEAK_HOLD_MS), MV_WINAMP_BARS(PEAK_FALL) };
        mtr_peak_step(&wviz_pk[b], wviz_disp[b], &pcfg, in->dt_ms);

        if (!mtr_delta(&wviz_drawn[b], &wviz_peak_drawn[b], wviz_disp[b], wviz_pk[b].peak, force)) continue;

        uint32_t x = x0 + b * (colw + gap);
        uint32_t bh = (wviz_disp[b] * h) / 255u;
        if (bh < 2u) bh = 2u;

        blit_probe_ensure();
        if (BLIT_READY()) {
            fb_bar(x, y, colw, h, bh, ui_accent, bg);
        } else {
            fb_rect(x, y + h - bh, colw, bh, ui_accent);
            if (h > bh) fb_rect(x, y, colw, h - bh, bg);
        }
        if (MV_WINAMP_BARS(PEAK_ON)) {
            uint32_t ph = (wviz_pk[b].peak * h) / 255u;
            if (ph > bh + 1u && ph < h)
                fb_rect(x, y + h - ph, colw, 1u, UI_WHITE);
        }
    }
    wviz_force = 0u;
}

/* Classic Winamp oscilloscope -- reuses wav_v[]/SCOPE_UNIT and the span-per-
 * column draw exactly as VIZ_WAVE does, plus temporal smoothing. scope_trail
 * is accepted but has no visible effect yet: a real soft trail needs B5
 * alpha blend (shelved). Explicit geometry, same reason as wviz_bars_tick().
 *
 * B-234: `use_gradient` picks how the trace's old position is erased.
 * ui_bg_restore() copies from a gradient strip pre-rendered ONLY for the
 * player screen's own UI_WAVE_Y row range (fw/player.c's UI_BG_X/UI_BG_W
 * comment) -- calling it with a `y` outside that range (as the Configure
 * page's own preview position does) reads whatever garbage happens to sit in
 * that off-screen memory for those rows, which is the reported "Scope
 * preview garbled." The player screen passes use_gradient=1 (its call site's
 * y IS UI_WAVE_Y, so the cache is valid there); Configure passes 0 and a flat
 * panel colour, matching how wviz_bars_tick() already takes an explicit bg
 * for exactly this reason. */
COLD_FN3 static void wviz_scope_tick(const mtr_in_t *in, int use_gradient)
{
    const uint32_t x0 = in->x, y = in->y, w = in->w, h = in->h;
    const uint16_t bg = in->bg;
    const int32_t  ey = (int32_t)(h / 2u) - 1;
    const uint32_t cy = y + h / 2u;

    /* B-234: context just changed -- re-seed the smoothing state cleanly
     * instead of lerping from a stale wviz_scope_y[] left over from a
     * different geometry or a different preset's smoothing amount. */
    if (in->force) { wviz_scope_init = 0u; wviz_force = 0u; }

    /* B-298/B-300/B-301 STOPGAP: the hardware wave path below draws up to 256 columns x up to 3
     * fb_rect() calls each (measured DRAW STALL 34,663 ms cumulative, CPU LOAD 100%, real audible
     * jitter on hardware) -- about 21x wviz_bars_tick()'s own baseline. Forced off here, keeping the
     * 64-column software path (tools/meter_cost_estimate.py: 198 commands, within budget) until the
     * real fix -- batching the hardware path's per-column draws -- is designed and built. Flip this
     * back to `if (wave_hw)` once that lands; do not remove the hardware branch below, it is the
     * higher-resolution path this was always meant to be. */
    if (0 && wave_hw) {
        /* B-283: the hardware capture -- 256 columns, each the min..max envelope of the mono mix over 2 samples, started at
         * its own rising zero crossing. Normalised to the PREVIOUS frame's peak (the software path uses the current
         * frame's; the peak of consecutive windows barely moves, and this needs no second pass). The capture is re-armed
         * after every draw and read next time round; while it is still running the last picture stays. */
        static uint32_t sc_pk = 1u;
        static uint8_t  sc_armed;
        if (!sc_armed) { REG(R_WAVE_CTL) = 2u | (1u << 8); sc_armed = 1u; return; }   /* nothing captured yet */
        if (!paused && !(REG(R_WAVE_ST) & 2u)) {
            const int32_t smooth = MV_WINAMP_SCOPE(SCOPE_SMOOTH);
            const int percol = ui_fullscreen;               /* fullscreen: erase per column, see the software path */
            if (!percol) {
                if (use_gradient) ui_bg_restore(x0, y, w, h);
                else              fb_rect(x0, y, w, h, bg);
                fb_rect(x0, cy, w, 1, UI_TRACK);
            }
            uint32_t npk = 1u;
            for (uint32_t c = 0; c < WAVE_HW_COLS; c++) {
                REG(R_WAVE_IDX) = c;
                const uint32_t d = REG(R_WAVE_DATA);
                int32_t mn = (int16_t)(d >> 16), mx = (int16_t)(d & 0xFFFFu);
                const uint32_t am = (uint32_t)(mn < 0 ? -mn : mn), ax = (uint32_t)(mx < 0 ? -mx : mx);
                if (am > npk) npk = am;
                if (ax > npk) npk = ax;
                int32_t lo = (mn * ey) / (int32_t)sc_pk, hi = (mx * ey) / (int32_t)sc_pk;
                /* Both bounds, both variables: a DC-biased or unusually polarised capture window can
                 * make `lo` come out positive or `hi` come out negative, and either one being clamped
                 * on only one side let it escape [-ey, ey] -- which is exactly what drew outside the
                 * meter's own box (real corruption seen on hardware, not a timing/RTL issue). */
                if (lo < -ey) lo = -ey; else if (lo > ey) lo = ey;
                if (hi < -ey) hi = -ey; else if (hi > ey) hi = ey;
                if (lo > hi) lo = hi;
                int32_t mid = (lo + hi) / 2, half = (hi - lo) / 2;
                if (!wviz_scope_init) wviz_scope_y[c] = (int16_t)mid;
                wviz_scope_y[c] = (int16_t)(wviz_scope_y[c] + (((mid - wviz_scope_y[c]) * (100 - smooth)) / 100));
                mid = wviz_scope_y[c];
                const uint32_t cx = x0 + (c * w) / WAVE_HW_COLS, cxn = x0 + ((c + 1u) * w) / WAVE_HW_COLS;
                /* Defence in depth: clamp the whole rect to the box in SIGNED space before ever
                 * casting to uint32_t, so a value that still somehow escaped the lo/hi clamp above
                 * clips to the box instead of wrapping to a huge address (a negative int32_t cast to
                 * uint32_t is how a bounds miss here turns into drawing at a garbage location). */
                int32_t s_top = (int32_t)cy - (mid + half), s_bot = s_top + (2 * half) + 2;
                if (s_top < (int32_t)y)     s_top = (int32_t)y;
                if (s_bot > (int32_t)(y + h)) s_bot = (int32_t)(y + h);
                if (s_bot < s_top) s_bot = s_top;
                const uint32_t top = (uint32_t)s_top, rh = (uint32_t)(s_bot - s_top);
                const uint32_t cwid = (cxn > cx) ? (cxn - cx) : 1u;
                if (percol) { fb_rect(cx, y, cwid, h, bg); fb_rect(cx, cy, cwid, 1, UI_TRACK); }
                fb_rect(cx, top, cwid, rh, ui_accent);
            }
            sc_pk = npk;
            wviz_scope_init = 1u;
            REG(R_WAVE_CTL) = 2u | (1u << 8);              /* arm the next capture: 2 samples per column */
        }
        if (paused) wviz_scope_init = 0u;
        return;
    }


    /* Fullscreen: erase COLUMN BY COLUMN, each just before its own trace segment, instead of one clear of the whole box. A
     * whole-box clear is only tear-free if the beam is kept out of the way (which is what starved the fullscreen frame rate), and
     * leaves a blank frame on screen if the beam catches it between the clear and the redraw. Per column, every column is always
     * either the old picture or the new one. The same pixel count; more, smaller commands. */
    const int percol = ui_fullscreen;
    if (!percol) {
        /* B-334: a trail. With the blend bitstream the old trace is faded toward the background instead of erased: `trail` % of it survives
         * each frame (the background strip is blended over the box with the remaining weight). 0, no blend bitstream, or the Configure preview
         * (flat background) erases as before.
         * B-413: neither the ui_bg_ready race (B-411) nor the sticky-base fence audit (B-412) fixed the
         * owner's reported "accumulates after a fullscreen/Configure visit" -- rather than guess a third
         * time, capture WHICH branch actually ran, live, so the next repro tells us directly instead of
         * needing another theory. Counted only when a blend was genuinely attempted (use_gradient && trail
         * && !paused all true), matching did_blend's own short-circuit exactly -- fullscreen/Configure's own
         * legitimate use_gradient=0 skip is not counted as a "failure" here, only a real attempted-and-failed
         * blend is. */
        const uint32_t trail = (uint32_t)MV_WINAMP_SCOPE(SCOPE_TRAIL);
        const int attempt = use_gradient && trail && !paused;
        const int did_blend = attempt && ui_bg_blend(x0, y, w, h, (100u - trail) * 256u / 100u);
        if (attempt) { if (did_blend) dbg_scope_blend_ok++; else dbg_scope_blend_fail++; dbg_strip_check();
                       dbg_pixel_log(x0 + w / 2u, y + 8u); }   /* B-449: B-447's (cy-4, centre column) spot
                       STILL read a dark, gradient-consistent value even during an owner-confirmed active
                       repro -- a real screenshot of the accumulation showed the actual bright mass sits
                       in the box's UPPER region (solid across nearly the full width there), not near the
                       centreline; the centre column at cy-4 most likely just hit a real local amplitude
                       dip for that one column, not a diagnostic bug. Moved to (x0+w/2, y+8) -- near the
                       box's own TOP edge, in the region the screenshot shows solidly filled regardless
                       of which column is sampled. */
        if (!did_blend) {
            if (use_gradient) ui_bg_restore(x0, y, w, h);
            else              fb_rect(x0, y, w, h, bg);
        }
        fb_rect(x0, cy, w, 1, UI_TRACK);
    }

    if (!paused) {
        int32_t smooth = MV_WINAMP_SCOPE(SCOPE_SMOOTH);      /* 0..90 */
        int32_t prev_y = 0;
        for (uint32_t c = 0; c < WAVE_COLS; c++) {
            uint32_t cx  = x0 + (c * w) / WAVE_COLS;
            uint32_t cxn = x0 + ((c + 1u) * w) / WAVE_COLS;
            uint32_t cw  = (cxn > cx) ? (cxn - cx) : 1u;

            int32_t raw = (in->wave[c] * ey) / SCOPE_UNIT;
            if (raw >  ey) raw =  ey;
            if (raw < -ey) raw = -ey;
            if (!wviz_scope_init) wviz_scope_y[c] = (int16_t)raw;
            wviz_scope_y[c] = (int16_t)(wviz_scope_y[c]
                             + (((raw - wviz_scope_y[c]) * (100 - smooth)) / 100));
            int32_t v = wviz_scope_y[c];

            int32_t a  = (c == 0) ? v : prev_y;
            int32_t lo = (a < v) ? a : v;
            int32_t hi = (a < v) ? v : a;
            prev_y = v;

            /* Same defence in depth as the hardware path above: clamp in signed space before the
             * uint32_t cast, so this can never draw outside its own box. */
            int32_t s_top = (int32_t)cy - hi, s_bot = s_top + (hi - lo) + 2;
            if (s_top < (int32_t)y)     s_top = (int32_t)y;
            if (s_bot > (int32_t)(y + h)) s_bot = (int32_t)(y + h);
            if (s_bot < s_top) s_bot = s_top;
            const uint32_t top = (uint32_t)s_top, rh = (uint32_t)(s_bot - s_top);
            if (percol) { fb_rect(cx, y, cw, h, bg); fb_rect(cx, cy, cw, 1, UI_TRACK); }
            fb_rect(cx, top, cw, rh, ui_accent);
        }
        wviz_scope_init = 1u;
    }
}

/* Forward declarations: chladni.inc/vu_master.inc are #included later in this file (after ov_frame()),
 * but B-415 needs to call both from here. */
static int  chladni_tick_box(const mtr_in_t *in);
static void vum_tick(const mtr_in_t *in);
static void lw_tick(const mtr_in_t *in);

/* Draws the meter `viz` into an arbitrary rect against a flat background: the Configure page's live preview. The one hand-written binding
 * between a generated parameter module and its drawing function (a meter module's `tick`, docs/METER_MODULE_SPEC.md section 3). */
/* B-415 (owner-reported: Chladni and VU Master "don't show up in the preset configure screen"):
 * the comment this replaces claimed their drawing is refused while an overlay is up -- true of a
 * plain fb_rect()/fb_bar() call with no ov_draw of its own, but wrong for THIS call site: the only
 * caller, wvcfg_preview_tick() (fw/settingsui.inc), already sets ov_draw=1 around its whole call to
 * mtr_preview(), the exact same way it does for Winamp Bars/Scope just above, which DO draw
 * correctly here. Whatever originally motivated excluding these two was either never actually true
 * for this call site or stopped being true once wvcfg_preview_tick() gained its own ov_draw handling
 * -- either way, chladni_tick_box()/vum_tick() take the identical mtr_in_t* signature as the two
 * meters that already work here, so wiring them in is the same pattern, not new plumbing. */
COLD_FN3 static void mtr_preview(uint32_t viz, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t bg)
{
    const mtr_in_t in = mtr_build(x, y, w, h, bg, wviz_force);
    if (viz == VIZ_WINAMP_SCOPE)      wviz_scope_tick(&in, 0);
    else if (viz == VIZ_WINAMP_BARS)  wviz_bars_tick(&in);
    else if (viz == VIZ_CHLADNI)      (void)chladni_tick_box(&in);
    else if (viz == VIZ_VU_MASTER)    vum_tick(&in);
    else if (viz == VIZ_LAYERED_WAVE) lw_tick(&in);
}

static void ui_draw_dynamic(void);
/* fw/cold.inc defines both of these (B-199..B-201, PHASE_F_SPEC.md sections 4.2-4.3) -- forward-
 * declared for the same textual-ordering reason as blit_probe.inc above: cold.inc's own #include
 * comes much later in this file. Both are themselves hot code; coldframe_tick() calls INTO a
 * synthetic cold probe, coldframe_record() just accumulates a cycle count the real cold
 * ui_draw_dynamic_cold() call (below) already measured itself. */
static void coldframe_tick(void);
static void coldframe_record(uint32_t dt);

/* The player-screen loader: eight dots turning round the middle of the album art plate, with "Loading track" under
 * them, the pair centred in the plate (owner request). Drawn every tick over the plate colour; the caption is drawn once
 * and again after any repaint (ui_loader_txt). */
static void ui_loader_tick(uint32_t t)
{
    static const signed char dot[8][2] = { {0,-20}, {14,-14}, {20,0}, {14,14}, {0,20}, {-14,14}, {-20,0}, {-14,-14} };
    const uint32_t cx = ART_X + ART_W / 2u;
    const uint32_t top = ART_Y + (ART_H - 88u) / 2u;          /* spinner 48 px + gap 8 px + two caption lines 32 px, centred */
    const uint32_t cy = top + 24u;
    for (uint32_t i = 0; i < 8u; i++) {
        uint32_t dist = (t + 8u - i) & 7u;                     /* 0 = the leading dot */
        uint32_t l = 31u - dist * 4u;
        if (l < 5u) l = 5u;
        ui_icon_dot(cx + dot[i][0] - 3u, cy + dot[i][1] - 3u, ui_mix(UI_PANEL, ui_accent, l, 31u));
    }
    if (ui_loader_txt) {
        ui_loader_txt = 0;
        /* Two centred lines so the words stay inside the 104 px plate. */
        uint32_t w1 = fb_text_width("Loading", TS_1X), w2 = fb_text_width("track", TS_1X);
        fb_set_color(UI_DIM, UI_PANEL);
        fb_text_clipped(cx - w1 / 2u, top + 56u, "Loading", TS_1X, TS_1X, w1 + 4u);
        fb_text_clipped(cx - w2 / 2u, top + 72u, "track", TS_1X, TS_1X, w2 + 4u);
    }
}

/* Draw the whole player frame and start the loader. with_text = 0: nothing is known about the track yet, its text rows are left
 * blank; 1: the title, artist and album are already known (a library track) and are drawn. */
static void ui_loader_begin_ex(int with_text)
{
    ui_boot_t = 0;
    ui_boot_next = cycles();
    ui_toast_t0   = 0;                                          /* the loader is the only status now */
    ui_toast_step = 0;
    ui_loader_on = 1u;
    ui_loading = with_text ? 0u : 1u;
    art_shown = art_pref ? 1u : 0u;
    art_x = art_shown ? ART_X : FB_W;
    /* B-075: NOT ui_art_mount() here. It wipes the off-screen stash (the only copy of the decoded picture), and the
     * art code below reuses that stash without redrawing when the new track shares the previous one's cover -- true on
     * every track of an album. Wiping it here left the panel blank on every track after the first: a fresh decode
     * refilled it, but nothing ever repainted it in the meantime. The load below decides, per track, whether the
     * stash needs clearing (a genuinely different or bad cover) or can stay as it is (the common case); this call
     * used to pre-empt that decision on every library track, whatever the outcome turned out to be.
     * The plate briefly shows the OUTGOING track's cover under the spinner instead of a neutral grey -- correct far
     * more often than it is not, since most tracks share their album's cover, and only ever wrong for the length of
     * one load. */
    ui_chrome_paint();
    ui_loading = 0u;
    {   /* transport row, clock and progress bar with nothing elapsed */
        uint32_t s0 = ui_sec, t0 = track_secs;
        ui_sec = 0; track_secs = 0;
        ui_draw_dynamic();
        ui_sec = s0; track_secs = t0;
    }
    ui_loader_txt = 1u;
}

static void ui_loader_begin(void) { ui_loader_begin_ex(0); }

static void ui_boot_note(const char *msg)
{
    ui_boot_msg  = msg;
    ui_boot_t    = 0;
    /* cycles(), not 0. The tick tests (int32_t)(cycles() - ui_boot_next) < 0,
     * which with 0 reduces to the SIGN OF THE COUNTER -- so for the half of
     * every 71.6 s wrap where cycles() is above 2^31 the tick returned early,
     * never painted, and never updated the deadline either. The dots were dead
     * for the whole of roughly every other load, which is why they were
     * "rarely seen". Same fault as fl_ui_next and pl_poll_at in c501764. */
    ui_boot_next = cycles();                /* first tick paints immediately */

    if (ui_splash_art_active) {
        /* Splash: the loading bar only, no words (owner request). */
        fb_rect(TAU_SPLASH_STATUS_X, TAU_SPLASH_STATUS_Y,
                TAU_SPLASH_STATUS_W, TAU_SPLASH_STATUS_H, TAU_SPLASH_BG);
        fb_rect(TAU_SPLASH_BAR_X, TAU_SPLASH_BAR_Y,
                TAU_SPLASH_BAR_W, TAU_SPLASH_BAR_H, TAU_SPLASH_BG);
        return;
    }
    (void)msg;
    ui_loader_begin();                      /* the player screen: full frame, loader in the art plate */
}

static void ui_boot_tick(void)
{
    if (!ui_boot_msg) return;
    if ((int32_t)(cycles() - ui_boot_next) < 0) return;
    ui_boot_next = cycles() + CLK_HZ / 30u;

    if (ui_splash_art_active) {
        uint32_t travel = TAU_SPLASH_BAR_W - TAU_SPLASH_SEG_W;
        uint32_t phase = ui_boot_t++ % (travel * 2u);
        uint32_t x = phase <= travel ? phase : travel * 2u - phase;
        fb_rect(TAU_SPLASH_BAR_X, TAU_SPLASH_BAR_Y,
                TAU_SPLASH_BAR_W, TAU_SPLASH_BAR_H, TAU_SPLASH_BG);
        fb_rect(TAU_SPLASH_BAR_X + x, TAU_SPLASH_BAR_Y,
                TAU_SPLASH_SEG_W, TAU_SPLASH_BAR_H, TAU_SPLASH_BAR_C);
        return;
    }

    /* Player screen (not the splash): the loader in the art plate, never the old three dots on the transport row. If no frame
     * was started for it (a note armed over a frame that was drawn some other way) the spinner simply draws over the plate. */
    ui_loader_on = 1u;
    ui_loader_tick(ui_boot_t++);
    return;

    uint16_t bg = ui_boot_bg();
    uint32_t dy = UI_BOOT_Y + (FB_CELL(TS_1X) - 7u) / 2u;   /* centred on the text */
    uint32_t t  = ui_boot_t++ % UI_DOT_TICKS;

    /* No erase pass: every dot is redrawn every tick at a fixed position, so
     * each one covers its own footprint. Erase-then-draw on a single-buffered
     * framebuffer is what makes things flicker when scanout catches the gap. */
    for (uint32_t i = 0; i < UI_DOT_N; i++) {
        uint32_t dist = (t + UI_DOT_TICKS - i * UI_DOT_STEPS) % UI_DOT_TICKS;
        uint32_t l    = (dist < UI_DOT_TAIL) ? (31u - (dist * 31u) / UI_DOT_TAIL)
                                             : 0u;
        /* A floor rather than skipping the dim ones: three dots that are always
         * present read as a three-dot indicator, where two-plus-a-gap reads as
         * something missing. The arrows can afford to vanish; a 7 px disc
         * cannot. */
        if (l < UI_DOT_FLOOR) l = UI_DOT_FLOOR;
        ui_icon_dot(ui_boot_x + i * (UI_DOT_W + UI_DOT_GAP), dy,
                    ui_mix(bg, ui_accent, l, 31u));
    }
}

static void ui_boot_clear(void)
{
    if (!ui_boot_msg) return;
    ui_boot_msg = 0;
    ui_loader_on = 0;
    if (ui_splash_art_active) {
        fb_rect(TAU_SPLASH_STATUS_X, TAU_SPLASH_STATUS_Y,
                TAU_SPLASH_STATUS_W, TAU_SPLASH_STATUS_H, TAU_SPLASH_BG);
        fb_rect(TAU_SPLASH_BAR_X, TAU_SPLASH_BAR_Y,
                TAU_SPLASH_BAR_W, TAU_SPLASH_BAR_H, TAU_SPLASH_BG);
        return;
    }
    fb_rect(UI_MARGIN, UI_BOOT_Y, UI_INNER_W, FB_CELL(TS_1X), ui_boot_bg());
}

/* Disarm WITHOUT repainting the row -- for callers whose next step redraws the
 * screen anyway.
 *
 * load_track() calls ui_draw_chrome() partway through, so by the time it
 * returns the player is already on screen and ui_boot_clear()'s wipe would
 * punch a gradient-coloured rectangle straight through the transport row.
 * Disarming still matters: ui_boot_tick() runs inside every blocking read, so
 * a note left armed would keep painting dots over the player forever. */
static void ui_boot_cancel(void) { ui_boot_msg = 0; ui_loader_on = 0; }

static inline uint32_t dt_read(uint32_t word);   /* defined with the file-open code, playlist.inc */

/* APF's datatable exactly as it stood before this core touched it. 1 KB, taken
 * once at boot, because by the time anyone can press a button our own 0190 has
 * already overwritten words 0..63. */
/* 1 KB, and ONLY the Select+A dump ever reads it -- which is behind
 * DEBUG_DIAG. In a release build this was a kilobyte of BSS taken from
 * the heap Helix mallocs its decoder out of, to feed a screen that
 * cannot be reached. */

static void dt_snapshot(void)
{
}

/* APF's dataslot ID/size table, BOOT value against LIVE value.
 *
 * The table is {slot_id, size} pairs at stride 2 from word 0 -- measured, not
 * assumed. Our 0190 response struct used to sit at word 0 and APF overwrote 64
 * words there on every getfile, destroying the table on the first track change.
 * The structs now live at words 64+.
 *
 * This screen is the verification: change track, then press Select+A. If BOOT
 * and LIVE still agree, the table survived and the fix holds. If LIVE has
 * turned into path characters, something is still writing over it.
 */

static void ui_gs_line(uint32_t y, const char *s, uint16_t fg, uint32_t ts)
{
    fb_set_color(fg, ui_grad_at(y));
    fb_text_clipped(UI_MARGIN, y, s, ts, ts, UI_INNER_W);
}

/* Shown when there is nothing to play: no library index has ever been built, or building one
 * failed. It is the first thing a new user sees, so it is a getting-started card rather than a
 * bare error -- the steps are the whole setup, in the order they have to happen. Widths were
 * measured against font_metrics.h; the widest line is 310 px of the 360 available.
 *
 * Rewritten when legacy playlist mode (copy files loose onto the card, list them in a
 * playlist.m3u, Load MP3/Load Playlist from the core menu) was removed: the media library is now
 * the only way to play more than one file, so these steps describe building and using it instead. */
/* Nothing playing but a library is loaded: point at it instead of the no-library getting-started steps. */
static void ui_idle_library(void)
{
    ui_gs_line(170u, "Library ready", ui_accent, TS_15X);
    ui_gs_line(214u, "Press the Select button to", UI_WHITE, TS_1X);
    ui_gs_line(232u, "browse and play your music.", UI_WHITE, TS_1X);
}

COLD_SR static void ui_idle_screen(const char *reason)     /* B-333: cold code */
{
    /* The authored image is a loading state, not an empty/error screen.  Keep
     * the inherited getting-started layout readable when there is no media. */
    ui_splash_art_active = 0u;
    ui_gradient();
    ui_splash_card(1u, 1u);
    if (lib_state == LIB_ST_OK) { ui_idle_library(); return; }

    /* Sits between the card (ends at 136) and the heading (170), 8 px clear of
     * each. At 148 it crowded the heading and read as part of it rather than
     * as a separate alert. */
    if (reason) ui_gs_line(144u, reason, UI_RED, TS_1X);

    ui_gs_line(170u, "Getting started",                     ui_accent, TS_15X);

    /* 18 px within a step, 26 between them. An even pitch throughout made the
     * steps read as one block -- the grouping has to be visible or the
     * numbers are doing all the work. */
    /* Colour carries meaning here, so it follows one rule: prose is white,
     * literal values and menu names are grey. */
    ui_gs_line(206u, "1  Copy your music to your SD card:",  UI_WHITE,  TS_1X);
    ui_gs_line(224u, "   /Assets/tau/common/",               UI_DIM,    TS_1X);

    ui_gs_line(250u, "2  Run the sync tool (see the README)", UI_WHITE, TS_1X);
    ui_gs_line(268u, "   to build tau-library.tdb.",          UI_WHITE, TS_1X);

    ui_gs_line(294u, "3  Press Select to browse and play.",  UI_WHITE,  TS_1X);
}

/* The failure screen, with the two explanatory lines supplied by the caller.
 *
 * "THE FILE COULD NOT BE READ" is right for a file that genuinely would not
 * read, and wrong for one this core simply cannot decode fast enough -- that
 * is not a broken file, and telling someone their music is corrupt when it is
 * not is worse than saying nothing. */
static void ui_failed_msg(const char *l1, const char *l2)
{
    /* Wake first if the screen is dark. This draws straight to the framebuffer
     * rather than through ui_draw_dynamic(), so without this it would paint
     * over a blanked screen while screen_blank stayed set -- lit, but frozen,
     * with every later update still suppressed and nothing to clear it.
     *
     * Waking is also the right call on its own terms. Blanking exists so
     * routine events -- track changes, meters, toasts -- do not light the panel
     * while music plays. Playback having STOPPED is not routine, and a silent
     * player behind a dark screen gives the user nothing to act on. The idle
     * timer restarts from here, so it blanks again on its own. */
    ui_blank_wake();
    ui_splash_art_active = 0u;
    fb_rect(0, 0, FB_W, FB_H, UI_BG);
    fb_set_color(UI_RED, UI_BG);
    fb_text_clipped(UI_MARGIN, UI_TITLE_Y, "LOAD FAILED", TS_2X, TS_2X, UI_INNER_W);

    /* This line used to print the file's first four bytes as hex. That is a
     * debugging aid, and it was on the ONE screen a user is most likely to see
     * when something has gone wrong -- the moment they least need a hex dump.
     * Say what happened in words instead. */
    fb_set_color(UI_DIM, UI_BG);
    fb_text_clipped(UI_MARGIN, UI_TITLE_Y + FB_CELL(TS_2X) + 14u,
                    l1, TS_1X, TS_1X, UI_INNER_W);
    fb_text_clipped(UI_MARGIN, UI_TITLE_Y + FB_CELL(2u) + 40u,
                    l2, TS_1X, TS_1X, UI_INNER_W);

    /* Stay alive so a reload can rescue us.
     *
     * poll_input() only sees the 008A NOTIFICATION, so a missed one left this
     * screen with no way out and the next pick appeared to do nothing -- pick
     * again and it works. Ask the slot directly as well. No audio is playing
     * here, so the query costs nothing that matters and can run often. */
    tk_poll_at = cycles() + CLK_HZ;
    for (;;) {
        poll_input();
        if (reload_pending) return;     /* a file was picked: the main loop handles it */
        if ((int32_t)(cycles() - tk_poll_at) >= 0) {
            tk_poll_at = cycles() + CLK_HZ;
            if (slot_changed()) reload_pending = 1u;
        }
    }
}

static void ui_load_failed(void)
{
    ui_failed_msg("THE FILE COULD NOT BE READ", "USE CORE MENU TO PICK");
}

/* Hi-res FLAC this core cannot decode in realtime.
 *
 * The cutoff is 48 kHz, and it is measured rather than chosen. Decode cost as
 * a percent of realtime, with I/O already at zero: 16/44.1 = 74, 24/44.1 = 80,
 * 24/88.2 = 150, 24/96 = 180. Cost tracks sample rate almost exactly, so 48 kHz
 * lands near 87 for 24-bit and everything above it is beyond reach -- 88.2 kHz
 * would need the decoder to be half again faster, and the largest single
 * optimisation available bought 1.3x on one of its two passes.
 *
 * Refusing is the kind thing to do. These files DO decode, just not fast
 * enough, so without this they play through to the end sounding broken -- and
 * a listener has no way to tell that from a damaged file or a broken core. */
/* 48000, from measurement: decode is 74% of realtime at 16/44.1 and 80% at
 * 24/44.1, rising almost exactly with sample rate to 150% at 88.2 kHz. 48 kHz
 * lands near 87% for 24-bit, which fits; nothing above it does. */
#define FLAC_MAX_RATE 48000u
static uint8_t rate_unsupported;   /* set at load, consumed by the main loop */
#if TAU_DIAGNOSTIC
/* Settings > Diagnostics > ACCEPT ALL RATES (default OFF). Bypasses ONLY the measured-performance
 * FLAC_MAX_RATE cutoff above -- deliberately not FLR_CHANS/FLR_DEPTH/FLR_BLOCK, which come from
 * flac_open() itself refusing a shape its decoder does not support at all (2 channels max, specific bit
 * depths), a hard capability boundary rather than a performance choice, and not safe to bypass the same
 * way. Lets a file like a 96kHz FLAC load and run far enough to get a real SR_T_DECPROF/DECPROF2 reading
 * (2026-09-28, B-353/B-356) -- it will very likely underrun above 48kHz per the measurement in the comment
 * above; that is the expected, useful result of turning this on, not a bug. Diagnostic-build-only, and off
 * by default even there, so it can never affect a normal listening session by accident. */
static uint8_t flac_accept_all_rates;

/* Settings > Diagnostics > CYMO RESAMPLER (default OFF). First-ever hardware test of the real 44.1:48
 * polyphase FIR resampler (B-471..B-478) -- hands the live audio path (EQ input) from pcm_fifo's own
 * zero-order hold to the resampler's output via mp3_soc.v's R_CYMO_CTRL bit 2 (sticky LIVE_ENABLE).
 * Diagnostic-build-only, off at every boot, never persisted -- same convention as TG_RATES/TG_SPEEDS:
 * this is a test switch for the owner's own hardware A/B, not a listening preference, until it has been
 * proven on real silicon. No-op if CYMO_RESAMP_READY() is false (the write lands on an unmapped
 * register on any bitstream without the unit, same inert-when-absent convention as every other probe
 * here). */
static uint8_t cymo_live_toggle;
#endif

/* Shown ON THE TRACK CARD rather than as a takeover screen. The card is
 * already where this player explains what is loaded, the filename still reads
 * as the title, and the layout does not jump -- a full-screen LOAD FAILED for
 * a file that is perfectly good is a heavier response than the situation
 * deserves. */
static void ui_rate_unsupported(void)
{
    /* KEEP the title and artist when we have them. For a rate rejection the
     * Vorbis comments were already parsed -- the gate runs after flac_open --
     * so the card reads "Jerry Garcia / Alabama Getaway" with the reason under
     * it. The other three reasons fire inside flac_open, before the comment
     * block is reached, so those fall back to the filename. */
    track_year[0] = 0;
    track_trk[0]  = 0;

    /* "NO PLAY: ..." leads every reason -- the earlier wording ("HI-RES 96kHz -
     * PLAYS 48kHz MAX") read as a capability statement ("it plays, capped at
     * 48kHz") when it means the opposite: this file will not play at all, and
     * the number after it is the core's ceiling, not what's about to happen.
     * Owner-reported: a real 96kHz FLAC read as a "cryptic" refusal for exactly
     * this reason. Kept short (shorter than the old wording in every case, not
     * just clearer) since this row has no marquee/scroll of its own and a long
     * line already clips against the art panel's column width -- a real,
     * separate layout gap (fb_text_boxed() here vs the title/artist's own
     * scrolling ui_mq_title/ui_mq_artist), not fixed by this wording change,
     * flagged but out of scope for a one-line message edit. */
    {
        char *q = track_album;
        uint32_t v = fl_reject_val;
        const char *p0 = "NO PLAY: ";
        while (*p0) *q++ = *p0++;

        if (fl_reject_kind == FLR_RATE) {
            q = ui_dec(q, v / 1000u);
            uint32_t frac = (v % 1000u) / 100u;
            if (frac) { *q++ = '.'; *q++ = (char)('0' + frac); }
            const char *p2 = "kHz (48kHz MAX)";
            while (*p2) *q++ = *p2++;
        } else if (fl_reject_kind == FLR_DEPTH) {
            q = ui_dec(q, v);
            const char *p2 = "-BIT (24-BIT MAX)";
            while (*p2) *q++ = *p2++;
        } else if (fl_reject_kind == FLR_CHANS) {
            q = ui_dec(q, v);
            const char *p2 = "-CH (STEREO MAX)";
            while (*p2) *q++ = *p2++;
        } else {
            const char *p1 = "BLOCK ";
            while (*p1) *q++ = *p1++;
            q = ui_dec(q, v);
            const char *p2 = " (6144 MAX)";
            while (*p2) *q++ = *p2++;
        }
        *q = 0;
    }
    ui_warn_row = 1u;

    ui_blank_wake();

    /* Repaint the whole frame before the card.
     *
     * ui_draw_chrome() draws the card and nothing behind it, so the previous
     * track's ALBUM ART stayed on screen next to a message about a file that
     * has none -- it read as though the refused file had that artwork. The
     * gradient clears it.
     *
     * The art panel is also parked off-screen, so nothing slides it back: a
     * refused file has no art of its own, and art_have/art_file_id still
     * describe the previous track, which is what should happen if the user
     * goes back to it. */
    art_shown = 0;
    art_x     = FB_W;
    ui_gradient();
    ui_chrome_paint();

    /* Stay alive so a reload can rescue us, exactly as the failure screen
     * does -- the difference is only what the user is looking at. */
    /* Same fallback as the failure screen: this is exactly where it was seen
     * -- refuse a hi-res file, pick a playable one, nothing happens. */
    tk_poll_at = cycles() + CLK_HZ;
    for (;;) {
        poll_input();
        if (reload_pending) { ui_warn_row = 0u; return; }
        if ((int32_t)(cycles() - tk_poll_at) >= 0) {
            tk_poll_at = cycles() + CLK_HZ;
            if (slot_changed()) reload_pending = 1u;
        }
    }
}

/* Called from the main loop every frame. Under rev 6 this had to be throttled
 * to 1-in-3 and stripped back, because each element cost one command PER ROW
 * and a clock redraw alone was ~180 MMIO round-trips landing in the same
 * per-frame budget that keeps the PCM FIFO fed. The engine owns those loops
 * now: the meter is 2 commands regardless of height, the clock is 5, and text
 * paints its own background so nothing needs erasing first. That is roughly a
 * 30x reduction, which is why the throttle is gone.
 *
 * The delta-guards below stay -- not for cost any more, but because redrawing
 * an unchanged value would make the meter and clock flicker as they are
 * rewritten mid-scanout (the framebuffer is single-buffered). */
/* ---- shared full-screen overlay chrome ------------------------------------
 *
 * (The legacy .m3u playlist overlay that used to live here -- a scrollable
 * list of a playlist's filenames -- was removed along with legacy playlist
 * mode. The library has its own equivalent browse overlay, fw/library.inc.)
 */
/* Full-screen overlay geometry (settings, library, Info/Check/Stress Status/Blit Test etc. all
 * share it). The panel is inset 8 px from every edge over a UI_BG frame; the header sits at y 24,
 * the list starts at y 52 and the hint line lives in the bottom PL_UI_PAD_B. 12 rows of 22 px fit
 * above it. The snapshot fixtures and tools/overlay_preview.py read these names. */
#define PL_UI_ROWS   12u
#define PL_UI_X      8u
#define PL_UI_W      (FB_W - 2u * PL_UI_X)
#define PL_UI_Y      8u
#define PL_UI_H      (FB_H - 2u * PL_UI_Y)
#define PL_UI_ROW_H  22u
#define PL_UI_LIST_Y 52u
#define PL_UI_TEXT_X (PL_UI_X + 16u)
#define PL_UI_PAD_B  36u

/* Frame, header and hint of a full-screen overlay -- redesigned to the owner's Figma reference
 * ("Menu Layout", node 196:1840, read for real via get_design_context once Figma access was fixed):
 * full-bleed edge to edge (no rounded panel, no margin), a solid navy header ("MENU"-style title
 * left, an optional right-hand status text), a plain black content area, and a navy action bar at
 * the true bottom edge, the same colour as the header. Caller must have set ov_draw. PL_UI_X/
 * PL_UI_LIST_Y (the row-content geometry every page already draws against) are UNCHANGED here on
 * purpose -- only the chrome around them moved, so this stays a one-function change instead of
 * touching every page's own row-drawing code too.
 *
 * Confirmed exact from the design (not guessed): header 28px, footer 28px, both #121C2E -- the
 * canvas is exactly 28 + 304 + 28 = 360 = FB_H. My first pass guessed a 44px header from the
 * screenshot alone; that was wrong, corrected here. NOT yet matched (out of scope for this pass,
 * each needs touching every ov_frame() caller's own row-drawing, not just this function): the
 * selected-row style (a full-width rounded lime bar with the row's own key letter right-aligned on
 * it, no background box on unselected rows) and the action bar's real structure (fixed "KEY Action"
 * slots with gaps, not one free-text sentence -- today's hint strings stay a single line for now). */
#define OV_HEAD_H 28u
#define OV_HINT_H 28u
#define OV_HINT_Y (FB_H - OV_HINT_H)

/* The action-bar strip at the bottom, full width: a SOLID navy bar, reserved exclusively for the
 * current action hint (owner: pages "all over" were letting their own content draw into that row,
 * e.g. a partial/rows-only refresh that skips repainting the chrome -- set_info_tick() is one
 * confirmed case, "repaint the rows only, never the panel"). Any page whose own content could reach
 * this low should call ov_hint_repaint() again after drawing, the same idea as a persistent overlay
 * always painted last. */
static void ov_hint_repaint(const char *hint)
{
    fb_rect(0u, OV_HINT_Y, FB_W, OV_HINT_H, OV_CHROME_BG);
    fb_set_color(UI_FAINT, OV_CHROME_BG);
    fb_text_clipped(PL_UI_TEXT_X, OV_HINT_Y + 7u, hint, TS_1X, TS_1X, FB_W - 32u);
}

static void ov_frame(const char *title, const char *right, const char *hint)
{
    fb_rect(0u, 0u, FB_W, OV_HEAD_H, OV_CHROME_BG);
    fb_rect(0u, OV_HEAD_H, FB_W, OV_HINT_Y - OV_HEAD_H, th_role[TR_BG_BOTTOM]);
    fb_set_color(ui_accent, OV_CHROME_BG);
    fb_text_clipped(PL_UI_TEXT_X, 7u, title, TS_1X, TS_1X, 230u);
    if (right && right[0]) {
        uint32_t w = fb_text_width(right, TS_1X);
        fb_set_color(UI_DIM, OV_CHROME_BG);
        fb_text_clipped(FB_W - 16u - w, 7u, right, TS_1X, TS_1X, w + 2u);
    }
    ov_hint_repaint(hint);
}

#include "chladni.inc"
#include "vu_master.inc"
#include "layered_wave.inc"
#include "fullscreen.inc"

/* G4 step 4 (B-199..B-201): the real "meters go cold" conversion. Body unchanged from the original
 * ui_draw_dynamic() (still calls ordinary hot fb_rect()/fb_bar()/etc. from cold code -- the same
 * "cold calls hot" pattern G4 steps 1-3 and fw/cold.inc's own cold_calls_hot() already prove safe),
 * just relocated and renamed so the thin wrapper below can time it and apply the COLD_READY()
 * fail-safe. */
#if TAU_DIAGNOSTIC
static void mt_take(void);          /* fw/suite.inc: meter trace recorder (M3), called once per displayed meter frame */
#endif

/* Helios review item 6 (docs/features/HELIOS_ARCHITECTURE_REVIEW_2026-09-28.md section 5): the meter
 * box as Helios's SECOND row-ranged region consumer (`ui_chrome_paint()` was the only one -- section
 * 11's own finding). ui_meter_redraw() (defined right below, called from ui_draw_dynamic_cold()'s own
 * beam-safety-gated `if` further down this file) is verbatim what used to live directly inside that
 * `if`; only its LOCATION moved
 * (into its own function, called through helios_mark_dirty()+helios_flush() instead of running
 * inline) -- nothing about wf/ww/bed or any of the dispatch logic changed. Registering this as a real
 * region (instead of just calling helios_rows_safe_counted() by hand, as this call site always has)
 * proves the region/beam-safety abstraction generalises beyond chrome's own full-screen, no-row-range
 * case, which is exactly what item 6's own text names as the precondition for a fuller
 * partial-invalidation vocabulary -- this does NOT attempt that fuller unification (the three separate
 * "invalidation" mechanisms section 3.1 lists are still three), it only proves the region half of it
 * now has two real, differently-shaped consumers to design the rest against. */
static uint8_t ui_meter_region = 0xFFu;

/* COLD_FN3, matching ui_draw_dynamic_cold() (this body used to live directly inside it): without it,
 * this ~150-line function would compile as ordinary hot code instead of landing in the cold image,
 * costing real RAM budget for no reason -- caught by the heap-gap number moving after the first build
 * of this change (50,784 B here vs the pre-move 56,864 B), not by inspection. */
COLD_FN3 static void ui_meter_redraw(void)
{
    uint32_t wf = ui_wave_force; ui_wave_force = 0;
    /* The accumulated maximum, not the instantaneous value: this tick
     * covers two display frames and both should count. */
    uint32_t src = wave_pend ? wave_pend : peak_amp;
    wave_pend = 0;
    uint32_t amp = (src * UI_WAVE_H) / 32768u;
    if (amp > UI_WAVE_H) amp = UI_WAVE_H;
    /* Frozen while paused: the forced pass exists only to recolour. */
    if (!paused) {
        for (uint32_t i = 0; i < UI_WAVE_N - 1u; i++) {
            wave[i]    = wave[i + 1];
            wave_pk[i] = wave_pk[i + 1];
        }
        wave[UI_WAVE_N - 1u]    = (unsigned char)amp;
        wave_pk[UI_WAVE_N - 1u] = (unsigned char)amp;

    }
    (void)wf;
    /* Peaks sink slowly back toward the bar, so the marker trails the
     * loudest recent moment instead of sitting at the ceiling. */
    for (uint32_t i = 0; i < UI_WAVE_N; i++)
        if (wave_pk[i] > wave[i]) wave_pk[i]--;

    uint32_t ww  = ui_wave_w();
    uint16_t bed = ui_grad_at(UI_WAVE_Y);

    /* ---- WATERFALL ----------------------------------------------------
     * Scroll the whole strip one pixel left with a single COPY, then draw
     * only the new right-hand column. That is ~4 commands a frame against
     * the bars' ~72, because COPY moves a block for the price of one
     * command -- the same primitive the album-art slide uses.
     *
     * Colour encodes loudness, so the strip becomes a picture of the
     * track's dynamics rather than an instantaneous reading. */
    /* ---- SCROLLING WAVEFORM -------------------------------------------
     * The waterfall's COPY-scroll, but the new column is drawn MIRRORED
     * about a centre line instead of colour-coded from the bottom -- a
     * DAW-style envelope building up left to right. ~5 commands a frame,
     * because COPY moves the whole strip for the price of one. */
    if (viz_mode == VIZ_SCROLL) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_scroll_tick(&in);
        goto viz_done;
    }

    /* ---- MIRRORED BARS ------------------------------------------------
     * The same wave[] history the bars use, grown up AND down from a
     * centre line. Same cost as the bars; different shape entirely. */
    /* ---- SPECTRUM ------------------------------------------------
     *
     * Eight columns of real frequency content from the octave cascade --
     * see SPEC_BANDS. Bass on the left, treble on the right, each moving
     * on its own.
     *
     * This replaced a two-channel level ladder. Six blocks drawn from one
     * number will always rise and fall together however they are styled;
     * "make them move independently" is not a tuning request, it needs
     * frequency data, and the cascade is what provides it. */
    if (viz_mode == VIZ_LED) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_led_tick(&in);
        goto viz_done;
    }

    /* ---- WINAMP BARS -----------------------------------------------
     * Classic Winamp bars, drawn at the normal meter box's own position
     * -- see wviz_bars_tick() (defined earlier in this file, alongside
     * wviz_ease_step()) for the actual drawing/easing logic, shared with
     * the Settings > Meter > Configure page (fw/settingsui.inc) so both
     * places animate from one source of truth. */
    if (viz_mode == VIZ_CHLADNI) {
        if (!ui_fullscreen) {
            const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, ui_grad_at(UI_WAVE_Y + UI_WAVE_H / 2u), wf);
            (void)chladni_tick_box(&in);
        }
        goto viz_done;
    }

    if (ui_fullscreen && (viz_mode == VIZ_WINAMP_BARS || viz_mode == VIZ_WINAMP_SCOPE)) goto viz_done;   /* fullscreen.inc draws these */
    if (viz_mode == VIZ_WINAMP_BARS) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wviz_force);
        wviz_bars_tick(&in);
        goto viz_done;
    }

    /* ---- WINAMP SCOPE ------------------------------------------------
     * Classic Winamp oscilloscope -- see wviz_scope_tick() for the actual
     * drawing/smoothing logic, shared with the Configure page. */
    if (viz_mode == VIZ_WINAMP_SCOPE) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, 0u, wviz_force);
        wviz_scope_tick(&in, 1);
        goto viz_done;
    }

    /* ---- MASTER VU -----------------------------------------------------
     * Mastering-style segmented dB peak ladder -- see vum_tick() (fw/vu_master.inc) for the
     * ladder/peak-hold/overlay logic. Not fullscreen-capable (fs_capable() in fullscreen.inc
     * doesn't list it), so no ui_fullscreen guard is needed here the way Winamp Bars/Scope have
     * one above -- fullscreen is always forced off before this meter can be the active one. */
    if (viz_mode == VIZ_VU_MASTER) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wviz_force);
        vum_tick(&in);
        goto viz_done;
    }

    /* ---- LAYERED WAVE ---------------------------------------------------
     * Nested mirrored envelope layers on a solid background -- see lw_tick() (fw/layered_wave.inc). Fullscreen-capable:
     * fullscreen.inc draws it there. */
    if (viz_mode == VIZ_LAYERED_WAVE) {
        if (ui_fullscreen) goto viz_done;
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wviz_force);
        lw_tick(&in);
        goto viz_done;
    }

    /* ---- PEAK DOTS ----------------------------------------------------
     * Only the peak-hold markers, no bars: a row of floating dots tracing
     * the loudness contour. ~2 commands a column and the sparsest mode
     * here. */
    if (viz_mode == VIZ_DOTS) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_dots_tick(&in);
        goto viz_done;
    }

    if (viz_mode == VIZ_WATER) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_water_tick(&in);
        goto viz_done;
    }

    /* ---- VU METERS ----------------------------------------------------
     * Two analogue movements side by side. Geometry is derived from
     * ui_wave_w() every pass rather than assumed: hiding the album art
     * widens the box from ~246 to ~360, and a fixed layout would leave the
     * pair huddled at the left -- the same trap the waterfall fell into. */
    if (viz_mode == VIZ_VU) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_vu_tick(&in);
        goto viz_done;
    }

    /* ---- OSCILLOSCOPE -------------------------------------------------
     * One clear, then one vertical rect per column: ~65 commands, fewer
     * than the bars. */
    if (viz_mode == VIZ_WAVE) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_wave_tick(&in);
        goto viz_done;
    }
    /* ---- STEREO PHASE SCOPE -------------------------------------------
     * One rect to clear, then one per point: ~65 commands, fewer than the
     * bars. The whole trace is redrawn each pass rather than erased point
     * by point, which would double the count for no gain. */
    if (viz_mode == VIZ_SCOPE) {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_phase_tick(&in);
        goto viz_done;
    }

    /* Classic bars: the default meter. */
    {
        const mtr_in_t in = mtr_build(UI_MARGIN, UI_WAVE_Y, ww, UI_WAVE_H, bed, wf);
        viz_bars_tick(&in);
    }
viz_done: ;
#if TAU_DIAGNOSTIC
    mt_take();               /* M3 meter trace recorder: the values every meter just drew from (fw/suite.inc) */
#endif
}

/* B-452 (owner-reported: in the Configure page's Meter preview, only Winamp Scope actually plays --
 * Bars/Chladni/VU Master preview a static, non-animating snapshot): this whole block used to live
 * inline inside `ui_draw_dynamic_cold()`, reached only when `!UI_OVERLAY_UP` -- correct for the
 * ordinary player screen (an overlay covers the meter box, so there is nothing to publish for), but the
 * SAME gate also silently starves `wvcfg_preview_tick()`'s live preview, since Settings being open is
 * itself a `UI_OVERLAY_UP` case. Winamp Scope's own tick function manages its `R_WAVE_CTL` capture
 * directly inside itself (see wviz_scope_tick()), so it never depended on this publish step and kept
 * working through the overlay regardless -- which is exactly why it was the one meter that "still
 * played" while the preview browsed. `meters_feed()` (called from the decode path, unconditional)
 * still accumulates `peak_acc*` regardless of any overlay; only the once-per-display-frame PUBLISH
 * into `peak_l`/`peak_r`/`spec_lvl[]` was gated. Extracted verbatim (no logic changed) so
 * `wvcfg_preview_tick()` can call it too, bypassing the overlay gate on purpose for exactly the
 * meter currently being previewed. */
COLD_FN3 static void meters_publish(void)
{
    /* Publish the peaks once per display frame, so every meter below reads a
     * value covering exactly the audio since the last frame. Nothing new means
     * hold the last -- correct during FLAC's channel-0 window, where there
     * genuinely is no new audio to show. */
    if (peak_acc_any) {
        /* Headroom. Nothing about HOW the meters respond changes here -- they
         * are still peak-driven, with the same ballistics and the same
         * peak-hold fall -- every bar simply sits lower.
         *
         * That distinction was learned the hard way: the first attempt at this
         * report replaced peak with a mean, which fixed the height and altered
         * the character of all ten meters at once. The complaint was the
         * level, not the response.
         *
         * 3/4 is measured, not chosen. On 56 windows of 1152 pairs from a real
         * track, decoded through the real decoder under tools/rv32sim.py, the
         * bar ran 33..71 of 72 pixels and sat at or above 95% in 8 of those
         * windows -- pegged, which is what "many of the meters are peaked out"
         * was. At 3/4 the same material runs 25..53 with nothing pegged, and
         * the loudest moment reaches 74% of the bar, so a track a third louder
         * than this one still has somewhere to go. 7/8 also clears this track
         * but leaves only 14% of headroom, which is thin when the report said
         * MANY songs.
         *
         * One constant, applied in one place, so all ten meters keep agreeing.
         * tools/meter_preview.py renders the effect from real audio. */
        if (wave_hw) {                       /* B-283: the block tracks max |L| / |R| continuously; read and clear it */
            const uint32_t pk = REG(R_WAVE_PK);
            REG(R_WAVE_CTL) = 1u;
            peak_acc_l = pk & 0xFFFFu; peak_acc_r = pk >> 16;
            peak_acc   = (peak_acc_l > peak_acc_r) ? peak_acc_l : peak_acc_r;
            peak_acc_any = 1u;
        }
        peak_amp     = (peak_acc   * MTR_HEADROOM_NUM) / MTR_HEADROOM_DEN;
        peak_l       = (peak_acc_l * MTR_HEADROOM_NUM) / MTR_HEADROOM_DEN;
        peak_r       = (peak_acc_r * MTR_HEADROOM_NUM) / MTR_HEADROOM_DEN;
        peak_acc     = peak_acc_l = peak_acc_r = 0;
        peak_acc_any = 0;

        /* Each band's mean magnitude over the window, tilted, scaled to
         * 0..255. Stage b was fed at 1/2^b of the rate, so the mean divides by
         * ITS OWN sample count and not the window's -- dividing everything by
         * the window would make every band below the first read low by exactly
         * the factor it was downsampled by, which is a convincing-looking
         * wrong answer. */
        uint32_t hw_mean[SPEC_BANDS];
        int have = spec_hw && (viz_mode == VIZ_LED || viz_mode == VIZ_WINAMP_BARS || viz_mode == VIZ_CHLADNI || viz_mode == VIZ_LAYERED_WAVE)
                   && spec_hw_fetch(hw_mean);
        if (have) {
            for (uint32_t b = 0; b < SPEC_BANDS; b++) {
                /* Both halves of an octave were fed at that OCTAVE's rate, so
                 * the divisor is per stage, not per band. (The hardware bank
                 * already divides: its window is 1024 samples and stage o saw
                 * 1024 >> o of them, so its mean is a shift.) */
                const uint32_t mean = hw_mean[b];
                uint32_t v    = (mean * spec_gain[SPEC_BANDS - 1u - b]) >> 4;

                /* LOGARITHMIC, because loudness is.
                 *
                 * A linear meter spends nearly all its range on the top 6 dB
                 * and almost none on everything below, so anything mastered
                 * louder than the two tracks these gains were measured on
                 * simply pegs -- which is what "the spectrum maxes out" was.
                 * It was not clipping; it was the scale.
                 *
                 * spec_log gives 16 units per octave, so the window below is
                 * 80 units = 30 dB of display range. Measured across both
                 * tracks the bands span 202..238, so the floor sits below the
                 * quietest and there are 32 units -- 12 dB -- of headroom
                 * above the loudest before anything reaches the top. On a
                 * linear scale that headroom was about 1.5 dB.
                 *
                 * Nothing about the per-band calibration changes: the gains
                 * still multiply first, and the log is taken of the result. */
                uint32_t lg = spec_log(v);
                v = (lg > SPEC_FLOOR)
                  ? ((lg - SPEC_FLOOR) * 255u) / SPEC_SPAN : 0u;
                if (v > 255u) v = 255u;
                /* Same ballistics the level ladder used: catch the transient,
                 * fall back smoothly. */
                if (v >= spec_lvl[b]) spec_lvl[b] = (unsigned char)v;
                else spec_lvl[b] -= (unsigned char)((spec_lvl[b] - v) / 4u + 1u);
            }
        }
        if (peak_amp > wave_pend) wave_pend = peak_amp;
    }
}

COLD_FN3 static void ui_draw_dynamic_cold(void)
{
    if (screen_blank) return;
    /* The overlay covers the meters, the card and the transport row. Letting
     * the player keep drawing underneath would punch holes straight through
     * it, once per frame. */
    /* Jump the UPPER screen, not the whole function.
     *
     * The overlay panel is y 18..284; the clock (288) and the progress bar
     * (334) sit BELOW it and stay visible. Returning early here froze both,
     * and worse: the elapsed-time accumulator lives further down this
     * function, so time itself stopped advancing while the list was open and
     * the clock came back stale.
     *
     * A goto over the drawing rather than a wrapped block -- the skipped
     * region is several hundred lines and every declaration in it is scoped
     * inside the sections being skipped. `viz_done` already sets the
     * precedent. */
    if (UI_OVERLAY_UP) goto ui_tail;

    meters_publish();
    /* Shift a new sample in and repaint the band. Every bar moves each update,
     * so there is nothing to gain from change-detection here -- instead it is
     * throttled, and each bar is two rects (lit + unlit), which the engine
     * draws in one command apiece. */
    /* A new accent has to reach three separate things, each of which only
     * redraws when its own value changes -- so they are all invalidated
     * explicitly rather than waiting for the music to move them. */
    if (ui_accent_changed) {
        ui_accent_changed = 0;
        ui_accent = th_accent_of(ui_pal_idx);
        /* The BACKGROUND is tinted from the accent now, so a colour change is
         * no longer a matter of recolouring a few elements -- the whole screen
         * is a different colour underneath them, and every cached element sits
         * on it. Hence a full repaint here, where the explicit invalidations
         * below were previously enough on their own.
         *
         * This is the one place the tint costs something: a repaint is ~360
         * gradient rects plus the chrome, pushed while the decoder is not
         * running. Colour is changed by a deliberate button press rather than
         * continuously, so it should stay under the FIFO's reserve -- but this
         * is the thing to listen for if a colour change ever ticks. */
        ui_grad_set(ui_accent);
        ui_chrome_paint();
        /* No invalidation list here any more. This used to repeat most of
         * ui_draw_chrome's, and keeping two copies is exactly how the mode row
         * ended up missing from the one that mattered. ui_draw_chrome repaints
         * the whole screen and now owns the full list.
         *
         * ui_wave_force is NOT invalidation -- it is a behaviour request, to
         * recolour the meter even while paused, which nothing else would do. */
        ui_wave_force = 1;
    }

    /* Frozen, not decaying, while paused: shifting the history along with a
     * zero sample scrolled the whole waveform off the screen, which reads as
     * "lost the audio" rather than "stopped". */
    /* The VU keeps updating while paused until both needles reach rest: a
     * meter that freezes mid-deflection looks broken, and the fall to zero is
     * the most characterful thing an analogue movement does. Every other mode
     * is genuinely static when paused and stays frozen. */
    /* The eye settles for the same reason the needles do: its shadow OPENING
     * back to rest is the movement that makes it look like a tube rather than
     * a graphic, and freezing it half-shut looks broken. eye_v counts DOWN to
     * rest, so "not yet settled" is a non-zero deflection, same as the VU. */
    uint32_t vu_settling = (viz_mode == VIZ_VU) && (vu_l || vu_r);
    /* B-267: draw the meter only when the scanning beam is not inside its rows (or about to be) -- the frame's
     * tear-free window. If the beam is in the way, ui_last_vu stays >= 2 and the very next pass tries again, so
     * nothing is lost, the meter just waits (at most a fraction of a frame) for the beam to move on. */
    if ((!paused || ui_wave_force || vu_settling) && ++ui_last_vu >= 2u
        && helios_rows_safe_counted(UI_WAVE_Y - UI_WAVE_TOP, UI_WAVE_Y + UI_WAVE_H - 1u)) {
        ui_last_vu = 0;
        if (ui_meter_region == 0xFFu)
            ui_meter_region = helios_region_register_rows_cold(ui_meter_redraw, UI_WAVE_Y - UI_WAVE_TOP,
                                                                UI_WAVE_Y + UI_WAVE_H - 1u);
        helios_mark_dirty(ui_meter_region);
        /* Safety was already confirmed just above; helios_flush()'s own re-check of the same row
         * range passes trivially (the beam has not moved in the few cycles since), so this draws NOW
         * -- the exact cadence/timing this call site always had, not deferred to a later pass. Kept
         * as an explicit gate rather than relying only on helios_flush()'s internal check, so
         * ui_last_vu's reset-only-on-an-actual-redraw semantics (the comment above) are unchanged:
         * that is what makes it retry every tick while genuinely beam-blocked rather than every
         * other tick. */
        helios_flush();
    }

    /* Sticky underrun latch (pcm_fifo.v) stays set until the next pcm_flush()
     * -- draw the indicator once when it first trips rather than every call;
     * costs nothing once shown. Cleared (screen wiped) by ui_draw_chrome() on
     * the next track load, which also clears the underrun flag via load_track.
     *
     * Switched OFF on request now that audio is healthy. Kept rather than
     * deleted because it is the fastest way to tell "the decoder lost the
     * race" from "something else is wrong with the audio" -- set
     * UI_SHOW_UNDERRUN back to 1 if stutter ever returns. */
#if UI_SHOW_UNDERRUN
    if (!ui_underrun_shown && pcm_underrun()) {
        ui_underrun_shown = 1;
        fb_rect(UI_UNDERRUN_X, UI_UNDERRUN_Y, UI_UNDERRUN_SZ, UI_UNDERRUN_SZ, UI_RED);
    }
#endif

ui_tail:
    /* Elapsed time by running accumulator rather than (frames*1152)/samprate.
     * That is a 64-bit divide -- __udivdi3, hundreds of cycles in software on
     * RV32 -- and it ran EVERY frame inside the same budget that keeps the PCM
     * FIFO fed. Adds and compares give the identical answer. */
    if (samprate && frames != ui_last_frames) {
        ui_last_frames = frames;
        ui_sec_acc += samp_per_frame;
        while (ui_sec_acc >= samprate) { ui_sec_acc -= samprate; ui_sec++; }
    }
    uint32_t sec = ui_sec;

    /* Measured average: bytes actually consumed divided by elapsed time. Only
     * after a few seconds, so a partly-filled ring cannot skew it. */
    /* Measured over the window since the last seek, never over the whole file.
     *
     * Taking file_pos and sec as absolutes made this self-referential: ui_sec is
     * computed FROM meas_rate on every seek, and meas_rate was then recomputed
     * FROM ui_sec. Holding the seek fed that loop four times a second, the rate
     * inflated, the clock collapsed backwards and never recovered. It only ever
     * bit files with no Xing header, because track_secs short-circuits this
     * whole branch -- which is exactly why it failed on some songs and not
     * others. */
    if (!track_secs) {
        uint32_t buffered = ring_fill - ring_rd;
        uint32_t played   = (file_pos > buffered) ? file_pos - buffered : 0u;
        if (played > meas_pos0 && sec > meas_sec0 + 3u)
            meas_rate = (played - meas_pos0) / (sec - meas_sec0);
    }
    if (sec != ui_last_sec) {
        ui_last_sec = sec;
        /* Elapsed, and total when the file size makes it knowable. Padded to a
         * fixed width so the digits do not jitter as they change. */
        char buf[16], *q = buf;
        q = ui_mmss(q, sec);
        /* Always draw the field. Showing nothing when the length is unknown
         * makes a missing VALUE look identical to a broken DISPLAY, which has
         * already cost two rounds of chasing the wrong half. */
        uint32_t total = ui_total_secs();
        *q++ = ' '; *q++ = '/'; *q++ = ' ';
        if (total) q = ui_mmss(q, total);
        else { *q++ = '-'; *q++ = '-'; *q++ = ':'; *q++ = '-'; *q++ = '-'; }
        *q = 0;
        uint16_t cbg = ui_grad_at(UI_TIME_Y);
        /* Clear FIRST, then set the colour. fb_rect() writes the colour
         * registers (fg = bg = its fill), so setting the text colour before a
         * clear leaves fg == bg and the glyphs paint invisibly. */
        fb_rect(UI_MARGIN, UI_TIME_Y, UI_INNER_W, FB_CELL(TS_15X), cbg);
        fb_set_color(UI_WHITE, cbg);
        fb_text_clipped(UI_MARGIN, UI_TIME_Y, buf, TS_15X, TS_15X, UI_INNER_W);

        /* 1.2x, while it is on. Right-aligned on this row, which puts it
         * directly under the track counter -- that is drawn right-aligned on
         * the transport row above, and the elapsed time only reaches about
         * halfway across, so the space is free.
         *
         * Drawn HERE rather than anywhere else because the clear above spans
         * the full inner width every second; anything painted on this row
         * outside this block is erased within a second of appearing. A toggle
         * forces ui_last_sec, so it shows up on the press rather than on the
         * next tick.
         *
         * Accent, not white: it is a state the user chose, and the same colour
         * every other active mode indicator uses. */
        if (speed_idx != SPEED_1X) {
            const char *sp = speed_txt[speed_idx];
            uint32_t sw = fb_text_width(sp, TS_1X);
            uint32_t sx = FB_W - UI_MARGIN - sw;
            uint32_t sy = UI_TIME_Y + (FB_CELL(TS_15X) > FB_CELL(TS_1X)
                                     ? (FB_CELL(TS_15X) - FB_CELL(TS_1X)) / 2u : 0u);
            fb_set_color(ui_accent, cbg);
            fb_text_clipped(sx, sy, sp, TS_1X, TS_1X, sw + 2u);
        }
    }

    /* Marquee. Steps by whole characters rather than pixels: the engine draws
     * a glyph at any x but does not clip one partially off the left edge, so a
     * pixel scroll would need clipping support that does not exist. One step
     * every ~350 ms reads as a scroll without being distracting. */
    if (!UI_OVERLAY_UP) {          /* title/artist rows are under the overlay */
        ui_marq_step(&ui_mq_title,  UI_WHITE);
        ui_marq_step(&ui_mq_artist, UI_DIM);
    }

    /* Loud, and it stays: a wrong file size means the card's directory is
     * damaged, which will not fix itself and puts every file in that folder in
     * question -- not something to mention in a toast that scrolls away. */
    if (size_suspect && !ui_size_warned && !UI_OVERLAY_UP) {
        ui_size_warned = 1;
        fb_set_color(UI_RED, ui_grad_at(ui_info_y));
        fb_text_clipped(UI_TEXT_X, ui_info_y, "! FILE SIZE WRONG - CHECK SD CARD",
                        TS_1X, TS_1X, ui_text_w);
    }

    /* Format line, drawn once the decoder has told us what the stream is. */
    uint32_t info = track_kbps * 1000u + track_hz / 100u
                  + (track_encoder[0] ? (uint32_t)track_encoder[0] << 24 : 0u);
    /* Work it out HERE if it is still unknown.
     *
     * track_kbps is derived from slot_size, and slot_size is no longer known at
     * load time -- the size search runs during playback now. Computing it only
     * where the search happens to finish made the row's appearance depend on
     * WHEN that was, and it showed up unreliably. This asks the question every
     * time the row is considered instead, so the answer appears the moment it
     * exists, whichever path produced it.
     *
     * Cheap: one compare while it is unknown, nothing at all afterwards. */
    if (!track_kbps && track_fmt == FMT_FLAC) {
        uint32_t kb = 0;
        if (track_secs && slot_size > fl_first_frame) {
            /* Best answer: the whole file over its whole duration. */
            uint64_t bits = (uint64_t)(slot_size - fl_first_frame) * 8u;
            kb = (uint32_t)DIV64(DIV64(bits, (uint64_t)track_secs), 1000u);
        } else if (ui_sec >= 3u && file_pos > fl_first_frame) {
            /* Otherwise ask the DECODER, which has been counting all along:
             * bytes consumed over seconds played is the average bitrate of
             * what has been heard, and it converges on the file's own.
             *
             * This exists because waiting for slot_size kept failing in ways
             * that were invisible -- the row simply did not appear. The size
             * arrives from a probe that has been moved twice, gated twice and
             * broken twice; file_pos and ui_sec are both already true by the
             * time anyone can read this row. Deriving from what is known beats
             * waiting for what might arrive.
             *
             * Survives a seek: both jump together, so the ratio still measures
             * from the start of the file. */
            uint64_t bits = (uint64_t)(file_pos - fl_first_frame) * 8u;
            kb = (uint32_t)DIV64(DIV64(bits, (uint64_t)ui_sec), 1000u);
        }
        if (kb) {
            track_kbps = kb;
            info = track_kbps * 1000u + track_hz / 100u
                 + (track_encoder[0] ? (uint32_t)track_encoder[0] << 24 : 0u);
        }
    }

    if (track_kbps && info != ui_last_info && !size_suspect && !UI_OVERLAY_UP) {
        ui_last_info = info;
        char b[40], *q = b;
        q = ui_dec(q, track_kbps);
        *q++ = ' '; *q++ = 'k'; *q++ = 'b'; *q++ = 'p'; *q++ = 's';
        *q++ = ' '; *q++ = '-'; *q++ = ' ';
        q = ui_dec(q, track_hz / 1000u);
        *q++ = '.';
        q = ui_dec(q, (track_hz % 1000u) / 100u);
        *q++ = ' '; *q++ = 'k'; *q++ = 'H'; *q++ = 'z';
        /* Who encoded it, from the LAME tag beside the duration header. This
         * belongs here with the other format facts rather than in a screen of
         * its own -- and putting it here is what stops the bitrate being
         * repeated, which it was when this had its own view. */
        if (track_encoder[0]) {
            *q++ = ' '; *q++ = '-'; *q++ = ' ';
            /* Bound by the BUFFER, not by a number that happened to work.
             * This was `q - b < 30`, and the prefix "430 kbps - 44.1 kHz - "
             * is 22 characters, so the codec field got exactly 8 -- which is
             * the length of "LAME3.99", so MP3 fit by luck and nothing ever
             * looked wrong. FLAC writes "FLAC 16-bit" and it showed as
             * "FLAC 16-" on every lossless track. b is char[40] and the whole
             * line is 34, so the space was always there. */
            for (const char *t = track_encoder;
                 *t && q < b + sizeof(b) - 1u; ) *q++ = *t++;
        }
        *q = 0;
        fb_set_color(UI_FAINT, ui_grad_at(ui_info_y));
        fb_text_clipped(UI_TEXT_X, ui_info_y, b, TS_1X, TS_1X, ui_text_w);
    }

    /* Thin progress bar. Total length comes from the file size APF reports
     * (0190/008A) minus the tag, divided by the byte rate -- there is no
     * duration in the stream itself unless the tag happens to carry TLEN. */
    /* Draw the track UNCONDITIONALLY. Previously the whole bar was gated on
     * knowing the duration, so an unknown file size meant nothing appeared at
     * all -- indistinguishable from the feature being broken. An empty bar
     * that never fills is at least self-describing. */
    /* Recomputed only when the displayed second moves -- the bar cannot change
     * faster than that, so running these divides every frame was pure waste. */
    uint32_t done = (ui_last_prog == 0xFFFFFFFFu) ? 0u : ui_last_prog;
    if (sec != ui_prog_sec) {
        ui_prog_sec = sec;
        done = 0;
        uint32_t total = ui_total_secs();
        if (total) {
            done = (sec * UI_INNER_W) / total;
            if (done > UI_INNER_W) done = UI_INNER_W;
        }
    }
    if (done != ui_last_prog) {
        ui_last_prog = done;
        /* Clear a band taller than the bar first: the knob overhangs it above
         * and below, so redrawing only the bar would leave the old knob's
         * overhang behind as two floating stubs. */
        fb_rect(UI_MARGIN, UI_PROG_Y - 3u, UI_INNER_W, UI_PROG_H + 6u,
                ui_grad_at(UI_PROG_Y));
        if (done) {
            fb_rect(UI_MARGIN, UI_PROG_Y, done, UI_PROG_H, ui_accent);
            /* One lit row along the top of the filled part, so the bar has a
             * direction to it instead of reading as a flat block. A third of
             * the way to white -- enough to catch the eye at 5 px tall,
             * little enough that it still reads as the accent colour.
             *
             * Free in practice: this band is only redrawn when `done` moves,
             * which is about once a second. */
            fb_rect(UI_MARGIN, UI_PROG_Y, done, 1u,
                    ui_mix(ui_accent, UI_WHITE, 1u, 3u));
        }
        if (UI_INNER_W > done)
            fb_rect(UI_MARGIN + done, UI_PROG_Y, UI_INNER_W - done,
                    UI_PROG_H, UI_TRACK);

        /* Round the OUTER ends of the whole bar, after both segments are down.
         * Cutting them here rather than drawing two rounded rects is what
         * keeps the join invisible: the filled part must meet the unfilled
         * part square in the middle, and only the two ends of the assembly are
         * ever a shape. At 5 px tall a radius of 2 is a 2 px bite from the top
         * and bottom rows and 1 px from the next -- a soft end rather than a
         * pill, which is all there is room for. */
        {
            const uint32_t r = UI_PROG_H / 2u;
            uint16_t bg = ui_grad_at(UI_PROG_Y);
            for (uint32_t i = 0; i < r; i++) {
                uint32_t dy = r - i, inner = 0;
                while ((inner + 1u) * (inner + 1u) + dy * dy <= r * r) inner++;
                uint32_t cut = r - inner;
                if (!cut) continue;
                fb_rect(UI_MARGIN, UI_PROG_Y + i, cut, 1, bg);
                fb_rect(UI_MARGIN + UI_INNER_W - cut, UI_PROG_Y + i, cut, 1, bg);
                fb_rect(UI_MARGIN, UI_PROG_Y + UI_PROG_H - 1u - i, cut, 1, bg);
                fb_rect(UI_MARGIN + UI_INNER_W - cut,
                        UI_PROG_Y + UI_PROG_H - 1u - i, cut, 1, bg);
            }
        }

        /* Position knob -- also the visual handle the seek controls move.
         *
         * A vertical marker, not a disc. A round handle was tried and looked
         * wrong here: at 5 px of bar it has to overhang so far to read as
         * round that it stops being a mark on a track and becomes a blob
         * sitting over one. Taller than it is wide is what makes it read as a
         * position rather than an object. Drawn last, so the end rounding
         * above cannot bite it. */
        {
            uint32_t kx = UI_MARGIN + done;
            if (kx < UI_MARGIN + 2u) kx = UI_MARGIN + 2u;
            if (kx > UI_MARGIN + UI_INNER_W - 3u) kx = UI_MARGIN + UI_INNER_W - 3u;
            fb_rect(kx - 2u, UI_PROG_Y - 3u, 5u, UI_PROG_H + 6u, UI_WHITE);
        }
    }

    /* Toast: hold, then dissolve into the background.
     *
     * The fade is real, not a trick -- glyphs are drawn fg-on-bg, so stepping
     * the foreground toward the background colour genuinely dissolves the
     * text. Redrawn only when the step changes, so a 10-step fade costs ten
     * repaints of one short line, not one per frame. */
    if (ui_toast_t0) {
        uint16_t tbg2 = ui_grad_at(UI_TOAST_Y);
        uint32_t age  = cycles() - ui_toast_t0;
        uint32_t step = (age <= UI_TOAST_HOLD) ? 0u
                      : ((age - UI_TOAST_HOLD) * UI_TOAST_STEPS) / UI_TOAST_FADE;
        if (step > UI_TOAST_STEPS) step = UI_TOAST_STEPS;

        if (step != ui_toast_step) {
            ui_toast_step = step;
            if (step >= UI_TOAST_STEPS) {
                fb_rect(UI_MARGIN, UI_TOAST_Y, UI_INNER_W, UI_TOAST_H, tbg2);
                ui_toast_t0  = 0;
                ui_toast_end = UI_MARGIN;
            } else {
                fb_set_color(ui_mix(UI_WHITE, tbg2, step, UI_TOAST_STEPS), tbg2);
                uint32_t end = fb_text_clipped(UI_MARGIN, UI_TOAST_Y, ui_toast,
                                               TS_1X, TS_1X, UI_INNER_W);
                /* Erase only the TAIL the previous message left behind.
                 *
                 * CHAR paints its own background, so a message replacing a
                 * LONGER one only repaints the cells it covers and the old
                 * ending stays on screen -- visible when flipping settings
                 * faster than a toast fades. Clearing the whole line instead
                 * would cost a rect on every one of the ten fade steps and
                 * put an erase-then-draw on a single-buffered framebuffer,
                 * which is what makes things flicker when scanout catches the
                 * gap.
                 *
                 * fb_text_clipped returns where it stopped, so the leftover is
                 * exactly [end, previous end). In the common case -- the same
                 * string redrawn a shade dimmer -- end is unchanged and this
                 * costs nothing. */
                if (end < ui_toast_end)
                    fb_rect(end, UI_TOAST_Y, ui_toast_end - end, UI_TOAST_H, tbg2);
                ui_toast_end = end;
            }
        }
    }

    /* Transport state, in the gap to the right of the clock. Both strings are
     * the same length so one overwrites the other cleanly. */
    /* NOT while a boot note is up. UI_BOOT_Y and UI_TRANSPORT_Y are the SAME
     * ROW -- both 262 -- so during a switch this repainted PLAYING, the
     * arrows and the EQ name straight over "LOADING PLAYLIST" while
     * ui_boot_tick() animated its dots on top of both. The user saw the
     * transport line garbled, assumed the pick had failed, and picked again:
     * that is a large part of what "I have to load it twice" has been.
     *
     * The note owns the row until ui_boot_cancel(), whose callers already
     * force a full transport repaint after it. */
    if (!ui_boot_msg || ui_loader_on) {
        uint16_t tbg = ui_grad_at(UI_TRANSPORT_Y + 8u);
        /* Left end of its own row. The arrows sit at a FIXED x derived from
         * the WIDER of the two words, so switching PLAYING <-> PAUSED cannot
         * shuffle them sideways. */
        const uint32_t ly = UI_TRANSPORT_Y;
        const uint32_t lx = UI_MARGIN;
        const uint32_t lbl_w = fb_text_width("STOPPED", TS_1X);
        const uint32_t ix = lx + lbl_w + 12u;
        const uint32_t iy = ly;

        /* Breathe: a triangle over 64 draws. Paused redraws at ~30 Hz, playing
         * is throttled below, so both land near a 2 s cycle. */
        uint32_t ph  = (++ui_breath) & 63u;
        uint32_t lvl = (ph < 32u) ? ph : (63u - ph);

        if (ui_loader_on) {
            /* Loading: the label says so, no arrows. */
            if (ui_last_pause != 3u) {
                ui_last_pause = 3u;
                fb_rect(lx, ly, lbl_w + 8u, FB_CELL(TS_1X), tbg);
                fb_set_color(UI_DIM, tbg);
                fb_text_clipped(lx, ly, "LOADING", TS_1X, TS_1X, lbl_w + 8u);
                fb_rect(ix, iy, UI_ARR_SPAN, UI_ICONBOX_H, tbg);
            }
        } else if (paused) {
            /* Stopped is a settled state, so it does not breathe -- the pulse
             * says "waiting to resume", which stop is not. It also means the
             * label and icon are drawn once instead of every frame. */
            uint16_t c = stopped ? UI_WHITE : ui_mix(UI_PANEL, UI_WHITE, lvl, 31u);
            if (!stopped || ui_last_pause != 2u) {
                fb_rect(lx, ly, lbl_w + 8u, FB_CELL(TS_1X), tbg);
                fb_set_color(c, tbg);
                fb_text_clipped(lx, ly, stopped ? "STOPPED" : "PAUSED",
                                TS_1X, TS_1X, lbl_w + 8u);
                fb_rect(ix, iy, UI_ARR_SPAN, UI_ICONBOX_H, tbg);
                if (stopped) ui_icon_stop(ix, iy, c);
                else         ui_icon_pause(ix, iy, c);
            }
            ui_last_pause = stopped ? 2u : 0xFFFFFFFFu;
        } else {
            if (ui_last_pause != 0u) {
                ui_last_pause = 0u;
                fb_rect(lx, ly, lbl_w + 8u, FB_CELL(TS_1X), tbg);
                fb_set_color(ui_accent, tbg);
                fb_text_clipped(lx, ly, "PLAYING", TS_1X, TS_1X, lbl_w + 8u);
            }
            /* The arrow breathes too, but this path runs EVERY frame rather
             * than at the paused refresh rate, so it is throttled -- redrawing
             * a dozen rects 38 times a second for an idle animation is exactly
             * the kind of cost that used to disturb the decoder. */
            if ((int32_t)(cycles() - ui_icon_next) >= 0) {
                ui_icon_next = cycles() + CLK_HZ / 30u;

                /* Brightness falls off CONTINUOUSLY with how long ago each
                 * arrow was the lit one, rather than snapping between a few
                 * fixed levels. The peak rotates and each arrow trails a smooth
                 * glow behind it, so the step boundaries stop being visible. */
                uint32_t t = ui_arr_t++ % UI_ARR_TICKS;
                fb_rect(ix, iy, UI_ARR_SPAN, UI_ICONBOX_H, tbg);
                for (uint32_t i = 0; i < UI_ARR_N; i++) {
                    uint32_t dist = (t + UI_ARR_TICKS - i * UI_ARR_STEPS)
                                    % UI_ARR_TICKS;
                    if (dist >= UI_ARR_TAIL) continue;
                    uint32_t l = 31u - (dist * 31u) / UI_ARR_TAIL;
                    ui_icon_arrow(ix + i * (UI_ARR_W + UI_ARR_GAP), iy,
                                  UI_ARR_W, UI_ARR_H,
                                  ui_mix(UI_PANEL, ui_accent, l, 31u));
                }
            }
        }

        /* Repeat / shuffle / position. Static between changes, so it is drawn
         * only when something actually changed rather than every frame -- the
         * same discipline the arrows needed, for the same reason. */
        if (ui_mode_dirty) {
            ui_mode_dirty = 0;

            const uint32_t mx = ix + UI_ARR_SPAN + 14u;
            const uint32_t my = iy + (UI_ICONBOX_H > UI_MODE_H
                                    ? (UI_ICONBOX_H - UI_MODE_H) / 2u : 0u);

            /* Cleared to the WIDEST name, not the current one: CLASSICAL is
             * 101 px and POP is 35, so a narrower clear would leave the tail of
             * the previous preset on screen. */
            fb_rect(mx, iy, (UI_MODE_W + 10u) * 3u + 106u, UI_ICONBOX_H, tbg);
            /* Inactive modes stay visible but recede, so the controls advertise
             * themselves instead of only appearing once found. */
            ui_icon_repeat(mx, my,
                           rep_mode == REP_OFF ? UI_FAINT : ui_accent,
                           rep_mode == REP_ONE);

            /* The preset NAME, not the word "EQ" -- it is the same amount of
             * screen and says which one is on rather than merely that the
             * feature exists. Persistent, so a user who walks away and comes
             * back can see the state without pressing anything. Dimmed on FLAT,
             * the same "off but still visible" treatment the repeat icon uses. */
            /* Volume, as a level rather than a number. Faint at mute, the
             * same "off but still visible" treatment repeat uses when it is
             * off. */
            {
                uint32_t vl = (volume == 0u)   ? 0u
                            : (volume <= 33u)  ? 1u
                            : (volume <= 66u)  ? 2u : 3u;
                ui_icon_speaker(mx + (UI_MODE_W + 10u) * 1u, my, vl,
                                vl ? ui_accent : UI_FAINT);
            }

            fb_set_color(eq_idx ? ui_accent : UI_FAINT, tbg);
            fb_text_clipped(mx + (UI_MODE_W + 10u) * 2u, my - 2u,
                            eq_name[eq_idx], TS_1X, TS_1X, 110u);

            /* "4 / 12", right-aligned so the numbers do not shuffle sideways as
             * the track index gains a digit.
             *
             * Counted over LIVE entries, not lines in the file: an entry that
             * will not open is not a song. Both halves use the same counting or
             * the position could exceed the total. */
            uint32_t cnt_x = 0, cnt_y = 0;
            if (lib_src && lib_qn) { cnt_x = (uint32_t)lib_qpos + 1u; cnt_y = lib_qn; }      /* the library queue */
            if (cnt_x) {
                char pos[16]; char *q = pos;
                q = ui_dec(q, cnt_x);
                *q++ = ' '; *q++ = '/'; *q++ = ' ';
                q = ui_dec(q, cnt_y);
                *q = 0;
                uint32_t pw = fb_text_width(pos, TS_1X);
                uint32_t px = FB_W - UI_MARGIN - pw;
                fb_rect(px - 4u, ly, pw + 8u, FB_CELL(TS_1X), tbg);
                fb_set_color(UI_DIM, tbg);
                fb_text_clipped(px, ly, pos, TS_1X, TS_1X, pw + 4u);
            }
        }
    }

    /* Only ever appears if the CPU actually blocked on a full draw FIFO. If
     * the audio is clean this stays invisible; if it is not, this says in one
     * number whether drawing is to blame -- which rev 6 could only establish
     * by A/B-ing the entire feature on hardware. */
    /* TEMPORARY diagnostic line, redrawn only when a value changes:
     *   A  tag length (where playback starts)
     *   S  file size APF reports for the slot
     *   T  computed total seconds
     *   F  slot identity from 0190, low byte -- 00 means it did not answer
     * These are exactly the inputs behind "started in the wrong place" and
     * "wrong total", so a bad load says which one is at fault instead of
     * needing another round of inference. Set UI_SHOW_DIAG 1 to show it. */
#if UI_SHOW_DIAG
    uint32_t diag = audio_start ^ slot_size ^ (track_secs << 3) ^ cur_file_id;
    if (diag != ui_last_stall) {
        ui_last_stall = diag;
        char b[64], *q = b;        /* A=tag length  S=file size  T=total secs  F=slot identity (low byte)
         * Exactly the four inputs behind "started in the wrong place" and
         * "wrong total", so a bad load says which one is at fault. */
        *q++ = 'A';
        q = ui_hex2(q, (uint8_t)(audio_start >> 8));
        q = ui_hex2(q, (uint8_t)audio_start);
        *q++ = ' '; *q++ = 'S';
        q = ui_hex2(q, (uint8_t)(slot_size >> 16));
        q = ui_hex2(q, (uint8_t)(slot_size >> 8));
        q = ui_hex2(q, (uint8_t)slot_size);
        *q++ = ' '; *q++ = 'T';
        q = ui_hex2(q, (uint8_t)(track_secs >> 8));
        q = ui_hex2(q, (uint8_t)track_secs);
        *q++ = ' '; *q++ = 'F';
        q = ui_hex2(q, (uint8_t)cur_file_id);
        *q = 0;
        uint16_t dbg = ui_grad_at((FB_H - 24u));
        fb_rect(UI_MARGIN, FB_H - 24u, UI_INNER_W, FB_CELL(TS_1X), dbg);
        fb_set_color(UI_RED, dbg);
        fb_text_clipped(UI_MARGIN, FB_H - 24u, b, TS_1X, TS_1X, UI_INNER_W);
    }

    /* The first underrun of this track, and what the CPU was doing when it
     * happened. Reads as:  U<sec> I<idle%> O<io%> P<szp> R<ring/64>
     *
     * U--  means NO underrun was recorded. If the hiccup is audible anyway,
     *      the decoder was never starved and the fault is downstream -- fade,
     *      glide or decode -- so buffers and SD reads are the wrong tree.
     * O    high says the decoder sat waiting on bytes: ring or card too slow.
     * I    high says the CPU had time to spare, so a stall took it away.
     * P    non-zero says the size probe was mid-search at that instant, which
     *      would convict the thing already suspected once.
     * R    ring bytes ahead / 64. Under 40 (2.5 KB) is genuinely empty. */
    {
        /* BELOW the progress bar, and repainted every second.
         *
         * It was at FB_H-34 and FB_H-24, which are both inside the band the
         * progress bar clears -- UI_PROG_Y-3 to +8, i.e. 331..342 -- and the
         * bar repaints every second while this repainted only when it changed,
         * so the bar always won and the row was invisible. The gap between the
         * bar and the bottom edge is 342..360 and nothing else paints there;
         * a 16px cell at 344 fits it exactly.
         *
         * Keyed on ui_sec as well so a stray repaint elsewhere cannot bury it
         * again. Once a second, not once a frame -- this build is diagnosing a
         * TIMING fault and must not add drawing to the very loop in question. */
        static uint32_t last_u = 0xFFFFFFFEu, last_us = 0xFFFFFFFFu;
        if (und1_sec != last_u || ui_sec != last_us) {
            last_u = und1_sec; last_us = ui_sec;
            char b[48], *q = b;
            *q++ = 'U';
            if (und1_sec == 0xFFFFFFFFu) { *q++ = '-'; *q++ = '-'; }
            else q = ui_dec(q, und1_sec);
            *q++ = ' '; *q++ = 'V';
            if (und2_sec == 0xFFFFFFFFu) { *q++ = '-'; *q++ = '-'; }
            else q = ui_dec(q, und2_sec);
            *q++ = ' '; *q++ = 'I'; q = ui_dec(q, und2_idle);
            *q++ = ' '; *q++ = 'O'; q = ui_dec(q, und2_io);
            *q++ = ' '; *q++ = 'P'; q = ui_dec(q, und2_szp);
            *q++ = ' '; *q++ = 'R'; q = ui_dec(q, und2_ring);
            *q++ = ' '; *q++ = 'E'; q = ui_dec(q, und_edges);
            *q++ = ' '; *q++ = 'C'; q = ui_dec(q, clk_n);
            *q++ = ' '; *q++ = 'S';
            if (clk_sec == 0xFFFFFFFFu) { *q++ = '-'; *q++ = '-'; }
            else q = ui_dec(q, clk_sec);
            *q++ = ' '; *q++ = 'D'; q = ui_dec(q, clk_max >> 8);
            *q = 0;
            uint16_t ub = ui_grad_at((FB_H - 16u));
            fb_rect(UI_MARGIN, FB_H - 16u, UI_INNER_W, FB_CELL(TS_1X), ub);
            fb_set_color(UI_RED, ub);
            fb_text_clipped(UI_MARGIN, FB_H - 16u, b, TS_1X, TS_1X, UI_INNER_W);
        }
    }
#endif

    /* Speed-branch readout. These five values ARE the suspected fault, not a
     * general-purpose dump:
     *
     *   1.0x/1.2x  which mode -- so a screenshot is unambiguous
     *   T   track_secs. THE discriminator. Non-zero means ui_byte_rate() uses
     *       the exact rate from the Xing header and the meas_rate branch is
     *       skipped entirely. Zero is the path the old hold-to-seek bug lived
     *       in, and "wrong on some files, fine on others" is its signature.
     *   M   meas_rate, the measured bytes/sec. If this DRIFTS at 1.2x while
     *       staying put at 1.0x on the same file, the fault is found.
     *   R   what ui_byte_rate() actually returns -- the number seek and the
     *       elapsed clock consume. Shown separately from M because which of
     *       the three sources won is exactly what is in question.
     *   S   ui_sec, the elapsed clock.
     *   K   file_pos in KB, so the position is visible without eight digits.
     *
     * Redrawn on a change of ui_sec, i.e. about once a second, which is cheap
     * enough not to disturb the very budget being investigated.
     *
     * HOW TO USE IT: play a file that misbehaves, note T. Watch M and R at
     * 1.0x for ten seconds, hold A, watch them again. Compare, do not infer. */
#if UI_SHOW_SPEED_DIAG
    /* ONE row, at y336..351.
     *
     * There were two, and the upper one sat at y318..333 against the toast
     * band at y314..329 -- they repainted over each other every second, which
     * is why a toast only ever showed as a hint. Anything added here must stay
     * below y330.
     *
     * The playlist-notification (N/L) and seek/resume fields are all gone: those
     * investigations are closed, and legacy playlist mode itself is removed. */
    if (ui_sec != ui_last_spd) {
        ui_last_spd = ui_sec;
        /* Latch and reset the attribution for the second just finished. */
        fl_idle_pct = (uint8_t)(fl_idle_cyc / (CLK_HZ / 100u));
        fl_io_pct   = (uint8_t)(fl_io_cyc   / (CLK_HZ / 100u));
        if (fl_idle_pct > 99u) fl_idle_pct = 99u;
        if (fl_io_pct   > 99u) fl_io_pct   = 99u;
        fl_idle_cyc = fl_io_cyc = 0u;
        char b[64], *q = b;
        /* The speed prefix this row was built for is dropped while the
         * playlist switch is under investigation: measured against the real
         * font, the full line clips past 296px once the counters reach two
         * digits, and N is already at 8. */
        /* The playlist fields are gone: that investigation closed with the
         * poll fix confirmed on hardware, and this row is now the only way to
         * tell WHY a FLAC file hiccups.
         *
         * D = idle, waiting on a full FIFO -- spare CPU.
         * O = blocked in flac_pull waiting for bytes -- starved of input.
         * U = sticky underruns, the audible fault itself.
         *
         * Read D and O together. High D with underruns means the decoder is
         * fast enough and the ring is the problem; both near zero means the
         * decoder is not fast enough. Those need opposite fixes, and without
         * this row the two are indistinguishable from the couch. */
        /* L/R/P are retired: they did their job -- the 48 kHz gate is set
         * from the numbers they produced -- and FLAC_PROFILE is now 0. What
         * remains is what the one OPEN defect needs: F names why a load
         * failed. */
        *q++ = 'D'; q = ui_dec(q, fl_idle_pct);
        *q++ = ' '; *q++ = 'O'; q = ui_dec(q, fl_io_pct);
        *q++ = ' '; *q++ = 'U'; q = ui_dec(q, pcm_under_n);
        *q++ = ' '; *q++ = 'F'; q = ui_dec(q, fl_open_err);
        *q++ = '/'; q = ui_dec(q, fl_open_fails);
        *q = 0;
        uint16_t sbg = ui_grad_at((FB_H - 24u));
        fb_rect(UI_MARGIN, FB_H - 24u, UI_INNER_W, FB_CELL(TS_1X), sbg);
        fb_set_color(UI_RED, sbg);
        fb_text_clipped(UI_MARGIN, FB_H - 24u, b, TS_1X, TS_1X, UI_INNER_W);
    }
#endif

#if UI_SHOW_DECODE_PROFILE
    /* Phase F step 1 (docs/PHASE_F_SPEC.md section 14): a bench-only row.
     * At the very top of the screen (y0..15), the one strip nothing else
     * ever draws to -- the card panel starts at UI_TITLE_Y-14 = 16. It was
     * first placed above the toast band (y296..311) to avoid the y318..333
     * collision the row below is on record as having hit, but that still
     * covered UI_TIME_Y (288, the elapsed/total clock) during a real
     * playback run -- moved here after the first hardware run (B-086) so it
     * stops eating screen the clock needs on every future run.
     *
     * H/I/S are MP3's three stages as a percent of realtime, each latched
     * and reset every second like D/O above: Huffman/bit-reading (folds in
     * UnpackScaleFactors), IMDCT (folds in Dequantize and alias reduction),
     * Subband (the polyphase synthesis filterbank). Not capped at 100 --
     * that is the point, same as FLAC's old L.
     *
     * R is FLAC's bit reader, kept in its ORIGINAL units -- a share of
     * channel 0's decode (res / (res+lpc)), not a share of realtime -- so
     * this refresh is comparable to the FLAC.md numbers it is refreshing. */
    if (ui_sec != ui_last_prof) {
        ui_last_prof = ui_sec;
        char b[64], *q = b;
        /* B-097: gated on track_fmt, same as the Check/Sweep readings --
         * found reading a real sweep that r_pct (a ratio of FLAC's own two
         * accumulators, not normalised against time) can carry a plausible-
         * looking nonzero value even during MP3 playback from a small leak
         * at the format transition. */
#if MP3_PROFILE
        uint32_t h_pct = track_fmt == FMT_MP3 ? mp3_huff_cyc  / (CLK_HZ / 100u) : 0u;
        uint32_t i_pct = track_fmt == FMT_MP3 ? mp3_imdct_cyc / (CLK_HZ / 100u) : 0u;
        uint32_t s_pct = track_fmt == FMT_MP3 ? mp3_sub_cyc   / (CLK_HZ / 100u) : 0u;
        /* B-345: the finer split, same units (percent of realtime) as H/I/S. I is now IMDCT() alone
         * (AntiAlias + HybridTransform); D/A/X below break that -- plus what used to be folded into
         * it, Dequantize -- into its three real pieces, so which one actually dominates is visible
         * instead of guessed. A + X should read close to I; D is the piece I's old meaning hid. */
        uint32_t d_pct = track_fmt == FMT_MP3 ? mp3_dequant_cyc / (CLK_HZ / 100u) : 0u;
        uint32_t a_pct = track_fmt == FMT_MP3 ? mp3_alias_cyc   / (CLK_HZ / 100u) : 0u;
        uint32_t x_pct = track_fmt == FMT_MP3 ? mp3_xform_cyc   / (CLK_HZ / 100u) : 0u;
        mp3_huff_cyc = mp3_imdct_cyc = mp3_sub_cyc = 0u;
        mp3_dequant_cyc = mp3_alias_cyc = mp3_xform_cyc = 0u;
        *q++ = 'H'; q = ui_dec(q, h_pct);
        *q++ = ' '; *q++ = 'I'; q = ui_dec(q, i_pct);
        *q++ = ' '; *q++ = 'S'; q = ui_dec(q, s_pct);
        *q++ = ' '; *q++ = 'D'; q = ui_dec(q, d_pct);
        *q++ = ' '; *q++ = 'A'; q = ui_dec(q, a_pct);
        *q++ = ' '; *q++ = 'X'; q = ui_dec(q, x_pct);
#endif
#if FLAC_PROFILE
        uint32_t r_total = flac_res_cyc + flac_lpc_cyc;
        uint32_t r_pct = (track_fmt == FMT_FLAC && r_total) ? (flac_res_cyc * 100u) / r_total : 0u;
        /* B-342: the finer split. unary() (the Rice-code quotient scan) calls __clzdi2 -- a real libgcc
         * subroutine on this rv32im target, confirmed by disassembly, not an inline instruction -- so
         * its total cost is estimated as calls x a separately, cheaply measured per-call cost
         * (clz_cal_cyc, a one-time boot calibration below), not timed live in this hot per-sample loop.
         * u_pct is that ESTIMATE as a percent of the whole residual pass, so it is directly comparable
         * to r_pct above, both shares of the same flac_res_cyc+flac_lpc_cyc denominator. */
        uint32_t u_est = flac_unary_calls * clz_cal_cyc;
        uint32_t u_pct = (track_fmt == FMT_FLAC && r_total) ? (u_est * 100u) / r_total : 0u;
        flac_res_cyc = flac_lpc_cyc = 0u; flac_unary_calls = 0u;
#if MP3_PROFILE
        *q++ = ' ';
#endif
        *q++ = 'R'; q = ui_dec(q, r_pct);
        *q++ = ' '; *q++ = 'U'; q = ui_dec(q, u_pct);
#endif
        *q = 0;
        uint32_t py = 0u;   /* top edge -- above the card panel at y16 */
        uint16_t pbg = ui_grad_at(py);
        fb_rect(UI_MARGIN, py, UI_INNER_W, FB_CELL(TS_1X), pbg);
        fb_set_color(UI_RED, pbg);
        fb_text_clipped(UI_MARGIN, py, b, TS_1X, TS_1X, UI_INNER_W);
    }
#endif

#if UI_SHOW_SEEK_DIAG
    /* Two rows, redrawn every pass so nothing erases them, and frozen on the
     * last seek so there is time to read them. Toasts are suppressed in this
     * build (see ui_toast_msg) because the toast band would sit on row A.
     *
     *   row A   T target second the seek asked for
     *           A byte it jumped to, in KB
     *           B max_blocksize      R sample rate / 100
     *           S blocking strategy: 0 fixed (frame numbers), 1 variable
     *   row B   N the coded number of the first three frames decoded after
     *           P second those imply for the FIRST of them
     *           U ui_sec as it stands now
     *           E decoder result on the first frame, 0 = OK
     *
     * How to read it: P should be close to T. If P is wildly off, the frame
     * the scan landed on is not the one the seek aimed at. If the three N
     * values are not consecutive, the scan is landing on false syncs. If B or
     * R read zero, the position arithmetic never had valid inputs -- which
     * would explain both failed fixes at a stroke. */
    {
        /* Row A is everything a seek DEPENDS ON, so the same file loaded two
         * ways can be compared field by field. Reported: seeking works after
         * Load MP3 and not from a .m3u, which means one of these differs. */
        char b[64], *q = b;
        *q++ = 'Z'; q = ui_dec(q, dg_size >> 10);        /* file size, KB   */
        *q++ = ' '; *q++ = 'D'; q = ui_dec(q, dg_dur);   /* track_secs      */
        *q++ = ' '; *q++ = 'F'; q = ui_dec(q, dg_first >> 10);
        *q++ = ' '; *q++ = 'K'; q = ui_dec(q, dg_pts);   /* seek points     */
        *q++ = ' '; *q++ = 'L'; q = ui_dec(q, dg_len);   /* STREAMINFO secs */
        *q = 0;
        uint16_t g = ui_grad_at((FB_H - 40u));
        fb_rect(UI_MARGIN, FB_H - 40u, UI_INNER_W, FB_CELL(TS_1X), g);
        fb_set_color(UI_RED, g);
        fb_text_clipped(UI_MARGIN, FB_H - 40u, b, TS_1X, TS_1X, UI_INNER_W);

        q = b;
        *q++ = 'T'; q = ui_dec(q, dg_tgt);               /* asked for       */
        *q++ = ' '; *q++ = 'P'; q = ui_dec(q, dg_pos);   /* measured landing*/
        *q++ = ' '; *q++ = 'U'; q = ui_dec(q, ui_sec);
        *q++ = ' '; *q++ = 'I'; q = ui_dec(q, dg_intent);
        *q++ = ' '; *q++ = 'p'; q = ui_dec(q, dg_pn);      /* probes tried  */
        *q++ = '/'; q = ui_dec(q, dg_pfail);               /* read/parse bad */
        *q++ = '/'; q = ui_dec(q, dg_prej);                /* false syncs    */
        *q++ = ' '; *q++ = 'E'; q = ui_dec(q, dg_fe == 0xFFu ? 99u : dg_fe);
        *q = 0;
        g = ui_grad_at((FB_H - 24u));
        fb_rect(UI_MARGIN, FB_H - 24u, UI_INNER_W, FB_CELL(TS_1X), g);
        fb_set_color(UI_RED, g);
        fb_text_clipped(UI_MARGIN, FB_H - 24u, b, TS_1X, TS_1X, UI_INNER_W);
    }
#endif
}
/* ui_draw_dynamic() itself (the thin wrapper) is defined later in this file, right after
 * #include "cold.inc" -- it needs COLD_READY(), which isn't visible yet at this point (same
 * textual-ordering constraint as everything else cold-code-related in this file). */


/* cont1_key bit assignments (APF standard layout) */
#define KEY_UP      (1u << 0)
#define KEY_DOWN    (1u << 1)
#define KEY_LEFT    (1u << 2)
#define KEY_RIGHT   (1u << 3)
#define KEY_A       (1u << 4)
#define KEY_B       (1u << 5)
#define KEY_X       (1u << 6)
#define KEY_Y       (1u << 7)   /* was entirely unused before the EQ */
#define KEY_L1      (1u << 8)
#define KEY_R1      (1u << 9)
#define KEY_SELECT  (1u << 14)
#define KEY_START   (1u << 15)
#define IN_MENU     (1u << 16)

#define UNCACHED    0xC0000000u
#define MP3_SLOT_ID 2u
#define FW_SLOT_ID  1u   /* touched only to flush APF's slot cache */

/* MP3 ring buffer: an APF DMA target, so firmware must read it through the
 * UNCACHED alias -- the cached window can return stale lines with no
 * indication. Its address comes from the linker, which reserves the region
 * above the heap, so the heap can never grow into it. */
extern char _ring_start, _ring_size;
#define RING_OFF     ((uint32_t)(uintptr_t)&_ring_start)
#define RING_SIZE    ((uint32_t)(uintptr_t)&_ring_size)
#define REFILL_CHUNK 4096u


static uint8_t * const ring = (uint8_t *)(uintptr_t)(UNCACHED + (uint32_t)(uintptr_t)&_ring_start);

/* Separate DMA landing zone for the ID3 re-probe and the art decoder. It
 * CANNOT share the ring: the ring holds audio the decoder is consuming, so
 * re-reading offset 0 into it would destroy playback. */
extern char _tag_start, _tag_size;
#define TAG_OFF  ((uint32_t)(uintptr_t)&_tag_start)
#define TAG_SIZE ((uint32_t)(uintptr_t)&_tag_size)
static uint8_t * const tagbuf = (uint8_t *)(uintptr_t)(UNCACHED + (uint32_t)(uintptr_t)&_tag_start);

static uint32_t st0;
static short pcm[MAX_NCHAN * MAX_NGRAN * MAX_NSAMP];

/* Ported from HarpMudd upstream v1.5.0's release/1.5.1 branch (`8f5eb11`,
 * "Meters yield to audio when the decoder is about to run dry"): a core that
 * is about to run out of decoded audio has no business spending CPU
 * analysing it. VIZ_LED and VIZ_WINAMP_BARS call this (B-215; both read the
 * same spec_lvl[] octave cascade) -- that cascade, measured (PHASE_F_SPEC.md
 * section 7) at ~1.5% of the CPU, cheaper than
 * upstream's own ~6% cascade, but the same principle applies whenever a
 * struggling file is close to the margin.
 *
 * TWO thresholds plus a hard cap (B-260). Upstream's reasoning for two thresholds stands: a single level would
 * make the meter start and stop every frame. But upstream's numbers (stop at a third full, resume at two
 * thirds) do not survive contact with this core: meter_afford() is called from the decode loop at the TROUGH of
 * the FIFO's fill/drain cycle (just before the next frame is pushed), where the level almost never reaches two
 * thirds, so ONE dip below a third -- a menu redraw is enough -- latched the meter off until the track ended
 * (measured on the Pocket: METER YIELD 31 s and counting, the bars frozen after the menu closed). Both
 * thresholds are therefore lower and meant to be read at the trough: yield only when the FIFO is nearly
 * starved (a sixth), resume from a third, and never stay off longer than METER_YIELD_MAX_S regardless, since
 * the cascade costs ~1.5% of the CPU and a wrongly-running meter is a far smaller failure than a frozen one.
 *
 * Degrades the right way by construction: skipping feeds leaves spec_n smaller but valid (see the accumulation
 * in ui_draw_dynamic), and if a whole window is skipped spec_n is 0, so the band-update block there is skipped
 * entirely and the bands HOLD their last values rather than decaying to nothing -- the meter updates less
 * often, it does not go wrong. */
#define METER_STOP  (2048u / 6u)        /* FIFO is 2048 entries -- pcm_fifo.v AW=11; read at the trough */
#define METER_GO    (2048u / 3u)
#define METER_YIELD_MAX_S 2u            /* never yield longer than this many seconds in a row */
static uint8_t meter_yield;

/* B-234: a cheap diagnostic to confirm or rule out a suspected cause of the
 * reported "spectrum meter freezes" -- METER_STOP/METER_GO's hysteresis band
 * covers nearly half the FIFO's depth, so if steady playback keeps the level
 * oscillating in a range that dips below METER_STOP sometimes but rarely
 * climbs back to METER_GO, meter_yield could latch on indefinitely. Counts
 * whole SECONDS, not cycles() directly -- cycles() is a 32-bit counter at
 * 60 MHz that wraps every ~71.6 s (see ui_blank_touch()'s own comment on the
 * same pitfall), and a real stuck period could easily run longer than that.
 * Re-arming a one-second deadline each tick, the same technique ui_blank_touch
 * already uses, keeps every individual comparison window safely under that
 * limit regardless of how long the overall stuck period turns out to be. */
static uint32_t meter_yield_secs;      /* current consecutive seconds yielding, live */
static uint32_t meter_yield_worst;     /* worst consecutive seconds ever seen, since boot */
static uint32_t meter_yield_deadline;  /* cycles() deadline for the next +1 s tick while yielding */

static int meter_afford(void)
{
    if (idle || paused) { meter_yield = 0; meter_yield_secs = 0; return 1; }
    uint32_t lv = pcm_level();
    if (meter_yield) {
        if (lv >= METER_GO || meter_yield_secs >= METER_YIELD_MAX_S) {
            meter_yield = 0; meter_yield_secs = 0;
        } else if ((int32_t)(cycles() - meter_yield_deadline) >= 0) {
            meter_yield_secs++;
            meter_yield_deadline = cycles() + CLK_HZ;
            if (meter_yield_secs > meter_yield_worst) meter_yield_worst = meter_yield_secs;
        }
    } else if (lv < METER_STOP) {
        meter_yield = 1; meter_yield_secs = 0; meter_yield_deadline = cycles() + CLK_HZ;
    }
    return !meter_yield;
}

/* Helios/Talos H0 (docs/HELIOS_SPEC.md section 9): proves the new vblank MMIO end to end on real
 * hardware before anything (Helios's own flush logic) is built on top of an unproven register --
 * this project's own repeated "prove the primitive, then build on it" discipline (BLIT_READY()/
 * COLD_READY() before any real feature used them). A single sampled level would just look like it
 * flickers randomly on a 1 Hz-refreshed Info row (vblank toggles far faster than that) -- counting
 * rising edges over a real one-second window and showing the rate is a much stronger proof: it
 * confirms both that the signal actually toggles AND that it does so at roughly the right cadence
 * for this core's own video timing, not just "sometimes reads 1". Counts whole seconds via a
 * re-armed deadline, the same wraparound-safe technique meter_afford()'s own yield counter and
 * ui_blank_touch() already use -- called every main-loop pass regardless of what page is showing,
 * since the sample rate has to be fast enough to catch every edge, not just while a diagnostic
 * page happens to be open. */
static uint32_t vblank_cnt0;       /* hardware frame counter at the start of the current window */
static uint32_t vblank_t0;         /* cycles() at the start of the current window */
static uint32_t vblank_rate;       /* frames per second over the last full window (0 until the second window) */
static uint32_t vblank_win_at;
static uint8_t  vblank_primed;

/* B-260: reads the hardware frame counter (MMIO 0xD0 bits 31:16, tau_vs_counter.sv) instead of polling the level.
 * The vsync pulse is only ~167 us wide and this runs once per main-loop pass, milliseconds apart, so the level
 * polling above (B-242) read 0/S on the Pocket. The counter cannot miss an edge; rate = frames counted over the
 * measured elapsed time (not an assumed 1 s, the pass timing jitters). Reads 0 on a bitstream without TAU_VBLANK. */
static void vblank_sample(void)
{
    uint32_t now = cycles();
    if ((int32_t)(now - vblank_win_at) < 0) return;
    uint32_t c = (REG(R_VBLANK) >> 16) & 0xFFFFu;
    if (vblank_primed) {
        uint32_t dt = now - vblank_t0, df = (c - vblank_cnt0) & 0xFFFFu;
        vblank_rate = dt ? (df * (CLK_HZ / 1000u) + (dt / 2000u)) / (dt / 1000u) : 0u;
    }
    vblank_primed = 1u; vblank_cnt0 = c; vblank_t0 = now;
    vblank_win_at = now + CLK_HZ;
}

/* Feeds every meter from one frame of interleaved PCM.
 *
 * Lifted out of the MP3 loop so the FLAC path drives the SAME nine meters
 * rather than growing a second, subtly different implementation of them. Takes
 * the buffer explicitly: MP3 hands it Helix's output, FLAC hands it a window
 * captured in flac_emit. */
HOT_O2 static void meters_feed(const short *pcm, int n, int stereo)
{
    /* Real amplitude, not a proxy: max |sample| over the frame just
     * decoded, so the meter reflects what is actually playing. */
    {
        int32_t pk = 0, pkl = 0, pkr = 0;
        for (int i = 0; i < n; i += (stereo ? 2 : 1)) {
            int32_t l = pcm[i];        if (l < 0) l = -l;
            int32_t r = stereo ? pcm[i + 1] : l; if (r < 0) r = -r;
            if (l > pkl) pkl = l;
            if (r > pkr) pkr = r;
        }
        pk = (pkl > pkr) ? pkl : pkr;
        /* Accumulate rather than assign -- see peak_acc. ui_draw_dynamic()
         * publishes the maximum once per display frame, so a chunk that lands
         * between frames still counts instead of being overwritten. */
        if ((uint32_t)pk  > peak_acc)   peak_acc   = (uint32_t)pk;
        if ((uint32_t)pkl > peak_acc_l) peak_acc_l = (uint32_t)pkl;
        if ((uint32_t)pkr > peak_acc_r) peak_acc_r = (uint32_t)pkr;
        peak_acc_any = 1u;

        /* The spectrum is measured by the hardware bank (tau_spec_bank.sv), read in ui_draw_dynamic(); nothing to do here.
         * (The software octave cascade was removed once the bank was proven: about 1 KB of hot code in this audio path.) */

        /* Even spread across the frame, so the trace covers the whole
         * period rather than clustering at its start. */
        uint32_t pairs = (uint32_t)(stereo ? (n / 2) : n);
        uint32_t step  = pairs / SCOPE_N;
        if (step && pk > 0) {
            /* AUTO-GAIN, normalised to this frame's peak.
             *
             * A fixed shift was sized for full-scale samples, and real
             * music sits far below that -- mid/side landed a couple of
             * pixels from centre and the trace was a smudge. Scaling to the
             * peak makes the shape readable at any level, which is the
             * whole point: this mode shows correlation, not loudness. The
             * L/R bars already show loudness.
             *
             * One divide per frame, then a shift per point. Averages rather
             * than sums, so mid and side each span +-pk and the reciprocal
             * maps them exactly onto the box. */
            int32_t scale = ((int32_t)SCOPE_UNIT << 15) / (int32_t)pk;
            scope_head = (uint8_t)((scope_head + 1u) % SCOPE_HIST);
            signed char *sx = scope_x[scope_head], *sy = scope_y[scope_head];
            for (uint32_t k = 0; k < SCOPE_N; k++) {
                uint32_t idx = k * step;
                int32_t l = pcm[stereo ? idx * 2u : idx];
                int32_t r = stereo ? pcm[idx * 2u + 1u] : l;
                int32_t mid  = (l + r) / 2;      /* mono -> x = 0 */
                int32_t side = (l - r) / 2;
                sx[k] = (signed char)((side * scale) >> 15);
                sy[k] = (signed char)((mid  * scale) >> 15);
            }

            /* Oscilloscope columns, same normalisation. Min and max over
             * each slice rather than a single sample: point-sampling a
             * waveform at 64 points aliases badly and the trace jumps
             * about; the envelope is stable and shows the real shape. */
            uint32_t need = WAVE_COLS * WAVE_SPAN;
            if (pairs > need) {
                /* Trigger: first rising crossing of zero, searched only in
                 * the slack between the window and the frame so there is
                 * always a full window left to draw. */
                uint32_t trig = 0, limit = pairs - need;
                int32_t prev = 0;
                for (uint32_t i2 = 0; i2 < limit; i2++) {
                    int32_t l = pcm[stereo ? i2 * 2u : i2];
                    int32_t rr = stereo ? pcm[i2 * 2u + 1u] : l;
                    int32_t m = (l + rr) / 2;
                    if (prev < 0 && m >= 0) { trig = i2; break; }
                    prev = m;
                }
                for (uint32_t c = 0; c < WAVE_COLS; c++) {
                    uint32_t idx = trig + c * WAVE_SPAN;
                    int32_t l = pcm[stereo ? idx * 2u : idx];
                    int32_t rr = stereo ? pcm[idx * 2u + 1u] : l;
                    int32_t m = (l + rr) / 2;
                    wav_v[c] = (signed char)((m * scale) >> 15);
                }
            }
        }
    }

}


/* ------------------------------------------------------------- controls --
 * A / Start : play-pause          Left / Right      : tap  = seek -/+ ~5 s
 * Up / Down : volume                                  hold = previous/next
 * B         : restart this track from 0:00
 * Select    : show/hide art       Select + L        : cycle repeat off/all/one
 * L / R     : accent colour       Select + R        : shuffle on/off
 *
 * Left/Right and Select both do one thing on a tap and another when held or
 * combined. Both resolve on RELEASE, so the tap action cannot fire and then be
 * followed by the hold action for the same press. A tap is tens of
 * milliseconds, so deferring it that long is not perceptible.
 */
static uint32_t skip_req;                /* +1 next, -1 previous (as unsigned) */
static uint32_t dt_dump_req;             /* Select+A: show the boot datatable  */
static uint32_t ui_dump_mode;            /* dump screen is up; drawing paused  */

/* Arm the idle timer. Called on every button press and whenever the timeout
 * setting changes. */
/* Counts SECONDS, deliberately. A cycles() deadline cannot express this: the
 * counter is 32-bit at 60 MHz, so it wraps every 71.6 s and the usual
 * (int32_t)(cycles() - deadline) >= 0 idiom only spans 35.8 s. One minute is
 * already past that and two minutes overflows the multiply outright, so the
 * first version could not have worked at any setting. Every other timeout in
 * this core is sub-second, which is why nothing had hit the ceiling before.
 *
 * A one-second tick is comfortably inside the safe range, and seconds are what
 * the setting is measured in anyway. */
static void ui_blank_touch(void)
{
    blank_sec  = 0;
    blank_tick = cycles() + CLK_HZ;
}

static void ui_blank_enter(void)
{
    if (screen_blank) return;
    screen_blank = 1u;
    fb_rect(0, 0, FB_W, FB_H, 0x0000u);
}

static void ui_blank_wake(void)
{
    if (!screen_blank) return;
    screen_blank = 0;
    ui_chrome_paint();          /* everything suppressed while blank, redrawn */
    /* ui_draw_chrome just painted the PLAYER. If the overlay was up when the
     * screen blanked it is still logically open and still eating the d-pad,
     * so without this the user would be left driving an invisible list. */
    if (lib_ui_open) lib_ui_dirty = 1u;
    if (set_open) set_dirty = 1u;
}

static void ui_blank_pump(void)
{
    if (screen_blank || !blank_min) return;
    if ((int32_t)(cycles() - blank_tick) < 0) return;
    blank_tick = cycles() + CLK_HZ;
    if (blank_sec < 0xFFFFu) blank_sec++;
    if (blank_sec >= blank_min * 60u) ui_blank_enter();
}

#if TAU_DIAGNOSTIC
#include "stress.inc"
#endif

/* Black the whole frame. Only the framebuffer -- there is no way to switch the
 * panel itself off from a core, so "blank" means every pixel black. The
 * backlight stays on regardless; see the note on blank_min. */
__attribute__((optimize("Os")))       /* button handling: no timing role; RAM is the constraint in the library build */
static void poll_input(void)
{
    static uint32_t prev;
    static uint32_t lr_t0[2];            /* when Left/Right went down          */
    static uint8_t  lr_fired[2];         /* the hold already skipped           */
    static uint8_t  sel_used;            /* Select was used as a modifier      */
    static uint32_t sel_t0;              /* when Select went down              */
    static uint8_t  sel_held;            /* the hold action already ran        */
#if TAU_DIAGNOSTIC
    stress_tick();
#endif
    uint32_t in   = REG(R_INPUT);
    uint32_t keys = in & 0xFFFFu;
    uint32_t edge = keys & ~prev;        /* rising edges only  */
    uint32_t fall = prev & ~keys;        /* falling edges      */
    prev = keys;

    /* Any press is activity. If the screen is blank the press ONLY wakes it and
     * is then swallowed -- reaching for a sleeping player to see what is on
     * should not pause it or skip the track. */
    if (edge || fall) ui_blank_touch();
    if (screen_blank) {
        if (edge) ui_blank_wake();
        /* Swallow the waking press -- but do NOT return. The tail of this
         * function handles the OS menu and the reload notification, and
         * returning early meant "Load MP3" was ignored while the screen was
         * blank. Clearing keys too, so a button already held cannot trigger a
         * hold action either; `prev` was assigned above and still carries the
         * real state, so the next pass sees correct edges. */
        edge = 0; fall = 0; keys = 0;
    }

    /* Start+X: jump straight to Meter > Configure, skipping Home -> Appearance -> Meter navigation.
     * Checked BEFORE SET_INPUT below, which otherwise treats any bare Start edge (settings closed) as
     * "open at SET_HOME" and would consume it first. Two-sided check -- either button's edge can land
     * first as long as the other is already held by the time it does -- because Start, unlike Select,
     * has a strong action of its own on a bare press, so a one-sided "Start held, X edges" convention
     * (the way Select+X/Y work) cannot be used: SET_INPUT would already have opened Settings before X
     * is ever pressed. */
    if (!set_open && !lib_ui_open && cold_code_ok &&
        (((edge & KEY_START) && (keys & KEY_X)) || ((edge & KEY_X) && (keys & KEY_START)))) {
        set_open_wvizcfg_direct();          /* cold code (fw/settingsui.inc) -- not visible this early in the file */
        edge &= ~(uint32_t)(KEY_START | KEY_X);
    }
    /* ---- the overlay owns most of the pad while it is up -------------------
     * Handled BEFORE every normal binding, then the consumed bits are cleared
     * so nothing downstream also acts on them. Select is deliberately left in
     * `keys`/`fall`: its tap-to-close is the same code that opened it, and its
     * hold timer reads `keys` directly. */
    /* Select is left in edge/fall: tapping it while Settings is up leaves Settings for the list (below),
     * the inverse of Start closing the list. */
    if (SET_INPUT(edge, keys)) {
        edge &= KEY_SELECT; fall &= KEY_SELECT;
        /* `keys` too: the Left/Right scrub below reads the held level with the timestamp of the last press it saw, so a
         * Left/Right pressed in a settings page (volume, the Check profile) counted as an old, long hold and sought the
         * track by 5 s per press (B-065). Select stays: its tap and hold logic read it directly. */
        keys &= KEY_SELECT;
    }
    if (lib_ui_open) lib_ui_input(&edge, &fall, &keys);

    /* A plays and pauses. Start only ever STOPS -- pressing it again does
     * nothing, which is what separates it from pause: stop is a state you
     * leave with play, not a toggle. Stopping also returns to 0:00. */
    /* A: TAP plays/pauses, LONG HOLD toggles 1.2x speed.
     *
     * 1.2x is a MODE -- it has always toggled rather than being momentary. The
     * problem was never that it toggled, it was how easy the gesture was to
     * perform by accident: the hold shared PL_HOLD_MS with the scrub, so
     * lingering on the play button put people into 1.2x with nothing on screen
     * to say so. SPEED_HOLD_MS is three times as long, and the indicator on the
     * time row makes the mode visible while it is on.
     *
     * Resolves on RELEASE, the same discipline Left/Right and Select already
     * use. Firing the tap action on the press instead would mean a long press
     * pauses AND changes speed -- it is on the way into every hold. */
    {
        static const uint8_t a_fired = 0u;    /* no hold action: every release is a tap */

        if ((fall & KEY_A) && !a_fired) {
            /* Select+A shows the boot datatable snapshot, mirroring Select+B
             * for the 0190 struct. Plain A still plays/pauses. */
            {
                paused ^= 1u;
                if (!(paused & 1u)) stopped = 0;  /* playing is never "stopped" */
            }
        }
    }
    /* Select+Y: fullscreen visualiser on/off. Select+X: next preset of the current meter. Both consume the key so the plain
     * actions (EQ preset on Y, next meter on X) do not also fire, and mark Select as used so releasing it does not open the
     * playlist. */
    if ((keys & KEY_SELECT) && (edge & (KEY_X | KEY_Y))) {
        sel_used = 1u;
        if (cold_code_ok) {                       /* both are cold code (COLD_READY() is not visible this early in the file) */
            if (edge & KEY_Y) ui_fs_toggle();
            if (edge & KEY_X) meter_preset_next();
        }
        edge &= ~(uint32_t)(KEY_X | KEY_Y);
    }
    if (edge & KEY_X) {
        /* Forward only. A reverse on Select+X existed and was dropped: nine
         * modes wrap in a handful of taps, and every Select combo the user has to
         * remember costs more than it saves. */
        viz_mode = (uint8_t)viz_sel_to_mode((viz_mode_to_sel(viz_mode) + 1u) % VIZ_SEL_COUNT);
        ui_wave_clear();                 /* modes do not share a screen layout */
        ui_wave_force = 1u;
        wviz_force = 1u;                 /* B-234: same reason -- Winamp Bars/Scope's own
                                           * change-cache/smoothing state must not carry over
                                           * from whatever was last drawn in this position. */
        for (uint32_t i = 0; i < UI_WAVE_N; i++) {
            wave_drawn[i] = 0xFFu; wave_pk_drawn[i] = 0xFFu;
            for (uint32_t z = 0; z < SPEC_BANDS; z++) spec_drawn[z] = 0xFFu;
        }
        if (art_ready && art_shown) ui_art_draw();
        ui_toast_msg(viz_mode == VIZ_BARS   ? "METER: BARS"
                   : viz_mode == VIZ_WATER  ? "METER: WATERFALL"
                   : viz_mode == VIZ_SCOPE  ? "METER: PHASE SCOPE"
                   : viz_mode == VIZ_WAVE   ? "METER: OSCILLOSCOPE"
                   : viz_mode == VIZ_VU     ? "METER: VU"
                   : viz_mode == VIZ_SCROLL ? "METER: WAVEFORM"
                   : viz_mode == VIZ_DOTS   ? "METER: PEAK DOTS"
                   : viz_mode == VIZ_LED    ? "METER: SPECTRUM"
                   : viz_mode == VIZ_WINAMP_BARS  ? "METER: WINAMP BARS"
                   : viz_mode == VIZ_WINAMP_SCOPE ? "METER: WINAMP SCOPE"
                   : viz_mode == VIZ_CHLADNI      ? "METER: CHLADNI"
                   : viz_mode == VIZ_VU_MASTER    ? "METER: MASTER VU"
                   : viz_mode == VIZ_LAYERED_WAVE ? "METER: LAYERED WAVE"
                                            : "METER");
        settings_mark_dirty();
    }
    if (edge & KEY_Y) {
        /* Forward only, matching X. Y was completely unused before the EQ. */
        eq_idx = (uint8_t)((eq_idx + 1u) % EQ_COUNT);
        eq_apply = 1u;
    }
#if TAU_DIAGNOSTIC
    /* The normal Start action stops playback. In the developer stress build,
     * Select+Start is an explicit HUD refresh and MUST consume the combo: the
     * test is only meaningful while audio and visualizer traffic continue. */
    if ((edge & KEY_START) && (keys & KEY_SELECT)) {
        sel_used = 1;
        stress_hud_tick = 0xFFFFFFFFu;
        stress_hud_draw();
        ui_toast_msg("SDRAM HUD");
        return;
    }
#endif
    if (edge & KEY_START) {
        if (!stopped) {
            stopped = 1u; paused |= 1u; stop_req = 1u;
        }
    }
    if (edge & KEY_B) {
        stop_req = 1u;              /* restart = reposition, NOT a cold reload */
    }

    /* ---- Left/Right: tap changes track, hold seeks ----
     * Skipping is the common action, so it gets the cheap gesture. Holding
     * then scrubs: the seek REPEATS while the button is down, which is what
     * makes holding useful rather than just a slower single jump. */
    {
        static uint8_t lr_reps[2];        /* consecutive scrub repeats, per side */
        const uint32_t kmask[2] = { KEY_LEFT, KEY_RIGHT };
        const uint32_t hold_cy  = CLK_HZ / 1000u * PL_HOLD_MS;
        const uint32_t rep_cy   = CLK_HZ / 1000u * 250u;   /* scrub rate */
        for (int i = 0; i < 2; i++) {
            if (edge & kmask[i]) { lr_t0[i] = cycles(); lr_fired[i] = 0; lr_reps[i] = 0; }

            /* Select + Left/Right: one second a press, for landing on a spot
             * rather than sweeping past it. The shoulder buttons already carry
             * Select+L and Select+R, so the D-pad pair was free. */
            if ((edge & kmask[i]) && (keys & KEY_SELECT)) {
                sel_used    = 1;
                lr_fired[i] = 1;              /* release must not skip track */
                seek_secs   = 1u;
                seek_req    = i ? 1u : (uint32_t)-1;
                ui_toast_msg(i ? "SEEK +1s" : "SEEK -1s");
                continue;
            }
            if (keys & KEY_SELECT) continue;  /* no scrub while modifying */

            if ((keys & kmask[i]) &&
                (int32_t)(cycles() - lr_t0[i]) >= (int32_t)hold_cy) {
                lr_fired[i] = 1;                  /* release must not skip */
                /* Accelerate: a fixed step means crossing a long track is a
                 * lot of repeats, and a big step means you cannot land near
                 * anything. Start small and grow the longer it is held. */
                if (lr_reps[i] < 255u) lr_reps[i]++;
                seek_secs = (lr_reps[i] > 8u) ? 30u : (lr_reps[i] > 3u) ? 10u : 5u;
                seek_req  = i ? 1u : (uint32_t)-1;
                ui_toast_set(i ? "SEEK +" : "SEEK -", seek_secs, "s");
                lr_t0[i] = cycles() + rep_cy - hold_cy;   /* next repeat */
            }

            if (fall & kmask[i]) {
                if (!lr_fired[i]) {               /* a tap: change track */
                    /* skip_req fires off the library queue (lib_qn) -- the only queue there is
                     * now that legacy playlist mode is gone. */
                    if (lib_src && lib_qn) skip_req = i ? 1u : (uint32_t)-1;
                    else     ui_toast_msg("NO LIBRARY QUEUE");
                }
                lr_fired[i] = 0;
            }
        }
    }

    /* Up/Down are now guarded by Select, which they were not before: plain
     * Select+Up used to change the volume AND toggle the art panel on release,
     * because nothing claimed the combo. Select+Down needs it properly. */
    if (!(keys & KEY_SELECT)) {
        if (edge & KEY_UP)   { volume = (volume + VOL_STEP > VOL_MAX)
                                      ? VOL_MAX : volume + VOL_STEP;
                               vol_apply(); ui_toast_set("VOLUME", volume, "%");
                               ui_mode_dirty = 1u;   /* the speaker icon */
                               settings_mark_dirty(); }
        if (edge & KEY_DOWN) { volume = (volume < VOL_STEP) ? 0u : volume - VOL_STEP;
                               vol_apply(); ui_toast_set("VOLUME", volume, "%");
                               ui_mode_dirty = 1u;   /* the speaker icon */
                               settings_mark_dirty(); }
    }

    /* Select+Down cycles the blank timeout. It is the ONLY way to reach this
     * now -- the Core Settings entry was given up so resume could have the
     * slot -- so the cycle has to cover the useful values, not just on/off.
     * Sits beside Select+L (repeat) and Select+R (shuffle), which is where a
     * user already looks for settings-ish combos. Not persisted: it is back to
     * OFF every launch, which is the honest cost of the trade. */
    else if (edge & KEY_DOWN) {
        static const uint8_t bl[] = { 0u, 1u, 5u, 10u, 30u };
        uint32_t i = 0;
        while (i < sizeof(bl) / sizeof(bl[0]) && bl[i] != blank_min) i++;
        i = (i + 1u) % (sizeof(bl) / sizeof(bl[0]));
        blank_min = bl[i];
        sel_used  = 1;
        ui_blank_touch();               /* restart the countdown from now */
        if (blank_min) ui_toast_set("SCREEN BLANK", blank_min, " MIN");
        else           ui_toast_msg("SCREEN BLANK OFF");
    }

    /* ---- Select as a modifier for L/R ---- */
    if (edge & KEY_SELECT) { sel_used = 0; sel_t0 = cycles(); sel_held = 0; }

    if (keys & KEY_SELECT) {
        if (edge & KEY_L1) {
            sel_used = 1;
            rep_mode = (uint8_t)((rep_mode + 1u) % 3u);
            ui_toast_msg(rep_mode == REP_OFF ? "REPEAT OFF"
                       : rep_mode == REP_ALL ? "REPEAT ALL" : "REPEAT ONE");
            ui_mode_dirty = 1u;
            settings_mark_dirty();
        }
    } else {
        /* Apply the colour HERE, not in ui_draw_dynamic(). Deferring it meant a
         * track change could run load_track() -> ui_draw_chrome() first and
         * repaint everything in the PREVIOUS accent, so the choice appeared to
         * be forgotten. ui_accent_changed still drives the invalidation work. */
        if (edge & KEY_R1) { ui_pal_idx = (ui_pal_idx + 1u) % UI_PALETTE_N;
                             ui_accent = th_accent_of(ui_pal_idx);
                             ui_grad_set(ui_accent);
                             ui_accent_changed = 1u;
                             ui_toast_set("COLOR: ", 0xFFFFFFFFu,
                                          ui_palette_name[ui_pal_idx]);
                             settings_mark_dirty(); }
        if (edge & KEY_L1) { ui_pal_idx = (ui_pal_idx + UI_PALETTE_N - 1u) % UI_PALETTE_N;
                             ui_accent = th_accent_of(ui_pal_idx);
                             ui_grad_set(ui_accent);
                             ui_accent_changed = 1u;
                             ui_toast_set("COLOR: ", 0xFFFFFFFFu,
                                          ui_palette_name[ui_pal_idx]);
                             settings_mark_dirty(); }
    }

    /* A Select that was neither a modifier nor a hold is a TAP: the library.
     * Opening lands wherever the browser was left (lib_ui_enter()'s own
     * browse-position memory). */
    if ((fall & KEY_SELECT) && !sel_used && !sel_held) {
        if (set_open) set_close();
        if (lib_ui_open) lib_ui_close();
        else if (lib_state == LIB_ST_OK) lib_ui_enter();
        else ui_toast_msg("NO LIBRARY LOADED");
    }

    /* Pause while the OS menu ("Load MP3" etc) is open, without clobbering the
     * user's own A/Start pause -- bit 1 is the menu's, bit 0 is theirs. */
    /* Opening the OS menu is the strongest signal that the core is about to
     * be left, so a pending settings write goes out now rather than waiting
     * out its quiet window that may never elapse. */
    /* The open/closed memory is its OWN flag, not paused's menu bit.
     * load_track() ends with `paused = 0`, which clears that bit wholesale --
     * so any load running while the menu was open erased the evidence and the
     * closing edge below never fired. */
    if (in & IN_MENU) {
        if (!menu_was) { set_flush_now = 1u; menu_was = 1u; }
        paused |= 2u;
    } else {
        menu_was = 0u;
        paused &= ~2u;
    }

    /* Only SET the flag here. This runs from inside the sample-push wait,
     * which is where the CPU spends most of its time when keeping up, so a
     * reload is noticed immediately instead of after the current frame. */
    if (REG(R_RELOAD) & RL_PENDING) reload_pending = 1u;
}

/* Drops every sample queued in the hardware FIFO. Required on any
 * discontinuity (track change, seek, restart) -- without it the OLD position's
 * queued ~43 ms keeps draining while the new position spins up. */
static inline void pcm_flush(void)
{
    REG(R_PCM_ST) = 1u;
    fade_left    = FADE_SAMPLES;  /* every flush is a discontinuity */
    under_shadow = 0;             /* flush clears the sticky underrun flag */
#if TAU_DIAGNOSTIC
    stress_frames_at_flush = frames;
#endif
}

static uint32_t rd_seq0, rd_deadline, rd_len;
static int      rd_pending, rd_ok;

static uint8_t  eof_hit;  /* a refill ran off the end: the file's true extent */
static uint8_t  sw_prev_head[16];   /* head of the song being left */
static uint8_t  sw_have_prev;
static uint32_t eof_at;   /* offset the failures are accumulating at        */
static uint32_t eof_fails;

/* Periodic ID3 re-probe: a light backstop behind the 0190 identity gate. */

static int  target_read_poll(void);
static void refill_drain(void);

static void target_read_start_slot(uint32_t slot, uint32_t off,
                                   uint32_t dst_off, uint32_t len)
{
    refill_drain();                     /* exactly one command in flight, ever */
    rd_seq0 = (REG(R_TGT_GO) >> 8) & 0xFFu;

    REG(R_TGT_ID)  = slot;
    REG(R_TGT_OFF) = off;
    REG(R_TGT_ADR) = dst_off;
    REG(R_TGT_LEN) = len;
    REG(R_TGT_GO)  = TGT_READ;

    rd_len      = len;
    rd_deadline = cycles() + CLK_HZ * 5u;
    rd_pending  = 1;
}

static void target_read_start(uint32_t off, uint32_t dst_off, uint32_t len)
{
    target_read_start_slot(MP3_SLOT_ID, off, dst_off, len);
}

/* Waits out an in-flight read WITHOUT committing it: callers that drain are
 * about to reset or move the ring anyway, and the bytes land above ring_fill
 * where nothing will read them. */
HOT_O2 static void refill_drain(void)
{
    while (rd_pending) if (target_read_poll()) rd_pending = 0;
}

/* Completion is detected by watching the SEQUENCE COUNTER change, not the
 * `done` bit. `done` stays asserted from the previous command until the next
 * one is picked up, so polling it right after issuing GO can observe the
 * PREVIOUS command's completion and return with the transfer still in flight.
 * Only the first command after reset escapes that -- which is exactly why the
 * boot read worked and every later one did not. See tgt_cmd.v. */
static int target_read_poll(void)
{
    uint32_t s = REG(R_TGT_GO);
    if (((s >> 8) & 0xFFu) != rd_seq0) { rd_ok = (((s >> 2) & 7u) == 0u); return 1; }
    if ((int32_t)(cycles() - rd_deadline) >= 0) { rd_ok = 0; return 1; }
    return 0;
}

/* Blocking form -- only for the head read, prefill and the probes, where there
 * is no audio to starve. The steady-state path must NOT use this. */
static int target_read_slot(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len)
{
    target_read_start_slot(slot, off, dst_off, len);
    /* The boot indicator is animated from HERE -- this spin is where the time
     * actually goes during a slow read. ui_boot_tick() is a single compare
     * and return unless a note is armed, so every other caller of this
     * function is unaffected. */
    while (!target_read_poll()) { ui_boot_tick(); ui_wave_anim_tick(); }
    rd_pending = 0;
    return rd_ok;
}

static int target_read(uint32_t off, uint32_t dst_off, uint32_t len)
{
    return target_read_slot(MP3_SLOT_ID, off, dst_off, len);
}

/* core_bridge_cmd's datatable, reachable by both the CPU and APF. Used for the
 * 0190 response struct. */
static inline uint32_t dt_read(uint32_t word)
{
    REG(R_DT_ADDR) = word;
    return REG(R_DT_DATA);
}

static uint32_t probe_clamp_ref;    /* what a past-EOF read returns, if any */
static int      probe_clamps;

/* Is `off` inside the file? Poison the landing zone and see whether anything
 * lands. Deliberately does not trust the command's status: a short read at the
 * tail can still report success. */
static int probe_readable(uint32_t off)
{
    volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)(UNCACHED + TAG_OFF);
    *w = 0xA5A5A5A5u;
    /* 512 bytes, not 4: a four-byte transfer is the sort of request a host may
     * refuse or round, and if every probe fails the search reports "no size". */
    if (!target_read_slot(MP3_SLOT_ID, off, TAG_OFF, 512u)) return 0;
    if (*w == 0xA5A5A5A5u) return 0;              /* nothing landed: past EOF */
    /* Some hosts CLAMP instead of failing -- a read past the end quietly
     * returns the tail, so the sentinel always says "readable". When that is
     * detected, treat "identical to a hopeless offset" as past-EOF instead. */
    if (probe_clamps && *w == probe_clamp_ref) return 0;
    return 1;
}


/* ------------------------------------------------- the same search, in steps
 *
 * probe_file_size() below is ~20 blocking reads, measured at 480 ms, and it
 * runs inside load_track(). That is 480 ms of every load of a file that does
 * not declare its own length -- which is every FLAC, since only MP3 carries a
 * Xing header -- and until this it was ALSO paid again on the first seek of a
 * track, because the load-time answer is wrong for a file opened by name.
 *
 * Same binary search, one read per call, driven from the main loop while the
 * ring is full. The size is not needed immediately: it feeds the progress bar,
 * the bitrate readout and the seek bracket, none of which matter in the first
 * second of playback. It is needed CORRECTLY, though, and a probe that runs a
 * little later is the one that gets the right answer -- a file opened with
 * 0192 has only just been opened at load, and a random read far into it still
 * fails, which is how a 30 MB track measured 5 MB.
 *
 * Phases: 0 idle, 1 the clamp reference, 2 doubling to bracket the end,
 * 3 bisecting, 4 done. */
static uint8_t  szp_phase;
static uint32_t szp_lo, szp_hi;

static void size_probe_arm(void)
{
    szp_phase = 1u;
    szp_lo = 0; szp_hi = 1u << 22;      /* start at 4 MB, as the blocking one does */
}

/* How far playback must have got before the search may start.
 *
 * This is the whole reason the measurement was wrong before. A file opened by
 * name with 0192 does not answer a random read far into it straight away, so a
 * probe at load time stops early and reports a fraction of the true size -- 5
 * MB of a 30 MB track, measured. Waiting for a quarter of a megabyte to have
 * been READ is direct evidence the slot is streaming properly, and it is a
 * better gate than a timer because it is the same thing the probe depends on.
 * A file smaller than this reaches eof_hit instead, and refill_pump() records
 * the true end there for free. */
#define SZP_START_AFTER (256u * 1024u)

/* One step. Returns 1 when a size has just been established. */
static int size_probe_step(void)
{
    /* Measured from the first AUDIO byte, not from the start of the file.
     *
     * It was `file_pos < SZP_START_AFTER`, which stopped working the moment
     * flac_open learned to skip metadata: on these files the cover art is
     * 642 KB, so the skip lands file_pos past a 256 KB gate during the LOAD --
     * before a single audio byte has been read. The probe then ran immediately,
     * which is exactly the too-early case the gate exists to prevent, and a
     * short answer followed. Below fl_first_frame it makes the bitrate
     * uncomputable and the format row stays blank; above it, the row shows a
     * wrong number. That is the "works on some FLACs" report.
     *
     * Against fl_first_frame the gate means what it always meant: a quarter of
     * a megabyte of AUDIO has been streamed, so the slot has settled. */
    /* Measured from the first AUDIO byte -- which is fl_first_frame on a FLAC
     * and audio_start on an MP3. It used fl_first_frame unconditionally, a
     * field only the FLAC path ever sets, so on an MP3 the gate was measured
     * from either zero or whatever the last FLAC left behind. */
    uint32_t first_audio = (track_fmt == FMT_FLAC) ? fl_first_frame : audio_start;
    if (szp_phase == 1u && !eof_hit &&
        file_pos < first_audio + SZP_START_AFTER) return 0;

    switch (szp_phase) {
    case 1:
        probe_clamps = 0;
        {
            volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)(UNCACHED + TAG_OFF);
            *w = 0xA5A5A5A5u;
            if (target_read_slot(MP3_SLOT_ID, 60u << 20, TAG_OFF, 512u) &&
                *w != 0xA5A5A5A5u) {
                probe_clamp_ref = *w;
                probe_clamps    = 1;
            }
        }
        if (!probe_readable(0)) { szp_phase = 4u; return 0; }
        szp_phase = 2u;
        return 0;

    case 2:
        if (szp_hi < (1u << 26) && probe_readable(szp_hi)) {
            szp_lo = szp_hi; szp_hi <<= 1;
            return 0;
        }
        if (szp_hi >= (1u << 26)) { szp_phase = 4u; return 0; }   /* runaway */
        szp_phase = 3u;
        return 0;

    case 3:
        if (szp_hi - szp_lo > 4096u) {
            uint32_t mid = szp_lo + (szp_hi - szp_lo) / 2u;
            if (probe_readable(mid)) szp_lo = mid; else szp_hi = mid;
            return 0;
        }
        szp_phase = 4u;
        /* Only ever grows it. An early probe stops where a read first fails,
         * so it can fall short of the true end but never run past it, and a
         * size that came from APF directly is better than any measurement. */
        if (szp_lo + 4096u > slot_size) {
            slot_size = szp_lo + 4096u;
            return 1;
        }
        return 0;

    default:
        return 0;
    }
}

/* Measure the file. APF only reports a size with a RELOAD notification, so a
 * track loaded at boot has none -- which is why the progress bar and the
 * end-of-track check need this. Binary-searching the last readable offset
 * needs no notification, no struct layout and no status semantics.
 *
 * Kept blocking for the one caller that cannot wait: the seek backstop, where
 * the answer is needed before the seek it was asked for. */
static uint32_t probe_file_size(void)
{
    probe_clamps = 0;
    {
        volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)(UNCACHED + TAG_OFF);
        *w = 0xA5A5A5A5u;
        if (target_read_slot(MP3_SLOT_ID, 60u << 20, TAG_OFF, 512u) &&
            *w != 0xA5A5A5A5u) {
            probe_clamp_ref = *w;
            probe_clamps    = 1;
        }
    }

    if (!probe_readable(0)) return 0;

    uint32_t lo = 0, hi = 1u << 22;                    /* start at 4 MB */
    while (hi < (1u << 26) && probe_readable(hi)) { lo = hi; hi <<= 1; }
    if (hi >= (1u << 26) && probe_readable(hi)) return 0;   /* runaway */

    while (hi - lo > 4096u) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (probe_readable(mid)) lo = mid; else hi = mid;
    }
    return lo + 4096u;
}

/* Where APF's 0190 response lands in the datatable.
 *
 * These used to live in playlist.inc, which is included further down -- so
 * slot_file_id() and slot_filename() below could not see them and were still
 * written against the ORIGINAL layout, with the response at word 0. It was
 * moved to word 64 because APF's dataslot ID/size table occupies the start of
 * this BRAM and every 0190 was overwriting it; these two never followed.
 *
 * They have therefore been reading the ID/SIZE TABLE this whole time: binary
 * pairs with no text in them, so slot_filename() found no printable run and
 * track_file came back EMPTY on every load. That is why an untagged file
 * showed UNKNOWN TRACK instead of its name. */
#define DT_RESP_W   64u     /* response struct  (APF writes) */
#define DT_PARAM_W  128u    /* parameter struct (APF reads)  */
#define DT_WORDS    64u     /* 256 bytes, matching slot_filename()'s window */

static void slot_filename(char *out, uint32_t out_size);

/* Ask APF which file is CURRENTLY in the slot (0190) and reduce its response
 * to one number. This is the authoritative answer to "has the slot changed
 * yet?" -- every content-based guess at that was defeated by a read that was
 * wrong but stable. HASHES the struct rather than parsing it, so nothing here
 * depends on a field offset that would otherwise be guesswork. */
/* `want_name` distinguishes the two callers. A LOAD wants the filename as a
 * side effect; the periodic identity poll must not have it, or it would
 * overwrite the playing track's name with the slot's before the load that
 * makes it true. */
static uint32_t slot_id_query(int want_name)
{
    /* refill_drain() waits out an in-flight read and DISCARDS it -- see its own
     * comment. That is right for a caller about to move the ring, and wrong for
     * a poll that runs every couple of seconds during playback: it would throw
     * away a 4 KB read each time and force it to be fetched again.
     *
     * Both callers now gate on !rd_pending, so this is a no-op in the steady
     * state. Kept because the command layer requires exactly one in flight, and
     * a future caller must not have to know that. */
    refill_drain();                     /* one command in flight, ever */
    uint32_t seq0 = (REG(R_TGT_GO) >> 8) & 0xFFu;
    REG(R_TGT_ID) = MP3_SLOT_ID;
    REG(R_TGT_GO) = TGT_GETFILE;

    uint32_t deadline = cycles() + CLK_HZ;
    for (;;) {
        uint32_t st = REG(R_TGT_GO);
        if (((st >> 8) & 0xFFu) != seq0) break;
        if ((int32_t)(cycles() - deadline) >= 0) return 0;   /* 0 = unknown */
    }

    if (want_name) slot_filename(track_file, sizeof(track_file));

    uint32_t h = 2166136261u;                   /* FNV-1a over the struct */
    for (uint32_t w = 0; w < DT_WORDS; w++) {
        uint32_t v = dt_read(DT_RESP_W + w);
        for (int b = 0; b < 4; b++) {
            h ^= (v >> (b * 8)) & 0xFFu;
            h *= 16777619u;
        }
    }
    return h ? h : 1u;                          /* keep 0 for "unknown" */
}

static uint32_t slot_file_id(void) { return slot_id_query(1); }

/* Is the slot pointing somewhere other than what is loaded?
 *
 * The playlist slot has had a periodic identity poll since the double-load bug
 * (pl_poll_at); the TRACK slot never got one, and relies entirely on the 008A
 * notification. When that notification goes missing the load simply does not
 * happen and nothing recovers it -- which is the "picked a song, waited, had
 * to pick it again" fault, the same shape as the playlist bug on the path that
 * was never fixed. */
static int slot_changed(void)
{
    uint32_t id = slot_id_query(0);
    return id && cur_file_id && id != cur_file_id;
}

/* Pull the filename out of the 0190 response WITHOUT knowing its layout: the
 * longest run of printable ASCII in the struct IS the name. */
static void slot_filename(char *out, uint32_t out_size)
{
    uint8_t raw[DT_WORDS * 4u];
    for (uint32_t w = 0; w < DT_WORDS; w++) {
        uint32_t v = dt_read(DT_RESP_W + w);
        /* BIG-endian unpack: the bridge byte-swaps, and a little-endian
         * unpack scrambles the name into 4-byte groups. */
        raw[w * 4 + 0] = (uint8_t)(v >> 24);
        raw[w * 4 + 1] = (uint8_t)(v >> 16);
        raw[w * 4 + 2] = (uint8_t)(v >> 8);
        raw[w * 4 + 3] = (uint8_t)v;
    }

    uint32_t best = 0, best_len = 0, i = 0;
    while (i < sizeof(raw)) {
        uint32_t start = i;
        while (i < sizeof(raw) && raw[i] >= 0x20u && raw[i] < 0x7Fu) i++;
        if (i - start > best_len) { best_len = i - start; best = start; }
        i++;
    }

    uint32_t n = best_len;
    if (n > out_size - 1u) n = out_size - 1u;
    for (uint32_t k = 0; k < n; k++) out[k] = (char)raw[best + k];
    out[n] = 0;
}

/* Force APF to forget what it knows about the MP3 slot. Its fragment cache is
 * dropped whenever a DIFFERENT slot is accessed, and after a reload those
 * cached fragments still describe the OLD file. LOAD TIME ONLY -- doing this
 * while streaming makes every refill re-walk the cluster chain. */
static void target_flush_slot_cache(void)
{
    target_read_slot(FW_SLOT_ID, 0, TAG_OFF, 512);
}


/* Which decoder the current track needs. Detected from the file's first four
 * bytes, not its extension -- a .flac that is really an MP3 should play, and a
 * mislabelled file should not silently fail. */
/* Declared up with the other track state -- see fl_first_frame. */
static flac_t   fl;
static int32_t *fl_buf;            /* one blocksize of int32, from the arena */

#include "art.inc"
#pragma GCC push_options
#pragma GCC optimize ("Os")          /* playlist control code: no timing role, RAM is the constraint in the library build */
#include "playlist.inc"
#include "cold.inc"
/* blit_probe.inc moved earlier in this file (see the include site before ui_draw_dynamic()) --
 * BLIT_READY()/blit_probe_ensure() are needed there, which comes before this point. */

/* B-199..B-201: thin hot wrapper around ui_draw_dynamic_cold() (defined much earlier in this file,
 * before this #include -- see the comment left there). Needs COLD_READY(), just defined above, so
 * it lives here rather than immediately after the function it wraps. When TAU_G4>=3 makes
 * ui_draw_dynamic_cold() actually cold, this applies the same fail-safe every other G4 cold-code
 * entry point uses (checked, never called blindly) and times every real call into the SAME
 * accumulator CT_COLDFRAME (fw/suite.inc) reads -- a permanent regression guard on the real
 * function, not the one-off synthetic-probe experiment section 4.2 started with. On an old
 * bitstream without PSRAM instruction fetch, this just stops updating the meters (screen keeps
 * whatever was last drawn); everything else -- single-file playback included -- is unaffected, the
 * same degrade-cleanly convention every other G4 fail-safe already uses. On any build below this
 * tier (TAU_G4 < 3, every release so far), ui_draw_dynamic_cold() is ordinary hot code and this is
 * exactly the original function, just with the section 4.2 synthetic-probe hook still available. */
static void ui_draw_dynamic(void)
{
#if TAU_G4 >= 3
    if (!COLD_READY()) return;
    uint32_t t0 = cycles();
    ui_draw_dynamic_cold();
    coldframe_record(cycles() - t0);
    if (ui_fullscreen) ui_fs_dynamic();
#else
    coldframe_tick();
    ui_draw_dynamic_cold();
    if (ui_fullscreen) ui_fs_dynamic();
#endif
}

#if TAU_G4
#define COLD_FN COLD_TEXT      /* G4: a function moved to PSRAM; every entry from hot code is gated on COLD_READY() or on state that implies it */
#else
#define COLD_FN
#endif
#pragma GCC pop_options
/* Size-optimised: the library is browse UI and one-time load code, and the RAM it costs comes out of the heap gap. */
#pragma GCC push_options
#pragma GCC optimize ("Os")
#include "library.inc"
#include "assets.inc"     /* theme step 0d: extra themes from tau-assets.bin (data slot 8) */
#pragma GCC pop_options
#if TAU_ART_TIMG
#include "timg.inc"
#endif
/* Settings is menu code with no timing role; size-optimise it in the library build, where RAM is the constraint. */
#pragma GCC push_options
#pragma GCC optimize ("Os")
#include "settings.inc"
#include "settingsui.inc"
#pragma GCC pop_options

static int list_ended(void)
{
    return lib_src && rep_mode == REP_OFF && (uint32_t)lib_qpos + 1u >= lib_qn;
}

/* Slide unconsumed bytes down and pull in ONE chunk. Compaction keeps Helix's
 * input pointer arithmetic simple -- it wants a flat span, not a wrap. */
static int refill_one(void)
{
    if (ring_fill + REFILL_CHUNK > RING_SIZE) {
        if (ring_rd == 0) return 1;                 /* genuinely full */

        uint32_t align = ring_rd & ~3u;             /* word-aligned source */
        uint32_t keep  = ring_fill - align;
        uint32_t off   = ring_rd - align;

        volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)(UNCACHED + RING_OFF);
        uint32_t words = (keep + 3u) >> 2;
        uint32_t src   = align >> 2;
        for (uint32_t i = 0; i < words; i++) w[i] = w[src + i];

        ring_fill = keep;
        ring_rd   = off;
    }

    if (!target_read(file_pos, RING_OFF + ring_fill, REFILL_CHUNK)) return 0;
    file_pos  += REFILL_CHUNK;
    ring_fill += REFILL_CHUNK;
    return 1;
}


#if IO_BENCH
static void io_bench(uint32_t from)
{
    static uint8_t done;
    if (done) return;
    done = 1;

    const uint32_t N = 32u;                 /* 128 KB, ~1 s at the low end */
    uint32_t t0 = cycles(), got = 0;
    for (uint32_t i = 0; i < N; i++) {
        if (!target_read_slot(MP3_SLOT_ID, from + i * REFILL_CHUNK,
                              TAG_OFF, REFILL_CHUNK)) break;
        got += REFILL_CHUNK;
    }
    uint32_t dt = cycles() - t0;
    io_bench_bytes = got;
    io_kbps = (dt && got)
            ? (uint16_t)(((uint64_t)got * (uint64_t)CLK_HZ)
                         / ((uint64_t)dt * 1024u))
            : 0u;
}
#endif

static void refill_pump(void);     /* defined below; the glue needs it */
static int  prefill(void);         /* likewise, for flac_restart()     */
static int  refill_one(void);      /* blocking read, for flac_pull()   */

/* ---------------------------------------------------------- FLAC glue ---
 *
 * Two adapters, and nothing else: the decoder is format logic, the firmware
 * owns the ring and the FIFO, and neither should know about the other.
 *
 * The shapes already exist in the MP3 path. flac_pull() consumes the ring the
 * way MP3Decode does; flac_emit() pushes samples the way the frame loop does,
 * including servicing input and refills from inside the full-FIFO wait --
 * which is where this CPU spends most of its time and the only reason the
 * decoder keeps up. */

static uint32_t flac_stall;        /* refills that came back empty          */

/* UI cadence, decoupled from the decode frame.
 *
 * ui_draw_dynamic() is driven once per decoded frame, which is right for MP3
 * -- 1152 samples is 26 ms, so ~38 a second. A FLAC frame is 4608 samples,
 * 104 ms, so the SAME loop refreshes the whole UI at ~9.6 fps: every meter,
 * marquee and clock at a quarter speed. That is what "sluggish" was, and it
 * is structural rather than anything to do with the meters.
 *
 * Driven on elapsed time instead, matching MP3's rate. The check runs once per
 * flac_emit call -- every 64 samples, ~1.45 ms -- so it costs one counter read
 * per call and cannot be late by more than that. */
#define FL_UI_PERIOD (CLK_HZ / 38u)
static uint32_t fl_ui_next;

static int flac_pull(void *ctx, uint8_t *dst, int n)
{
    (void)ctx;
    int got = 0;
    while (got < n) {
        /* Collect any finished read and let the next one start, BEFORE asking
         * whether the ring is dry.
         *
         * Reads used to be started only inside the full-FIFO wait in
         * flac_emit, which works while there is idle time to wait in and does
         * nothing once there is not. Measured: Pink Floyd has D27 of idle and
         * shows O0, while every 24-bit file has D0 and pays O27..O39. Demand
         * against the measured 736 KB/s is 28/42/53%, against measured O of
         * 27/35/39 -- so the card was never slow. The reads were simply never
         * STARTED until the ring had already run dry, and by then the only
         * option left is a blocking one. O is a CONSEQUENCE of D reaching
         * zero, and it compounds.
         *
         * This is safe against the blocking path: target_read_start_slot()
         * already calls refill_drain(), so there is exactly one command in
         * flight ever. An earlier build blamed corruption here on a race and
         * added a drain loop of its own -- the race did not exist, the
         * corruption was the frame-header bug fixed in f0f6bac, and the loop
         * turned every failed read into a ~35-second retry chain. */
        refill_pump();

        /* UI refresh from HERE as well as from flac_emit, sharing one deadline
         * so the rate is unchanged.
         *
         * flac_emit only runs while the SECOND channel is being decoded --
         * subframe() decodes all of channel 0 first and emits nothing, which
         * is ~half of a 104 ms frame with no refresh at all, then a burst.
         * That bunching is the "tad of sluggishness" left after the rate fix:
         * the average was right, the spacing was not. flac_pull is called
         * every 512 bytes, right through both channels, so it fills the gap.
         *
         * Gated on fl_buf, which is null until flac_open has returned and the
         * block buffer is allocated -- so this never fires while metadata is
         * still being walked and the card is half-populated. */
        if (fl_buf && (int32_t)(cycles() - fl_ui_next) >= 0) {
            fl_ui_next = cycles() + FL_UI_PERIOD;
            if (!ui_dump_mode) ui_draw_dynamic();
        }

        if (ring_rd >= ring_fill) {
            /* Dry. Pump and wait, the same as a full FIFO -- the decoder
             * cannot proceed and the CPU has nothing better to do. */
            /* refill_one, not refill_pump: this is a BLOCKING read, and it
             * has to be. refill_pump only issues a request and returns, so
             * spinning on it here would depend on the main loop collecting --
             * which cannot happen, because the main loop is inside this call.
             * Metadata skipping needs real progress: the hi-res file on the
             * test card carries 59 KB of picture and 151 KB of padding before
             * its first audio frame. */
            uint32_t t0 = cycles();
            int ok = refill_one();
            fl_io_cyc += cycles() - t0;
            if (!ok || ring_rd >= ring_fill) {
                if (++flac_stall > 8u) break;       /* genuine end of file */
                continue;
            }
        }
        flac_stall = 0;
        uint32_t avail = ring_fill - ring_rd;
        uint32_t take  = ((uint32_t)(n - got) < avail) ? (uint32_t)(n - got)
                                                       : avail;
        for (uint32_t i = 0; i < take; i++) dst[got + i] = ring[ring_rd + i];
        ring_rd += take;
        got     += (int)take;
    }
    return got;
}

static uint32_t fl_cap;            /* max_blocksize, kept across reopens */

/* Rewinds a FLAC stream to its first audio frame.
 *
 * Needed because every reposition -- track start, restart, stop -- resets the
 * ring to file_pos and the decoder's own state would be left mid-stream. MP3
 * survives that by re-finding a sync word; FLAC cannot, because its metadata
 * was consumed once at open and the bit reader holds partial bytes.
 *
 * Reopening from byte 0 re-reads the metadata, which is a few hundred bytes
 * and only happens on a reposition. Simpler and surer than trying to record
 * where the audio began and restore the reader's bit position to match. */
/* ---- FLAC seek support -------------------------------------------------
 *
 * Byte-proportional seeking cannot work on FLAC, and the test files say so
 * numerically: between two seek points of the Psychedelic Furs file the rate
 * is 164 KB/s, and between the next two it is 213 KB/s. A single average
 * applied across a track that varies by 30% lands somewhere different every
 * time -- "jumps all over the place" is exactly what that produces.
 *
 * The SEEKTABLE gives sample -> byte pairs, so the fix is to INTERPOLATE
 * between the two points bracketing the target rather than extrapolate one
 * global rate across the whole file. Within a ~10 second interval the rate is
 * near enough constant for the landing to be accurate.
 *
 * Seeking straight to a seek point would be exact but useless: the points are
 * ~10 s apart on every one of these files, so a 5 s seek would frequently not
 * move at all.
 *
 * The table is left in the FILE and read into tagbuf at seek time -- 227
 * points fit a 4 KB read, and holding it in BSS would want ~1.8 KB of the
 * ~1.8 KB of link slack that remains.
 */
/* Where a run of seek presses has ASKED to be, as opposed to where playback
 * actually is.
 *
 * The transport must advance by the step on every press. Computing the next
 * target from the clock alone cannot guarantee that: if a landing falls short,
 * the next target is computed from the short position and the errors compound
 * until the seek stops moving. Holding the intent separately means a run of
 * presses advances monotonically whatever the landings do, while the clock
 * stays truthful about where the audio is. The guard band drops the intent
 * once ordinary playback has caught up, so it never lingers. */
static uint32_t fl_seek_intent;

static uint32_t fl_seek_off;      /* absolute offset of the SEEKTABLE body  */

static uint32_t fl_seek_pts;      /* 18-byte points; 0 = no table           */
/* Declared up with the other track state, not down with the FLAC seek code
 * where it used to live: the format row needs it, and that is drawn well above
 * here. A tentative definition either way -- it is the same object. */

static uint32_t fl_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

/* Walks the metadata blocks by absolute file offset, recording the seek table
 * and where the audio actually begins. Same shape as art_find_flac_picture();
 * both read through tagbuf rather than the ring, so neither disturbs playback. */
static void flac_scan_metadata(void)
{
    fl_seek_off = fl_seek_pts = fl_first_frame = 0;

    if (!target_read_slot(MP3_SLOT_ID, 0, TAG_OFF, 4u)) return;
    if (tagbuf[0] != 'f' || tagbuf[1] != 'L' ||
        tagbuf[2] != 'a' || tagbuf[3] != 'C') return;

    uint32_t off = 4u;
    for (uint32_t guard = 0; guard < 64u; guard++) {
        if (!target_read_slot(MP3_SLOT_ID, off, TAG_OFF, 4u)) return;
        uint32_t last = (uint32_t)(tagbuf[0] >> 7);
        uint32_t type = (uint32_t)(tagbuf[0] & 0x7Fu);
        uint32_t len  = ((uint32_t)tagbuf[1] << 16)
                      | ((uint32_t)tagbuf[2] << 8) | (uint32_t)tagbuf[3];
        uint32_t body = off + 4u;

        if (type == 3u && len >= 18u) {
            uint32_t pts = len / 18u;
            if (pts > TAG_SIZE / 18u) pts = TAG_SIZE / 18u;
            fl_seek_off = body;
            fl_seek_pts = pts;
        }
        off = body + len;
        if (last) { fl_first_frame = off; return; }
    }
}


/* ------------------------------------------------------- accurate seeking
 *
 * A byte offset interpolated from a time is a GUESS, and on material whose
 * bitrate varies it is a poor one -- measured on a file whose quiet half
 * occupies a four-hundredth of the bytes of its loud half, asking for 15s
 * landed at 20s. Two attempts were made to fix the resulting clock error by
 * correcting the clock AFTERWARDS, from the frame the decoder resynced on.
 * Both failed, the second leaving the transport unusable, and the reason is
 * structural rather than a coding slip: the next seek target is computed FROM
 * the clock, so a truthful clock plus an inaccurate landing means each press
 * moves by the step MINUS the landing error. Reproduced under tools/rv32sim.py
 * with the real decoder: presses advanced +22, +11, +5, then +0, +0, +0 --
 * stuck, exactly as reported.
 *
 * So the landing is made accurate instead. Every FLAC frame header states its
 * own position, and flac_probe_frame() reads one without decoding audio, so
 * the offset can be measured and refined until it is right. Then the clock and
 * the transport agree because there is nothing left to disagree about.
 *
 * Probing reads the card directly rather than through the ring: the ring is
 * the playback path and refilling it for a measurement that is about to be
 * thrown away would cost far more than the 512 bytes a probe needs. */
static uint32_t fl_probe_pos;

static int flac_probe_pull(void *ctx, uint8_t *dst, int n)
{
    (void)ctx;
    if (n <= 0 || fl_probe_pos >= slot_size) return 0;
    uint32_t want = (uint32_t)n;
    if (want > 512u) want = 512u;
    if (fl_probe_pos + want > slot_size) want = slot_size - fl_probe_pos;
    if (!want) return 0;
    if (!target_read_slot(MP3_SLOT_ID, fl_probe_pos, TAG_OFF, want)) return 0;
    for (uint32_t i = 0; i < want; i++) dst[i] = tagbuf[i];
    fl_probe_pos += want;
    return (int)want;
}

static uint64_t fl_sample_of(void)
{
    return fl.number_is_sample ? fl.frame_number
                               : fl.frame_number * (uint64_t)fl.max_blocksize;
}

/* Finds the byte offset whose frame starts closest to `want` samples, and
 * reports the sample position it actually found there.
 *
 * False position between a bracketing pair. Each probe replaces one end, so
 * nothing is assumed about how bytes map to time -- a badly nonlinear file
 * simply takes another step. Measured: one probe for an ordinary file, and
 * convergence on the pathological one.
 *
 * A probe reporting a position outside the bracket cannot be real. That is a
 * false sync whose arbitrary frame number happened to survive the CRC-8 --
 * there are 22 of them in one file on the test card, and trusting one is what
 * broke the previous attempt. Here it is simply discarded, and because the
 * bracket only ever narrows, a rejected probe costs an iteration rather than
 * the answer. */
COLD_SR static uint32_t flac_seek_locate(uint64_t want, uint64_t *landed)     /* B-333: cold code (RAM shrink); a seek is a rare, SD-bound event */
{
    *landed = 0;
    if (!fl_first_frame || !fl.rate || !fl.max_blocksize) return 0;
    if (slot_size <= fl_first_frame) return 0;

    uint64_t total = fl.total_samples;
    if (!total && track_secs) total = (uint64_t)track_secs * (uint64_t)fl.rate;
    if (!total) return 0;
    if (want > total) want = total;

    uint32_t lo_b = fl_first_frame, hi_b = slot_size;
    uint64_t lo_s = 0, hi_s = total;

    /* Seed from the seek table when there is one: it brackets the target
     * exactly, which is why an ordinary file converges on the first probe. */
    if (fl_seek_pts) {
        uint32_t n = fl_seek_pts * 18u;
        if (target_read_slot(MP3_SLOT_ID, fl_seek_off, TAG_OFF, n)) {
            for (uint32_t i = 0; i < fl_seek_pts; i++) {
                const uint8_t *e = &tagbuf[i * 18u];
                if (fl_be32(e) == 0xFFFFFFFFu || fl_be32(e) != 0u) continue;
                uint64_t smp = (uint64_t)fl_be32(e + 4u);
                uint32_t byt = fl_first_frame + fl_be32(e + 12u);
                if (byt <= lo_b || byt >= hi_b) continue;
                if (smp <= want && smp >= lo_s) { lo_s = smp; lo_b = byt; }
                else if (smp > want && smp <= hi_s) { hi_s = smp; hi_b = byt; }
            }
        }
    }

    flac_read_fn  saved_read = fl.read;
    void         *saved_ctx  = fl.ctx;
    uint32_t      best_b     = lo_b;
    uint64_t      best_s     = lo_s;
    uint64_t      best_d     = (want > lo_s) ? want - lo_s : lo_s - want;
    /* Whether anything here is MEASURED. A seek table seeds a real pair, and
     * a successful probe produces one. With neither, best_s is still the
     * initial 0 and reporting it would send the clock -- and playback -- to
     * the start of the track on a file the probes could not read at all.
     * Refusing the seek leaves playback untouched, and the intent still
     * advances so a second press tries again further on. */
    int           measured  = fl_seek_pts ? 1 : 0;
    fl.read = flac_probe_pull;
    fl.ctx  = 0;
#if UI_SHOW_SEEK_DIAG
    dg_pn = dg_pfail = dg_prej = 0;
#endif

    for (uint32_t it = 0; it < 12u; it++) {
        if (hi_s <= lo_s || hi_b <= lo_b + 1u) break;
        uint32_t at = lo_b + (uint32_t)DIV64((uint64_t)(hi_b - lo_b) *
                                              (want - lo_s), hi_s - lo_s);
        if (at <= lo_b) at = lo_b + 1u;
        if (at >= hi_b) at = hi_b - 1u;

        fl_probe_pos = at;
        flac_flush_input(&fl);
#if UI_SHOW_SEEK_DIAG
        if (dg_pn < 200u) dg_pn++;
#endif
        if (flac_probe_frame(&fl) != FLAC_OK) {
#if UI_SHOW_SEEK_DIAG
            if (dg_pfail < 200u) dg_pfail++;
#endif
            hi_b = at; continue;
        }

        uint64_t got = fl_sample_of();
        if (got < lo_s || got > hi_s) {                        /* false sync */
#if UI_SHOW_SEEK_DIAG
            if (dg_prej < 200u) dg_prej++;
#endif
            hi_b = at; continue;
        }

        measured = 1;
        uint64_t d = (got > want) ? got - want : want - got;
        if (d < best_d) { best_d = d; best_b = at; best_s = got; }
        if (d <= (uint64_t)fl.max_blocksize) break;

        if (got < want) { lo_b = at; lo_s = got; }
        else            { hi_b = at; hi_s = got; }
    }

    fl.read = saved_read;
    fl.ctx  = saved_ctx;
    flac_flush_input(&fl);

    if (!measured) return 0;
    *landed = best_s;
    return best_b;
}

/* Move the stream forward without delivering the bytes -- flac_open()'s way of
 * walking past a metadata block it does not want.
 *
 * Almost always the cover art. Measured on this hardware: 259318 bytes of
 * PICTURE and 75214 of PADDING, read through the bit reader one byte at a time,
 * 628 ms of a FLAC load -- for data load_track() then reads AGAIN by itself to
 * decode the artwork. flac_open was not gathering it, only walking past it.
 *
 * Two cases. Bytes already in the ring are simply consumed, which is exactly
 * what flac_pull does with them. Beyond that the ring is dropped and file_pos
 * moves, so the next refill fetches from the new place -- the same manoeuvre a
 * seek performs, minus the decoder reset, because flac_open is between blocks
 * here and has no decoder state to lose.
 *
 * Refuses rather than guesses when the move would leave the file: returning 0
 * puts flac_open back on its read-and-discard path, which is slow but always
 * correct. */
static int flac_skip_bytes(void *ctx, uint32_t n)
{
    (void)ctx;
    if (!n) return 1;

    uint32_t avail = ring_fill - ring_rd;
    if (n <= avail) { ring_rd += n; return 1; }
    n -= avail;

    if (n > 0xFFFFFFFFu - file_pos) return 0;
    if (slot_size && file_pos + n > slot_size) return 0;

    refill_drain();                 /* no read may be in flight across this */
    ring_fill = 0; ring_rd = 0;
    file_pos += n;
    return 1;
}

static int flac_restart(void)
{
    ring_fill = 0; ring_rd = 0; file_pos = 0;
    flac_stall = 0;
    if (!prefill()) return 0;
    fl.skip = flac_skip_bytes;      /* set BEFORE open: it survives the zeroing */
    if (flac_open(&fl, flac_pull, 0, fl_buf, fl_cap) != FLAC_OK) return 0;
    pcm_rate_apply(fl.rate);
    return 1;
}

/* Stereo pairs of the current FLAC frame captured for the meters, and how
 * many. The buffer is Helix's output array, which is idle for the whole of a
 * FLAC track -- the decoder is freed at load. Reusing it costs nothing and
 * keeps the meters on one code path; a buffer of its own would not fit
 * anyway, with ~1.8 KB of link slack left.
 *
 * The first 1152 pairs of a 4608-sample frame, which is 26 ms of every 104 ms.
 * They have to be CONTIGUOUS rather than spread across the frame: the
 * oscilloscope searches for a zero crossing to trigger on and then walks
 * WAVE_COLS * WAVE_SPAN consecutive samples. */
#define FL_METER_PAIRS 1152u
static uint32_t fl_meter_n;


/* `src`, not `pcm`: the file-scope pcm[] is the meter capture buffer, and a
 * parameter of that name would shadow it. */
HOT_O2 static void flac_emit(void *ctx, const int16_t *src, uint32_t frames)
{
    (void)ctx;
    /* Safe here: ui_draw_dynamic() performs no I/O and cannot re-enter the
     * decoder. It reads position from `frames`, which has not been advanced
     * for the frame in flight, so the clock trails by at most one frame.
     *
     * Reads the counter itself now the profiling timestamp it used to borrow
     * is gone -- one MMIO read per 64 samples. */
    uint32_t t_now = cycles();
    if ((int32_t)(t_now - fl_ui_next) >= 0) {
        fl_ui_next = t_now + FL_UI_PERIOD;
        if (!ui_dump_mode) ui_draw_dynamic();
    }

    for (uint32_t i = 0; i < frames; i++) {
        /* Fill CONTINUOUSLY and flush every FL_METER_PAIRS, rather than
         * grabbing the head of each frame and dropping the rest.
         *
         * This was the real "delayed, not realtime" fault, and it was in the
         * DATA, not the redraw. meters_feed() ran once per FLAC frame -- 9.6 a
         * second against MP3's 38 -- from the first 1152 of 4608 pairs, so the
         * peaks changed nine times a second and three quarters of the audio
         * was never looked at. Repainting faster cannot help a number that is
         * not moving.
         *
         * At 1152 pairs the flush interval is 26 ms, which is exactly MP3's
         * frame, so both formats now drive the meters at the same rate through
         * the same code. Ignoring frame boundaries keeps the interval even. */
#if UI_SHOW_DIAG
        {
            int32_t xl = (int32_t)src[i * 2];
            int32_t d  = xl - clk_prev;
            if (d < 0) d = -d;
            if ((uint32_t)d > clk_max) clk_max = (uint32_t)d;
            if (d > CLK_THRESH) {
                clk_n++;
                if (clk_sec == 0xFFFFFFFFu) clk_sec = ui_sec;
            }
            clk_prev = xl;
        }
#endif
        pcm[fl_meter_n * 2u]      = src[i * 2];
        pcm[fl_meter_n * 2u + 1u] = src[i * 2 + 1];
        if (++fl_meter_n == FL_METER_PAIRS) {
            meters_feed(pcm, (int)(FL_METER_PAIRS * 2u), 1);
            fl_meter_n = 0;
        }
        int32_t l = src[i * 2], r = src[i * 2 + 1];
        /* Volume, which this path did not apply at all -- the d-pad moved the
         * number on screen while FLAC played at full scale, and volume 0 was
         * not silent. The MP3 loop has had this the whole time; adding FLAC
         * added a second push path and only one of them was volume-aware.
         * Same order as MP3: volume first, then the fade, so a fade-in at low
         * volume stays at low volume. Capped at unity, so it only ever
         * attenuates and cannot overflow. */
        if (vol_gain != 256) {
            l = (l * vol_gain) >> 8;
            r = (r * vol_gain) >> 8;
        }
        if (fade_left) {
            int32_t g = (int32_t)((FADE_SAMPLES - fade_left) >> 3);
            l = (l * g) >> 8;
            r = (r * g) >> 8;
            fade_left--;
        }
        if (PCM_FULL(REG(R_PCM_ST))) {
            uint32_t t0 = cycles();
            do {
                poll_input();
                refill_pump();
            } while (PCM_FULL(REG(R_PCM_ST)));
            fl_idle_cyc += cycles() - t0;
        }
        REG(R_AUDIO) = ((uint32_t)(uint16_t)(int16_t)r << 16)
                     | (uint32_t)(uint16_t)(int16_t)l;
    }
}

/* Issue a read, then go back to decoding and collect it later.
 *
 * This is NOT an optimisation. Until the handshake was fixed the target
 * command returned early, so a refill cost almost nothing and the DMA landed
 * in the background -- accidental async I/O, and the only reason the decoder
 * kept up. Making the handshake correct made refills genuinely blocking, and a
 * 4 KB read stalling the decoder is far more than the slack left after decode.
 *
 * Safe because a read only ever writes ABOVE ring_fill while the decoder only
 * reads BELOW it. Compaction, which does move the decodable region, is gated
 * on no read being in flight. */
/* Step the incremental search, and pick up what becomes knowable when it
 * finishes. Returns 1 on the step that completed it.
 *
 * The bitrate is derived from the size, so it arrives at the same instant --
 * and that is the FLAC format row, which stayed blank because track_kbps is
 * computed once at load and slot_size is no longer known by then. Anything
 * else that waits on the size belongs here too rather than in another copy of
 * this. */
static int size_probe_pump(void)
{
    /* ONLY for files whose duration cannot be known any other way.
     *
     * This search binary-searches with reads at far random offsets ON THE SAME
     * SLOT the decoder is streaming from -- 60 MB out for the clamp reference,
     * then doubling -- and that corrupts the stream it is measuring. Confirmed
     * by A/B on hardware: with this disabled the stutter is gone.
     *
     * The mechanism is the fragment cache, and this codebase already knew
     * about it. The periodic slot-3 identity check a few thousand lines down
     * is deliberately a 0190 metadata query rather than a read, and says so:
     * "does not drag the MP3 slot's fragment cache down with it the way a
     * periodic poll of slot 3 would." A far read on the STREAMING slot does
     * exactly that, the refills that follow return the wrong bytes, and the
     * decoder resyncs at the next frame. It is heard as a light stutter and
     * leaves the FIFO full, which is why no underrun was ever recorded and why
     * three fixes aimed at starvation all missed.
     *
     * It fired at fl_first_frame + 256 KB -- about two seconds into a
     * ~1000 kbps FLAC, which is exactly where it was audible.
     *
     * Almost nothing needs it now:
     *   duration -- ui_total_secs() returns track_secs first, from STREAMINFO
     *               for FLAC and Xing/VBRI for MP3.
     *   bitrate  -- derived from playback, not from the size.
     *   seek     -- runs these same steps SYNCHRONOUSLY at the press, where a
     *               flush follows anyway and corruption cannot be heard.
     *   EOF      -- refill_pump discovers the true size for free when a read
     *               past the end fails.
     *
     * What is left is the headerless file with no duration at all, where the
     * choice is a rare tic against no progress bar and no total time. Those
     * are the tracks this was built for in the first place, and a load-time
     * probe is not an option for them: a file opened by name with 0192 has
     * only just been opened, and a far read still fails then -- which is how a
     * 30 MB track once measured 5 MB. So they keep the old behaviour, and
     * everything else stops paying for it. */
    if (track_secs) return 0;
    if (!szp_phase || szp_phase >= 4u) return 0;
    if (!size_probe_step()) return 0;

    if (track_fmt == FMT_FLAC && track_secs && slot_size > fl_first_frame) {
        uint64_t bits = (uint64_t)(slot_size - fl_first_frame) * 8u;
        track_kbps = (uint32_t)DIV64(DIV64(bits, (uint64_t)track_secs), 1000u);
    }
    return 1;
}

#if UI_SHOW_DIAG
/* Sample the underrun line and record edges. Called from both decode loops,
 * beside the existing check but independent of it. */
static void und_sample(void)
{
    uint8_t now = pcm_underrun() ? 1u : 0u;
    if (now && !und_prev) {
        und_edges++;
        if (und2_sec == 0xFFFFFFFFu && ui_sec >= 1u) {
            und2_sec  = ui_sec;
            und2_idle = fl_idle_pct;
            und2_io   = fl_io_pct;
            und2_szp  = (uint8_t)szp_phase;
            und2_ring = (uint16_t)((ring_fill - ring_rd) >> 6);
        }
    }
    und_prev = now;
}
#endif

HOT_O2 static void refill_pump(void)
{
    if (rd_pending) {
        if (!target_read_poll()) return;
        rd_pending = 0;
        if (!rd_ok) {
            /* A RING read past the end of the file FAILS on this host --
             * probe_file_size() was built on exactly that behaviour. So a
             * failed refill IS the end of the file announcing itself, and this
             * is where the size of a headerless file gets discovered: at the
             * end, where the stream reports it for free, instead of by twenty
             * BLOCKING reads at the start of playback -- which were the tic on
             * the one track with no Xing header. These reads are async; the
             * decoder never waits on them.
             *
             * Halve toward the true boundary so the tail is not lost: a 4 KB
             * read straddling EOF fails outright rather than shortening. */
            if (rd_len > 512u) {
                target_read_start(file_pos, RING_OFF + ring_fill, rd_len / 2u);
                return;
            }
            /* CONFIRM before believing it. A read failing once does not mean
             * end-of-file: right after a 0192 the slot is still settling and a
             * refill can fail transiently. Treating that as EOF set slot_size
             * to the current position and the main loop restarted the track --
             * exactly the "plays half a second, jumps back to the beginning"
             * report, and impossible on stop/restart because no file changes
             * there. Three consecutive failures at the SAME offset, which a
             * real end always produces and a settling slot does not. */
            if (eof_at != file_pos) { eof_at = file_pos; eof_fails = 0; }
            if (++eof_fails < 3u) {
                target_read_start(file_pos, RING_OFF + ring_fill, 512u);
                return;
            }
            eof_hit = 1u;
            if (!slot_size || slot_size > file_pos) slot_size = file_pos;
            return;
        }
        file_pos += rd_len; ring_fill += rd_len;
        return;
    }

    /* No NEW I/O during a track transition: the slot may already be serving
     * the incoming file, and a refill at the outgoing position would hand the
     * decoder bytes from the middle of the wrong track. In-flight reads are
     * still collected above. */
    if (reload_armed || reload_pending) return;

    /* Playing PAST the measured end proves the measurement was short, which
     * is exactly what an early probe on a 0192-opened file produces. Measure
     * again rather than carrying a number the file has already disproved --
     * the seek bracket, the progress bar and the bitrate all read it. */
    if (szp_phase == 4u && slot_size && file_pos > slot_size && !eof_hit)
        size_probe_arm();

    /* Ring first, always -- audio starvation beats a late tag update. */
    if (ring_fill - ring_rd >= RING_SIZE / 2u) {
        /* The ring is at least half ahead, so this pass has an I/O slot going
         * spare: spend it measuring the file, one 512-byte probe at a time.
         * This is the "one read per pass while the buffer is full" the size
         * probe was always meant to use -- it stalls nothing, and it is why
         * load_track() no longer blocks for 480 ms. */
        size_probe_pump();
        return;
    }
    if (eof_hit) return;                    /* nothing past the end to fetch */

    if (ring_fill + REFILL_CHUNK > RING_SIZE) {
        if (ring_rd == 0) return;                           /* genuinely full */

        uint32_t align = ring_rd & ~3u;
        uint32_t keep  = ring_fill - align;
        uint32_t off   = ring_rd - align;

        volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)(UNCACHED + RING_OFF);
        uint32_t words = (keep + 3u) >> 2;
        uint32_t src   = align >> 2;
        for (uint32_t i = 0; i < words; i++) w[i] = w[src + i];

        ring_fill = keep;
        ring_rd   = off;
    }

    target_read_start(file_pos, RING_OFF + ring_fill, REFILL_CHUNK);
}

/* Load just enough to start decoding, then let the asynchronous refill top up
 * during playback. Filling the whole ring here was eight BLOCKING reads with
 * the PCM FIFO just flushed -- the FIFO covers only ~46 ms, so anything longer
 * was an audible gap on every track change. */
#define PREFILL_CHUNKS 3u

static int prefill(void)
{
    eof_hit = 0; eof_fails = 0; eof_at = 0xFFFFFFFFu;   /* every reposition passes here */
    uint32_t want = PREFILL_CHUNKS * REFILL_CHUNK;
    if (want > RING_SIZE) want = RING_SIZE;
    while (ring_fill < want && ring_fill + REFILL_CHUNK <= RING_SIZE)
        if (!refill_one()) return 0;
    return 1;
}

/* ID3v2 size is "syncsafe": 7 significant bits per byte. Total tag size is
 * (10-byte header + size) PLUS a 10-byte footer when that flag is set. Takes
 * the buffer explicitly so the same parsing serves the ring at load time and
 * the probe buffer during playback. */
static uint32_t id3_len(const uint8_t *b)
{
    if (b[0] != 'I' || b[1] != 'D' || b[2] != '3') return 0;
    uint32_t footer = (b[5] & 0x10u) ? 10u : 0u;
    return 10u + footer +
           (((uint32_t)(b[6] & 0x7Fu) << 21) |
            ((uint32_t)(b[7] & 0x7Fu) << 14) |
            ((uint32_t)(b[8] & 0x7Fu) << 7)  |
            ((uint32_t)(b[9] & 0x7Fu)));
}

/* Whether b[0..n) validates as UTF-8 (RFC 3629 lead/continuation shape,
 * 1-4 byte sequences). Ported from HarpMudd upstream v1.5.0's encoding-0
 * guess ("taken as UTF-8 when it validates, else Windows-1252") -- adapted
 * for Tau, which does not have the extended font that release also shipped:
 * a codepoint's actual value is never inspected here (overlong/surrogate
 * sequences are accepted as "UTF-8 enough"), since fb_glyph() already turns
 * every codepoint outside 0x20..0x7E into a space regardless of how it got
 * decoded -- the only thing this changes is the placeholder COUNT, one per
 * real character instead of one per raw byte (see id3_text_body below). */
static int id3_bytes_are_utf8(const uint8_t *b, uint32_t n)
{
    uint32_t i = 0;
    while (i < n && b[i]) {
        uint8_t  c = b[i];
        uint32_t cont;
        if (c < 0x80u) { i++; continue; }
        else if (c >= 0xC2u && c <= 0xDFu) cont = 1u;
        else if (c >= 0xE0u && c <= 0xEFu) cont = 2u;
        else if (c >= 0xF0u && c <= 0xF4u) cont = 3u;
        else return 0;
        if (i + cont >= n) return 0;
        for (uint32_t k = 1u; k <= cont; k++)
            if ((b[i + k] & 0xC0u) != 0x80u) return 0;
        i += cont + 1u;
    }
    return 1;
}

/* Extracts a text frame (TIT2, TPE2, TALB, ...) from a tag already in memory.
 * Scoped deliberately: only what the caller loaded, and only ISO-8859-1/UTF-8
 * -- UTF-16 is reported as its own case rather than silently garbled. Handles
 * v2.3 (plain big-endian size) and v2.4 (syncsafe). */
/* Decode one text frame BODY -- the encoding byte and the bytes after it -- into
 * out. Shared by the in-memory parser and the card walk so the two cannot drift
 * on what a given encoding means.
 *
 * UTF-16 (encodings 1 and 2) used to be refused outright, surfacing as its own
 * error text. That is honest but it is still a track with no title, and UTF-16
 * is what several taggers emit by default -- one of the ten tracks on the test
 * card has every text frame in it. The font atlas is ASCII 0x20..0x7E, so
 * anything above Latin-1 could not be drawn regardless; a code unit that does
 * not fit becomes '?', which loses an accent but keeps the title.
 *
 * Encoding 0 (nominally ISO-8859-1) used to be copied through byte-for-byte,
 * which meant a UTF-8-tagged file -- common; plenty of taggers write UTF-8
 * under encoding 0 despite the spec calling for Latin-1 there -- left every
 * continuation byte of an accented character as its own garbage byte in the
 * title (mojibake), each one still eating a character cell even though
 * fb_glyph() draws it as a space. Ported from HarpMudd upstream v1.5.0
 * ("encoding 0 taken as UTF-8 when it validates, else Windows-1252"): the
 * byte range is checked with id3_bytes_are_utf8() first; if it validates,
 * each multi-byte UTF-8 character collapses to ONE placeholder byte (0xFF,
 * which fb_glyph() already turns into a space -- no '?' is drawn, matching
 * this codebase's own established out-of-range convention rather than
 * upstream's, which has the extended font to actually draw '?' with).
 * Anything that does not validate falls back to the original byte-for-byte
 * copy (Windows-1252 and Latin-1 draw identically here regardless -- the
 * font cannot render either one's non-ASCII range, so there is nothing to
 * gain telling them apart). */
static int id3_text_body(const uint8_t *b, uint32_t fsize, char *out,
                         uint32_t out_size)
{
    if (fsize < 2u) return ID3_NO_FRAME;
    uint8_t  enc = b[0];
    uint32_t n   = fsize - 1u;
    uint32_t i   = 0;

    if (enc == 1u || enc == 2u) {
        uint32_t s  = 1u;
        int      be = (enc == 2u);              /* 2 is UTF-16BE, no BOM */
        if (enc == 1u && n >= 2u) {
            if (b[1] == 0xFFu && b[2] == 0xFEu)      { be = 0; s = 3u; }
            else if (b[1] == 0xFEu && b[2] == 0xFFu) { be = 1; s = 3u; }
        }
        while (s + 1u < fsize && i + 1u < out_size) {
            uint32_t u = be ? (((uint32_t)b[s] << 8) | b[s + 1u])
                            : (((uint32_t)b[s + 1u] << 8) | b[s]);
            if (!u) break;
            out[i++] = (u < 0x100u) ? (char)u : '?';
            s += 2u;
        }
    } else {
        if (n > out_size - 1u) n = out_size - 1u;
        if (id3_bytes_are_utf8(&b[1], n)) {
            uint32_t s = 1u;
            while (s < 1u + n && i + 1u < out_size) {
                uint8_t c = b[s];
                if (!c) break;
                if (c < 0x80u) { out[i++] = (char)c; s += 1u; }
                else {
                    uint32_t cont = (c >= 0xF0u) ? 3u : (c >= 0xE0u) ? 2u : 1u;
                    out[i++] = (char)0xFFu;   /* placeholder; fb_glyph() spaces it */
                    s += cont + 1u;
                }
            }
        } else {
            for (i = 0; i < n; i++) {
                uint8_t c = b[1u + i];
                if (c == 0) break;
                out[i] = (char)c;
            }
        }
    }
    out[i] = 0;
    return i ? ID3_OK : ID3_NO_FRAME;
}

static int id3_find_text(const uint8_t *ring, uint32_t avail,
                          uint32_t tag_total_len, const char *frame_id,
                          char *out, uint32_t out_size)
{
    if (ring[0] != 'I' || ring[1] != 'D' || ring[2] != '3') return ID3_NO_TAG;
    uint8_t  major = ring[3];
    uint32_t scan_limit = tag_total_len < avail ? tag_total_len : avail;

    uint32_t p = 10;
    while (p + 10 <= scan_limit) {
        if (ring[p] == 0) break;   /* padding reached */

        uint32_t fsize = (major >= 4)
            ? (((uint32_t)(ring[p+4] & 0x7Fu) << 21) | ((uint32_t)(ring[p+5] & 0x7Fu) << 14) |
               ((uint32_t)(ring[p+6] & 0x7Fu) << 7)  | ((uint32_t)(ring[p+7] & 0x7Fu)))
            : (((uint32_t)ring[p+4] << 24) | ((uint32_t)ring[p+5] << 16) |
               ((uint32_t)ring[p+6] << 8)  |  (uint32_t)ring[p+7]);

        if (fsize == 0 || p + 10 + fsize > scan_limit) break;

        if (ring[p] == (uint8_t)frame_id[0] && ring[p+1] == (uint8_t)frame_id[1] &&
            ring[p+2] == (uint8_t)frame_id[2] && ring[p+3] == (uint8_t)frame_id[3] &&
            fsize > 1)
            return id3_text_body(&ring[p + 10], fsize, out, out_size);
        p += 10u + fsize;
    }
    return ID3_NO_FRAME;
}

/* Walk the tag off the CARD, a frame header at a time, and fill whatever text
 * fields are still empty.
 *
 * The in-memory parser above cannot reach these. It stops at the first frame
 * whose body runs past what is loaded, and three tracks on the test card put
 * APIC FIRST: 41 KB, 34 KB and 847 KB of picture ahead of every text frame. The
 * ring is 32 KB, so pulling more of the tag in -- the existing fallback -- can
 * never work for them however much it pulls. Those tracks displayed nothing at
 * all, which reads as "this file has no tags" rather than as a limitation.
 *
 * Cost is proportional to the NUMBER of frames, not to the size of the tag: the
 * body of a picture frame is skipped by arithmetic, never read. Widespread
 * Panic's 851 KB tag is about a dozen 16-byte reads. That is what makes this
 * affordable where an earlier frame-by-frame walk was not -- that one ran on
 * every track; this runs only where the cheap path already failed, which is the
 * handful of tracks that would otherwise show nothing. */
/* ID3v1: a fixed 128-byte block at the very END of the file, starting "TAG".
 *
 * Predates ID3v2 and is still the only tag a lot of older files carry, so
 * without this they display nothing at all. Fields are fixed width and padded
 * with spaces or NULs rather than terminated, so each one has to be trimmed
 * from the right.
 *
 * Only ever called after the v2 paths have found nothing, so a file carrying
 * both keeps its v2 values -- those are longer, and not limited to 30
 * characters or to Latin-1. */
static void id3v1_read(void)
{
    if (slot_size < 128u) return;
    if (!target_read_slot(MP3_SLOT_ID, slot_size - 128u, TAG_OFF, 128u)) return;
    if (tagbuf[0] != 'T' || tagbuf[1] != 'A' || tagbuf[2] != 'G') return;

    static const struct { uint8_t off, len; } f[3] = {
        {   3u, 30u },      /* title  */
        {  33u, 30u },      /* artist */
        {  63u, 30u },      /* album  */
    };
    char *const dst[3] = { track_title, track_artist, track_album };
    const uint32_t cap[3] = { sizeof(track_title), sizeof(track_artist),
                              sizeof(track_album) };

    for (uint32_t k = 0; k < 3u; k++) {
        if (dst[k][0]) continue;                  /* v2 already supplied it */
        uint32_t n = f[k].len;
        while (n && (tagbuf[f[k].off + n - 1u] == ' ' ||
                     tagbuf[f[k].off + n - 1u] == 0)) n--;
        if (n > cap[k] - 1u) n = cap[k] - 1u;
        for (uint32_t i = 0; i < n; i++) dst[k][i] = (char)tagbuf[f[k].off + i];
        dst[k][n] = 0;
    }

    if (!track_year[0]) {
        uint32_t n = 0;
        while (n < 4u && tagbuf[93u + n] >= '0' && tagbuf[93u + n] <= '9') {
            track_year[n] = (char)tagbuf[93u + n]; n++;
        }
        track_year[n] = 0;
    }

    /* ID3v1.1 puts a track number in the last two bytes of the comment: a NUL
     * followed by the number. A plain v1 comment runs through both, so the NUL
     * is what distinguishes them. */
    if (!track_trk[0] && tagbuf[125] == 0 && tagbuf[126]) {
        uint32_t t = tagbuf[126], i = 0;
        if (t >= 100u) track_trk[i++] = (char)('0' + t / 100u % 10u);
        if (t >= 10u)  track_trk[i++] = (char)('0' + t / 10u % 10u);
        track_trk[i++] = (char)('0' + t % 10u);
        track_trk[i] = 0;
    }
}

static void id3_walk_collect(uint32_t tag_len)
{
    /* TPE2 (band/album artist) is preferred, but plenty of files carry only
     * TPE1 (lead performer); TDRC is v2.4's year, TYER v2.3's. Duplicated
     * targets are harmless because a field already filled is skipped, so the
     * first of each pair to appear in the tag wins. */
    static const char *const want[7] = { "TIT2", "TPE2", "TPE1", "TALB",
                                         "TRCK", "TDRC", "TYER" };
    char *const dst[7] = { track_title, track_artist, track_artist, track_album,
                           track_trk, track_year, track_year };
    const uint32_t cap[7] = { sizeof(track_title), sizeof(track_artist),
                              sizeof(track_artist), sizeof(track_album),
                              sizeof(track_trk), sizeof(track_year),
                              sizeof(track_year) };

    if (tag_len < 20u) return;

    /* A sliding window rather than a read per frame header. Frames after a
     * picture are packed tightly -- Sea Wolf has eight in 200 bytes -- so a
     * header-sized read each time cost 29 round trips where four cover it. The
     * window also usually holds the text body, saving a second read. */
    uint32_t wo = 0, wl = 0;
#define ID3_WIN 512u
#define ID3_HAVE(o, n) ((o) >= wo && (o) + (n) <= wo + wl)

    if (!target_read_slot(MP3_SLOT_ID, 0, TAG_OFF, ID3_WIN)) return;
    wo = 0; wl = ID3_WIN;
    if (tagbuf[0] != 'I' || tagbuf[1] != 'D' || tagbuf[2] != '3') return;
    uint8_t major = tagbuf[3];

    uint32_t p = 10;
    while (p + 10u <= tag_len) {
        if (!ID3_HAVE(p, 10u)) {
            if (!target_read_slot(MP3_SLOT_ID, p, TAG_OFF, ID3_WIN)) return;
            wo = p; wl = ID3_WIN;
        }
        const uint8_t *h = tagbuf + (p - wo);
        if (h[0] == 0) return;                         /* padding reached */

        uint32_t fsize = (major >= 4)
            ? (((uint32_t)(h[4] & 0x7Fu) << 21) | ((uint32_t)(h[5] & 0x7Fu) << 14) |
               ((uint32_t)(h[6] & 0x7Fu) << 7)  |  (uint32_t)(h[7] & 0x7Fu))
            : (((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) |
               ((uint32_t)h[6] << 8)  |  (uint32_t)h[7]);
        if (!fsize || p + 10u + fsize > tag_len) return;

        for (uint32_t k = 0; k < 7u; k++) {
            if (dst[k][0]) continue;                   /* already have it */
            if (h[0] != (uint8_t)want[k][0] || h[1] != (uint8_t)want[k][1] ||
                h[2] != (uint8_t)want[k][2] || h[3] != (uint8_t)want[k][3])
                continue;
            /* n is the whole frame body: the encoding byte plus its text. Pass
             * exactly what was READ -- passing one more made the decoder take a
             * byte beyond the buffer, which showed up as the next frame's first
             * letter stuck on the end of every walked title. */
            uint32_t n = fsize < 160u ? fsize : 160u;
            if (ID3_HAVE(p + 10u, n)) {
                id3_text_body(tagbuf + (p + 10u - wo), n, dst[k], cap[k]);
            } else if (target_read_slot(MP3_SLOT_ID, p + 10u, TAG_OFF, n)) {
                id3_text_body(tagbuf, n, dst[k], cap[k]);
                wl = 0;                    /* the read replaced the window */
            }
            break;
        }
        p += 10u + fsize;
    }
#undef ID3_HAVE
#undef ID3_WIN
}

static int title_is_stale(const char *title)
{
    if (slot_size == stale_ref_size) return 0;
    if (!title[0] || !stale_ref_title[0]) return 0;
    for (uint32_t i = 0; i < sizeof(stale_ref_title); i++) {
        if (title[i] != stale_ref_title[i]) return 0;
        if (!title[i]) break;
    }
    return 1;
}

/* Look for a VBR header in the first audio bytes. Searching for the ASCII tag
 * directly, rather than computing its offset from the frame header's
 * version/channel layout, keeps this independent of MPEG version. */
static uint32_t vbr_frame_count(void)
{
    uint32_t lim = ring_fill < 2048u ? ring_fill : 2048u;
    if (lim < 32u) return 0;
    for (uint32_t i = 0; i + 20u < lim; i++) {
        uint8_t a = ring[i], b = ring[i+1], c = ring[i+2], d = ring[i+3];
        if ((a == 'X' && b == 'i' && c == 'n' && d == 'g') ||
            (a == 'I' && b == 'n' && c == 'f' && d == 'o')) {
            uint32_t flags = ((uint32_t)ring[i+4] << 24) | ((uint32_t)ring[i+5] << 16) |
                             ((uint32_t)ring[i+6] << 8)  |  (uint32_t)ring[i+7];
            if (!(flags & 1u)) return 0;            /* no FRAMES field */
            /* The BYTES field, when present, is the file's own statement of how
             * long its audio is -- an exact figure to check the directory
             * against, where a bitrate estimate is only a guess on VBR. */
            if (flags & 2u)
                track_bytes = ((uint32_t)ring[i+12] << 24) | ((uint32_t)ring[i+13] << 16) |
                              ((uint32_t)ring[i+14] << 8)  |  (uint32_t)ring[i+15];

            /* The LAME extension follows the optional Xing fields, so its
             * offset depends on which flags are set -- FRAMES and BYTES are 4
             * bytes each, the TOC is 100, QUALITY is 4. Computing it from the
             * flags rather than assuming a fixed offset is the difference
             * between reading an encoder name and reading the middle of the
             * seek table. */
            uint32_t e = i + 8u;
            if (flags & 1u) e += 4u;            /* FRAMES  */
            if (flags & 2u) e += 4u;            /* BYTES   */
            if (flags & 4u) e += 100u;          /* TOC     */
            if (flags & 8u) e += 4u;            /* QUALITY */

            /* Only accept it if the whole field is in the buffer AND the string
             * is printable. A short read or a file with no extension would
             * otherwise put ring garbage on screen as an encoder name. */
            if (e + 10u < lim) {
                int ok = 1;
                for (uint32_t k = 0; k < 9u; k++)
                    if (ring[e+k] < 0x20u || ring[e+k] > 0x7Eu) { ok = 0; break; }
                if (ok) {
                    for (uint32_t k = 0; k < 9u; k++) track_encoder[k] = (char)ring[e+k];
                    track_encoder[9] = 0;
                    /* Trailing spaces: some encoders pad the field. */
                    for (int k = 8; k >= 0 && track_encoder[k] == ' '; k--)
                        track_encoder[k] = 0;
                    track_vbr_method = ring[e+9] & 0x0Fu;
                }
            }
            return ((uint32_t)ring[i+8]  << 24) | ((uint32_t)ring[i+9]  << 16) |
                   ((uint32_t)ring[i+10] << 8)  |  (uint32_t)ring[i+11];
        }
        if (a == 'V' && b == 'B' && c == 'R' && d == 'I') {
            track_bytes = ((uint32_t)ring[i+10] << 24) | ((uint32_t)ring[i+11] << 16) |
                          ((uint32_t)ring[i+12] << 8)  |  (uint32_t)ring[i+13];
            return ((uint32_t)ring[i+14] << 24) | ((uint32_t)ring[i+15] << 16) |
                   ((uint32_t)ring[i+16] << 8)  |  (uint32_t)ring[i+17];
        }
    }
    return 0;
}

/* Reads the head of the file and skips any ID3 tag, leaving the ring and
 * file_pos positioned at real audio. Returns 0 on I/O failure. */
/* B-333: cold code (RAM shrink). Only load_track() calls it; both run once per track, in the silent gap. */
COLD_SR static int read_track_head(void)
{
    refill_drain();     /* settle anything in flight before touching the ring */

    int attempt = 0, have_prev = 0;
    uint32_t skip = 0, prev_skip = 0;
    char prev_try[TITLE_MAX];
    /* PROVE THE SLOT HAS SETTLED before reading anything we will act on.
     *
     * After a 0192 the slot does not switch instantly, and the old design
     * loaded immediately and then tried to DETECT having read the wrong file --
     * stale-title compares, retry budgets, periodic re-probes. Every one of
     * those was a way of noticing a bad read after committing to it, and a
     * wrong tag length means a wrong audio_start, which means the first audio
     * read lands mid-frame: a single loud click, exactly as reported, and
     * impossible on stop/restart where no file changes.
     *
     * Cheaper and surer: read the first 16 bytes twice, ~30 ms apart, and
     * require them to agree. A slot mid-switch does not return the same bytes
     * twice running; a settled one always does. Up to ~1 s, then proceed
     * anyway -- the retry loop below is the existing backstop.
     *
     * Costs nothing on the common path: the first two reads agree and it
     * proceeds, and it does not run at all for a restart, which never gets
     * here. */
    {
        /* Wait until the slot returns something DIFFERENT from the song we
         * just left, then stable.
         *
         * Requiring only stability was useless: a slot still serving the
         * PREVIOUS file returns identical bytes every time, so the check
         * passed instantly on exactly the stale data it existed to reject.
         * Proving "settled" is not the same as proving "switched".
         *
         * sw_prev_head is the outgoing file's first 16 bytes, captured before
         * the 0192. Sixteen, not four: every ID3v2.3 tag begins "ID3" plus a
         * version, so four bytes are the same constant for every tagged file --
         * the discriminator-that-does-not-discriminate this project has been
         * caught by before. Sixteen reaches the tag LENGTH and the first frame
         * id, which do differ.
         *
         * Then two agreeing reads, so a half-switched slot is not trusted
         * either. ~1 s cap, after which the existing retry loop takes over. */
        uint8_t prev[16];
        uint32_t tries = 0, agree = 0, differs = !sw_have_prev;
        for (uint32_t i = 0; i < sizeof(prev); i++) prev[i] = 0u;
        while (tries++ < 32u && !(differs && agree >= 2u)) {
            if (!target_read_slot(MP3_SLOT_ID, 0, TAG_OFF, 16u)) { agree = 0; continue; }
            if (!differs) {
                for (uint32_t i = 0; i < sizeof(prev); i++)
                    if (tagbuf[i] != sw_prev_head[i]) { differs = 1; break; }
            }
            uint32_t same = 1;
            for (uint32_t i = 0; i < sizeof(prev); i++) {
                if (tagbuf[i] != prev[i]) same = 0;
                prev[i] = tagbuf[i];
            }
            agree = same ? agree + 1u : 0u;
            if (!(differs && agree >= 2u)) {
                uint32_t wait = cycles() + CLK_HZ / 32u;   /* ~30 ms */
                while ((int32_t)(cycles() - wait) < 0) { }
            }
        }
        sw_have_prev = 0;         /* one switch, one use */
    }

    /* ONCE, before the loop -- not on every retry.
     *
     * Flushing makes APF forget the slot's cluster chain, so the next read has
     * to walk it from the start. That is the point of doing it after a file
     * change, but doing it again on each retry means every attempt pays a full
     * walk, and the walk is proportional to file size -- which is why the
     * longest file in the set was the one that hiccuped. One flush is enough to
     * discard the stale chain; the retries that follow want the fresh one kept,
     * not thrown away again. */
    target_flush_slot_cache();

    for (;;) {

    /* ...then throw away one MP3-slot read before the one that matters. Only
     * the FIRST read after a file change comes back stale: the audio has
     * always been correct, and the audio comes from the SECOND read. */
    target_read_slot(MP3_SLOT_ID, 0, TAG_OFF, 512);

    file_pos = 0; ring_fill = 0; ring_rd = 0;

    /* Poison the landing zone: stale-content and nothing-arrived otherwise
     * produce byte-for-byte the same evidence, since whatever was already in
     * the ring is also old mid-stream audio. */
    {
        volatile uint32_t *w = (volatile uint32_t *)(uintptr_t)(UNCACHED + RING_OFF);
        w[0] = 0xA5A5A5A5u; w[1] = 0xA5A5A5A5u;
    }

    if (!target_read(0, RING_OFF, REFILL_CHUNK)) return 0;
    ring_fill = REFILL_CHUNK;

    for (int i = 0; i < 4; i++) head_bytes[i] = ring[i];

    /* FLAC or MP3, decided by the file's first four bytes rather than its
     * extension -- a mislabelled file should play, and a .flac that is really
     * an MP3 should not fail mysteriously.
     *
     * Detected here because everything below is ID3-shaped: tag length, the
     * settle-compare on the tag bytes, audio_start. None of it applies to a
     * FLAC, whose metadata the decoder consumes itself. */
    if (ring[0] == 'f' && ring[1] == 'L' && ring[2] == 'a' && ring[3] == 'C') {
        track_fmt   = FMT_FLAC;
        audio_start = 0;
        ring_rd     = 0;
        /* The head read above put bytes [0, REFILL_CHUNK) in the ring, so the
         * next refill must continue from there. Without this file_pos keeps
         * whatever the previous track left and every refill reads the wrong
         * part of the file. */
        file_pos    = REFILL_CHUNK;
        track_title[0] = 0; track_artist[0] = 0;
        track_album[0] = 0; track_year[0]   = 0; track_trk[0] = 0;
        title_status   = ID3_NO_TAG;      /* filename fallback shows the name */
        cur_file_id    = slot_file_id();
        return 1;
    }
    track_fmt = FMT_MP3;

    /* Converge rather than try to RECOGNISE a bad read: re-read until two
     * consecutive reads agree. That needs no reference value, no file size and
     * no theory of the cause, and it terminates on its own. */
    skip = id3_len(ring);
    track_title[0]  = 0;
    track_artist[0] = 0;
    track_album[0]  = 0;
    track_year[0]   = 0;
    track_trk[0]    = 0;
    title_status = ID3_NO_TAG;
    if (skip) {
        /* MUST happen before the audio re-read below, which overwrites ring[]
         * with audio content. TPE2 (band/album artist) rather than TPE1. */
        title_status = id3_find_text(ring, ring_fill, skip, "TIT2",
                                     track_title,  sizeof(track_title));
        id3_find_text(ring, ring_fill, skip, "TPE2",
                      track_artist, sizeof(track_artist));
        id3_find_text(ring, ring_fill, skip, "TALB",
                      track_album, sizeof(track_album));
        id3_find_text(ring, ring_fill, skip, "TRCK",
                      track_trk, sizeof(track_trk));

        /* Everything above only saw the first 4 KB of the tag. If the title is
         * not in there, the text frames sit past a large picture -- so walk the
         * tag properly rather than reporting a well-formed file as untagged.
         * Only the fields actually missing are looked up again. */
        /* Text frames past the artwork: pull MORE OF THE TAG into the ring and
         * parse it in memory, rather than walking it a frame header at a time.
         *
         * The walk cost ~29 separate SD reads, all inside the window where
         * pcm_flush() has emptied the FIFO -- audible on the one track whose
         * frames sit past a 15 KB picture, and on no other, because no other
         * track reaches this path. A 16 KB tag is three more 4 KB reads, and
         * the in-memory parser then finds everything for free. The ring is
         * 32 KB and is reloaded with audio immediately below, so filling it
         * with tag bytes here costs nothing. */
        if (title_status != ID3_OK && skip > ring_fill) {
            uint32_t want = skip;
            if (want > RING_SIZE) want = RING_SIZE;
            int ok = 1;
            while (ring_fill < want && ok) {
                uint32_t n2 = want - ring_fill;
                if (n2 > REFILL_CHUNK) n2 = REFILL_CHUNK;
                ok = target_read(ring_fill, RING_OFF + ring_fill, n2);
                if (ok) ring_fill += n2;
            }
            title_status = id3_find_text(ring, ring_fill, skip, "TIT2",
                                         track_title,  sizeof(track_title));
            if (!track_artist[0]) id3_find_text(ring, ring_fill, skip, "TPE2",
                                                track_artist, sizeof(track_artist));
            if (!track_album[0])  id3_find_text(ring, ring_fill, skip, "TALB",
                                                track_album, sizeof(track_album));
            if (!track_trk[0])    id3_find_text(ring, ring_fill, skip, "TRCK",
                                                track_trk, sizeof(track_trk));
            if (!track_year[0] && id3_find_text(ring, ring_fill, skip, "TDRC",
                                                track_year, sizeof(track_year)) != ID3_OK)
                id3_find_text(ring, ring_fill, skip, "TYER",
                              track_year, sizeof(track_year));
        }

        /* Still nothing found: the text frames sit behind a picture too large
         * for the ring to reach, however much of the tag was pulled in. Walk
         * the tag off the card, which skips a picture by arithmetic instead of
         * having to load it. Measured on the test card, this is the difference
         * between three tracks showing no metadata at all and showing all of
         * it. Runs only on those tracks -- anything the cheap path resolved
         * never gets here. */
        if (title_status != ID3_OK) {
            id3_walk_collect(skip);
            if (track_title[0]) title_status = ID3_OK;
        }
        /* Still nothing: the file may carry only an ID3v1 block at the end. */
        if (title_status != ID3_OK) {
            id3v1_read();
            if (track_title[0]) title_status = ID3_OK;
        }
        for (uint32_t i = 0; i < sizeof(track_trk); i++)
            if (track_trk[i] == '/') { track_trk[i] = 0; break; }  /* "5/12" -> "5" */
        if (id3_find_text(ring, ring_fill, skip, "TDRC",
                          track_year, sizeof(track_year)) != ID3_OK)
            id3_find_text(ring, ring_fill, skip, "TYER",
                          track_year, sizeof(track_year));

        track_year[4] = 0;
    }

    {
        int same = have_prev && (skip == prev_skip);
        for (uint32_t i = 0; same && i < sizeof(track_title); i++) {
            if (track_title[i] != prev_try[i]) same = 0;
            if (!track_title[i]) break;
        }
        if (same) break;                 /* two reads agree -> settled */
        if (attempt >= 1) break;         /* 0190 gates the load; belt and braces */
    }

    for (uint32_t i = 0; i < sizeof(track_title); i++) prev_try[i] = track_title[i];
    prev_skip = skip;
    have_prev = 1;

    reload_retries++;      /* R on screen = convergence passes, not failures */
    attempt++;
    uint32_t until = cycles() + CLK_HZ / 4u;         /* ~250 ms, then re-read */
    while ((int32_t)(cycles() - until) < 0) { }
    }

    audio_start  = skip;
    track_frames = 0;
    track_encoder[0] = 0; track_vbr_method = 0;
    track_secs   = 0;
    meas_rate    = 0; meas_pos0 = 0; meas_sec0 = 0;
    vbr_seen     = 0;

    /* Remember what we settled on, so later probes have a reference the load
     * itself cannot corrupt. */
    for (uint32_t i = 0; i < sizeof(track_title); i++) last_title[i] = track_title[i];
    cur_file_id = slot_file_id();

    if (skip) {
        file_pos  = skip;
        ring_fill = 0; ring_rd = 0;
        if (!target_read(skip, RING_OFF, REFILL_CHUNK)) return 0;
        ring_fill = REFILL_CHUNK;
    }
    track_bytes  = 0;
    size_suspect = 0;
    track_frames = vbr_frame_count();

    /* CROSS-CHECK the directory against the file's own account of itself.
     *
     * A Xing/VBRI BYTES field states exactly how many bytes of audio follow the
     * header, so audio_start + that is the file's real length -- give or take an
     * ID3v1 trailer. A corrupt directory entry inflates the SIZE while leaving
     * the audio intact, which is the damage seen twice on this card: .mp3s
     * reporting ~4x their true length with every frame still decoding.
     *
     * 1/4 over is far beyond any legitimate trailer and far below the observed
     * corruption, so it neither cries wolf nor misses the real thing. Files
     * without a Xing header simply are not checked -- a precise test on some
     * files beats a vague one on all of them. */
    if (track_bytes && slot_size) {
        uint32_t real = audio_start + track_bytes;
        if (slot_size > real + real / 4u) {
            size_suspect = 1;
            slot_size    = real;      /* trust the audio, not the directory */
        }
    }
    /* Nothing to check on a file with no Xing/VBRI header -- it never states
     * its own length, so there is nothing to disagree with. Better an exact
     * test on the files that can be tested than a guess applied to all of
     * them; a player that cries wolf about healthy files is worse than one
     * that stays quiet about a case it genuinely cannot judge. */
    return 1;
}

/* Everything needed to start a track from the beginning, shared by boot and by
 * a reload. ONE function deliberately -- two copies of this drift apart. */
__attribute__((optimize("Os")))       /* runs between tracks (audio silent), dominated by SD reads: size matters more than speed here */
COLD_SR static int load_track(void)
{
    /* Release the FLAC buffer FIRST. This runs before the format is known --
     * detection needs the file's head, which read_track_head() has not fetched
     * yet -- so Helix is rebuilt on every load and handed back below if the
     * track turns out to be FLAC.
     *
     * Order is load-bearing. Rebuilding Helix while an 18 KB FLAC buffer was
     * still live asked the arena for 42 KB of 24, MP3InitDecoder() returned 0,
     * and EVERY load failed from the first FLAC attempt onwards -- including
     * MP3s, which is how it presented. */
    if (fl_buf) { free(fl_buf); fl_buf = 0; }

    /* Helix carries bit-reservoir state internally, so a fresh instance is
     * needed rather than just resetting our own bookkeeping. */
    if (dec) MP3FreeDecoder(dec);
    dec = MP3InitDecoder();
    if (!dec) { REG(R_STAT2) = 0xF0000000u; return 0; }

    /* Silence the old track's tail before anything else touches the ring. */
    pcm_flush();

    frames = 0; errs = 0; rate_set = 0; min_level = 0xFFFFFFFFu;
    /* A NEW track starts at 0:00, and this is the one place that knows one
     * started. ui_draw_chrome used to do it, which caught every repaint too. */
    ui_sec = 0; ui_sec_acc = 0; ui_last_frames = 0xFFFFFFFFu;
    track_kbps = 0; track_hz = 0; samp_per_frame = 1152u;
    /* FLAC-only, and it LEAKED. Nothing cleared it on an MP3 load, so an
     * MP3 played after a FLAC inherited that FLAC's first-frame offset --
     * 642 KB on an album with large embedded art -- and the size probe's
     * gate is measured from it. On a 128 kbps track that pushed the probe
     * from 16 seconds out to 56, which on a short track means never. */
    fl_first_frame = 0;
    bytes_per_sec = 16000u;
    paused = 0;
    /* Arm the free-running-counter deadlines from NOW.
     *
     * Both are compared as `(int32_t)(cycles() - deadline) >= 0`, which is the
     * right way to handle a 32-bit counter that wraps every 71.6 s -- but only
     * once the deadline holds a real timestamp. Left at 0, the comparison
     * reduces to the sign of cycles() itself, so a track loaded while the
     * counter sits in its upper half reads NEGATIVE and the timer does not
     * fire until the counter wraps: up to 35.8 seconds, ~18 on average.
     *
     * That is not theoretical. It was reported as the FLAC meters flowing
     * slowly and out of time for about 17 seconds and then snapping into
     * place -- the wrap. With fl_ui_next dead, ui_draw_dynamic() ran only once
     * per FLAC frame, 9.6 Hz, scrolling the bars at 4.8. */
    fl_ui_next = cycles();
    tk_poll_at = cycles() + CLK_HZ * 2u;
    /* `stopped` is sticky and was cleared in exactly ONE place -- the A handler,
     * on un-pause. A new track starts PLAYING, so leaving it set meant the
     * first A press paused while the transport still read STOPPED, and it took
     * a second play/pause to clear. Clearing it here is what `paused = 0`
     * already means: this track is running, not parked at 0:00. */
    stopped = 0;
    seek_req = 0; soft_restart_req = 0;
    st0 = 0; REG(R_STAT0) = 0; REG(R_STAT1) = 0; REG(R_STAT2) = 0; REG(R_STAT3) = 0;
    st0 |= (1u << 0); REG(R_STAT0) = st0;            /* decoder up */

    /* The RTL only latches R_SLOT_SZ on a reload edge, so at boot there is
     * genuinely nothing to read. Take APF's number when there is one -- but
     * NOT after a core-initiated 0192 open, which raises no such edge and
     * would leave the PREVIOUS track's size here: wrong total time, wrong
     * end-of-track, and an end-of-track that fires early or never. */
    slot_size = force_size_probe ? 0u : REG(R_SLOT_SZ);
    force_size_probe = 0;
    seek_size_tried  = 0;

    uint32_t t0 = cycles(), tphase = t0;
    if (!read_track_head()) { REG(R_STAT2) = 0xE0000000u; return 0; }
    ld_head = LD_MS(cycles() - tphase); tphase = cycles();

    if (track_fmt == FMT_FLAC) {
        /* Hand the arena to FLAC. Helix has to go first -- the two decoders
         * swap the same space, and Helix's 23824 leaves no room beside a
         * blocksize buffer. */
        if (dec) { MP3FreeDecoder(dec); dec = 0; }
        fl_buf = 0;

        /* Open with a provisional cap so STREAMINFO can be read; the real
         * buffer is sized from max_blocksize once it is known. */
        static int32_t probe_cap;
        probe_cap = (int32_t)(ARENA_LIMIT / sizeof(int32_t));

        /* Tags come out of the Vorbis comment block during the metadata walk,
         * straight into the same fields the ID3 path fills -- so the card,
         * the marquees and the idle screen need to know nothing about format.
         * Set BEFORE flac_open, which is where the walk happens. */
        fl.tag_title  = track_title;
        fl.tag_artist = track_artist;
        fl.tag_album  = track_album;
        fl.tag_year   = track_year;
        fl.tag_trk    = track_trk;
        fl.tag_cap    = sizeof(track_title);

        fl.skip = flac_skip_bytes;  /* set BEFORE open: it survives the zeroing */
        flac_err fe = flac_open(&fl, flac_pull, 0, 0, (uint32_t)probe_cap);
        if (fe == FLAC_ERR_UNSUPPORTED) {
            /* Mirrors the order of the checks inside flac_open, so the reason
             * reported is the one that actually fired. */
            if (fl.channels < 1u || fl.channels > 2u) {
                fl_reject_kind = FLR_CHANS; fl_reject_val = fl.channels;
            } else if (fl.bps != 8u && fl.bps != 16u &&
                       fl.bps != 20u && fl.bps != 24u) {
                fl_reject_kind = FLR_DEPTH; fl_reject_val = fl.bps;
            } else {
                fl_reject_kind = FLR_BLOCK; fl_reject_val = fl.max_blocksize;
            }
            REG(R_STAT2) = 0xC4000000u | (uint32_t)fl_reject_kind;
            dec = MP3InitDecoder();
            track_fmt = FMT_MP3;
            rate_unsupported = 1u;
            return 0;
        }
        if (fe != FLAC_OK) {
            /* Hand the arena back to Helix rather than leaving the core with
             * no decoder -- otherwise one bad file breaks every load after
             * it, which is exactly what happened. */
            REG(R_STAT2) = 0xC0000000u | (uint32_t)fe;
            dec = MP3InitDecoder();
            track_fmt = FMT_MP3;
            return 0;
        }
        /* Refuse hi-res BEFORE committing the arena to it. Measured cutoff --
         * see ui_rate_unsupported(). Handing the arena back to Helix on the
         * way out matters: leaving the core with no decoder is what once made
         * a single bad file break every load after it. */
        if (fl.rate > FLAC_MAX_RATE
#if TAU_DIAGNOSTIC
            && !flac_accept_all_rates
#endif
        ) {
            REG(R_STAT2) = 0xC3000000u | fl.rate;
            fl_reject_kind = FLR_RATE;
            fl_reject_val  = fl.rate;
            dec = MP3InitDecoder();
            track_fmt = FMT_MP3;
            rate_unsupported = 1u;
            return 0;
        }
        fl_buf = (int32_t *)malloc((size_t)fl.max_blocksize * sizeof(int32_t));
        if (!fl_buf) { REG(R_STAT2) = 0xC1000000u; return 0; }
        fl.ch0     = fl_buf;
        fl.ch0_cap = fl.max_blocksize;
        fl_cap     = fl.max_blocksize;

        track_hz       = fl.rate;
        samprate       = fl.rate;
        track_kbps     = 0;      /* computed after the size probe, below */
        /* The format row's third field. On an MP3 it names the encoder; for a
         * lossless file the bit depth is the equivalent fact, and it is the
         * one thing about a FLAC that the rate does not already say. */
        {
            char *q = track_encoder;
            *q++ = 'F'; *q++ = 'L'; *q++ = 'A'; *q++ = 'C'; *q++ = ' ';
            q = ui_dec(q, fl.bps);
            *q++ = '-'; *q++ = 'b'; *q++ = 'i'; *q++ = 't';
            *q = 0;
        }
        /* Seek distance falls back to bytes_per_sec until the size probe lands
         * slot_size, and the 16000 default would make every FLAC seek about
         * six times too short. Estimate from the uncompressed rate instead:
         * FLAC lands around 60-77% of PCM on the test files (16/44.1 measured
         * 105 KB/s against 176 uncompressed, 24/44.1 204 against 265), so 70%
         * is within ~15% either way -- and it stops mattering entirely the
         * moment slot_size is known, when both helpers switch to the exact
         * size/duration figure. */
        bytes_per_sec  = ((uint32_t)fl.rate * (uint32_t)fl.channels
                          * (uint32_t)fl.bps / 8u) * 7u / 10u;
        samp_per_frame = fl.max_blocksize;
        track_secs     = fl.rate ? (uint32_t)DIV64(fl.total_samples, fl.rate) : 0;
        rate_set       = 1;
        pcm_rate_apply(fl.rate);
        flac_stall     = 0;
        flac_scan_metadata();      /* seek table + where audio starts */
        fl_rate_hz     = fl.rate;
        fl_bps_mirror  = fl.bps;
    }
#if IO_BENCH
    /* Here specifically: the FIFO is flushed and the gap is already silent,
     * so a one-off burst costs gap length rather than audio. */
    io_bench(audio_start);
    tphase = cycles();                      /* keep ld_size honest */
#endif
    if (audio_start) { st0 |= (1u << 1); REG(R_STAT0) = st0; }

    /* The size probe is ~20 blocking reads -- measured at 480 ms, and it runs
     * with the FIFO empty, which is most of the hiccup. Two attempts at caching
     * it away both silently failed to hit, so the answer is not a better cache:
     * an operation that long simply cannot live here.
     *
     * It is now INCREMENTAL. The main loop performs one read per pass, only
     * when the buffer is full, so the search spreads harmlessly across a couple
     * of seconds of playback instead of stalling the start of it. Nothing needs
     * the size immediately: it feeds the total time, the progress bar and the
     * end-of-track check, none of which matter in the first second.
     *
     * A file that declares its own length still skips all of this. */
    if (slot_size <= audio_start && track_bytes)
        slot_size = audio_start + track_bytes;
    if (slot_size <= audio_start)
        /* ARMED, not run. It used to block here, deliberately, on the argument
         * that a load is silent anyway so the reads cost gap length rather
         * than audio. True, but it cost 480 ms of every FLAC load, and worse,
         * it produced the WRONG answer for a file opened by name -- the slot
         * has only just been opened and a random read far into it still fails,
         * so a 30 MB track measured 5 MB and seeking could not work at all.
         * Running it a second later, spread across the main loop, is both
         * faster to load and correct. */
        size_probe_arm();
    /* FLAC's bitrate is only knowable once the file SIZE is -- the stream
     * carries a duration but never a rate, and it is variable anyway, so this
     * is the average over the whole file. It has to be here rather than in the
     * format branch above, which runs before the probe. */
    if (track_fmt == FMT_FLAC && track_secs && slot_size > fl_first_frame) {
        uint64_t bits = (uint64_t)(slot_size - fl_first_frame) * 8u;
        track_kbps = (uint32_t)DIV64(DIV64(bits, (uint64_t)track_secs), 1000u);
    }
    ld_size = LD_MS(cycles() - tphase); tphase = cycles();

    /* Library track: show the title, artist and album at once (taken from the index), and let the cover art and the
     * rest of the details follow once the file has fully loaded. The panel is parked so the early frame has no stale cover. */
    if (lib_src) {
        lib_meta_apply();
        ui_loader_begin_ex(1);      /* full frame with the index's title/artist/album; the loader turns in the empty art plate until the cover arrives */
    }
    /* Cover art BEFORE the chrome, because whether it exists decides the
     * layout: no art means no panel and a full-width waveform. Also before
     * prefill, so its blocking reads cannot starve playback. */
    /* Skip the art entirely when the file has not changed.
     *
     * This is the long pole in a restart. pcm_flush() has already emptied the
     * FIFO, and everything between it and prefill() runs with the DAC holding a
     * DC level -- the head read, the size probe, and a full JPEG decode with
     * its own SD reads. Re-decoding artwork that is already in the SDRAM stash
     * bought nothing and dominated that gap, which is what the hiccup on B
     * actually was. Same file, same picture: reuse it.
     *
     * Keyed on the 0190 file identity, so a genuine track change still decodes
     * and only a restart of the same file skips. */
    art_ready = 0;
    int has_art;
    if (cur_file_id && cur_file_id == art_file_id) {
        /* Same file. Free, and the common case on a restart. */
        has_art = art_have;
    } else {
        /* Different file, but very often the same PICTURE: every track of an
         * album embeds one cover. Measured on this hardware, that decode is
         * 2801 ms of a 3731 ms load, so asking first is worth a few reads.
         *
         * Must happen BEFORE ui_art_mount(), which fills the stash with the
         * panel colour -- checking afterwards would compare against an image
         * it had already destroyed. */
#if TAU_G4 >= 2
        uint32_t sig = COLD_READY() ? art_sig_of(audio_start) : 0u;     /* cold code */
#else
        uint32_t sig = art_sig_of(audio_start);
#endif
        art_bad = 0;
#if TAU_ART_TIMG
        /* Library track with a pre-converted cover beside it: no JPEG decode at all (docs/COVER_TIMG_READER.md). */
        char timg_path[LIB_MAX_PATH + 40u];
        const uint32_t timg_sig = COLD_READY() ? timg_cover_path(timg_path, sizeof(timg_path)) : 0u;
        if (timg_sig && art_have && timg_sig == art_sig) {
            has_art = 1;                     /* same album: the stash already holds this cover */
        } else if (timg_sig && timg_cover(timg_sig, timg_path)) {
            ui_art_round();
            art_sig = timg_sig;
            has_art = 1;
        } else
#endif
        if (art_have && sig && sig == art_sig) {
            has_art = 1;                 /* the stash already holds this cover */
        } else if (sig && sig == art_bad_sig) {
            /* Known bad. The frame and its reason are still drawn -- that is a
             * standing fact about the file, not an event -- but nothing is
             * decoded again and nothing is announced again. Repeating either
             * on all thirteen tracks of an album helps nobody. */
            ui_art_mount();
            ui_art_reason(art_bad_code == PJPG_UNSUPPORTED_MODE);
            ui_art_round();
            has_art = 0;
            art_bad = 1;
        } else {
            ui_art_mount();
#if TAU_G4 >= 2
            has_art = COLD_READY() ? art_decode(audio_start) : 0;     /* cold code: no cover without it */
#else
            has_art = art_decode(audio_start);
#endif
            if (!has_art) {
                if (art_fail_code) {     /* present, and unreadable */
                    art_bad      = 1;
                    art_bad_code = art_fail_code;
                    art_bad_sig  = sig;
                    ui_art_reason(art_bad_code == PJPG_UNSUPPORTED_MODE);
                } else {
                    ui_art_placeholder();   /* simply no artwork */
                }
            }
            ui_art_round();
            art_sig = has_art ? sig : 0u;

            /* A cover that IS there and cannot be read is worth a word. An
             * empty frame with no reason for it reads as the core being
             * broken, and the reason was already in hand -- picojpeg says
             * exactly why and it was being thrown away.
             *
             * Only when a picture was actually found: art_fail_code stays 0
             * when a file simply has no artwork, which is not a fault and
             * needs no announcement. */
            if (art_bad)
                ui_toast_msg(art_bad_code == PJPG_UNSUPPORTED_MODE
                             ? "COVER: PROGRESSIVE JPEG"
                             : "COVER: CANNOT BE READ");
        }
        art_file_id = cur_file_id;
    }
    art_ready = 1;
    ld_art = LD_MS(cycles() - tphase); tphase = cycles();

    /* Panel state follows the TRACK, not the session. art_x is set directly
     * rather than animated -- a track change should not look like a slide. */
    art_have  = (uint8_t)has_art;
    art_shown = (uint32_t)(ART_PANEL_WANTED && art_pref);
    art_x     = art_shown ? ART_X : FB_W;

    ui_chrome_paint();   /* title/artist are populated now -- draw the UI */
    ui_boot_cancel();   /* the loader is over: prefill()'s reads tick ui_boot_tick(), which would otherwise keep painting the spinner and its caption over the cover */

    if (!prefill()) { REG(R_STAT2) = 0xD0000000u; return 0; }

    /* Warm the decoder BEFORE playback, still inside the silent gap.
     *
     * The user's stop-vs-skip experiment isolated this: a warm decoder starts
     * clean, a fresh one tics. A fresh decoder's first successful frame is
     * synthesised through empty polyphase and overlap state; decode up to and
     * including that frame here and discard it, so the first frame that
     * actually PLAYS goes through a decoder in steady state -- the same
     * condition the proven-clean warm path starts from. Costs ~26 ms of gap
     * and the first ~26 ms of the track.
     *
     * Discarding was tried once before and made things worse -- but that was
     * before the glide, when the FIFO held a DC level and every extra frame
     * lengthened the hold. The output now rests at true zero through the gap,
     * so the trade is purely: one inaudible frame for a steady-state start. */
    {
        /* Discard until the RESERVOIR is genuinely populated, not just until
         * one frame has decoded.
         *
         * MP3 lets a frame reference up to 511 bytes of main_data from frames
         * BEFORE it. After a decoder reset that data does not exist, so early
         * frames are synthesised from an empty reservoir -- garbage, at full
         * amplitude. Discarding a single frame covers that only if one frame
         * exceeds 511 bytes, which holds at 256 kbps and fails at 112.
         *
         * This is why the fault tracked the Xing header rather than anything
         * about the loads: a Xing header IS a real frame containing silence, so
         * on those files the reservoir warms up on inaudible content before any
         * music is decoded. Headerless files start straight into audio and had
         * no such runway.
         *
         * Consume at least 512 bytes of successfully decoded frames -- one
         * frame at high bitrates, two or three at low, adaptive rather than a
         * fixed count, and 26-78 ms of a track's very start. */
        int guard = 12;                      /* never loop on a broken stream */
        uint32_t warmed = 0;                 /* bytes of DECODED frames so far */
        while (guard-- && warmed < 512u) {
            int bl = (int)(ring_fill - ring_rd);
            if (bl < 512) break;
            int off = MP3FindSyncWord(&ring[ring_rd], bl);
            if (off < 0) break;
            ring_rd += (uint32_t)off; bl -= off;
            unsigned char *ib = &ring[ring_rd];
            int before = bl;
            int e = MP3Decode(dec, &ib, &bl, pcm, 0);
            uint32_t used = (uint32_t)(before - bl);
            ring_rd += used;
            if (e == 0) {
                warmed += used;              /* only real frames count */

                /* Establish the stream's real rate HERE, off the warm-up
                 * frame, not on the first frame that plays.
                 *
                 * The hand-off ends load_track by running the reposition body,
                 * which finishes with ui_draw_dynamic() -- and that used to run
                 * before any frame had been decoded, so track_secs was 0 and
                 * bytes_per_sec was still the 16000 placeholder. The total was
                 * computed from a fake bitrate and drawn wrong on EVERY file,
                 * where before only headerless ones were ever estimated.
                 *
                 * These frames are decoded and discarded anyway; taking the
                 * frame info from them costs nothing and means the duration is
                 * right before anything can draw it. */
                if (!rate_set) {
                    MP3FrameInfo wfi;
                    MP3GetLastFrameInfo(dec, &wfi);
                    if (wfi.samprate) {
                        pcm_rate_apply(wfi.samprate);
                        if (wfi.bitrate) bytes_per_sec = wfi.bitrate / 8u;
                        samprate   = wfi.samprate;
                        track_hz   = wfi.samprate;
                        if (wfi.nChans && wfi.outputSamps)
                            samp_per_frame = (uint32_t)wfi.outputSamps
                                           / (uint32_t)wfi.nChans;
                        track_kbps = wfi.bitrate / 1000u;
                        if (track_frames && wfi.nChans) {
                            uint32_t spf = (uint32_t)wfi.outputSamps / (uint32_t)wfi.nChans;
                            track_secs = (uint32_t)DIV64((uint64_t)track_frames * spf,
                                                              wfi.samprate);
                        }
                        rate_set = 1;
                    }
                }
            }
            else if (!used) ring_rd++;       /* no progress: step past it */
        }
    }

    ld_pre   = LD_MS(cycles() - tphase);
    ld_total = LD_MS(cycles() - t0);
#if TAU_DIAGNOSTIC
    chk_loads++;
#endif

#if UI_SHOW_LOAD_TIMES
    /* The load-phase breakdown, as a toast, after every load.
     *
     * ROADMAP has had "track changes take too long" open since 2026-08-12 with
     * TWO guesses in this codebase pointing at different culprits: that entry
     * blames the artwork, the comment on the size probe blames the probe. The
     * timers have existed the whole time and settle it in one session.
     *
     * A toast rather than a key binding: the numbers are wanted for the load
     * that just happened, and needing to press something to see them means
     * pressing it during the gap you are trying to measure. Nothing to
     * remember, nothing to hold.
     *
     * H head read, S size probe, A art decode, P prefill, T total, all in ms.
     * OFF for any build a user sees. */
    {
        /* P is gone: measured at 6..26 ms against a 2200..3700 ms total, so it
         * only ever cost this row the width that clipped T -- "T3731" showed
         * as "T373", a total smaller than one of its own parts. */
        char b[40], *q = b;
        const char *lbl = "HSAT";
        const uint16_t v[4] = { ld_head, ld_size, ld_art, ld_total };
        for (uint32_t k = 0; k < 4u; k++) {
            *q++ = lbl[k];
            q = ui_dec(q, v[k]);
            if (k < 3u) *q++ = ' ';
        }
        *q = 0;
        ui_toast_set(b, 0xFFFFFFFFu, 0);
    }
#endif
    return 1;
}

int main(void)
{
    /* Bitstream/firmware interlock. On mismatch paint an unmistakable pattern
     * and stop, rather than running on stale RTL and presenting it as a
     * mysterious hardware fault. */
    if (!VERSION_OK(REG(R_VERSION))) {
        REG(R_STAT0) = 0xAAAAAAAAu; REG(R_STAT1) = 0x55555555u;
        REG(R_STAT2) = 0xAAAAAAAAu; REG(R_STAT3) = 0x55555555u;
        for (;;) { }
    }
    helios_beam_ok = (uint8_t)((REG(R_SCAN) >> 9) & 1u);   /* B-267: beam position present on this bitstream? */
    dbuf_hw = (uint8_t)((REG(R_DBUF_DISP) >> 31) & 1u);    /* Helios/Talos H2: double buffering present? -- a real dedicated
                                                             * presence bit (unlike BLIT_READY()/RRECT_READY()'s functional
                                                             * probe, needed only because THOSE opcodes have no such bit) */
    wave_hw = (uint8_t)(REG(R_WAVE_ST) & 1u);      /* B-283: hardware level/scope block present? */
    spec_hw = (uint8_t)(REG(R_SPEC_ST) & 1u);      /* B-263: hardware spectrum bank present? (0 on any other bitstream) */
    text_mode_hw = (uint8_t)((REG(R_TEXT_MODE) >> 31) & 1u);   /* theme/gamma: second text weight table present? (0 on an older bitstream) */
    hw_poly = (uint8_t)(REG(R_POLY_ST) & 1u);      /* B-292: hardware MP3 window unit present? (0 on any other bitstream) */
#if TAU_POLY_FW
    tau_poly_hw_enable = hw_poly;
#endif
    hw_lpc = (uint8_t)(REG(R_LPC_STATUS) & 1u);    /* B-369: hardware FLAC LPC unit present? (0 on any other bitstream) */
#if TAU_LPC_FW
    tau_lpc_hw_enable = hw_lpc;
#endif
    hw_cymo = (uint8_t)(REG(R_CYMO_STATUS) & 1u);  /* B-471/B-476: hardware Cymo resampler present? (0 on any other bitstream) */
#if FLAC_PROFILE
    /* B-342: one-time boot calibration of __clzdi2's real cost on THIS CPU, so flac.c's unary() call
     * count can be turned into an estimated cycle share without ever timing unary() itself live (which
     * would perturb the very decode being measured -- see flac.c's own comment on flac_unary_calls).
     * The loop's own overhead (the branch, the XOR, the accumulate) is included in clz_cal_cyc along
     * with the call/return -- an overestimate of __clzdi2 alone, but the same fixed per-iteration shape
     * every time, so it cancels out when comparing calibration runs across builds. x is loop-carried
     * (never a compile-time constant) and the result is folded into an accumulator the compiler cannot
     * prove is unused, so neither the calls nor the loop can be optimised away. */
    {
        uint64_t x = 0x1u;
        uint32_t acc = 0u;
        const uint32_t reps = 4096u;
        uint32_t t0 = cycles();
        for (uint32_t i = 0; i < reps; i++) {
            x = (x << 1) ^ (x >> 3) ^ (uint64_t)i ^ 1u;   /* never zero, never a repeating short cycle */
            acc += (uint32_t)__builtin_clzll(x);
        }
        clz_cal_cyc = (cycles() - t0) / reps;
        if (acc == 0xFFFFFFFFu) clz_cal_cyc++;   /* touch acc so it cannot be dead-code-eliminated */
    }
#endif

    /* Clear the screen FIRST. SDRAM powers up holding garbage and the scanout
     * engine displays it the moment video comes alive, so anything slow before
     * the first fill is visible as a screenful of noise. */
    fb_rect(0, 0, FB_W, FB_H, UI_BG);

    vol_apply();

    /* Phase F step 1 (docs/PHASE_F_SPEC.md section 14): point the decoders'
     * cycle-counting hooks at R_CYCLES. Compiles to nothing unless the
     * profiling build turns MP3_PROFILE and/or FLAC_PROFILE on. */
#if MP3_PROFILE
    mp3_tick = cycles;
#endif
#if FLAC_PROFILE
    flac_tick = cycles;
#endif

    /* ---- DIAGNOSTIC: dump APF's datatable, hold SELECT at boot ----
     *
     * Analogue's docs say a slot's size comes from "the Dataslot ID/Size Table
     * BRAM in the core" and that 0xF8xxxxxx is reserved for framework
     * communication -- but they never give the table's layout. This core has
     * been using that same BRAM as scratch (0190 response at words 0..63, 0192
     * parameters at 64..127, settings at 128+), which is the leading suspect
     * for both the 0184 corruption and the nonvolatile boot hang.
     *
     * So read it before anything of ours touches it. MUST run before
     * settings_load(): the first 0190 overwrites words 0..63.
     *
     * SNAPSHOT here, VIEW later. Gating this on a held button did not work:
     * it runs microseconds after the CPU leaves reset, before the Pocket has
     * delivered any controller state, so the read was always 0. The capture
     * has to happen now; the viewing does not. Select+A shows it. */
    dt_snapshot();

    /* Settings FIRST, so the splash is drawn in the accent the user actually
     * chose. It only reads a slot -- nothing on screen depends on it -- and
     * painting before it meant the very first thing shown was always the
     * default orange regardless of what had been saved. */
    settings_load_ok = (uint8_t)settings_load();

    /* Derive the background tint ONCE, here, where ui_accent has reached its
     * final value whether it was restored or left at the default. Doing it
     * inside the restore instead would miss the default path entirely --
     * settings_load() returns early when nothing has been saved -- and the
     * first boot would show a saved-colour UI on the untinted ramp. */
    ui_grad_set(ui_accent);

    /* Something deliberate on screen BEFORE the slow work -- loading the
     * library, opening a track, decoding cover art. Previously the first
     * paint came after all of that. */
    ui_splash_anim();

    cold_boot_load();                 /* first: the cold image holds data the menus need */
    /* B-162: blit_probe() is NOT called here at boot any more, for TAU_BLIT_PROBE or
     * TAU_METER_THUMBS -- it hung boot on real hardware (TAU_0_5_0_A_6, first time this function
     * was ever exercised on real hardware in any build). Deferred to blit_probe_ensure(), first
     * actual need, in set_draw_thumb(). See fw/blit_probe.inc's header for the full account. */
    ui_boot_note("LOADING LIBRARY");
#if TAU_G4
    if (COLD_READY()) lib_boot_load();
    else { lib_state = LIB_ST_OFF; lib_err = 18u; }     /* the library UI is cold code: without it the library stays off (Info shows E18) */
#else
    lib_boot_load();
#endif
#if TAU_G4
    if (COLD_READY()) th_assets_load();
#else
    th_assets_load();
#endif
    /* B-416 follow-up: settings_load() (well above, deliberately first for the splash accent)
     * validated the saved theme index against TH_COUNT() before th_assets_load() had a chance to
     * populate th_file_n -- a saved index pointing at an EXTRA theme (from tau-assets.bin) would be
     * wrongly rejected every boot, staying stuck at the built-in default, purely because of this
     * ordering, not because it was never saved. Re-validate now that TH_COUNT() is accurate. Not
     * confirmed to be what the owner's "theme: fail" report actually hit (their choice may simply
     * have been a built-in theme, fully explained by B-416's real cause: the save trigger itself was
     * missing) -- fixed anyway since it's a real, separate latent bug either way. */
    {
        uint32_t v = set_rd32(SW_THEME);
        if (v < TH_COUNT() && v != th_theme) { th_theme = (uint8_t)v; th_apply(); }
    }
    /* The SDRAM CPU window still needs proving at boot even though nothing here reads a
     * playlist any more: fw/suite.inc's Check (CT_SDW/CT_SDC) and Blit Test's crumb trail both
     * rely on this same proof having already run. */
    (void)pl_sdram_ready();
    ui_boot_clear();
    /* Land it HERE. Running on through load_track() was tried and looked wrong.
     * (Historical note, now-playing UI pass 1: this was originally because
     * ART_Y sat inside the meter band and the art panel painted over the bars
     * mid-animation -- art moved to its own static row above the meter, so
     * that specific race no longer applies, but stopping the settle animation
     * at this exact boot point is still the right call regardless.) */
    ui_wave_anim_stop();

    /* Try whatever is already in the slot -- a file picked from the Pocket's
     * browser, or one left there by a previous session -- and separately
     * restore the library's own last position. */
    /* The same indicator a boot-time load always gets: opening a track blocks
     * on the head read, the prefill and the artwork decode, and with nothing
     * on this row the splash just sits there looking hung until the player
     * appears -- which is precisely how it was reported.
     *
     * The dots cost nothing to animate: ui_boot_tick() is driven from inside
     * target_read_slot()'s spin, and it returns immediately unless a note is
     * armed, so arming one here is the whole change. */
    ui_boot_note("LOADING TRACK");
    int from_slot = 0, from_lib = 0;
    /* Library: open what the user was last playing (or the first track) but do not start it. */
    if (lib_state == LIB_ST_OK) { from_lib = lib_boot_restore();
        lib_boot_ok = (uint8_t)from_lib;   /* B-080: shown on Info so a release-vs-diagnostic mismatch has evidence, not a guess */
    }
    if (lib_state != LIB_ST_OK)      /* a library is browsed from the Select button; no auto-start from the file slot */
    from_slot = SR_READY() ? load_track() : 0;      /* B-333: load_track is cold code; no cold image, nothing plays */
    ui_boot_cancel();          /* not _clear: see the note on that function */

    if (!from_slot && !from_lib) {
        idle = 1;
        ui_wave_anim_stop();
        /* B-080: a library with nothing to restore (a first boot, or an index with no playable history) used to show
         * the separate "Library ready" getting-started card here, then the ordinary player chrome once the library
         * was opened and closed -- two different screens for the same "nothing loaded" state. ui_draw_chrome() now
         * carries its own message for this case (above), so it is the one screen, consistent with every other way of
         * reaching it. */
        if (lib_state == LIB_ST_OK) ui_chrome_paint();
        /* No library either: the idle screen (ui_draw_chrome()'s own "Sync your library" message,
         * added when legacy playlist mode was removed) carries the instruction now. */
        else if (SR_READY()) ui_idle_screen((const char *)0);
    }

    for (;;) {
        poll_input();

        /* Ticks the idle counter and blanks when it reaches the timeout.
         *
         * MUST be at the top of the loop, not the bottom: paused, stopped and
         * idle all `continue` before reaching the end, so a pump down there
         * only ever ran while a track was actively decoding -- which is the one
         * state that does NOT need burn-in protection, the meters being in
         * motion anyway. A player parked on a static screen (paused, or stopped
         * at the end of a non-repeating playlist, which sets `paused` itself)
         * is the whole reason this feature exists and was exactly the case it
         * missed. poll_input() resets the counter on a press just above, so the
         * ordering here is right. */
        ui_blank_pump();

        /* Keeps the LOADING dots moving through the reload gate. ui_boot_tick()
         * is otherwise driven only from inside target_read_slot()'s spin, and
         * the settle/probe wait issues no reads at all -- so without this the
         * indicator would appear and then sit frozen for over a second, which
         * looks more broken than no indicator. Returns immediately unless a
         * note is armed. */
        ui_boot_tick();

        /* Helios's display-list flush (docs/HELIOS_SPEC.md section 4/9). Placed here, not at the
         * loop's bottom, for the same reason as ui_blank_pump()/ui_boot_tick() just
         * above: paused/stopped/idle all `continue` before reaching the bottom, and a parked screen
         * still needs its dirty regions drawn. No region is registered yet (fw/helios.inc's own
         * header explains why), so this call costs one bounded loop over zero entries today. */
        helios_flush();

        /* A reload is NOT acted on the instant it is announced: 008A fires
         * when the user PICKS a file, not when the slot is readable. The old
         * track keeps decoding through the wait, so this costs no silence. */
        if (reload_pending && !reload_armed) {
            reload_status  = REG(R_RELOAD);

            /* Capture what we are leaving BEFORE slot_size is replaced. */
            for (uint32_t i = 0; i < sizeof(last_title); i++)
                stale_ref_title[i] = last_title[i];
            stale_ref_size    = slot_size;
            stale_ref_file_id = cur_file_id;
            slot_size         = REG(R_SLOT_SZ);

            /* Same cut as a skip, for the same reasons: the slot may already
             * be serving the NEW file, so both continuing to decode the ring
             * and refilling it are wrong. The old advice that the old track
             * "keeps playing through the wait" predates 0192 and was never
             * safe once the slot switches under the reader. */
            pcm_flush();
            refill_drain();
            ring_fill = 0; ring_rd = 0;

            /* The user has chosen a file: whatever queue was playing is no longer authoritative. */
            lib_src = 0u;

            REG(R_RELOAD)  = 1;             /* ack */
            reload_pending = 0;
            reload_armed    = 1;
            reload_probe_at = cycles();
            reload_settle   = cycles() + CLK_HZ * 3u / 2u;   /* blind fallback */
            reload_at       = cycles() + CLK_HZ * 5u;        /* hard cap       */
            /* Same gap: this gate waits at least 1.5 s before the track even
             * opens. The splash carries LOADING TRACK from the idle screen,
             * but with the player up there was nothing at all. */
            ui_boot_note("LOADING TRACK");

            /* Say so NOW, not when the gate below finally opens. That wait is
             * at least 1.5 s and can reach 5 s, and with the screen unchanged
             * for that long the pick looks ignored -- which is what "I have to
             * load it twice" was: no feedback, so the natural response is to
             * pick again. Same row as LOADING PLAYLIST, via the same helper,
             * because they are the same status line saying what it is doing.
             *
             * Only from the idle screen: UI_BOOT_Y falls between the
             * getting-started steps, so the splash goes up first to give the
             * indicator a clean card. With the player already on screen its
             * own repaint is the feedback. */
            if (idle) { ui_splash(); ui_boot_note("LOADING TRACK"); }
        }
        if (reload_armed) {
            /* ASK APF, do not infer: slot_file_id() hashes the 0190 response,
             * so "has the slot changed?" is answered by the file's identity
             * rather than by its contents. */
            /* ONE rule for both cases, because the old split had no
             * confirmation at all when stale_ref_file_id was 0 -- it waited a
             * blind 1.5 s and then read the slot whether or not APF had
             * switched it. That is the state every first load of a session is
             * in (nothing has been opened, so there is no previous identity),
             * so the very first Load MP3 was the one case that guessed. When
             * the guess was early the read failed, the idle screen came back,
             * and the second attempt worked because the slot had caught up by
             * then: exactly the reported "load it twice".
             *
             * A missing previous identity does not mean there is nothing to
             * confirm. Zero means APF is serving nothing; non-zero means it is
             * serving a file. So non-zero IS the change when there was no
             * previous id, and the same comparison covers both cases.
             *
             * reload_settle stays as a FLOOR rather than an alternative: if a
             * previous load failed the slot can still hold the old file, whose
             * id is non-zero and would satisfy the probe immediately. */
            int ready = 0;
            if ((int32_t)(cycles() - reload_settle)   >= 0 &&
                (int32_t)(cycles() - reload_probe_at) >= 0) {
                reload_probe_at = cycles() + CLK_HZ / 10u;
                /* No toast here. The boot row went up when the pick was
                 * detected and holds by itself until ui_boot_cancel(); a toast
                 * refreshed alongside it put the SAME words on a second line,
                 * which is the duplicate that kept being reported. */
                uint32_t id = slot_file_id();
                if (id != 0u && id != stale_ref_file_id) ready = 1;
            }

            if (ready || (int32_t)(cycles() - reload_at) >= 0) {
                reload_armed = 0;
                uint32_t tk_ge = ready ? 1u : 2u;
                uint32_t tk_was = tk_prev_name;

                /* The indicator went up when the pick was detected, not here.
                 * was_idle only decides what to restore if the open fails. */
                int was_idle = idle;
                if (!was_idle)
                    ui_boot_note("LOADING TRACK");
                int opened   = SR_READY() ? load_track() : 0;
                {   /* FNV over the filename APF reports for the slot. */
                    uint32_t h = 2166136261u;
                    for (uint32_t i = 0; i < sizeof(track_file)
                                      && track_file[i]; i++) {
                        h ^= (uint32_t)(uint8_t)track_file[i];
                        h *= 16777619u;
                    }
                    tk_prev_name = h;
                    tk_hist[0] = tk_hist[1];
                    tk_hist[1] = tk_hist[2];
                    tk_hist[2] = (uint16_t)((tk_ge << 8)
                               | ((opened ? 1u : 0u) << 4)
                               |  ((h != tk_was) ? 1u : 0u));
                }
                ui_boot_cancel();          /* chrome has repainted the row */
                /* Icons and the PLAYING label are tracked separately, so a
                 * wiped row needs both invalidated or the label never returns. */
                ui_mode_dirty = 1;
                ui_last_pause = 0xFFFFFFFFu;

                if (!opened && was_idle) {
                    /* The splash went up to carry the indicator; put the
                     * instructions back rather than leaving a bare card. */
                    if (SR_READY()) ui_idle_screen((const char *)0);
                }

                if (opened) {
                    idle = 0;
                    /* Hand off to the reposition body that is PROVEN clean.
                     *
                     * The user's evidence has been consistent for many rounds:
                     * stop-then-play and restart never click, a track change
                     * always does, and both end on the same audio at the same
                     * position. Rather than keep hunting the specific defect
                     * inside load_track's start-up -- one guess a round, none of
                     * them right -- let load_track do only what it uniquely
                     * must (open the file, read its tag, decode its art) and
                     * then START it the way the working path starts things:
                     * flush, rewind to audio_start, refill, prefill.
                     *
                     * Costs one extra prefill inside a gap that is already
                     * silent. Buys the guarantee that every route into playback
                     * is the same route. */
                    stop_req = 1u;
                    if (hold_paused) { hold_paused = 0; paused |= 1u; stopped = 1u; }
                    /* Nothing queues the first track any more. Picking a
                      * file is an explicit request to hear it, and the old
                      * behaviour -- the first track after launch loading
                      * paused, so music never starts the instant the core
                      * opens -- read as the player being stuck. */
                } else if (rate_unsupported) {
                    /* A file we CAN read and cannot play fast enough. Says so,
                     * rather than claiming the file could not be read. */
                    rate_unsupported = 0;
                    if (!idle) ui_rate_unsupported();
                } else { if (!idle) ui_load_failed(); }
                continue;
            }
        }


        /* The same backstop for the TRACK slot: a periodic identity check depending on nothing, so a
         * dropped 008A heals itself within one interval instead of waiting for the user to retry. */
        if (!idle && cur_file_id
            && !reload_pending    && !reload_armed
            && !rd_pending
            && (int32_t)(cycles() - tk_poll_at) >= 0) {
            tk_poll_at = cycles() + CLK_HZ * 2u;
            if (slot_changed()) reload_pending = 1u;
        }

        /* Track skip (Left/Right held), library queue only now that legacy playlist mode is gone. */
        if (skip_req) {
            uint32_t d = skip_req; skip_req = 0;
            /* load_track() clears `paused`, so a track changed while paused or
             * stopped would start playing on its own. Browsing the queue
             * without committing to hearing it is the point of allowing this
             * while paused. */
            hold_paused = (paused & 1u) ? 1u : 0u;
            /* Cut the outgoing track AT THE PRESS. pl_open_name blocks on 0190
             * and 0192 for longer than the 43 ms the FIFO holds, so leaving
             * the old track running meant it dipped to silence, faded BACK IN
             * for the settle window, then was cut again -- a stutter heard in
             * the outgoing song on every skip. A skip means the user is done
             * with this track; end it cleanly at the button. */
            pcm_flush();
            refill_drain();
            if (lib_skip(d == 1u ? 1 : -1)) {
                /* Slot now serves the NEW file: the ring's remaining bytes are
                 * the only old-track audio left, and refilling at the old
                 * offset would read the wrong file. Drop them; stay silent
                 * until the reload lands. */
                ring_fill = 0; ring_rd = 0;
                ui_toast_set("TRACK", (uint32_t)lib_qpos + 1u, 0);
                ui_mode_dirty = 1;
            }
            continue;
        }

        /* EQ preset change. The filter is in the RTL and the audio never stops
         * flowing, so this is seamless in a way a track change can never be --
         * no flush, no reload, nothing to resynchronise. */
        if (eq_apply) {
            eq_apply = 0;
            REG(R_EQ) = eq_idx;
            ui_eq_pill();
            ui_mode_dirty = 1u;          /* the mode row NAMES the preset */
            settings_mark_dirty();
        }

        /* Only when nothing is loading: a write is an SD round trip, and the
         * quiet window means it never lands in the middle of a track change. */
        if (!reload_armed && !reload_pending) settings_pump();

        /* Drive the size search from HERE as well, one step a pass.
         *
         * It was driven only from refill_pump()'s "ring at least half full"
         * branch, which on a dense FLAC comes round about once a second -- so
         * a sixteen-step search took sixteen seconds, and often never finished
         * at all. Two things had already broken on that: resume gave up
         * waiting for slot_size, and the FLAC format row stayed blank because
         * track_kbps is derived from it. Both were treated as their own bugs;
         * they were one.
         *
         * A step is a single 512-byte read and there are about sixteen of
         * them, so from here the whole thing is over in a fraction of a
         * second. Nothing new is asked of the card -- the same reads, sooner.
         * The gate inside still holds it off until 256 KB has been read, which
         * is what stops it measuring a file the slot has not settled into.
         *
         * ONLY WITH THE RING WELL AHEAD, though, and that qualifier is the
         * whole point of this line rather than a precaution.
         *
         * A probe step is a BLOCKING read. refill_pump has always spent its
         * steps from the "ring at least half full" branch for exactly that
         * reason; driving them from here as well, ungated, put a burst of
         * sixteen blocking reads wherever the main loop happened to be -- and
         * the gate above says WHERE: 256 KB of audio in, which on a ~1000 kbps
         * FLAC is about two seconds. The ring holds 24 KB, roughly 0.19 s at
         * that rate, so the burst outran it and the decoder went hungry once,
         * two seconds into the track, and was fine afterwards because the
         * search had finished. That is the single hiccup that settles in.
         *
         * Three quarters rather than the half refill_pump uses: this loop
         * spins far faster, so it wants the wider margin to avoid nibbling the
         * ring down at the boundary. */
        if (!reload_armed && !reload_pending &&
            ring_fill - ring_rd >= (RING_SIZE / 4u) * 3u)
            size_probe_pump();


        if (art_toggle) {
            art_toggle = 0;
            art_pref  = (uint8_t)!art_pref;
            art_shown = (uint32_t)(art_pref && ART_PANEL_WANTED);
            settings_mark_dirty();
            /* Instant, no slide: jump to the target and repaint the screen. */
            art_x = art_shown ? ART_X : FB_W;
            ui_chrome_paint();
            ui_wave_clear();
        }

        /* Probe-driven correction: jump to the audio_start it just proved
         * right, keeping the tag it just recovered. Deliberately does not go
         * through load_track(), which would re-read the head. */
        if (soft_restart_req) {
            soft_restart_req = 0;
            if (dec) MP3FreeDecoder(dec);
            dec = MP3InitDecoder();
            if (!dec) { REG(R_STAT2) = 0xF0000000u; ui_load_failed(); continue; }
            pcm_flush();
            refill_drain();
            frames = 0; errs = 0; rate_set = 0; min_level = 0xFFFFFFFFu;
            track_kbps = 0; track_hz = 0;
            file_pos  = audio_start;
            ring_fill = 0; ring_rd = 0;
            if (!prefill()) { st0 |= (1u << 4); REG(R_STAT0) = st0; }
            continue;
        }

        /* Reposition to 0:00 -- serves BOTH Start (stop, stays paused) and B
         * (restart, keeps playing). One body, differing only in the pause
         * flags poll_input set.
         *
         * B used to run a FULL cold load here -- fresh decoder, head reads,
         * art -- and that path clicked where this one does not. The proof was
         * the user's own experiment: stop-then-play lands on the identical
         * audio at the identical position through this body and is clean, so
         * restart now IS this body. The cold reload was a relic of the
         * stale-tag era; for the SAME file there is nothing to re-resolve.
         * Track CHANGES still load cold, as they must. */
        if (stop_req) {
            stop_req = 0;
            if (idle) continue;             /* nothing loaded to reposition */
            pcm_flush();
            refill_drain();
            if (track_fmt == FMT_FLAC) {
                /* Reopen: the decoder cannot resume from a rewound ring. */
                if (!flac_restart()) { st0 |= (1u << 4); REG(R_STAT0) = st0; }
                frames = 0; min_level = 0xFFFFFFFFu;
                ui_sec = 0; ui_sec_acc = 0;
                ui_last_sec = 0xFFFFFFFFu; ui_prog_sec = 0xFFFFFFFFu;
                ui_last_pause = 0xFFFFFFFFu;
                ui_draw_dynamic();
                continue;
            }
            file_pos  = audio_start;
            ring_fill = 0; ring_rd = 0;
            frames = 0; min_level = 0xFFFFFFFFu;
            ui_sec = 0; ui_sec_acc = 0;
            ui_last_sec = 0xFFFFFFFFu; ui_prog_sec = 0xFFFFFFFFu;
            ui_last_pause = 0xFFFFFFFFu;      /* repaint the transport label */
            if (!prefill()) { st0 |= (1u << 4); REG(R_STAT0) = st0; }
            ui_draw_dynamic();
            continue;
        }

        /* Seeking is allowed while paused or stopped -- moving through a track
         * without having to play it is the point of a transport. This sits
         * ABOVE the paused check for that reason; it used to be below it, so
         * the controls did nothing unless audio was running. */
        if (seek_req) {
            /* A track opened from a PLAYLIST arrives with no size: pl_arm_load()
             * zeroes slot_size and sets force_size_probe, because the file was
             * opened by name rather than mounted as a sized slot. Playback
             * never notices -- it only reads forward, and refill_pump()
             * records the real end when a read finally comes back short.
             * Seeking notices immediately: it needs to know where the end IS
             * before it can aim at a point inside the file.
             *
             * This is why the same FLAC seeks correctly after Load MP3 and not
             * after being played from a .m3u -- reported against Crash Test
             * Dummies, and true of any file whose size was never mounted.
             * flac_seek_locate() refuses outright on an unknown size, and
             * ui_byte_rate() below has the same dependency, so the MP3 path
             * was degraded by it too.
             *
             * Re-measured, not merely filled in when absent. An earlier
             * version only ran when slot_size was ZERO and never fired,
             * because the size was not missing -- it was WRONG. Measured on
             * hardware, one 30 MB FLAC: 5307 KB via a playlist against 30856
             * KB via Load MP3, and 172 kbps displayed instead of 1063.
             *
             * load_track() probes at load, when a file opened by name with
             * 0192 has only just been opened and a random read far into it
             * still fails. That truncated answer then stands for the whole
             * track. It poisons more than seeking -- the bitrate readout and
             * the end-of-track check read the same number -- but seeking is
             * where it shows, because the bracket then claims the whole
             * duration fits in a fifth of the file and every target lands
             * far short.
             *
             * Taking the LARGER is safe in the one direction that matters: an
             * early probe can only stop short of the true end, never run past
             * it, since it stops where a read first fails.
             *
             * ONCE per track. The probe is ~20 blocking reads at 480 ms, and
             * repeating it per press would replace a seek that does not move
             * with one that stalls first. */
            if (!seek_size_tried) {
                seek_size_tried = 1u;
                /* Finish the incremental search rather than starting a second
                 * one: same reads, none of them repeated. It has usually
                 * completed during playback long before a seek, in which case
                 * this costs nothing at all -- which is the point, because
                 * paying ~480 ms on the first press of every track was the
                 * "not as smooth as MP3" complaint. */
                uint32_t guard = 0;
                while (szp_phase && szp_phase < 4u && ++guard < 64u)
                    size_probe_step();

                uint32_t z = slot_size ? slot_size : probe_file_size();
                if (z > slot_size) {
                    slot_size = z;
                    /* The bitrate was derived from the old figure and is on
                     * screen right now -- 172 kbps for a FLAC that is really
                     * 1063. Recompute it here rather than leaving the header
                     * disagreeing with the file until the next load. */
                    if (track_fmt == FMT_FLAC && track_secs &&
                        slot_size > fl_first_frame) {
                        uint64_t bits = (uint64_t)(slot_size - fl_first_frame) * 8u;
                        track_kbps = (uint32_t)DIV64(DIV64(bits, (uint64_t)track_secs), 1000u);

                    }
                }
            }
#if UI_SHOW_SEEK_DIAG
            dg_size  = slot_size;
            dg_dur   = track_secs;
            dg_first = fl_first_frame;
            dg_pts   = fl_seek_pts;
            dg_len   = (fl.rate && fl.total_samples)
                     ? (uint32_t)(fl.total_samples / (uint64_t)fl.rate) : 0u;
            dg_intent = fl_seek_intent;
#endif

            uint32_t step = ui_byte_rate() * (seek_secs ? seek_secs : 5u);
            uint32_t want;

            /* FLAC with a seek table works in TIME, not bytes: the target
             * second is exact, and flac_seek_locate() MEASURES the offset
             * rather than interpolating one: it probes frame headers until the
             * byte it returns really is the second asked for. The byte path
             * below stays for MP3, where it has a great deal of history in
             * it. */
            if (track_fmt == FMT_FLAC && fl_first_frame && fl.rate) {
                uint32_t secs = seek_secs ? seek_secs : 5u;
                uint32_t tgt;
                /* Base a press on the last thing asked for while a run is in
                 * progress, otherwise on where playback actually is. Without
                 * this a short landing is inherited by the next press and the
                 * transport can wedge -- measured under tools/rv32sim.py as
                 * +22, +11, +5, +0, +0, +0. */
                uint32_t base = ui_sec;
                if (fl_seek_intent > ui_sec && fl_seek_intent - ui_sec < 30u)
                    base = fl_seek_intent;
                if (seek_req == 1u) {
                    tgt = base + secs;
                    /* Leave three seconds so the end is audible and the track
                     * finishes normally, the same rule the byte path uses. */
                    uint32_t last = (track_secs > 3u) ? track_secs - 3u : 0u;
                    if (tgt > last) tgt = last;
                } else {
                    tgt = (base > secs) ? base - secs : 0u;
                }
                fl_seek_intent = tgt;

                uint64_t landed = 0;
                uint32_t at = SR_READY() ? flac_seek_locate((uint64_t)tgt * (uint64_t)fl.rate, &landed) : 0u;
                seek_req = 0;
#if UI_SHOW_SEEK_DIAG
                /* A REFUSED seek leaves the rows stale and reads as "nothing
                 * happened", which is the one outcome that must not be
                 * ambiguous. Record it. */
                if (!at) { dg_tgt = tgt; dg_at = 0; dg_pos = 0; dg_fe = 98u; }
                dg_intent = fl_seek_intent;
#endif
                if (at && at != file_pos) {
                    file_pos = at;
                    stopped  = 0;
                    /* The landing was MEASURED before the jump, not assumed,
                     * so the clock is simply set to it. */
                    ui_sec      = (uint32_t)DIV64(landed, (uint64_t)fl.rate);
                    ui_sec_acc  = 0;
                    ui_last_sec = 0xFFFFFFFFu;
                    ui_prog_sec = 0xFFFFFFFFu;
                    meas_pos0   = file_pos;
                    meas_sec0   = ui_sec;
#if UI_SHOW_SEEK_DIAG
                    dg_tgt  = tgt;   dg_at   = at;
                    dg_blk  = fl.max_blocksize;
                    dg_rate = fl.rate;
                    dg_ui   = ui_sec;
                    dg_pos  = fl.rate ? (uint32_t)(landed / (uint64_t)fl.rate)
                                      : 0u;
                    dg_n    = 0;     dg_fe   = 0xFFu;  dg_live = 1u;
                    dg_num[0] = dg_num[1] = dg_num[2] = 0;
#endif

                    pcm_flush();
                    refill_drain();
                    ring_fill = 0; ring_rd = 0;
                    flac_flush_input(&fl);
                    flac_stall = 0;
                    fl_meter_n = 0;
                    /* From the MEASURED landing, not from the request: the
                     * two are the same only because the offset was refined
                     * until they were, and if a pathological file leaves them
                     * a frame apart the audio is what counts. */
                    if (fl.max_blocksize)
                        frames = (uint32_t)DIV64(landed, (uint64_t)fl.max_blocksize);
                    /* Must follow the rebase, and must equal it: the clock
                     * accumulator fires on `frames != ui_last_frames`, so a
                     * stale value here spends a phantom frame -- and the
                     * sentinel 0xFFFFFFFF would guarantee one. */
                    ui_last_frames = frames;
                    if (!prefill()) { st0 |= (1u << 4); REG(R_STAT0) = st0; }
                    if (paused) { ui_draw_dynamic(); continue; }
                }
                goto seek_done;
            }

            if (seek_req == 1u) {
                /* Forward stops short of the end. Backward has always clamped
                 * to audio_start; forward never did, so holding it walked
                 * file_pos past the end of the file, the refill came back empty
                 * and the EOF path advanced the track. Three seconds of tail is
                 * left so the end is audible and the track finishes normally. */
                want = file_pos + step;
                uint32_t rate = ui_seek_rate();
                if (slot_size > audio_start && rate) {
                    uint32_t last = slot_size - 3u * rate;
                    if (last < audio_start) last = audio_start;
                    if (want > last) want = last;
                }
                /* A forward seek may never end up BEHIND where it started.
                 *
                 * slot_size is not a constant: when a refill runs off the end,
                 * refill_pump() sets slot_size = file_pos to record the file's
                 * true extent. Seeking near the end makes the prefill do exactly
                 * that, so slot_size collapses to the current position -- and
                 * the limit computed from it lands BEHIND file_pos. The clamp
                 * then produced a backwards target, the movement guard saw a
                 * genuine change and repositioned, and slot_size shrank again
                 * on the next prefill. That is the clock walking backwards and
                 * never recovering, and why it depended on the song: it turns
                 * on where the reads first start failing.
                 *
                 * Clamping to file_pos makes the request a no-op instead, which
                 * the movement guard below then drops entirely. */
                if (want < file_pos) want = file_pos;
            } else {
                want = (file_pos > audio_start + step)
                     ? file_pos - step : audio_start;
            }

            seek_req = 0;

            /* A seek that does not MOVE must do nothing at all.
             *
             * Clamping alone was not enough: once parked at the limit, every
             * repeat still ran the whole reposition below -- pcm_flush(),
             * refill_drain(), ring_fill = 0, prefill() -- at the same offset,
             * four times a second. Playback advanced the clock between repeats
             * and each reposition snapped it back to the parked position, which
             * is the clock walking backwards; the constant re-prefill at the
             * boundary is what then fell into EOF. Holding at either limit is
             * now genuinely inert. */
            if (want != file_pos) {
                file_pos = want;
                stopped  = 0;                 /* no longer at 0:00 */

                uint32_t rate = ui_byte_rate();
                ui_sec      = rate ? (file_pos - audio_start) / rate : 0u;
                ui_sec_acc  = 0;
                ui_last_sec = 0xFFFFFFFFu;
                ui_prog_sec = 0xFFFFFFFFu;

                /* Start a fresh measurement window: the jump just moved
                 * file_pos without any time passing, and folding that into the
                 * average is what made the rate run away. */
                meas_pos0 = file_pos;
                meas_sec0 = ui_sec;

                pcm_flush();
                refill_drain();
                ring_fill = 0; ring_rd = 0;

                if (track_fmt == FMT_FLAC) {
                    /* The ring just moved; the decoder must not carry the old
                     * position's buffered bytes and half-consumed bit
                     * reservoir across with it. Without this it decodes stale
                     * input as though it belonged at the new offset and never
                     * resyncs -- the hang. frame_header() then scans for the
                     * next sync from clean state, and rejects a false one via
                     * its blocksize check against ch0_cap. */
                    flac_flush_input(&fl);
                    flac_stall  = 0;
                    fl_meter_n  = 0;

                    /* Rebase the frame counter. The FLAC path derives ui_sec
                     * from `frames`, so leaving it alone would let the next
                     * decoded frame snap the clock straight back to where the
                     * seek started. */
                    if (fl.max_blocksize)
                        frames = (uint32_t)DIV64((uint64_t)ui_sec * (uint64_t)fl.rate,
                                                  (uint64_t)fl.max_blocksize);
                }

                if (!prefill()) { st0 |= (1u << 4); REG(R_STAT0) = st0; }
                if (paused) { ui_draw_dynamic(); continue; }
            }
        seek_done: ;
        }

        /* B-406: moved AHEAD of the "B-234" block below (was after it, together with the rest of
         * this tick's other draws). A view transition queued THIS tick (e.g. opening Settings) must
         * have its chrome/art repaint actually landed in the framebuffer BEFORE anything else this
         * same tick reads "whatever is currently displayed" as a known-good snapshot -- concretely,
         * Settings' own hardware crossfade (fw/settingsui.inc's set_xfade_render()) does exactly
         * that the very first time set_draw() runs below. Before this reorder, a same-tick
         * open-Settings-from-Library transition captured Library's still-undrawn-over leftovers as
         * the crossfade's "before" frame -- B-406's reported "shows some elements of the playing
         * screen" during that specific transition. Nothing between the old and new position depended
         * on the old order: vblank_sample()/set_info_tick()/the diagnostic ticks/the library redraw
         * are all independent of whether chrome/art was just repainted. */
        if (helios_pending_mask) {
            uint8_t hm = helios_pending_mask;
            helios_pending_mask = 0;
            /* Helios H2: this is exactly the "genuine full-frame redraw" HELIOS_SPEC.md section 5
             * means -- chrome and (the same transition's) album art together, the one place every
             * view-transition's full repaint already funnels through (B-389/B-391). Bracketing HERE,
             * once, covers every current and future transition automatically instead of touching
             * each of the six call sites that used to set the old pl_ui_restore flag by hand. */
            const uint8_t dbuf_active = (hm & (HELIOS_INV_CHROME | HELIOS_INV_ART)) ? dbuf_redraw_begin() : 0u;
            if (hm & HELIOS_INV_CHROME) {
                /* ui_draw_chrome() paints the gradient itself -- calling it here
                   too would push ~360 rects twice for one repaint. */
                ui_chrome_paint();
                ui_mode_dirty = 1u;
                ui_last_info  = 0xFFFFFFFFu;
                ui_last_sec   = 0xFFFFFFFFu;
                ui_prog_sec   = 0xFFFFFFFFu;
            }
            if ((hm & HELIOS_INV_ART) && art_ready && art_shown) ui_art_draw();
            dbuf_redraw_end(dbuf_active);
            /* The meters' background strip lives in the invisible columns 400..511 of the meter rows, and the Check's
             * blit storm and the Blit Test write into those same columns. Rebuild it on every return to the player
             * screen, or the next erase copies the test's leftovers back as three 112 px tiles (B-256). */
            if (hm & HELIOS_INV_BG_STRIP) ui_bg_ready = 0;
            /* The meters cache what they last drew; the overlay painted over
             * all of it, so every column has to be considered stale.
             * B-406: also force this whenever `dbuf_active` fired even WITHOUT the HELIOS_INV_WAVE
             * bit (e.g. an art-only invalidation) -- H2's redraw bracket physically flips the
             * displayed buffer, and every non-chrome incremental drawer (Winamp Bars/Scope, VU
             * Master, Chladni, all reached through wviz_force/mtr_build()) was never told that
             * happened; its per-band/trail caches were built assuming the buffer they drew into
             * last frame is still the one now shown, which H2 makes false for one frame right after
             * any flip.
             * CORRECTED same session: gating wviz_force on `dbuf_active` ALONE (as first written)
             * missed the fullscreen visualiser case entirely -- FB_HELD() is true throughout
             * fullscreen (fw/player.c's own FB_HELD() macro includes ui_fullscreen), so
             * dbuf_redraw_begin() always refuses and `dbuf_active` is 0 for every fullscreen
             * enter/exit transition, even though those ARE real view transitions carrying
             * HELIOS_INV_ALL (so `hm & HELIOS_INV_WAVE` IS set). Matches the reported "scope
             * accumulation... comes back when going fullscreen then normal" exactly: the stale-trail
             * reset never fired on that specific transition. Now gated the same way as ui_wave_force
             * just above, on EITHER signal. (Fullscreen's own bar flicker turned out to be a separate,
             * unrelated bug -- OP_BAR's 7-bit lit-row field wrapping above 127, fixed at fb_bar()'s
             * own definition -- not this gap; corrected here so this comment doesn't keep overclaiming
             * a fix it didn't provide.) */
            if ((hm & HELIOS_INV_WAVE) || dbuf_active) {
                ui_wave_force = 1u;
                for (uint32_t i = 0; i < UI_WAVE_N; i++) { wave_drawn[i] = 0xFFu; wave_pk_drawn[i] = 0xFFu; }
            }
            if ((hm & HELIOS_INV_SPEC) || dbuf_active) { for (uint32_t z = 0; z < SPEC_BANDS; z++) spec_drawn[z] = 0xFFu; }
            if ((hm & HELIOS_INV_WAVE) || dbuf_active) wviz_force = 1u;
            if (hm & HELIOS_INV_EXCL) { for (uint8_t i = 0; i < HELIOS_MAX_EXCL; i++) helios_exclude_clear(i); }
        }
        /* B-234: the Meter > Configure page's live preview reads real playing
         * audio (spec_lvl[]/wav_v[]) every draw, same as the player screen's
         * own meter box -- but unlike every other Settings page, it needs to
         * ANIMATE continuously, not just repaint when the user presses a key.
         * Every other page is dirty-tracked because its content only changes
         * on input; this one's content changes on its own, every tick, which
         * set_dirty's edge-only model has no way to express. Forcing it here
         * (matching ui_blank_wake()'s own "if (set_open) set_dirty = 1u"
         * idiom for the same "something outside input changed" reason) was
         * the missing piece behind the reported "preview wasn't active with
         * music" -- previously it only ever redrew once per key press. */
        if (set_open && set_dirty) { set_dirty = 0u; set_draw(); }
        else if (set_open && set_page == SET_WVIZCFG_PG) wvcfg_preview_tick();
        vblank_sample();
        set_info_tick();
#if TAU_DIAGNOSTIC
        chk_tick();
#if MP3_PROFILE || FLAC_PROFILE
        sw_tick();
#endif
        bt_tick();
        mw_tick();
        mt_tick();
#endif
#if TAU_DIAGNOSTIC
        dg_soak_tick();
#endif
        if (lib_ui_open && lib_ui_dirty) { lib_ui_dirty = 0u; lib_ui_draw(); }
        if (lib_ui_open) lib_ui_marquee();
        if (lib_play_req) {
            stop_req = 0;
            if (lib_play_start()) { ui_mode_dirty = 1u; continue; }
            ui_toast_msg("TRACK WOULD NOT OPEN");
        }
        /* Nothing to decode. poll_input() and the reload handling above still
         * run, so Load MP3 / Load Playlist work from here. */
        if (idle) continue;

        if (paused) {
            st0 |= (1u << 7); REG(R_STAT0) = st0;
            /* Keep drawing. The UI update lives at the BOTTOM of this loop, so
             * simply continuing here meant PAUSED could only ever be drawn on
             * the one frame where the keypress landed mid-push. Force a
             * repaint on the TRANSITION -- ui_pause_next is left over from the
             * last pause and the cycle counter wraps, so the timer alone
             * silently skips that first draw about half the time. */
            if (!ui_was_paused || (int32_t)(cycles() - ui_pause_next) >= 0) {
                if (!ui_was_paused) {
                    ui_wave_force = 1;
                    for (uint32_t i = 0; i < UI_WAVE_N; i++) {
                        wave_drawn[i] = 0xFFu; wave_pk_drawn[i] = 0xFFu;
            for (uint32_t z = 0; z < SPEC_BANDS; z++) spec_drawn[z] = 0xFFu;
                    }
                }
                ui_was_paused = 1;
                ui_pause_next = cycles() + CLK_HZ / 30u;
                if (!ui_dump_mode) ui_draw_dynamic();
            }
            continue;
        }
        st0 &= ~(1u << 7); REG(R_STAT0) = st0;
        if (ui_was_paused) {
            ui_wave_force = 1;              /* recolour back to full on resume */
            for (uint32_t i = 0; i < UI_WAVE_N; i++) {
                wave_drawn[i] = 0xFFu; wave_pk_drawn[i] = 0xFFu;
            for (uint32_t z = 0; z < SPEC_BANDS; z++) spec_drawn[z] = 0xFFu;
            }
            /* The FIFO drained during the pause and its output has glided to
             * zero, so the resume must ramp up from zero like any other
             * discontinuity -- see FADE_SAMPLES. */
            fade_left = FADE_SAMPLES;
        }
        ui_was_paused = 0;

        /* Advance the asynchronous refill: start one if the ring has dropped
         * to half, or collect one that has landed. Never blocks. */
        refill_pump();

        int bytesLeft = (int)(ring_fill - ring_rd);
        if (bytesLeft < 512) {
            /* End of file: everything APF says the file holds has been read
             * and the ring is drained. Repeat -- with a playlist this is where
             * it advances instead, which is why it is a soft restart. */
            if (((slot_size && file_pos >= slot_size) || eof_hit) &&
                !rd_pending && !reload_armed && !reload_pending) {
                /* With a library queue this advances; lib_advance_auto() returns 0
                 * when it deliberately did not (repeat-one, or the end of a
                 * non-repeating list), and the old replay-this-track behaviour
                 * is the fallback -- so a single file still loops as before. */
                if (lib_advance_auto()) { ui_mode_dirty = 1; continue; }
                if (list_ended()) {
                    paused |= 1u;              /* end of the list: stop here */
                    ui_toast_msg("END OF LIST");
                }
                soft_restart_req = 1;
                continue;
            }
            st0 |= (1u << 5); REG(R_STAT0) = st0;
            continue;
        }

        if (track_fmt == FMT_FLAC) {
            /* One FLAC frame per pass. The decoder pulls from the ring and
             * pushes to the FIFO through the two adapters, so this loop keeps
             * its existing shape -- UI, input and end-of-track all still run
             * between frames. */
            /* The MP3 loop has this net a few lines further down; the FLAC
             * loop needs its own, and the count is what tells the diag row a
             * hiccup actually happened rather than was imagined. */
#if UI_SHOW_DIAG
            und_sample();
#endif
            if (!under_shadow && pcm_underrun()) {
                under_shadow = 1u;
                pcm_under_n++;
#if TAU_DIAGNOSTIC
                stress_note_underrun();
#endif
                fade_left    = FADE_SAMPLES;
                /* Latch the circumstances of the FIRST one only -- the later
                 * ones are consequences and would overwrite the evidence. */
                if (und1_sec == 0xFFFFFFFFu) {
                    und1_sec  = ui_sec;
                    und1_idle = fl_idle_pct;
                    und1_io   = fl_io_pct;
                    und1_szp  = (uint8_t)szp_phase;
                    und1_ring = (uint16_t)((ring_fill - ring_rd) >> 6);
                }
            }
            flac_err fe = flac_decode_frame(&fl, flac_emit, 0);
            /* Meters are fed from flac_emit on a fixed 1152-pair interval,
             * not here: once per frame is 9.6 Hz and looks delayed. */
            if (fe == FLAC_END || fe == FLAC_ERR_SHORT) {
                if (lib_advance_auto()) { ui_mode_dirty = 1; continue; }
                if (list_ended()) {
                    paused |= 1u;
                    ui_toast_msg("END OF LIST");
                }
                /* NOT soft_restart_req: that re-creates the Helix decoder, which cannot be allocated while the
                 * FLAC buffers hold the arena, so a FLAC at the end of a list (or repeat-one, or a single file)
                 * ended on LOAD FAILED. stop_req reopens the FLAC from 0:00, staying paused if paused above. */
                stop_req = 1;
                continue;
            }
            if (fe != FLAC_OK) { errs++; REG(R_STAT2) = 0xC2000000u | fe; }
            frames++;
#if UI_SHOW_SEEK_DIAG
            /* The three frames after a seek, exactly as the decoder saw them.
             * Recorded even when fe is an error, because "the first frame
             * failed" is itself a candidate explanation. */
            if (dg_live && dg_n < 3u) {
                if (dg_fe == 0xFFu) dg_fe = (uint8_t)fe;
                dg_num[dg_n++] = (uint32_t)fl.frame_number;
                dg_samp = fl.number_is_sample;
                if (dg_n == 1u && fl.rate) {
                    uint64_t smp = fl.number_is_sample
                                 ? fl.frame_number
                                 : fl.frame_number * (uint64_t)fl.max_blocksize;
                    dg_pos = (uint32_t)(smp / fl.rate);
                }
                if (dg_n >= 3u) dg_live = 0u;
            }
#endif
            /* The clock is NOT set here. ui_draw_dynamic() already advances
             * ui_sec by an accumulator whenever `frames` moves, and this line
             * recomputed it independently -- two writers, the same nominal
             * rate, different rounding, so they disagreed by a second
             * depending on which ran last. That was the +-1s twitch on the
             * progress row, and driving the UI more often made it worse by
             * running the accumulator far more often.
             *
             * The accumulator is also the cheaper of the two: samp_per_frame
             * is fl.max_blocksize and samprate is fl.rate, so it is adds and
             * compares in place of a 64-bit divide (__udivdi3, hundreds of
             * cycles on RV32) once per frame. */
            st0 |= (1u << 3);
            REG(R_STAT0) = st0;
            if (!ui_dump_mode) ui_draw_dynamic();
            continue;
        }

        int off = MP3FindSyncWord(&ring[ring_rd], bytesLeft);
        if (off < 0) { ring_rd = ring_fill; continue; }
        ring_rd += (uint32_t)off;
        bytesLeft -= off;

        uint32_t lvl = pcm_level();
        if (lvl < min_level) min_level = lvl;

        unsigned char *inbuf = &ring[ring_rd];
        int before = bytesLeft;
        int err = MP3Decode(dec, &inbuf, &bytesLeft, pcm, 0);
        ring_rd += (uint32_t)(before - bytesLeft);

        if (err) {
            errs++;
            /* -2 MAINDATA_UNDERFLOW is normal for a frame or two while the bit
             * reservoir fills; only a persistent run matters. */
            if (before - bytesLeft <= 0) ring_rd++;
            continue;
        }

        /* No reason to push what we just decoded from the OLD track once we
         * know it is about to be flushed. */
        if (reload_pending) continue;

        MP3FrameInfo fi;
        MP3GetLastFrameInfo(dec, &fi);

        /* One comparison per frame, and it decides which rate source the seek
         * arithmetic is allowed to trust. */
        if (fi.bitrate && rate_set && fi.bitrate / 8u != bytes_per_sec) vbr_seen = 1u;

        if (!rate_set && fi.samprate) {
            pcm_rate_apply(fi.samprate);
            if (fi.bitrate) bytes_per_sec = fi.bitrate / 8u;
            samprate   = fi.samprate;
            track_hz   = fi.samprate;
            track_kbps = fi.bitrate / 1000u;
            /* Exact when the file declares its frame count; otherwise the
             * size/bitrate fallback, which is only right for CBR. */
            if (fi.nChans && fi.outputSamps) {
                uint32_t spf = (uint32_t)fi.outputSamps / (uint32_t)fi.nChans;
                /* Set samp_per_frame HERE too, not only on the load-time path.
                 *
                 * This is the fallback that runs when the warm-up decode in
                 * load_track() did not establish the rate, and it computed spf
                 * for track_secs while leaving samp_per_frame at its reset
                 * default of 1152. That default is an MPEG-1 assumption:
                 * MPEG-2 and MPEG-2.5 carry 576 samples a frame, so on those
                 * files the elapsed clock -- which advances by samp_per_frame
                 * per frame -- ran at DOUBLE speed, and the progress bar with
                 * it. The two paths now derive it identically.
                 *
                 * Latent until now because MPEG-2 is rare in music; a 64 kbps
                 * spoken-word rip is full of it. On an MPEG-1 file spf is 1152
                 * and this changes nothing. */
                samp_per_frame = spf;
                if (track_frames && fi.samprate)
                    track_secs = (uint32_t)DIV64((uint64_t)track_frames * spf,
                                                      fi.samprate);
            }
            rate_set = 1;
            st0 |= (1u << 2); REG(R_STAT0) = st0;
        }

        /* If the FIFO ran dry, its output has glided toward zero and the
         * samples about to be pushed are mid-waveform: ramp them in. The flag
         * is sticky until the next flush, so this catches the FIRST underrun
         * of an epoch -- the net under the causes removed elsewhere, not a
         * licence to underrun. */
#if UI_SHOW_DIAG
        und_sample();
#endif
        if (!under_shadow && pcm_underrun()) {
            under_shadow = 1u;
            pcm_under_n++;
#if TAU_DIAGNOSTIC
            stress_note_underrun();
#endif
            fade_left    = FADE_SAMPLES;
        }

        int n = fi.outputSamps;                  /* interleaved L,R */
        int stereo = (fi.nChans == 2);

        meters_feed(pcm, n, stereo);

        for (int i = 0; i < n; i += (stereo ? 2 : 1)) {
            int32_t l = pcm[i];
            int32_t r = stereo ? pcm[i + 1] : l;
            /* Capped at unity, so this only ever attenuates and cannot
             * overflow -- no clamp needed. */
            if (vol_gain != 256) {
                l = (l * vol_gain) >> 8;
                r = (r * vol_gain) >> 8;
            }
            /* Ramp out of a discontinuity: one shift per sample, and only
             * while the fade is live. Grows 0 -> 255/256 across FADE_SAMPLES. */
            if (fade_left) {
                int32_t g = (int32_t)((FADE_SAMPLES - fade_left) >> 3);
                l = (l * g) >> 8;
                r = (r * g) >> 8;
                fade_left--;
            }
            /* Block while the FIFO is full -- pcm_fifo silently DROPS pushes
             * when full, so skipping this corrupts the audio rather than
             * merely delaying it. Being blocked here is the healthy state.
             * This is also where the CPU spends most of its time, so input and
             * I/O are serviced from inside the wait. */
            if (PCM_FULL(REG(R_PCM_ST))) {
                uint32_t t0 = cycles();
                do {
                    poll_input();
                    refill_pump();
                    if (reload_pending) { fl_idle_cyc += cycles() - t0;
                                          goto next_outer; }
                } while (PCM_FULL(REG(R_PCM_ST)));
                fl_idle_cyc += cycles() - t0;
            }
            REG(R_AUDIO) = ((uint32_t)(uint16_t)(int16_t)r << 16)
                         | (uint32_t)(uint16_t)(int16_t)l;
        }

        frames++;
        st0 |= (1u << 3);
        REG(R_STAT0) = st0;

        if (!ui_dump_mode) ui_draw_dynamic();

next_outer: ;
    }

    return 0;
}
