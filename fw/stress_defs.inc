/* SDRAM stress pump: constants and state (A-102).
 * Diagnostic Build only: included from player.c under #if TAU_DIAGNOSTIC (never compiled into `release`).
 * Moved out of that file unchanged (B-251); behaviour and generated code are identical. */
/* Developer-only contention pump. This is deliberately MMIO-only: it never
 * changes the player linker map or exposes mapped SDRAM. */
/* The playlist buffers live at physical 1 MiB (A-105); the pump must not touch them, so it
 * works above 2 MiB instead. (Word units of 16 bits, like the mailbox address.)
 * The helios H2 back buffer (buffer 1: mp3_fb.sv DBUF_BASE1, fw/player.c DBUF_BASE1_W) occupies words 0x100000 .. 0x12D000 (1,048,576 + 512 x 360).
 * The pump used to start at 0x100000 and overwrote it with its pattern: whenever buffer 1 was the DISPLAYED buffer during a Check with the stress test
 * the screen showed random colour noise (seen on a Pocket, 2026-10-04). It now starts above the buffer, and the assert below keeps it there. */
#define STRESS_BASE 0x00140000u
#define STRESS_LAST 0x0017FFFEu
_Static_assert(STRESS_BASE >= 1048576u + 512u * 360u, "the stress pump region must not overlap the H2 back buffer (buffer 1)");
_Static_assert(STRESS_BASE >= 0x100000u && STRESS_LAST < 0x00200000u, "the stress pump stays inside 2..4 MiB (clear of the playlist at 1 MiB and the 8 MiB scratch)");
#define STRESS_WORDS_PER_PASS (((STRESS_LAST - STRESS_BASE) / 2u) + 1u)
#define STRESS_GAP  (CLK_HZ / 16000u)
#define STRESS_TIMEOUT (CLK_HZ / 4u)
static uint8_t stress_on, stress_read;
static uint32_t stress_addr, stress_due, stress_started, stress_expect;
static uint32_t stress_words, stress_passes, stress_failures, stress_crc;
static uint32_t stress_under0, stress_fb0;
static uint8_t stress_level;                    /* 0 off, 1..3 = 16k/64k/128k ops/s */
static uint32_t stress_rd_max, stress_wr_max;   /* worst window access, cycles   */
static uint8_t stress_win_ok;                   /* preflight verified the window */
static uint32_t stress_frames_at_flush;          /* decoded-frame counter at the last pcm flush */
static uint32_t stress_und_early, stress_und_late; /* underruns <1 s / >=1 s after a flush */
static uint32_t stress_rate_words;              /* words at the last HUD tick       */
static uint32_t stress_rate;                    /* achieved ops per second          */
/* An underrun is EARLY if fewer than 9 frames (about 0.2 s of audio) were decoded since
 * the last flush (track start, seek, resume, including long cover decodes before the first
 * frame), LATE otherwise. `frames` is reset to 0 at track load, hence the wrap guard. */
static inline void stress_note_underrun(void)
{
    uint32_t d = (frames >= stress_frames_at_flush) ? frames - stress_frames_at_flush : frames;
    if (d < 9u) stress_und_early++; else stress_und_late++;
}
/* R_CYCLES is a 32-bit 60 MHz counter: it wraps every 71.58 s.  Keep a
 * small wrap-safe stopwatch by consuming deltas every main-loop iteration;
 * do not retain a raw start/end difference as a multi-minute pass duration. */
static uint32_t stress_clock_prev, stress_pass_secs, stress_pass_rem;
static uint32_t stress_last_secs, stress_last_pass;
static uint32_t stress_hud_tick = 0xFFFFFFFFu;
static const char *stress_fault;
