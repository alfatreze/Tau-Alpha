/* ================================================================== CHECK (TAU_CHECK): the user-facing diagnostics
 * Menu > Settings > Check runs one fixed profile (docs/TEST_SUITE_SPEC.md sections 7-8): memory tests, playback counters,
 * startup status. Result: a verdict, one line per test, a QR code with the full report (fw/suite_core.h record, decoded by
 * tools/decode_tau_suite.py) and a 36-character short code. The four-word summary is also stored in the persisted
 * settings words 8-11 (saved by the Pocket when the core is quit); the legacy playlist-name resume that used them does not
 * exist in library builds. The tests, the report builder and the QR encoder are COLD CODE (run from PSRAM), so the
 * resident cost is a few hundred bytes; without the cold image the page says so and nothing else is affected.
 * Never touches the framebuffer, the library index, the queue or the playlist buffers: scratch is SDRAM physical 8 MiB
 * (the Tests page's area), PSRAM 26 MiB (unused) and PSRAM 0x3000 (report buffers). */
#if TAU_CHECK
#if !TAU_COLD_CODE || !TAU_LIBRARY || !TAU_PL_SDRAM
#error "TAU_CHECK needs TAU_LIBRARY and TAU_COLD_CODE (the tests and the QR encoder are cold code)"
#endif
#if !TAU_METER_THUMBS
#error "TAU_CHECK needs TAU_METER_THUMBS (Blit Test, B-166, reuses THUMB_STASH_Y/meter_thumb_pal/blit_probe_ensure() unconditionally, and every real build target already bundles the two together)"
#endif
#define SR_FN COLD_TEXT static
#include "suite_core.h"
#define QR_FN COLD_TEXT static
#define QR_TBL COLD_DATA
#include "qrcode.h"

#define CHK_QR      ((uint8_t *)(uintptr_t)0xA4003000u)     /* QR working buffer, QR_BUF_LEN = 13,206 B (to 0x6398) */
#define CHK_REC     ((uint8_t *)(uintptr_t)0xA4007000u)     /* record, 2,048 B */
#define CHK_TXT     ((char *)(uintptr_t)0xA4007800u)        /* QR text, 2,800 B (version 38 at level L holds 2,699 B) */
#define CHK_REC_CAP 2048u
#define CHK_TXT_CAP 2800u
#define CHK_QR_MAXV 38u
#define CHK_SDRAM   0xA0800000u                             /* physical 8 MiB, as the Tests page */
#define CHK_PSRAM   0xA5A00000u                             /* PSRAM offset 26 MiB */
#define CHK_AUDIO_S 15u                                     /* length of the playback counter test */
#if TAU_DIAG_TESTS                                          /* Diagnostic Build: Check sits in SETTINGS > DIAGNOSTICS */
#define CHK_PARENT  SET_DGN
#else
#define CHK_PARENT  SET_DIAG
#endif
#define CHK_BASE    7u                                      /* the USER CHECK: test ids 0..6 */
#if TAU_DIAG_TESTS && TAU_SDRAM_STRESS_WINDOW               /* Diagnostic Build: STANDARD and FULL add stress and a soak (ids 7..10) */
#define CHK_DEV     1
#if TAU_COLDFRAME_ON
#define CHK_MAX     15u    /* B-199: CT_COLDFRAME is id 14, the last id sp_t's 15-bit mask can hold */
#else
#define CHK_MAX     14u
#endif
#else
#define CHK_DEV     0
#define CHK_MAX     7u
#endif
static void set_close(void);
enum { CK_IDLE = 0, CK_RUN, CK_DONE, CK_QR };
enum { CT_SDW = 0, CT_SDC, CT_PSW, CT_COLD, CT_LIB, CT_AUD, CT_START, CT_R1, CT_R2, CT_R3, CT_SOAK, CT_TRK, CT_CX, CT_BLT, CT_COLDFRAME };   /* test ids -- CT_BLT (B-127) is 13; CT_COLDFRAME (B-199) is 14, the LAST id sp_t's 15-bit pass/fail mask (ids 0..14) can ever hold */
#define CHK_TRK_N    10u                                    /* track changes in the track-change test */
#define CHK_STRESS_S 30u                                    /* seconds per stress level */
/* B-127: the blit-storm scratch strip -- framebuffer columns 400-511 of every displayed row (never shown;
 * mp3_fb.sv's own header says only columns 0-399 of the 512-word stride are displayed), reusing the exact
 * "never displayed" trick B-126's BLIT_READY() probe already proved safe, widened to the full off-screen
 * width (112 words, under OP_BLIT's 127-word glyphbuf limit) for a closer-to-worst-case load. This needs
 * no custom DST_STRIDE: the default sticky stride (512) already steps one real scanline per row, so a
 * single command with h=FB_H issues the whole frame's rows at once -- the "no line buffer needed" case
 * PHASE_F_SPEC.md section 5 (B4) describes for a randomly-addressable source. Never touches columns 0-399. */
#define CHK_BLT_COL  400u
#define CHK_BLT_W    112u

static uint8_t  chk_state, chk_step, chk_run, chk_n = CHK_BASE;
static uint8_t  chk_prof, chk_sel, chk_soak = 1u, chk_lvl = 2u;   /* profile 0-3, option row, soak length index (set_soak_min), stress level */
/* Per-test results live in the PSRAM scratch (uncached window, valid only while the cold image is), not in scarce fast RAM. */
struct chk_mem { uint32_t val[CHK_MAX]; uint16_t sd[6], ps[6]; uint8_t res[CHK_MAX]; uint8_t ti, tph;
                 uint32_t tc0, tnext, tsettle, cold_at, cold_runs, cold_fails, tlast, blt_busy0;
                 uint8_t aud_full, blt_full;
                 uint16_t tnq; uint8_t tsrc; };    /* sd/ps: read min/avg/max, write min/avg/max; blt_busy0: R_SDR_BUSY at CT_BLT start (B-127);
                                                    * aud_full/blt_full (B-139): latched 0 the instant CT_AUD/CT_BLT sees playback stop/pause
                                                    * mid-window, so "PASS" can be told apart from "PASS, but audio was not actually running
                                                    * the whole time" -- the ambiguity the owner hit on the first 0.5.0-alpha.1 Check run.
                                                    * tnq/tsrc (B-165): CT_TRK's queue length and lib_src at entry, captured so a FAIL can report
                                                    * WHY, not just that it happened -- see CT_TRK's own comment. */
#define CKM ((struct chk_mem *)(uintptr_t)0xA4008400u)
#define chk_val (CKM->val)
#define chk_sd  (CKM->sd)
#define chk_ps  (CKM->ps)
#define chk_res (CKM->res)
#define chk_aud_full (CKM->aud_full)
#define chk_blt_full (CKM->blt_full)
static uint32_t chk_at, chk_u0, chk_s0, chk_under, chk_stall, chk_qr_len, chk_qr_ver;
/* B-088/B-089: latched once when the CT_AUD window ends, from the run-total
 * accumulators (mp3_*_total_cyc / flac_*_total_cyc), so chk_finish() reads a
 * stable value instead of touching the profiling globals directly. */
static uint16_t chk_dec_h, chk_dec_i, chk_dec_s, chk_dec_r;

/* All text of the page in one cold blob (NUL separated), fetched by index. */
enum { CS_N0 = 0, CS_PASS = 7, CS_FAIL, CS_SKIP, CS_NA, CS_INTRO = 11, CS_RUNNING = 20, CS_H_IDLE, CS_H_RUN, CS_H_DONE, CS_TITLE,
       CS_DOTS, CS_S, CS_ONEF, CS_MANYF, CS_OKP, CS_OKALL, CS_VER, CS_RUNL, CS_SHOT, CS_X0, CS_PROF = CS_X0 + 8 /* B-127: +1 for BLIT STORM; B-199: +1 for COLD FRAME */, CS_PROFL = CS_PROF + 4, CS_SOAKL, CS_LVLL, CS_H_DEV, CS_TENTH };
COLD_DATA static const char chk_blob[] =
    "SDRAM WINDOW\0" "SDRAM SPEED\0" "PSRAM WINDOW\0" "COLD CODE\0" "LIBRARY\0" "PLAYBACK\0" "STARTUP\0"
    "PASS\0" "FAIL\0" "SKIPPED\0" "N/A\0"
    "Checks the memory, playlist and\0" "startup of this core and, if music\0" "is playing, 15 seconds of playback.\0" "\0"
    "It takes about 30 seconds. Music\0" "keeps playing.\0" "\0" "The result is a code you can send\0" "with a screenshot.\0"
    "RUNNING\0" "A START   B BACK\0" "B STOP\0" "A QR CODE   Y AGAIN   B BACK\0" "CHECK\0"
    "...\0" " S\0" " CHECK FAILED\0" " CHECKS FAILED\0" "PASSED (PLAY MUSIC FOR ALL)\0" "ALL CHECKS PASSED\0"
    "VERSION \0" "   RUN \0" "MENU+START SCREENSHOT   B BACK\0"
    "STRESS R1\0" "STRESS R2\0" "STRESS R3\0" "SOAK\0" "TRACK CHANGES\0" "COLD X20\0" "BLIT STORM\0" "COLD FRAME\0"
    "USER CHECK\0" "STANDARD\0" "FULL\0" "ENDURANCE\0"
    "PROFILE  \0" "SOAK LENGTH  \0" "SOAK LEVEL  R\0" "A START  B BACK  UP DOWN ROW  L R CHANGE\0" "/10";
COLD_TEXT static const char *chk_s(uint32_t i)
{
    const char *p = chk_blob;
    while (i--) { while (*p) p++; p++; }
    return p;
}

COLD_TEXT static uint32_t chk_pat(uint32_t a) { return (a * 2654435761u) ^ 0xA5C3E17Bu; }

/* Words written and read back-to-back, sub-word merges, and one address per address line, in a scratch window at `base`.
 * Line addresses are base ^ (1 << k) for the lines that stay inside memory nobody else uses. */
COLD_TEXT static uint32_t chk_window(uint32_t base, uint32_t lo, uint32_t hi, uint32_t skip, uint32_t *checks)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)base;
    uint32_t bad = 0, n = 65u;
    for (uint32_t i = 0; i < 64u; i++) p[i] = chk_pat(i);
    for (uint32_t i = 0; i < 64u; i++) if (p[i] != chk_pat(i)) bad++;
    p[64] = 0xFFFFFFFFu;
    volatile uint8_t *b = (volatile uint8_t *)(uintptr_t)base;
    volatile uint16_t *h = (volatile uint16_t *)(uintptr_t)base;
    b[256] = 0x11u; b[257] = 0x22u; h[129] = 0x4433u;
    if (p[64] != 0x44332211u) bad++;
    uint32_t off = base & 0x03FFFFFFu, top = base & ~0x03FFFFFFu;
    for (uint32_t k = lo; k <= hi; k++) {
        if ((skip >> k) & 1u) continue;
        *(volatile uint32_t *)(uintptr_t)(top | (off ^ (1u << k))) = chk_pat(k + 9u);
    }
    for (uint32_t k = lo; k <= hi; k++) {
        if ((skip >> k) & 1u) continue;
        n++;
        if (*(volatile uint32_t *)(uintptr_t)(top | (off ^ (1u << k))) != chk_pat(k + 9u)) bad++;
    }
    *checks = n;
    return bad;
}

/* Net cycles per access over 256 accesses: best, average and worst. Contention (album-art decode, drawing, audio DMA) inflates
 * the average and the worst, so the pass rule judges the best case, which only a healthy path reaches; all three are reported. */
COLD_TEXT static void chk_cost(uint32_t base, int write, uint16_t *out)
{
    volatile uint32_t *p = (volatile uint32_t *)(uintptr_t)base;
    uint32_t ovh = 0xFFFFFFFFu, mn = 0xFFFFFFFFu, mx = 0, sum = 0;
    for (uint32_t i = 0; i < 16u; i++) { uint32_t t = cycles(); uint32_t d = cycles() - t; if (d < ovh) ovh = d; }
    for (uint32_t i = 0; i < 256u; i++) {
        uint32_t t = cycles();
        if (write) p[i & 63u] = i; else (void)p[i & 63u];
        uint32_t d = cycles() - t; d = d > ovh ? d - ovh : 0u;
        if (d < mn) mn = d;
        if (d > mx) mx = d;
        sum += d;
    }
    out[0] = (uint16_t)(mn > 0xFFFFu ? 0xFFFFu : mn); out[1] = (uint16_t)(sum / 256u); out[2] = (uint16_t)(mx > 0xFFFFu ? 0xFFFFu : mx);
}

COLD_TEXT static void chk_set(uint32_t ok, uint32_t v) { chk_res[chk_step] = (uint8_t)ok; chk_val[chk_step] = v; }   /* results are per step */

/* Steps after the seven base checks, per profile (0 USER CHECK, 1 STANDARD, 2 FULL, 3 ENDURANCE).
 * B-127 adds CT_BLT to all three dev profiles per PHASE_F_SPEC.md section 12.1 ("Add to STANDARD,
 * FULL and ENDURANCE"), widening the row from 6 to 7. B-199..B-201 (PHASE_F_SPEC.md sections 4.2-4.3)
 * add CT_COLDFRAME right after CT_BLT in the same three profiles, but only when TAU_COLDFRAME_ON
 * (either the one-off synthetic-probe experiment, or the real G4-step-4 conversion it now also
 * permanently guards) -- every build below that stays at the original width, CT_COLDFRAME's id
 * simply never reachable. */
#if TAU_COLDFRAME_ON
COLD_DATA static const uint8_t chk_ex[4][8] = { { 0 }, { CT_TRK, CT_CX, CT_R1, CT_R2, CT_R3, CT_BLT, CT_COLDFRAME, 0 },
                                                { CT_TRK, CT_CX, CT_R1, CT_R2, CT_R3, CT_SOAK, CT_BLT, CT_COLDFRAME }, { CT_SOAK, CT_BLT, CT_COLDFRAME, 0, 0, 0, 0, 0 } };
COLD_DATA static const uint8_t chk_exn[4] = { 0, 7, 8, 3 };
#else
COLD_DATA static const uint8_t chk_ex[4][7] = { { 0 }, { CT_TRK, CT_CX, CT_R1, CT_R2, CT_R3, CT_BLT, 0 },
                                                { CT_TRK, CT_CX, CT_R1, CT_R2, CT_R3, CT_SOAK, CT_BLT }, { CT_SOAK, CT_BLT, 0, 0, 0, 0, 0 } };
COLD_DATA static const uint8_t chk_exn[4] = { 0, 6, 7, 2 };
#endif
COLD_TEXT static uint32_t chk_id(uint32_t step) { return step < CHK_BASE ? step : chk_ex[chk_prof][step - CHK_BASE]; }

#if CHK_DEV
/* One pass of the cold-code checks (the Tests page's COLD CODE TEST without its timing): calls in and out, a call back into
 * hot code, and 12 KB of code that cannot stay in the I-cache. Counts runs and failures. */
COLD_TEXT static void chk_cold_once(void)
{
    uint32_t f = 0;
    if (cold_add(40u, 2u) != 42u) f++;
    if (cold_loop(100u) != 15150u) f++;
    if (cold_calls_hot(21u) != 43u) f++;
    if (cold_big(7u) != 3007u) f++;
    CKM->cold_runs++;
    if (f) CKM->cold_fails++;
}
#endif

/* One step of the run. Steps 0-4 and 6 finish at once; step 5 (playback) spans CHK_AUDIO_S seconds and is re-entered
 * until done (returns 0 while waiting). */
COLD_TEXT static int chk_do(uint32_t id)
{
    switch (id) {
    case CT_SDW:
#if TAU_PL_SDRAM
        if (!pl_sdram_ready()) { chk_set(SR_FAIL, 0xFFFFu); break; }
        { uint32_t n, bad = chk_window(CHK_SDRAM, 2u, 25u, 1u << 23, &n);       /* bit 23 would fold onto the framebuffer */
          chk_set(bad ? SR_FAIL : SR_PASS, bad ? bad : n); }
#else
        chk_set(SR_NA, 0);
#endif
        break;
    case CT_SDC:
#if TAU_PL_SDRAM
        if (!pl_sdram_ready()) { chk_set(SR_FAIL, 0xFFFFu); break; }
        chk_cost(CHK_SDRAM, 0, &chk_sd[0]); chk_cost(CHK_SDRAM, 1, &chk_sd[3]);
        chk_set((chk_sd[0] <= 60u && chk_sd[3] <= 45u) ? SR_PASS : SR_FAIL, chk_sd[0]);     /* best case: 48 read, 31 write when healthy */
#else
        chk_set(SR_NA, 0);
#endif
        break;
    case CT_PSW: {
        if (!REG(0x80000088u) || REG(0x80000088u) != 0x50535231u) { chk_set(SR_NA, 0); break; }
        uint32_t n, bad = chk_window(CHK_PSRAM, 2u, 24u, 0u, &n);
        chk_cost(CHK_PSRAM, 0, &chk_ps[0]); chk_cost(CHK_PSRAM, 1, &chk_ps[3]);
        if (chk_ps[0] > 48u || chk_ps[3] > 40u) bad++;          /* 39/30 measured on the G3 bitstream (32/26 on P4): its arbiter adds a few cycles */
        chk_set(bad ? SR_FAIL : SR_PASS, bad ? bad : n);
        break; }
    case CT_COLD:
        /* this function is itself cold code, so being here proves the fetch path; also require the probe to return */
        chk_set((COLD_READY() && cold_probe(6u, 7u) == 43u) ? SR_PASS : SR_FAIL, cold_err);
        break;
    case CT_LIB:
        if (lib_disabled || lib_state == LIB_ST_NONE) { chk_set(SR_NA, pl_count); break; }
        if (lib_state != LIB_ST_OK) { chk_set(SR_FAIL, lib_err); break; }
        {
            uint32_t bad = 0, n = lib.n_tracks, step = n > 128u ? n / 128u : 1u;
            char buf[128];
            for (uint32_t i = 0; i < n; i += step) {
                lib_track_t t; lib_track(&lib, i, &t);
                if (!lib_path(&lib, i, buf, sizeof(buf)) || t.album >= lib.n_albums) bad++;
            }
            chk_set(bad ? SR_FAIL : SR_PASS, bad ? bad : n);
        }
        break;
    case CT_AUD:
        if (!chk_at) {                                          /* first entry: is anything playing? */
            if (!track_hz || paused || stopped) { chk_set(SR_SKIP, 0); break; }
            chk_u0 = pcm_under_n; chk_s0 = REG(R_FB_STALL); chk_at = (cycles() + CHK_AUDIO_S * CLK_HZ) | 1u;
            chk_val[chk_step] = 0; chk_aud_full = 1u;
#if MP3_PROFILE
            mp3_huff_total_cyc = mp3_imdct_total_cyc = mp3_sub_total_cyc = 0u;
#endif
#if FLAC_PROFILE
            flac_res_total_cyc = flac_lpc_total_cyc = 0u;
#endif
            return 0;
        }
        /* B-139: was playing at entry proves nothing about the other 14 seconds -- a track that
         * ends mid-window, or a pause the tester didn't intend, would otherwise pass silently on
         * whatever underrun count happened to be true so far. Latched, not re-armed: one drop is
         * enough to mark the whole window's result as measured under incomplete playback. */
        if (!track_hz || paused || stopped) chk_aud_full = 0u;
        if ((int32_t)(cycles() - chk_at) < 0) { chk_val[chk_step] = (uint32_t)((int32_t)(chk_at - cycles()) / (int32_t)CLK_HZ + 1); return 0; }
        chk_under = pcm_under_n - chk_u0;
        chk_stall = (REG(R_FB_STALL) - chk_s0) / (CLK_HZ / 1000u);
        /* B-088/B-089: latch this window's decode-stage percentages once,
         * here, at the exact moment the run-total accumulators stop moving --
         * same CHK_AUDIO_S/CLK_HZ arithmetic as the screen row's per-second
         * version, just over the whole window instead of one second.
         * B-097: gated on track_fmt, same reasoning and same fix as
         * sw_sample() -- a stage that cannot apply to the track actually
         * playing is forced to 0 regardless of what its accumulator says. */
#if MP3_PROFILE
        if (track_fmt == FMT_MP3) {
            chk_dec_h = (uint16_t)(mp3_huff_total_cyc  / (CHK_AUDIO_S * (CLK_HZ / 100u)));
            chk_dec_i = (uint16_t)(mp3_imdct_total_cyc / (CHK_AUDIO_S * (CLK_HZ / 100u)));
            chk_dec_s = (uint16_t)(mp3_sub_total_cyc   / (CHK_AUDIO_S * (CLK_HZ / 100u)));
        } else { chk_dec_h = chk_dec_i = chk_dec_s = 0u; }
#endif
#if FLAC_PROFILE
        if (track_fmt == FMT_FLAC) {
            uint32_t rt = flac_res_total_cyc + flac_lpc_total_cyc;
            chk_dec_r = (uint16_t)(rt ? (flac_res_total_cyc * 100u) / rt : 0u);
        } else { chk_dec_r = 0u; }
#endif
        chk_set((chk_under == 0u && chk_stall < 50u) ? SR_PASS : SR_FAIL, chk_under);
        break;
#if CHK_DEV
    case CT_R1: case CT_R2: case CT_R3:                         /* stress pump beside playback: no mismatch, no late underrun */
        if (!chk_at) {
            stress_set_level(0u); stress_set_level((uint8_t)(id - CT_R1 + 1u));
            if (!stress_on) { chk_set(SR_FAIL, 0xFFFFu); break; }
            chk_u0 = stress_und_late; chk_at = (cycles() + CHK_STRESS_S * CLK_HZ) | 1u; chk_val[chk_step] = CHK_STRESS_S;
            return 0;
        }
        if ((int32_t)(cycles() - chk_at) < 0 && stress_on && !stress_failures) { chk_val[chk_step] = (uint32_t)((int32_t)(chk_at - cycles()) / (int32_t)CLK_HZ + 1); return 0; }
        {
            uint32_t late = stress_und_late - chk_u0;
            int ok = stress_on && !stress_failures && !late;
            stress_set_level(0u);
            chk_set(ok ? SR_PASS : SR_FAIL, late);
        }
        break;
    case CT_BLT:  /* B-127, PHASE_F_SPEC.md section 12.1: the L0 regression net for the blit engine's new SDRAM traffic.
                   * Full-height OP_BLIT commands into the never-displayed columns 400-511, re-issued the instant the
                   * draw engine goes idle (non-blocking poll, not fb_wait() -- a blocking wait here would itself risk
                   * starving the main loop and manufacturing an underrun unrelated to the blit engine's real cost).
                   * Verdict is late underruns only, exactly like CT_R1-3; the value carried out is the SDRAM busy
                   * permille over the window (0xFFFF if TAU_SDRAM_BUSY is off for this bitstream) -- informational,
                   * not a pass/fail criterion, per section 12.1's "this is the half that matters" note. */
        if (!chk_at) {
            if (!track_hz || paused || stopped) { chk_set(SR_NA, 0); break; }   /* needs real playback, like CT_AUD */
            chk_u0 = pcm_under_n; chk_blt_full = 1u;
#if TAU_SDRAM_BUSY
            CKM->blt_busy0 = REG(R_SDR_BUSY);
#endif
            chk_at = (cycles() + CHK_STRESS_S * CLK_HZ) | 1u; chk_val[chk_step] = CHK_STRESS_S;
            return 0;
        }
        if (!track_hz || paused || stopped) chk_blt_full = 0u;   /* B-139, same reasoning as CT_AUD: latched, not re-armed */
        if ((int32_t)(cycles() - chk_at) < 0) {
            if (!(REG(R_FB_GO) & 1u)) {
                REG(R_FB_ADDR)  = CHK_BLT_COL;
                REG(R_FB_SIZE)  = (FB_H << 9) | CHK_BLT_W;
                REG(R_FB_COLOR) = 0u;                       /* src offset 0 -- a benign, read-only flat SDRAM read */
                REG(R_FB_GO)    = FB_OP_BLIT;
                fb_color_shadow = 0xFFFFFFFFu;              /* BLIT clobbers the colour regs for its own use, same as COPY */
            }
            chk_val[chk_step] = (uint32_t)((int32_t)(chk_at - cycles()) / (int32_t)CLK_HZ + 1); return 0;
        }
        {
            uint32_t late = pcm_under_n - chk_u0;
            uint32_t busy_permille = 0xFFFFu;
#if TAU_SDRAM_BUSY
            {
                uint32_t delta = REG(R_SDR_BUSY) - CKM->blt_busy0;
                uint32_t total_k = CHK_STRESS_S * (SDR_CLK_HZ / 1000u);   /* window length in clk_sdram cycles, pre-divided by 1000 (exact) to keep delta/total_k overflow-free at 32 bits */
                busy_permille = delta / total_k;
            }
#endif
            /* B-139: bit 16 carries chk_blt_full (1 = playback ran the whole window), well clear
             * of busy_permille's own range (<=1000, or the 0xFFFF "not measured" sentinel) and of
             * the 16 bits SR_T_TEST's own value field already spends elsewhere -- this field is a
             * plain uint32, so the extra bit costs nothing and needs no format change. */
            chk_set(late == 0u ? SR_PASS : SR_FAIL, busy_permille | (chk_blt_full ? 0x10000u : 0u));
        }
        break;
#if TAU_COLDFRAME_ON
    case CT_COLDFRAME:  /* B-199, PHASE_F_SPEC.md section 4.2: does calling a synthetic cold probe once per
                          * ui_draw_dynamic() call (~38 Hz, exactly the real "meters go cold" question) ever
                          * cost enough to cause a late underrun. coldframe_tick() (fw/cold.inc) already runs
                          * unconditionally at that rate whenever this macro is on; this test only arms the
                          * accumulator window and reads it back at close, the same shape as CT_BLT. */
        if (!chk_at) {
            if (!track_hz || paused || stopped) { chk_set(SR_NA, 0); break; }   /* needs real playback, like CT_AUD/CT_BLT */
            chk_u0 = pcm_under_n;
            coldframe_calls = 0u; coldframe_cyc_sum = 0u; coldframe_cyc_max = 0u; coldframe_cyc_min = 0xFFFFFFFFu;
            chk_at = (cycles() + CHK_STRESS_S * CLK_HZ) | 1u; chk_val[chk_step] = CHK_STRESS_S;
            return 0;
        }
        if ((int32_t)(cycles() - chk_at) < 0) { chk_val[chk_step] = (uint32_t)((int32_t)(chk_at - cycles()) / (int32_t)CLK_HZ + 1); return 0; }
        {
            uint32_t late = pcm_under_n - chk_u0;
            /* Worst single-call cost seen over the window, full 32 bits -- sr_test()'s value field
             * is a plain uint32 (fw/suite_core.h), not 16 bits; an earlier cut of this code capped
             * it to 0xFFFF for no real reason and saturated on the very first COLDFRAME_BIG run
             * (B-200 addendum: 65,535 exactly, the cap firing, not the real number). The
             * safety-relevant number is the worst single call (did any one spike badly), not the
             * average; min/avg stay in the plain globals for now (coldframe_cyc_min/_sum/_calls),
             * readable over JTAG or a future decoder extension if this experiment graduates past a
             * one-off measurement. */
            chk_set(late == 0u ? SR_PASS : SR_FAIL, coldframe_cyc_max);
        }
        break;
#endif
    case CT_SOAK:                                               /* the Diagnostics soak; ENDURANCE adds a cold-code test a minute */
        if (!chk_at) {
            stress_set_level(0u); stress_set_level(chk_lvl);
            if (!stress_on) { chk_set(SR_FAIL, 0xFFFFu); break; }
            dg_soak_start(chk_soak);                            /* keeps the level just set */
            if (dg_soak_res != 1u) { stress_set_level(0u); chk_set(SR_FAIL, 0xFFFFu); break; }
            CKM->cold_runs = 0u; CKM->cold_fails = 0u; CKM->cold_at = (cycles() + 60u * CLK_HZ) | 1u;
            chk_at = 1u; chk_val[chk_step] = dg_soak_left;
            return 0;
        }
        if (dg_soak_res == 1u) {
            if (chk_prof == 3u && COLD_READY() && (int32_t)(cycles() - CKM->cold_at) >= 0) { CKM->cold_at = (cycles() + 60u * CLK_HZ) | 1u; chk_cold_once(); }
            chk_val[chk_step] = dg_soak_left;
            return 0;
        }
        chk_set((dg_soak_res == 2u && !stress_und_late && !CKM->cold_fails) ? SR_PASS : SR_FAIL, stress_und_late);
        dg_soak_cancel(); stress_set_level(0u);
        break;
    case CT_TRK:                                                /* CHK_TRK_N track changes, next and previous alternating, each waited out */
        if (!chk_at) {
            {
                uint32_t nq = lib_src ? lib_qn : pl_count;        /* entries in the current queue or playlist */
                if (nq < 2u) { chk_set(SR_SKIP, nq); break; }     /* nothing to change to (value = entries) */
                CKM->tnq = (uint16_t)nq; CKM->tsrc = lib_src;     /* B-165: for a FAIL's diagnostic value, below */
            }
            CKM->ti = 0u; CKM->tph = 0u; CKM->tc0 = chk_loads; chk_val[chk_step] = 0u;
            skip_req = 1u; CKM->tnext = (cycles() + 60u * CLK_HZ) | 1u; chk_at = 1u;
            return 0;
        }
        if (CKM->tph == 0u) {
            if (chk_loads != (uint16_t)CKM->tc0) {              /* a load finished */
                CKM->ti++; chk_val[chk_step] = CKM->ti;
                if (CKM->ti >= CHK_TRK_N) { chk_set(SR_PASS, CKM->ti); break; }
                CKM->tph = 1u; CKM->tsettle = (cycles() + CLK_HZ / 2u) | 1u;
                return 0;
            }
            /* B-165: a queue of two or more, yet no load finished in 60 s -- stuck. Packs, so the
             * QR/decoder can say WHY instead of just "it failed": bits[7:0] changes done so far
             * (was the whole value before), bits[23:8] the queue length at entry (CKM->tnq),
             * bit 24 lib_src at entry, bit 25 whether skip_req is STILL pending right now -- set
             * means the main loop's own `if (skip_req)` consumer (player.c) never ran/cleared it,
             * cleared means it WAS consumed but no load ever completed from it (a different bug,
             * downstream of pl_skip()/lib_skip()). */
            if ((int32_t)(cycles() - CKM->tnext) >= 0) {
                uint32_t v = (uint32_t)CKM->ti | ((uint32_t)CKM->tnq << 8) | ((uint32_t)CKM->tsrc << 24)
                           | ((skip_req != 0u) ? (1u << 25) : 0u);
                chk_set(SR_FAIL, v);
                break;
            }
            return 0;
        }
        if ((int32_t)(cycles() - CKM->tsettle) < 0) return 0;
        CKM->tph = 0u; CKM->tc0 = chk_loads; skip_req = (CKM->ti & 1u) ? (uint32_t)-1 : 1u; CKM->tnext = (cycles() + 60u * CLK_HZ) | 1u;
        return 0;
    case CT_CX:                                                 /* the cold-code test 20 times */
        if (!COLD_READY()) { chk_set(SR_FAIL, 0xFFFFu); break; }
        CKM->cold_runs = 0u; CKM->cold_fails = 0u;
        for (uint32_t k = 0; k < 20u; k++) chk_cold_once();
        chk_set(CKM->cold_fails ? SR_FAIL : SR_PASS, CKM->cold_fails ? CKM->cold_fails : CKM->cold_runs);
        break;
#endif
    default: {                                                  /* CT_START: what the boot found */
        uint32_t bad = 0;
        if (cold_state != 1u || cold_err) bad++;
#if TAU_PL_SDRAM
        if (pl_sdram_state == 2u) bad++;
#endif
        chk_set(bad ? SR_FAIL : SR_PASS, ld_total);
        break; }
    }
    return 1;
}

/* B-204 (PHASE_F_SPEC.md section 4.1): the whole 16 KB stack region is painted with a sentinel at
 * boot (fw/start.S, before anything -- even the bss-clear loop -- can push a single byte onto it).
 * Scanning from the low end (the deepest possible point) upward for the first word that no longer
 * matches finds the high-water mark: everything below that point has never been touched by any call
 * depth reached so far. This function's own tiny frame is itself part of "how deep has execution
 * gone" at the moment it runs, same as any other call -- it cannot under-report, only ever read the
 * true historical peak or (if called at a new deepest point) extend it by its own negligible depth. */
extern char _stack_top, _stack_size;
#define STACK_SENTINEL 0xDEADC0DEu
COLD_TEXT static uint32_t stack_high_water(void)
{
    uint32_t total = (uint32_t)(uintptr_t)&_stack_size;
    volatile uint32_t *low = (volatile uint32_t *)((char *)&_stack_top - total);
    uint32_t n = total / 4u, i = 0;
    while (i < n && low[i] == STACK_SENTINEL) i++;
    return total - i * 4u;
}

/* Build the report: record, QR text, persisted words, short code. */
COLD_TEXT static void chk_finish(void)
{
    sr_t s;
    uint32_t fails = 0, doneall = 1;
    sr_init(&s, CHK_REC, CHK_REC_CAP, chk_prof == 0u ? 1u : chk_prof == 1u ? 3u : chk_prof == 2u ? 4u : 5u);
    uint32_t flags = (uint32_t)(TAU_LIBRARY ? 1u : 0u) | 2u | 4u | (uint32_t)(TAU_DIAG_TESTS ? 8u : 0u) | (uint32_t)(TAU_PL_SDRAM ? 16u : 0u)
                   | (uint32_t)(TAU_ART_PSRAM ? 32u : 0u);
    uint32_t fwv = ((uint32_t)(APP_VER[0] - '0') << 16) | ((uint32_t)(APP_VER[2] - '0') << 8) | (uint32_t)(APP_VER[4] - '0');
    uint32_t build[4] = { fwv, REG(R_VERSION), flags, (uint32_t)(&_heap_end - &_heap_start) };
    sr_vals(&s, SR_T_BUILD, 4, build, 4);
    uint32_t stackv[2] = { stack_high_water(), (uint32_t)(uintptr_t)&_stack_size };
    sr_vals(&s, SR_T_STACK, 4, stackv, 2);
    for (uint32_t i = 0; i < chk_n; i++) {
        sr_test(&s, chk_id(i), chk_res[i], chk_val[i]);
        if (chk_res[i] == SR_FAIL) fails |= 1u << i;
        if (chk_res[i] == SR_SKIP) doneall = 0;
    }
    uint32_t v[6];
    for (uint32_t i = 0; i < 6u; i++) v[i] = chk_sd[i];
    sr_vals(&s, SR_T_SDRAM, 2, v, 6);
    for (uint32_t i = 0; i < 6u; i++) v[i] = chk_ps[i];
    sr_vals(&s, SR_T_PSRAM, 2, v, 6);
    uint32_t cold[4] = { cold_err, cold_ms, CHK_DEV ? CKM->cold_runs : 0u, CHK_DEV ? CKM->cold_fails : 0u };   /* err, ms, soak/repeat runs, fails */
    sr_vals(&s, SR_T_COLD, 2, cold, 4);
#if CHK_DEV
    if (chk_prof >= 2u) { uint8_t st[2] = { set_soak_min[chk_soak], chk_lvl }; sr_tlv(&s, SR_T_SET, st, 2); }   /* soak minutes, stress level */
#endif
    uint32_t tm[4] = { ld_head, ld_size, ld_art, ld_total };
    sr_vals(&s, SR_T_TIME, 4, tm, 4);
    /* word[1] used to be a fixed 0; B-139 repurposes it: 1 if CT_AUD's window ran with playback
     * continuously active start to finish, 0 if it never ran (chk_aud_full stays at its 0 reset
     * value) or dropped out partway through. */
    uint32_t au[4] = { chk_under, chk_aud_full, chk_stall, CHK_AUDIO_S };
    sr_vals(&s, SR_T_AUDIO, 2, au, 4);
#if MP3_PROFILE || FLAC_PROFILE
    { uint32_t dp[4] = { chk_dec_h, chk_dec_i, chk_dec_s, chk_dec_r };
      sr_vals(&s, SR_T_DECPROF, 2, dp, 4); }
#endif
    uint32_t lb[4] = { lib_state == LIB_ST_OK ? lib.n_tracks : 0u, lib_ms, (uint32_t)lib_err | ((uint32_t)lib_state << 8), lib_disabled };
    sr_vals(&s, SR_T_LIB, 4, lb, 4);
    uint8_t er[3] = { lib_err, cold_err, (uint8_t)pl_sdram_state };
    sr_tlv(&s, SR_T_ERR, er, 3);
    uint32_t len = sr_finish(&s);
    chk_qr_len = 0;
    if (len && sr_text(CHK_REC, len, CHK_TXT, CHK_TXT_CAP)) {
        uint32_t txtlen = 0; while (CHK_TXT[txtlen]) txtlen++;
        uint32_t ver = 0, mask = 0;
        /* fixed mask: the automatic choice scores eight masks by reading the uncached PSRAM buffer, about half a second of
         * blocked main loop (an audible gap); any mask decodes */
        uint32_t sz = qr_encode((const uint8_t *)CHK_TXT, txtlen, 1u, CHK_QR_MAXV, 0, CHK_QR, &ver, &mask);
        if (sz) { chk_qr_len = sz; chk_qr_ver = ver; }
    }
    sp_t p;
    p.profile = chk_prof == 0u ? 1u : chk_prof == 1u ? 3u : chk_prof == 2u ? 4u : 5u; p.run = (chk_rec_on ? ((chk_w[0] >> 7) & 255u) : 0u) + 1u; chk_run = (uint8_t)p.run;
    p.verdict = fails ? 2u : (doneall ? 1u : 3u);
    p.pass = 0; p.fail = 0;
    for (uint32_t i = 0; i < chk_n; i++) { if (chk_res[i] == SR_PASS) p.pass |= 1u << chk_id(i); if (chk_res[i] == SR_FAIL) p.fail |= 1u << chk_id(i); }
    p.worst = chk_sd[2] > chk_sd[5] ? chk_sd[2] : chk_sd[5]; p.cold10 = 0; p.late = chk_under; p.stall = chk_stall;
    p.load100 = ld_total / 100u; p.lib_err = lib_err; p.cold_err = cold_err; p.fw_minor = (uint32_t)(APP_VER[2] - '0');
    sp_pack(&p, chk_w);
    chk_rec_on = 1u;
    for (uint32_t i = 0; i < 4u; i++) set_wr(SW_PLN0 + i, chk_w[i]);
    settings_mark_dirty();
}

/* ---- drawing (cold): intro / progress, summary, QR ---- */
COLD_TEXT static void chk_row(uint32_t i, uint32_t rh, const char *label, const char *val)
{
    uint32_t y = PL_UI_LIST_Y + i * rh;
    fb_rect(PL_UI_X + 4u, y - 2u, PL_UI_W - 8u, rh, UI_PANEL);
    fb_set_color(UI_DIM, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, y, label, TS_1X, TS_1X, 170u);
    uint32_t w = fb_text_width(val, TS_1X);
    fb_set_color(UI_WHITE, UI_PANEL);
    fb_text_clipped(PL_UI_X + PL_UI_W - 16u - w, y, val, TS_1X, TS_1X, w + 2u);
}

COLD_TEXT static void chk_draw_qr(void)
{
    const uint32_t sz = chk_qr_len;
    uint32_t m = 354u / (sz + 8u);                                  /* module size in pixels: 4 down to 2 (version 38) */
    if (m > 4u) m = 4u;
    const uint32_t side = (sz + 8u) * m, x0 = (FB_W - side) / 2u;
    fb_rect(0u, 0u, FB_W, FB_H, UI_BG);
    fb_rect(x0, 0u, side, side, 0xFFFFu);                       /* quiet zone (4 modules) is part of the white square */
    for (uint32_t y = 0; y < sz; y++) {
        uint32_t x = 0;
        while (x < sz) {
            if (!qr_get(CHK_QR, sz, (int)x, (int)y)) { x++; continue; }
            uint32_t e = x + 1u;
            while (e < sz && qr_get(CHK_QR, sz, (int)e, (int)y)) e++;
            fb_rect(x0 + (x + 4u) * m, (y + 4u) * m, (e - x) * m, m, 0x0000u);
            x = e;
        }
    }
    if (side + 44u > FB_H) return;                                  /* no room for the caption: the code fills the screen */
    char t[40], *q = t;
    q = set_str(q, chk_s(CS_VER)); q = ui_dec(q, chk_qr_ver); q = set_str(q, chk_s(CS_RUNL)); q = ui_dec(q, chk_run); *q = 0;
    fb_set_color(UI_DIM, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, side + 6u, t, TS_1X, TS_1X, PL_UI_W - 32u);
    fb_set_color(UI_FAINT, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, FB_H - 22u, chk_s(CS_SHOT), TS_1X, TS_1X, PL_UI_W - 32u);
}

/* One result row (also repainted alone once a second while a timed step counts down, so a run does not redraw the whole page:
 * a full redraw of a long profile costs more CPU time than the audio FIFO holds). */
COLD_TEXT static void chk_line(uint32_t i)
{
    char v[24], *q = v;
    uint32_t id = chk_id(i);
    if (chk_state == CK_RUN && i > chk_step) q = set_str(q, "");
    else if (chk_state == CK_RUN && i == chk_step) {
        if (chk_at && id == CT_SOAK) { q = ui_mmss(q, chk_val[i]); }
        else if (chk_at && id == CT_TRK) { q = ui_dec(q, chk_val[i]); q = set_str(q, chk_s(CS_TENTH)); }
        else if (chk_at) { q = ui_dec(q, chk_val[i]); q = set_str(q, chk_s(CS_S)); }
        else q = set_str(q, chk_s(CS_DOTS));
    }
    else {
        q = set_str(q, chk_s(CS_PASS + (chk_res[i] & 3u)));
        /* B-139: a PASS/FAIL on CT_AUD/CT_BLT only means something if audio was actually running
         * the whole window -- mark the row when it wasn't, right where the verdict is, rather than
         * only in the QR/decoded report (a screenshot alone should be enough to catch this). */
        if ((chk_res[i] == SR_PASS || chk_res[i] == SR_FAIL) &&
            ((id == CT_AUD && !chk_aud_full) || (id == CT_BLT && !chk_blt_full)))
            *q++ = '*';
    }
    *q = 0;
    chk_row(i, chk_n > 12u ? 20u : PL_UI_ROW_H, chk_s(id < CHK_BASE ? id : CS_X0 + id - CHK_BASE), v);
}

COLD_TEXT static void chk_draw(void)
{
    if (chk_state == CK_QR && chk_qr_len) { chk_draw_qr(); return; }
    uint32_t f = 0, sk = 0;
    char head[32], *hq = head;
    *hq = 0;
    if (chk_state == CK_DONE) {
        for (uint32_t i = 0; i < chk_n; i++) { f += chk_res[i] == SR_FAIL; sk += chk_res[i] == SR_SKIP; }
        if (chk_n > CHK_BASE) {                                 /* long profiles: the verdict goes in the title bar, the rows need the room */
            if (f) { hq = ui_dec(hq, f); hq = set_str(hq, chk_s(f == 1u ? CS_ONEF : CS_MANYF)); }
            else hq = set_str(hq, chk_s(sk ? CS_OKP : CS_OKALL));
            *hq = 0;
        }
    }
    ov_frame(chk_s(CS_TITLE), chk_state == CK_RUN ? chk_s(CS_RUNNING) : head,
             chk_s(chk_state == CK_IDLE ? (CHK_DEV ? CS_H_DEV : CS_H_IDLE) : chk_state == CK_RUN ? CS_H_RUN : CS_H_DONE));
    if (chk_state == CK_IDLE) {
        fb_set_color(UI_WHITE, UI_PANEL);
        for (uint32_t i = 0; i < 9u; i++) fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + i * PL_UI_ROW_H, chk_s(CS_INTRO + i), TS_1X, TS_1X, PL_UI_W - 32u);
#if CHK_DEV
        for (uint32_t r = 0; r < 3u; r++) {                     /* Up/Down pick the row, Left/Right change it */
            char ln[48], *q = ln;
            if (r == 0u) { q = set_str(q, chk_s(CS_PROFL)); q = set_str(q, chk_s(CS_PROF + chk_prof)); }
            else if (r == 1u) { q = set_str(q, chk_s(CS_SOAKL)); q = set_str(q, set_soak_nm[chk_soak]); }
            else { q = set_str(q, chk_s(CS_LVLL)); q = ui_dec(q, chk_lvl); }
            *q = 0;
            fb_set_color(r == chk_sel ? ui_accent : UI_DIM, UI_PANEL);
            fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + (9u + r) * PL_UI_ROW_H, ln, TS_1X, TS_1X, PL_UI_W - 32u);
        }
#endif
        return;
    }
    for (uint32_t i = 0; i < chk_n; i++) chk_line(i);
    if (chk_state == CK_DONE && chk_n == CHK_BASE) {
        char v[40], *q = v;
        if (f) { q = ui_dec(q, f); q = set_str(q, chk_s(f == 1u ? CS_ONEF : CS_MANYF)); }
        else q = set_str(q, chk_s(sk ? CS_OKP : CS_OKALL));
        *q = 0;
        fb_set_color(f ? UI_WHITE : ui_accent, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 8u * PL_UI_ROW_H, v, TS_1X, TS_1X, PL_UI_W - 32u);
        char sc[40];
        sp_short(chk_w, REG(R_VERSION), sc);
        char cl[2][21];                                         /* the short code in groups of six, three groups a line */
        for (uint32_t l = 0; l < 2u; l++) {
            char *p = cl[l];
            for (uint32_t g = 0; g < 3u; g++) {
                for (uint32_t k = 0; k < 6u; k++) *p++ = sc[(l * 3u + g) * 6u + k];
                if (g < 2u) *p++ = ' ';
            }
            *p = 0;
        }
        fb_set_color(UI_DIM, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 9u * PL_UI_ROW_H + 4u, cl[0], TS_1X, TS_1X, PL_UI_W - 32u);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 10u * PL_UI_ROW_H + 4u, cl[1], TS_1X, TS_1X, PL_UI_W - 32u);
    }
}

/* Hot glue: the main loop calls chk_tick(); the work is cold. */
COLD_TEXT static void chk_advance(void)
{
    if (!COLD_READY()) { chk_state = CK_IDLE; set_dirty = 1u; return; }
    if (!chk_do(chk_id(chk_step))) {
        if (chk_at) {                                            /* waiting: repaint the countdown once a second */
            if ((int32_t)(cycles() - CKM->tlast) >= 0) {
                CKM->tlast = cycles() + CLK_HZ;
                uint8_t o = ov_draw; ov_draw = 1u; chk_line(chk_step); ov_draw = o;     /* just the running row */
            }
        } else set_dirty = 1u;
        return;
    }
    chk_at = 0u;
    if (++chk_step >= chk_n) { chk_finish(); chk_state = CK_DONE; }
    set_dirty = 1u;
}
COLD_TEXT static void chk_begin(void)
{
#if CHK_DEV
    chk_n = (uint8_t)(CHK_BASE + chk_exn[chk_prof]);
#endif
    chk_state = CK_RUN; chk_step = 0u; chk_at = 0u;
    for (uint32_t i = 0; i < CHK_MAX; i++) chk_res[i] = SR_NA;
    chk_aud_full = chk_blt_full = 0u;   /* B-139: a SKIPped CT_AUD/CT_BLT must not report a stale value left over from a previous run */
    set_dirty = 1u;
}
COLD_TEXT static void chk_abort(void)                           /* B, or the page was left: stop what the run started */
{
#if CHK_DEV
    stress_set_level(0u); dg_soak_cancel();
#endif
    chk_state = CK_IDLE; chk_at = 0u; set_dirty = 1u;
}
static void chk_open(void) { chk_state = CK_IDLE; }
static void chk_tick(void)
{
    if (chk_state != CK_RUN) return;
    if (set_open && set_page == SET_CHECK_PG) chk_advance(); else chk_abort();
}
/* Returns 1 when the page consumed the key. */
COLD_TEXT static int chk_input(uint32_t edge)
{
    if (edge & KEY_START) {                                     /* Start closes the menu, except on the result pages: it is half of */
        if (chk_state == CK_IDLE || chk_state == CK_RUN) set_close();   /* the Menu+Start screenshot combination */
        return 1;
    }
    if (chk_state == CK_IDLE) {
        if (edge & KEY_A) { if (COLD_READY()) chk_begin(); }
#if CHK_DEV
        else if (edge & (KEY_UP | KEY_DOWN)) { chk_sel = (uint8_t)((chk_sel + ((edge & KEY_DOWN) ? 1u : 2u)) % 3u); set_dirty = 1u; }
        else if (edge & (KEY_LEFT | KEY_RIGHT)) {
            uint32_t up = (edge & KEY_RIGHT) ? 1u : 0u;
            if (chk_sel == 0u) { chk_prof = (uint8_t)((chk_prof + (up ? 1u : 3u)) % 4u); chk_soak = chk_prof == 3u ? 3u : 1u; }   /* ENDURANCE defaults to 30 min */
            else if (chk_sel == 1u) chk_soak = (uint8_t)(1u + (chk_soak - 1u + (up ? 1u : 3u)) % 4u);
            else chk_lvl = (uint8_t)(1u + (chk_lvl - 1u + (up ? 1u : 2u)) % 3u);
            set_dirty = 1u;
        }
#endif
        else if (edge & (KEY_B | (CHK_DEV ? 0u : KEY_LEFT))) { set_page = CHK_PARENT; set_dirty = 1u; }
    } else if (chk_state == CK_RUN) {
        if (edge & KEY_B) chk_abort();
    } else if (chk_state == CK_DONE) {
        if ((edge & (KEY_A | KEY_RIGHT)) && chk_qr_len) { chk_state = CK_QR; set_dirty = 1u; }
        else if (edge & KEY_Y) chk_begin();
        else if (edge & (KEY_B | KEY_LEFT)) { chk_state = CK_IDLE; set_page = CHK_PARENT; set_dirty = 1u; }
    } else {
        if (edge & (KEY_A | KEY_B | KEY_LEFT | KEY_RIGHT)) { chk_state = CK_DONE; set_dirty = 1u; }
    }
    return 1;
}

#if MP3_PROFILE || FLAC_PROFILE
/* ============================================================ DECODE PROFILE SWEEP (B-090/B-091)
 * A batch companion to CT_AUD's single sample: sweeps the library's CURRENT
 * QUEUE (whatever album or playlist is open), measuring each track up to
 * SW_WIN_S seconds (capped by the track's own length), and emits ONE QR at
 * the end covering every track (docs/TEST_SUITE_SPEC.md section 12).
 *
 * Its own page, not a Check profile: a sweep has no pass/fail verdict, so
 * folding it into Check's framing would strain both. It shares CHK_QR/
 * CHK_REC/CHK_TXT (PSRAM scratch) with Check -- the two pages are never open
 * at once, so there is no real second user of that memory. */
enum { SW_IDLE = 0, SW_RUN, SW_DONE, SW_QR, SW_REFUSED };
/* B-096 (owner: a track_idx alone was not enough to identify which file a
 * reading came from -- confirmed the hard way, reading a real sweep off the
 * card, with no way to tell). Each entry now carries a truncated title
 * (SW_TITLE_MAX bytes, from track_title -- the same lib_meta_apply() global
 * B-095's album-name snapshot already relies on), so this is a capacity
 * trade, not a free addition: entry cost goes from 11 B (10 fixed + 1 TLV
 * length... byte was already counted) to up to 2 + 10 + SW_TITLE_MAX = 28 B.
 * Budget (docs/TEST_SUITE_SPEC.md section 12): 666 - 4 (header) - 20 (build)
 * - 4 (CRC) = 638 B / 28 B = 22 tracks worst case; kept at a round 20. */
#define SW_TITLE_MAX  16u
#define SW_MAX_TRACKS 20u
#define SW_WIN_S      15u   /* matches CHK_AUDIO_S */
static uint8_t  sw_state, sw_started, sw_qr_len, sw_qr_ver;
static uint16_t sw_idx, sw_n, sw_done_n, sw_skip_n;
static uint32_t sw_at, sw_win0, sw_wait0;
static sr_t     sw_rec;
static char     sw_album[40];   /* snapshot of track_album at sw_begin(), for the QR page */

/* Latches one track's decode-stage percentages against the ACTUAL elapsed
 * window (not always SW_WIN_S*CLK_HZ -- a track shorter than the cap ends
 * the window early) and appends a repeatable SR_T_DECSWEEP entry. */
COLD_TEXT static void sw_sample(uint32_t elapsed)
{
    uint32_t unit = elapsed / 100u; if (!unit) unit = 1u;
    uint16_t h = 0, i = 0, s = 0, r = 0;
    /* B-097: r_pct is res/(res+lpc), a ratio of FLAC's OWN two accumulators
     * to each other -- not normalised against elapsed time, so even a small
     * leak of leftover decode work (found reading a real sweep: MP3 tracks
     * immediately after a FLAC track showed a plausible-looking nonzero R,
     * never MP3-after-MP3) produces a believable-looking percentage rather
     * than an obviously-wrong one. The robust fix does not depend on finding
     * the leak: track_fmt already says, unambiguously, what format is
     * actually playing right now -- so a stage that cannot apply to it is
     * forced to 0 here, regardless of what its accumulator says. */
#if MP3_PROFILE
    if (track_fmt == FMT_MP3) {
        h = (uint16_t)(mp3_huff_total_cyc  / unit);
        i = (uint16_t)(mp3_imdct_total_cyc / unit);
        s = (uint16_t)(mp3_sub_total_cyc   / unit);
    }
#endif
#if FLAC_PROFILE
    if (track_fmt == FMT_FLAC) {
        uint32_t rt = flac_res_total_cyc + flac_lpc_total_cyc;
        r = (uint16_t)(rt ? (flac_res_total_cyc * 100u) / rt : 0u);
    }
#endif
    /* B-092: the active speed as a percent (100 = 1.00x), so a future kernel
     * change can be checked against "did this hold across speeds" from the
     * record alone, without assuming the B-087 finding (speed doesn't move
     * H/I/S/R because the resampler runs downstream of decode) still holds. */
    uint8_t speed_pct = (uint8_t)((uint32_t)speed_num[speed_idx] * 100u / speed_den[speed_idx]);
    uint8_t t[10u + SW_TITLE_MAX] = { (uint8_t)sw_idx, speed_pct, (uint8_t)h, (uint8_t)(h >> 8), (uint8_t)i, (uint8_t)(i >> 8),
                                       (uint8_t)s, (uint8_t)(s >> 8), (uint8_t)r, (uint8_t)(r >> 8) };
    /* track_title is the same lib_meta_apply() global B-095's sw_album
     * capture already relies on -- valid for THIS track because sw_advance()
     * only reaches sw_sample() after track_hz confirmed playback started,
     * which is well after the open (and lib_meta_apply()) completed. */
    uint32_t tn = 0;
    for (; tn < SW_TITLE_MAX && track_title[tn]; tn++) t[10u + tn] = (uint8_t)track_title[tn];
    sr_tlv(&sw_rec, SR_T_DECSWEEP, t, 10u + tn);
    sw_done_n++;
}

COLD_TEXT static void sw_finish(void)
{
    /* Leaving the last track playing under the result page is not what a
     * sweep run implies -- the measurement is over. Same three flags the
     * ordinary end-of-playlist stop uses. */
    if (!stopped) { stopped = 1u; paused |= 1u; stop_req = 1u; }
    uint32_t len = sr_finish(&sw_rec);
    sw_qr_len = 0;
    if (len && sr_text(CHK_REC, len, CHK_TXT, CHK_TXT_CAP)) {
        uint32_t txtlen = 0; while (CHK_TXT[txtlen]) txtlen++;
        uint32_t ver = 0, mask = 0;
        uint32_t sz = qr_encode((const uint8_t *)CHK_TXT, txtlen, 1u, CHK_QR_MAXV, 0, CHK_QR, &ver, &mask);
        if (sz) { sw_qr_len = sz; sw_qr_ver = ver; }
    }
}

/* One tick of one track: first phase waits for playback to actually START
 * (never count a load/silence gap as decode cost -- the same trap CT_AUD's
 * own chk_at gating avoids for a single track), then runs the window. A
 * track that refuses to open within 3 s is skipped, not retried forever. */
COLD_TEXT static void sw_advance(void)
{
    if (!COLD_READY()) { sw_state = SW_IDLE; set_dirty = 1u; return; }
    if (!sw_started) {
        if (!track_hz || paused || stopped) {
            if ((int32_t)(cycles() - sw_wait0) > (int32_t)(3u * CLK_HZ)) {
                sw_skip_n++;
                if (++sw_idx >= sw_n) { sw_finish(); sw_state = sw_qr_len ? SW_QR : SW_DONE; }
                else { lib_skip(+1); sw_wait0 = cycles(); }
                set_dirty = 1u;
            }
            return;
        }
#if MP3_PROFILE
        mp3_huff_total_cyc = mp3_imdct_total_cyc = mp3_sub_total_cyc = 0u;
#endif
#if FLAC_PROFILE
        flac_res_total_cyc = flac_lpc_total_cyc = 0u;
#endif
        sw_win0 = cycles(); sw_at = sw_win0 + SW_WIN_S * CLK_HZ; sw_started = 1u;
        set_dirty = 1u;
        return;
    }
    if (!track_hz || paused || stopped) sw_sample(cycles() - sw_win0);   /* ended before the cap */
    else if ((int32_t)(cycles() - sw_at) < 0) return;                    /* still running */
    else sw_sample(SW_WIN_S * CLK_HZ);                                   /* hit the cap */
    sw_started = 0u;
    if (++sw_idx >= sw_n) { sw_finish(); sw_state = sw_qr_len ? SW_QR : SW_DONE; }
    else { lib_skip(+1); sw_wait0 = cycles(); }
    set_dirty = 1u;
}

COLD_TEXT static void sw_begin(void)
{
    if (!lib_qn || lib_qn > SW_MAX_TRACKS) { sw_state = SW_REFUSED; set_dirty = 1u; return; }
    sw_n = lib_qn; sw_idx = 0u; sw_done_n = 0u; sw_skip_n = 0u;
    sr_init(&sw_rec, CHK_REC, CHK_REC_CAP, 6u);
    uint32_t fwv = ((uint32_t)(APP_VER[0] - '0') << 16) | ((uint32_t)(APP_VER[2] - '0') << 8) | (uint32_t)(APP_VER[4] - '0');
    uint32_t build[4] = { fwv, REG(R_VERSION), 0u, 0u };
    sr_vals(&sw_rec, SR_T_BUILD, 4, build, 4);
    lib_play_span(0u, +1, lib_qn);        /* deterministic start: track 0 of the queue */
    /* lib_meta_apply() already ran inside that call (it is part of the
     * synchronous open chain, before the art decode even) -- track_album is
     * valid right now. Snapshotted rather than read again at draw time: it
     * should not change track to track within one sweep, but B-092's own
     * "make it slightly clearer what is loading" note is exactly the kind of
     * assumption worth not re-deriving from a possibly-stale global later. */
    { uint32_t i = 0; for (; i < sizeof(sw_album) - 1u && track_album[i]; i++) sw_album[i] = track_album[i]; sw_album[i] = 0; }
    sw_started = 0u; sw_wait0 = cycles();
    sw_state = SW_RUN;
    set_dirty = 1u;
}
static void sw_open(void) { sw_state = SW_IDLE; }
static void sw_tick(void)
{
    if (sw_state != SW_RUN) return;
    if (set_open && set_page == SET_SWEEP_PG) sw_advance(); else sw_state = SW_IDLE;
}

COLD_TEXT static void sw_draw_qr(void)
{
    const uint32_t sz = sw_qr_len;
    uint32_t m = 354u / (sz + 8u);
    if (m > 4u) m = 4u;
    const uint32_t side = (sz + 8u) * m, x0 = (FB_W - side) / 2u;
    fb_rect(0u, 0u, FB_W, FB_H, UI_BG);
    fb_rect(x0, 0u, side, side, 0xFFFFu);
    for (uint32_t y = 0; y < sz; y++) {
        uint32_t x = 0;
        while (x < sz) {
            if (!qr_get(CHK_QR, sz, (int)x, (int)y)) { x++; continue; }
            uint32_t e = x + 1u;
            while (e < sz && qr_get(CHK_QR, sz, (int)e, (int)y)) e++;
            fb_rect(x0 + (x + 4u) * m, (y + 4u) * m, (e - x) * m, m, 0x0000u);
            x = e;
        }
    }
    if (side + 44u > FB_H) return;    /* the code fills the screen: no room for any caption */
    /* Owner: "ill always want a screenshot" -- so the QR page IS the result
     * page, not a second step reached from a DONE summary (sw_advance()/
     * sw_finish() jump straight here), and it carries enough on its own face
     * that the screenshot alone is the record: what completed, the source,
     * how many tracks. */
    char t[48], *q = t;
    q = set_str(q, "TEST COMPLETE  "); q = ui_dec(q, sw_done_n); q = set_str(q, " TRACKS");
    if (sw_skip_n) { q = set_str(q, "  "); q = ui_dec(q, sw_skip_n); q = set_str(q, " SKIPPED"); }
    *q = 0;
    fb_set_color(ui_accent, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, side + 6u, t, TS_1X, TS_1X, PL_UI_W - 32u);
    if (side + 6u + 2u * FB_CELL(TS_1X) + 20u <= FB_H) {
        fb_set_color(UI_DIM, UI_BG);
        fb_text_clipped(PL_UI_TEXT_X, side + 6u + FB_CELL(TS_1X), sw_album, TS_1X, TS_1X, PL_UI_W - 32u);
    }
    fb_set_color(UI_FAINT, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, FB_H - 22u, "MENU+START FOR A SCREENSHOT", TS_1X, TS_1X, PL_UI_W - 32u);
}

COLD_TEXT static void sw_draw(void)
{
    if (sw_state == SW_QR && sw_qr_len) { sw_draw_qr(); return; }
    ov_frame("DECODE SWEEP",
             sw_state == SW_RUN ? "RUNNING" : sw_state == SW_REFUSED ? "REFUSED" : sw_state == SW_DONE ? "DONE" : "",
             sw_state == SW_IDLE ? "A START   B BACK" : "B BACK");
    if (sw_state == SW_IDLE) {
        fb_set_color(UI_WHITE, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 0u * PL_UI_ROW_H, "Runs decode-cost sampling over", TS_1X, TS_1X, PL_UI_W - 32u);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 1u * PL_UI_ROW_H, "the currently open album or", TS_1X, TS_1X, PL_UI_W - 32u);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 2u * PL_UI_ROW_H, "playlist, up to 15s per track.", TS_1X, TS_1X, PL_UI_W - 32u);
        char ln[40], *q = ln;
        q = set_str(q, "Queue: "); q = ui_dec(q, lib_qn); q = set_str(q, " tracks"); *q = 0;
        fb_set_color(lib_qn && lib_qn <= SW_MAX_TRACKS ? UI_DIM : UI_RED, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 4u * PL_UI_ROW_H, ln, TS_1X, TS_1X, PL_UI_W - 32u);
        fb_set_color(UI_FAINT, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 5u * PL_UI_ROW_H, "A START   B BACK", TS_1X, TS_1X, PL_UI_W - 32u);
        return;
    }
    if (sw_state == SW_REFUSED) {
        fb_set_color(UI_RED, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 0u * PL_UI_ROW_H, lib_qn ? "TOO MANY TRACKS (48 MAX)" : "NOTHING QUEUED", TS_1X, TS_1X, PL_UI_W - 32u);
        fb_set_color(UI_FAINT, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 2u * PL_UI_ROW_H, "B BACK", TS_1X, TS_1X, PL_UI_W - 32u);
        return;
    }
    char ln[40], *q = ln;
    q = set_str(q, "Track "); q = ui_dec(q, sw_idx + (sw_state == SW_DONE ? 0u : 1u)); q = set_str(q, " / "); q = ui_dec(q, sw_n); *q = 0;
    fb_set_color(UI_WHITE, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 0u * PL_UI_ROW_H, ln, TS_1X, TS_1X, PL_UI_W - 32u);
    /* B-094: which album/queue is actually loaded, not just a bare count --
     * row 2, clear of RUN's row-1 status and DONE's row-3 action hint in
     * both states. sw_album is the same snapshot the QR page (B-095) uses. */
    fb_set_color(UI_FAINT, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 2u * PL_UI_ROW_H, sw_album, TS_1X, TS_1X, PL_UI_W - 32u);
    if (sw_state == SW_RUN) {
        fb_set_color(UI_DIM, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 1u * PL_UI_ROW_H, sw_started ? "sampling..." : "waiting for playback...", TS_1X, TS_1X, PL_UI_W - 32u);
    }
    if (sw_state == SW_DONE) {
        char dn[40], *dq = dn;
        dq = set_str(dq, "sampled "); dq = ui_dec(dq, sw_done_n);
        if (sw_skip_n) { dq = set_str(dq, ", skipped "); dq = ui_dec(dq, sw_skip_n); }
        *dq = 0;
        fb_set_color(sw_qr_len ? ui_accent : UI_RED, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 1u * PL_UI_ROW_H, dn, TS_1X, TS_1X, PL_UI_W - 32u);
        fb_set_color(UI_FAINT, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 3u * PL_UI_ROW_H, sw_qr_len ? "A QR   B BACK" : "B BACK", TS_1X, TS_1X, PL_UI_W - 32u);
    }
}

COLD_TEXT static int sw_input(uint32_t edge)
{
    if (edge & KEY_START) { if (sw_state != SW_RUN) set_close(); return 1; }
    if (sw_state == SW_IDLE) {
        if (edge & KEY_A) sw_begin();
        else if (edge & (KEY_B | KEY_LEFT)) { set_page = CHK_PARENT; set_dirty = 1u; }
    } else if (sw_state == SW_REFUSED) {
        if (edge & (KEY_B | KEY_LEFT | KEY_A)) { sw_state = SW_IDLE; set_page = CHK_PARENT; set_dirty = 1u; }
    } else if (sw_state == SW_RUN) {
        if (edge & KEY_B) sw_state = SW_IDLE;   /* abort */
    } else if (sw_state == SW_DONE) {
        if ((edge & (KEY_A | KEY_RIGHT)) && sw_qr_len) { sw_state = SW_QR; set_dirty = 1u; }
        else if (edge & (KEY_B | KEY_LEFT)) { sw_state = SW_IDLE; set_page = CHK_PARENT; set_dirty = 1u; }
    } else {   /* SW_QR */
        if (edge & (KEY_A | KEY_B | KEY_LEFT | KEY_RIGHT)) { sw_state = SW_DONE; set_dirty = 1u; }
    }
    return 1;
}
#endif

/* ================================================================== BLIT TEST (B-166)
 * Exercises every draw-engine opcode individually, full-screen, at increasing concurrency,
 * without needing audio -- deliberately separate from Check (which assumes playback context) and
 * from Decode Sweep (decoder-cost, not draw-engine). Opcode correctness is already covered by
 * B-125's reference-renderer/mutation tests; this measures real hardware throughput/coverage and
 * is, for OP_RUN/OP_BLIT/OP_BAR/OP_SBLIT, the FIRST operational use of those opcodes from firmware
 * at all (fb_rect always emits OP_RECT even for a 1-row fill; B-112's audit found nothing had ever
 * called OP_BLIT/OP_BAR/OP_SBLIT operationally before fb_cblit()/CT_BLT existed, and neither of
 * those covers RUN/BAR/SBLIT).
 *
 * B-162's lesson applied directly, twice over: never runs at boot (menu-driven only, same as
 * Decode Sweep), and the HUD line is drawn BEFORE each tick's risky draw command, not after -- so
 * a real hang leaves the screen showing exactly which op/level/cell it was attempting, per the
 * owner's own request. BLIT_READY() gates entry the same way set_draw_thumb() already does: an old
 * bitstream without the blit engine gets a clear refusal, not a hang on OP_RUN's truncated
 * fallback (OP_RUN is opcode 0, so an old 2-bit decode reads it as itself -- the one opcode B-103's
 * fail-safe does NOT catch, which is exactly why the explicit BLIT_READY() check matters here). */
#define BT_OPS      8u          /* RUN RECT CHAR COPY BLIT BAR SBLIT CBLIT, mp3_fb.sv's own OP_* order */
#define BT_LEVELS   3u
#define BT_WIN_S    2u          /* per (op, level) window */
#define BT_CELL     16u         /* test-cell size, tiled across the full 400x360 visible screen */
#define BT_COLS     (FB_W / BT_CELL)
#define BT_ROWS     (FB_H / BT_CELL)
enum { BT_IDLE = 0, BT_RUN, BT_REFUSED, BT_DONE, BT_QR };
static const uint8_t bt_ops_per_tick[BT_LEVELS] = { 1u, 4u, 16u };   /* concurrency: commands issued per bt_advance() call */
static const char *const bt_op_name[BT_OPS] = { "RUN", "RECT", "CHAR", "COPY", "BLIT", "BAR", "SBLIT", "CBLIT" };
static uint8_t  bt_state, bt_op, bt_lvl;
static uint32_t bt_at, bt_win0, bt_stall0, bt_ops_done, bt_cx, bt_cy, bt_last_stall_pct;
static sr_t     bt_rec;
static uint8_t  bt_qr_len, bt_qr_ver;

/* One cell's worth of one opcode, at the current (bt_cx, bt_cy) grid position, then advances the
 * grid (row-major, wrapping). Source data for COPY/BLIT/SBLIT comes from the off-screen art stash
 * (ART_STASH_Y, never displayed past FB_H -- always holds valid SDRAM content, its actual pixels
 * do not matter here); CBLIT reuses the meter-thumbnail flat buffers (THUMB_STASH_Y) and CLUT
 * already built for B8, loaded once in bt_begin(), not per cell. */
COLD_FN static void bt_draw_cell(void)
{
    uint32_t x = bt_cx * BT_CELL, y = bt_cy * BT_CELL;
    uint16_t c1 = (uint16_t)(0x1F * (bt_cx & 1u) | (0x3F * (bt_cy & 1u)) << 5 | (0x1F * ((bt_cx + bt_cy) & 1u)) << 11);
    uint16_t c2 = (uint16_t)~c1;
    switch (bt_op) {
    case 0: fb_run(x, y, BT_CELL, c1); break;
    case 1: fb_rect(x, y, BT_CELL, BT_CELL, c1); break;
    case 2: fb_set_color(c1, c2); fb_char(x, y, (char)('A' + (bt_cx + bt_cy) % 26), 1u, 1u); break;
    case 3: fb_copy(0u, ART_STASH_Y, x, y, BT_CELL, BT_CELL); break;
    case 4: fb_blit(0u, ART_STASH_Y, x, y, BT_CELL, BT_CELL); break;
    case 5: fb_bar(x, y, BT_CELL, BT_CELL, (bt_cx * 3u) % (BT_CELL + 1u), c1, c2); break;
    case 6: fb_sblit(0u, ART_STASH_Y, x, y, BT_CELL, BT_CELL, 2u, 2u); break;   /* 2x from an 8x8 source */
    default: fb_cblit(0u, THUMB_STASH_Y, x, y, BT_CELL, BT_CELL); break;        /* one meter-thumb-sized corner */
    }
    if (++bt_cx >= BT_COLS) { bt_cx = 0u; if (++bt_cy >= BT_ROWS) bt_cy = 0u; }
    bt_ops_done++;
}

COLD_FN static void bt_hud(void)
{
    char b[48], *q = b;
    q = set_str(q, bt_op_name[bt_op]); q = set_str(q, " L"); q = ui_dec(q, bt_lvl + 1u);
    q = set_str(q, "  X"); q = ui_dec(q, bt_cx); q = set_str(q, " Y"); q = ui_dec(q, bt_cy);
    q = set_str(q, "  STALL"); q = ui_dec(q, bt_last_stall_pct); q = set_str(q, "%"); *q = 0;
    fb_set_color(UI_WHITE, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, FB_H - 18u, b, TS_1X, TS_1X, PL_UI_W - 32u);
}

/* B-174: bt_begin() runs BEFORE the HUD above ever draws -- its own sub-steps (the blit-engine
 * probe, the flat-thumbnail-buffer build, the CLUT load) are each a real chance to hang, and the
 * two hangs already seen on hardware (B-162, B-170) both happened somewhere in this exact
 * territory. Same "identify where we were" principle as the HUD, one level earlier, using ONLY
 * fb_rect()/fb_text_clipped() -- already proven safe everywhere else in the UI, including the
 * idle page bt_begin() is called from -- never the opcodes under test. Fixed-width clear first so
 * a shorter message never leaves a ghost of a longer one. Row 5 of the idle panel: rows 0-2 are
 * the description text, row 4 the A/B hint (see bt_draw()'s BT_IDLE branch), row 5 is unused. */
COLD_FN static void bt_checkpoint(const char *msg)
{
    fb_rect(PL_UI_TEXT_X, PL_UI_LIST_Y + 5u * PL_UI_ROW_H, PL_UI_W - 32u, FB_CELL(TS_1X), UI_PANEL);
    fb_set_color(UI_DIM, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 5u * PL_UI_ROW_H, msg, TS_1X, TS_1X, PL_UI_W - 32u);
}

/* B-176: a checkpoint that never touches the draw engine at all -- B-174's own draw-based
 * checkpoints showed NOTHING on the last run (not even the very first one), which rules out
 * blit_probe()/CBLIT specifically as the sole cause and raises a harder question: is even a plain,
 * everywhere-else-proven-safe fb_rect()/fb_text_clipped() call unsafe from THIS calling context
 * (bt_begin(), reached synchronously from the KEY_A input-handler edge, not from the normal
 * per-frame set_draw() dispatch every other page's drawing goes through -- no other "begin"
 * function in this codebase draws directly; checked chk_begin()/sw_begin(), neither does). This
 * decouples "record where we are" from the draw engine entirely: a raw CPU write to a fixed
 * off-screen SDRAM word (0xA0000000 + word_addr*2, the same uncached-window idiom blit_probe()
 * already uses for its own sentinel, but a different word -- column 482, clear of both
 * blit_probe()'s probe_addr=480 and its probe_row2=480+512=992), no fb_wait(), no R_FB_GO touch,
 * so it cannot hang even if the draw engine itself is what's stuck. Read back later from the
 * idle page (drawn through the ordinary, always-safe set_draw() dispatch, not synchronously) --
 * SDRAM content is expected to survive a forced Quit/reload (not explicitly re-initialised), so
 * the last value written before a hang should still be there on the next visit to this page.
 *
 * B-191/B-192 correction: the original base here, 0xA0000000, is NOT the CPU's mapped uncached
 * SDRAM window -- it is the first 1 MiB of SDRAM, reserved for the draw engine's own framebuffer
 * (tau_sdram_addr_decode.sv's own SDRAM_U_BASE is 0xA0100000, matching PL_SDRAM_BASE below, and
 * fb_cmd_addr is hard-capped at 19 bits -- max ~1 MiB -- so the draw engine can never even reach
 * past this boundary). A raw CPU load/store below 0xA0100000 hits no decoded range at all and the
 * Wishbone bus never gets an ACK, hanging forever -- this was the actual root cause of the whole
 * Blit Test hang investigated since B-166: this checkpoint mechanism, built specifically because
 * it "cannot hang even if the draw engine itself is what's stuck," was hanging on its own address.
 * Moved onto PL_SDRAM_BASE (a proven, working base -- playlist SDRAM, the Window Test's
 * DG_SCRATCH, and the SDRAM stress pump all already use it successfully) at a fixed offset well
 * clear of the playlist's own reserved bytes (kilobytes) and the stress pump's region (1-2 MiB
 * further in) -- 64 KiB in is comfortably isolated from both. */
#define BT_CRUMB_BASE (PL_SDRAM_BASE + 0x00010000u)
static void bt_crumb(uint32_t step)
{
    volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)BT_CRUMB_BASE;
    *p = (uint16_t)step;
    /* B-186: same checkpoint, also written to a plain MMIO register a JTAG ISSP probe can read
     * live regardless of whether the CPU itself is stuck -- the SDRAM crumb above needs the
     * firmware to reach the idle page's normal draw path to read it back, which a genuine CPU
     * hang never lets happen. This write is a no-op on any bitstream without TAU_ISSP built. */
    REG(R_DBG_MARK) = step;
}
static uint32_t bt_crumb_read(void)
{
    volatile uint16_t *p = (volatile uint16_t *)(uintptr_t)BT_CRUMB_BASE;
    return *p;
}

/* B-194: this whole function had zero bt_crumb() coverage -- if the hang is actually here (the
 * last (op, level) window's tick is exactly where bt_advance() first falls through past its usual
 * early return, so it's a strong suspect for a hang that only shows up after many ticks of normal
 * operation), the crumb would freeze at 15 forever with no way to tell finish from any of its three
 * real sub-steps. */
COLD_FN static void bt_finish(void)
{
    bt_crumb(30);   /* entered bt_finish() */
    uint32_t len = sr_finish(&bt_rec);
    bt_crumb(31);   /* sr_finish() returned */
    bt_qr_len = 0;
    if (len && sr_text(CHK_REC, len, CHK_TXT, CHK_TXT_CAP)) {
        bt_crumb(32);   /* sr_text() returned nonzero */
        uint32_t txtlen = 0; while (CHK_TXT[txtlen]) txtlen++;
        uint32_t ver = 0, mask = 0;
        uint32_t sz = qr_encode((const uint8_t *)CHK_TXT, txtlen, 1u, CHK_QR_MAXV, 0, CHK_QR, &ver, &mask);
        bt_crumb(33);   /* qr_encode() returned */
        if (sz) { bt_qr_len = sz; bt_qr_ver = ver; }
    }
    bt_crumb(34);   /* bt_finish() about to return */
}

/* Advances one (op, level) window at a time: draws the HUD (the last-known-good state, so a hang
 * in the cell(s) that follow still leaves an identifying line on screen), issues this level's
 * concurrency worth of cells, and on window expiry records one SR_T_BLITTEST entry and moves to
 * the next level or opcode. Reaching a window's expiry at all IS the pass condition -- there is no
 * timeout on the draw-engine primitives to recover from a genuine hang, so a FAIL entry is never
 * actually written; its absence (this op/level missing from the report, or the whole run never
 * reaching SR_finish) is itself the evidence, same as B-162's own diagnosis relied on a screenshot
 * of where the HUD stopped, not a reported failure. */
COLD_FN static void bt_advance(void)
{
    bt_crumb(12);   /* entered bt_advance() at least once */
    if (!bt_at) {   /* entering a new (op, level) window */
        bt_win0 = cycles(); bt_at = bt_win0 + BT_WIN_S * CLK_HZ; bt_stall0 = REG(R_FB_STALL);
        bt_ops_done = 0u; bt_cx = bt_cy = 0u;
    }
    bt_crumb(13);   /* about to draw the HUD (fb_text_clipped) */
    bt_hud();
    bt_crumb(14);   /* HUD drawn -- about to issue this tick's cell(s) */
    /* B-194: per-cell checkpoint (0x2000 | k) so a hang *inside* one of the bt_ops_per_tick[bt_lvl]
     * (1/4/16) calls this tick leaves the index of the last cell that fully returned, distinct from
     * crumb(14)/(15) which only bracket the whole loop. */
    for (uint32_t k = 0; k < bt_ops_per_tick[bt_lvl]; k++) { bt_draw_cell(); bt_crumb(0x2000u | k); }
    bt_crumb(15);   /* this tick's cell(s) all issued without hanging */
    if ((int32_t)(cycles() - bt_at) < 0) { bt_crumb(16); return; }   /* window not yet expired -- the
                                                                       * common case, most ticks return
                                                                       * here without reaching 17+ */
    bt_crumb(17);   /* window expired -- about to record this (op, level)'s result */
    uint32_t window_cyc = BT_WIN_S * CLK_HZ;
    uint32_t stall = REG(R_FB_STALL) - bt_stall0;
    bt_last_stall_pct = (stall > window_cyc) ? 100u : stall * 100u / window_cyc;
    uint8_t t[6] = { bt_op, bt_lvl, SR_PASS, (uint8_t)bt_last_stall_pct,
                     (uint8_t)bt_ops_done, (uint8_t)(bt_ops_done >> 8) };
    sr_tlv(&bt_rec, SR_T_BLITTEST, t, 6);
    bt_crumb(18);   /* result recorded -- about to advance level/op or finish */
    bt_at = 0u;
    if (++bt_lvl >= BT_LEVELS) {
        bt_lvl = 0u;
        if (++bt_op >= BT_OPS) {
            bt_crumb(19);   /* last op/level reached -- about to call bt_finish() */
            bt_finish();
            bt_crumb(20);   /* bt_finish() returned */
            bt_state = bt_qr_len ? BT_QR : BT_DONE;
            /* B-197 root cause: set_draw() (fw/player.c) only redraws `if (set_open && set_dirty)`
             * -- this finishing return never set it, so the whole test silently completes (QR
             * encoded, bt_finish() done) and the screen never once redraws to show BT_DONE/BT_QR.
             * It isn't hung: it's finished, frozen on the last running HUD frame forever, with zero
             * visual indication, which is exactly what B-194 observed. Every other transition in
             * this function (the plain "advance to next level/op" path just below) already sets
             * this; only the terminal one didn't. */
            set_dirty = 1u;
            return;
        }
    }
    set_dirty = 1u;
}

COLD_FN static void bt_begin(void)
{
    bt_crumb(1);   /* entered bt_begin() */
    bt_checkpoint("checkpoint: blit probe...");
    bt_crumb(2);   /* the checkpoint's own fb_rect()/fb_text_clipped() returned */
#if TAU_METER_THUMBS
    blit_probe_ensure();   /* B-168: must run BEFORE the BLIT_READY() check below, not after --
                             * it is what actually sets blit_ready. Checking first meant this
                             * refused unconditionally on a fresh boot (blit_ready defaults to 0
                             * until something runs the probe), regardless of real hardware
                             * support -- confirmed a pure ordering bug, not an RTL/hardware issue,
                             * since B-164 already proved OP_BLIT works on this exact bitstream. */
#endif
    bt_crumb(3);   /* blit_probe_ensure() returned */
    bt_checkpoint("checkpoint: blit probe OK");
    bt_crumb(4);
    if (!BLIT_READY()) { bt_state = BT_REFUSED; set_dirty = 1u; return; }
    bt_crumb(5);   /* BLIT_READY() passed */
    bt_op = bt_lvl = 0u; bt_at = 0u;
    sr_init(&bt_rec, CHK_REC, CHK_REC_CAP, 8u);
    uint32_t fwv = ((uint32_t)(APP_VER[0] - '0') << 16) | ((uint32_t)(APP_VER[2] - '0') << 8) | (uint32_t)(APP_VER[4] - '0');
    uint32_t build[4] = { fwv, REG(R_VERSION), 0u, 0u };
    sr_vals(&bt_rec, SR_T_BUILD, 4, build, 4);
    bt_crumb(6);
#if TAU_METER_THUMBS
    bt_checkpoint("checkpoint: thumb build...");
    bt_crumb(7);   /* about to call set_thumb_flat_build() (or skip it if already built) */
    if (!thumb_flat_ready) set_thumb_flat_build();
    bt_crumb(8);   /* set_thumb_flat_build() returned, or was skipped */
    bt_checkpoint("checkpoint: thumb build OK");
    bt_checkpoint("checkpoint: clut load...");
    bt_crumb(9);   /* about to call fb_clut_load() */
    fb_clut_load(meter_thumb_pal[0], 8u);   /* CBLIT's cell reads one corner of viz 0's thumbnail */
    bt_crumb(10);  /* fb_clut_load() returned */
    bt_checkpoint("checkpoint: clut load OK");
#endif
    bt_crumb(11);  /* about to set bt_state = BT_RUN, bt_begin() is otherwise done */
    bt_state = BT_RUN;
    set_dirty = 1u;
}
static void bt_open(void) { bt_state = BT_IDLE; }
/* B-196: this else branch silently drops bt_state to BT_IDLE with no crumb and no set_dirty -- if
 * this ever fires even once while BT_RUN, bt_advance() is never called again and the screen is
 * left frozen on whatever it last drew, forever, with nothing to distinguish it from a genuine
 * hang. Distinct crumbs for each half so a live JTAG read of DBGM can tell "this fired" (41/42)
 * from "bt_tick() itself is somehow not being called at all any more" (crumb stays wherever
 * bt_advance() last left it, e.g. 16). */
static void bt_tick(void)
{
    if (bt_state != BT_RUN) return;
    if (set_open && set_page == SET_BLITTEST_PG) bt_advance();
    else { bt_crumb(set_open ? 41u : 42u); bt_state = BT_IDLE; }
}

COLD_TEXT static void bt_draw_qr(void)
{
    const uint32_t sz = bt_qr_len;
    uint32_t m = 354u / (sz + 8u);
    if (m > 4u) m = 4u;
    const uint32_t side = (sz + 8u) * m, x0 = (FB_W - side) / 2u;
    fb_rect(0u, 0u, FB_W, FB_H, UI_BG);
    fb_rect(x0, 0u, side, side, 0xFFFFu);
    for (uint32_t y = 0; y < sz; y++) {
        uint32_t x = 0;
        while (x < sz) {
            if (!qr_get(CHK_QR, sz, (int)x, (int)y)) { x++; continue; }
            uint32_t e = x + 1u;
            while (e < sz && qr_get(CHK_QR, sz, (int)e, (int)y)) e++;
            fb_rect(x0 + (x + 4u) * m, (y + 4u) * m, (e - x) * m, m, 0x0000u);
            x = e;
        }
    }
    if (side + 44u > FB_H) return;
    char t[32], *q = t;
    q = set_str(q, "BLIT TEST DONE"); *q = 0;
    fb_set_color(ui_accent, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, side + 6u, t, TS_1X, TS_1X, PL_UI_W - 32u);
    fb_set_color(UI_FAINT, UI_BG);
    fb_text_clipped(PL_UI_TEXT_X, FB_H - 22u, "MENU+START FOR A SCREENSHOT", TS_1X, TS_1X, PL_UI_W - 32u);
}

COLD_TEXT static void bt_draw(void)
{
    if (bt_state == BT_QR && bt_qr_len) { bt_draw_qr(); return; }
    if (bt_state == BT_RUN) return;   /* the test itself owns the whole screen; the HUD is its only chrome */
    ov_frame("BLIT TEST",
             bt_state == BT_REFUSED ? "REFUSED" : bt_state == BT_DONE ? "DONE" : "",
             bt_state == BT_IDLE ? "A START   B BACK" : "B BACK");
    if (bt_state == BT_IDLE) {
        fb_set_color(UI_WHITE, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 0u * PL_UI_ROW_H, "Runs every draw-engine opcode,", TS_1X, TS_1X, PL_UI_W - 32u);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 1u * PL_UI_ROW_H, "full screen, at 3 concurrency", TS_1X, TS_1X, PL_UI_W - 32u);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 2u * PL_UI_ROW_H, "levels each -- no audio needed.", TS_1X, TS_1X, PL_UI_W - 32u);
        fb_set_color(UI_FAINT, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 4u * PL_UI_ROW_H, "A START   B BACK", TS_1X, TS_1X, PL_UI_W - 32u);
        /* B-176: the last run's furthest crumb, read back from raw SDRAM (never the draw engine) --
         * so a hang that leaves NOTHING drawn (as the last run did) still leaves this line correct
         * the next time this page is visited, no cable or screenshot-mid-hang needed. 0 = never run
         * since boot (a fresh crumb word, or one a different core's session left behind). */
        {
            uint32_t c = bt_crumb_read();
            char ln[32], *q = ln;
            q = set_str(q, "last run reached: "); q = ui_dec(q, c); *q = 0;
            fb_set_color(UI_DIM, UI_PANEL);
            fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 5u * PL_UI_ROW_H, ln, TS_1X, TS_1X, PL_UI_W - 32u);
        }
        return;
    }
    if (bt_state == BT_REFUSED) {
        fb_set_color(UI_RED, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 0u * PL_UI_ROW_H, "BLIT ENGINE NOT DETECTED", TS_1X, TS_1X, PL_UI_W - 32u);
        fb_set_color(UI_FAINT, UI_PANEL);
        fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 2u * PL_UI_ROW_H, "B BACK", TS_1X, TS_1X, PL_UI_W - 32u);
        return;
    }
    char ln[40], *q = ln;
    q = set_str(q, "op "); q = ui_dec(q, (uint32_t)bt_op * BT_LEVELS + bt_lvl); q = set_str(q, " / "); q = ui_dec(q, BT_OPS * BT_LEVELS); *q = 0;
    fb_set_color(UI_WHITE, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 0u * PL_UI_ROW_H, ln, TS_1X, TS_1X, PL_UI_W - 32u);
    fb_set_color(bt_qr_len ? ui_accent : UI_RED, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 1u * PL_UI_ROW_H, bt_qr_len ? "done" : "record did not fit", TS_1X, TS_1X, PL_UI_W - 32u);
    fb_set_color(UI_FAINT, UI_PANEL);
    fb_text_clipped(PL_UI_TEXT_X, PL_UI_LIST_Y + 3u * PL_UI_ROW_H, bt_qr_len ? "A QR   B BACK" : "B BACK", TS_1X, TS_1X, PL_UI_W - 32u);
}

COLD_TEXT static int bt_input(uint32_t edge)
{
    if (edge & KEY_START) { if (bt_state != BT_RUN) set_close(); return 1; }
    if (bt_state == BT_IDLE) {
        if (edge & KEY_A) bt_begin();
        else if (edge & (KEY_B | KEY_LEFT)) { set_page = CHK_PARENT; set_dirty = 1u; }
    } else if (bt_state == BT_REFUSED) {
        if (edge & (KEY_B | KEY_LEFT | KEY_A)) { bt_state = BT_IDLE; set_page = CHK_PARENT; set_dirty = 1u; }
    } else if (bt_state == BT_RUN) {
        if (edge & KEY_B) bt_state = BT_IDLE;   /* abort */
    } else if (bt_state == BT_DONE) {
        if ((edge & (KEY_A | KEY_RIGHT)) && bt_qr_len) { bt_state = BT_QR; set_dirty = 1u; }
        else if (edge & (KEY_B | KEY_LEFT)) { bt_state = BT_IDLE; set_page = CHK_PARENT; set_dirty = 1u; }
    } else {   /* BT_QR */
        if (edge & (KEY_A | KEY_B | KEY_LEFT | KEY_RIGHT)) { bt_state = BT_DONE; set_dirty = 1u; }
    }
    return 1;
}
#endif
