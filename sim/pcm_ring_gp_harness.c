/* B-750: host harness for the gapless boundary control flow of fw/pcm_ring_fw.inc (gp_eof_begin / gp_begin_load / gp_end_load / gp_handoff / gp_tick / gp_abort and the flush bookkeeping in
 * fw/pcm_ring_state.h), the REAL code, compiled against stubs of everything the player provides. The scenarios drive the sequence the player runs around a natural track end and check what must
 * and must not happen: the screen stays on the old track until the marker, the flush happens only on the fallback paths, the marker follows the heard count, a flush brings it forward. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TAU_PCM_RING_FW 1
#define COLD_FN2
#define COLD_SR
#define TITLE_MAX 80
#define FMT_FLAC 2
#define FMT_MP3 1
#define CLK_HZ 66666667u
#define PCM_LEVEL(s)  ((s) & 0xFFFu)
#define PCM_EMPTY(s)  (((s) >> 16) & 1u)
#define LD_WAIT(d) ((void)(d))
static uint32_t regs[1024];
#define REG(a) (regs[(a) & 1023u])
#define R_PCM_ST    0x80000034u
#define R_STAT0     0x80000040u
#define GP_SHADOW_ATTR
typedef struct { int32_t track_cdb, album_cdb; uint8_t have; uint32_t track_peak, album_peak; } rg_t;
typedef struct { int32_t cur, target; uint8_t hw; } pcm_vol_t_;
static uint32_t cycles(void);
#include "../fw/pcm_ring_hw.h"
#include "../fw/pcm_ring_state.h"
/* ---- stubs of the player's world ---- */
static uint32_t cyc; static uint32_t cycles(void) { cyc += 1000u; return cyc; }
static int calls_flush, calls_prefill, calls_flac_restart, calls_tail, calls_vol_apply, calls_bootcancel, calls_poll;
static struct { uint32_t rate; } fl;
static struct { uint8_t hw; } vol_st = { 1 };
static uint32_t fl_idle_cyc, paused, reload_pending, lib_src = 1, track_fmt = FMT_MP3, st0, mt_wu, file_pos, ring_fill, ring_rd, audio_start = 777, frames = 5, min_level;
static char track_title[TITLE_MAX], last_title[TITLE_MAX], track_artist[64], track_album[64], track_year[8], track_trk[8];
static int title_status;
static uint32_t track_secs, track_kbps, track_hz, samprate, samp_per_frame, bytes_per_sec, rg_factor, rg_cap_hw, art_ready, art_x, art_shown, art_file_id, art_sig, art_bad_sig;
static uint32_t ui_sec, ui_sec_acc, ui_last_sec, ui_prog_sec, ui_last_pause, ui_mode_dirty;
static rg_t rg_cur; static uint8_t art_have, art_bad, art_bad_code;
static short pcm[2304 * 2];
static int fb_held;
#define FB_HELD() (fb_held)
static void poll_input(void) { calls_poll++; }
static void refill_pump(void) { }
static void refill_drain(void) { }
static int  flac_restart(void) { calls_flac_restart++; return 1; }
static int  prefill(void) { calls_prefill++; return 1; }
static void vol_apply(void) { calls_vol_apply++; }
static void ui_boot_cancel(void) { calls_bootcancel++; }
static void meters_feed(const short *p, int n, int st) { (void)p; (void)n; (void)st; }
static void pcm_flush(void) { calls_flush++; rg_on_flush(); }
static uint32_t ring_mem[RG_SIZE];
#define RG_WIN ring_mem
static void load_ui_tail(uint32_t *tphase) { (void)tphase; calls_tail++; }
#include "../fw/pcm_ring_fw.inc"
/* ---- helpers ---- */
static int fails;
#define CHECK(c, msg) do { if (!(c)) { fails++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)
static void set_old(void) { strcpy(track_title, "OLD"); strcpy(track_artist, "OLDART"); track_secs = 200; track_kbps = 128; track_hz = 44100; samprate = 44100; samp_per_frame = 1152; bytes_per_sec = 16000; rg_factor = 100; art_sig = 11; art_have = 1; art_shown = 1; art_ready = 1; }
static void set_new(void) { strcpy(track_title, "NEW"); strcpy(track_artist, "NEWART"); track_secs = 301; track_kbps = 320; track_hz = 44100; samprate = 44100; samp_per_frame = 1152; bytes_per_sec = 40000; rg_factor = 777; art_sig = 22; art_have = 0; art_shown = 0; art_ready = 0; }
static void reset(void)
{
    memset(regs, 0, sizeof regs); calls_flush = calls_prefill = calls_flac_restart = calls_tail = calls_vol_apply = calls_bootcancel = 0; paused = reload_pending = 0; lib_src = 1; track_fmt = FMT_MP3;
    gp_want = gp_mode = gp_pend = 0; gp_applied = gp_aborted = 0; fb_held = 0; vol_st.hw = 1;
    prh_init(&rg, RG_SIZE, RG_MARGIN, 0u); rg_on = 1; rg_held = 0; rg_put_total = 0; rg_fed = 0;
    ui_sec = 100; ui_sec_acc = 500; frames = 5; set_old();
}
static void put_pairs(uint32_t n) { rg_put_total += n; rg.w = (rg.w + n) % RG_SIZE; }
static void heard(uint32_t cnt, uint32_t level) { REG(R_RING_CNT) = cnt; REG(R_PCM_ST) = level; }
int main(void)
{
    /* S1: a full gapless hand-over */
    reset(); put_pairs(5000);
    gp_eof_begin();
    CHECK(gp_want == 1 && gp_marker == 5000 && gp_pre_pp == 100u * 44100u + 500u && gp_old_fmt == FMT_MP3 && gp_old_rate == 44100, "eof_begin sets the marker and the old track's facts");
    gp_begin_load();
    CHECK(gp_mode == 1 && gp_want == 0, "begin_load enters gapless mode");
    set_new();                                                        /* what load_track writes while the old track still plays */
    const int go = gp_end_load(1);
    CHECK(go == 1 && gp_pend == 1 && gp_mode == 0, "end_load: take the gapless hand-off, boundary pending");
    CHECK(!strcmp(track_title, "OLD") && !strcmp(track_artist, "OLDART") && track_secs == 200 && track_kbps == 128 && rg_factor == 100 && art_sig == 11 && art_have == 1 && art_shown == 1, "the old display is back on screen");
    gp_handoff();
    CHECK(calls_flush == 0 && calls_prefill == 1 && frames == 0 && mt_wu == 0 && ui_sec == 0 && ui_sec_acc == 0 && file_pos == 777, "hand-off: no flush, the file restarted, new counters from 0");
    heard(4000, 100);                                                 /* 3900 pairs heard: before the marker */
    gp_tick();
    CHECK(gp_pend == 1 && calls_tail == 0 && !strcmp(track_title, "OLD"), "no screen change before the marker");
    fb_held = 1; heard(6000, 0); gp_tick();
    CHECK(gp_pend == 1 && calls_tail == 0, "not while an overlay holds the draw engine");
    fb_held = 0; heard(5100, 100);                                    /* 5000 heard: exactly the marker */
    gp_tick();
    CHECK(gp_pend == 0 && calls_tail == 1 && calls_vol_apply == 1 && gp_applied == 1 && !strcmp(track_title, "NEW") && track_secs == 301 && rg_factor == 777 && art_sig == 22, "at the marker: the new display, its ReplayGain, the cover/chrome tail");
    CHECK(calls_flush == 0, "the whole boundary never flushed");
    /* S2: another sample rate: the fallback is the ordinary path */
    reset(); put_pairs(300); gp_eof_begin(); gp_begin_load(); set_new(); regs[(R_RING_RIDX) & 1023u] = rg.w; REG(R_PCM_ST) = 1u << 16;
    gp_abort();
    CHECK(gp_mode == 0 && gp_aborted == 1 && calls_flush == 1 && calls_vol_apply == 1 && calls_poll > 0, "abort: the old tail plays out (the wait ran), the FIFO is flushed, ReplayGain applied");
    CHECK(gp_end_load(1) == 0 && gp_pend == 0, "after an abort the load ends as an ordinary one");
    /* S3: the file would not open */
    reset(); put_pairs(300); gp_eof_begin(); gp_begin_load(); set_new();
    CHECK(gp_end_load(0) == 0 && gp_pend == 0 && !strcmp(track_title, "OLD") && calls_flush == 0, "open failure: the old display back, nothing flushed, no marker");
    /* S4: a flush (seek, Next) while the marker is pending brings it forward */
    reset(); put_pairs(5000); gp_eof_begin(); gp_begin_load(); set_new(); gp_end_load(1); gp_handoff();
    heard(2000, 100); pcm_flush();
    CHECK(gp_marker == 2000 && rg_put_total == 2000, "a flush resets the put count and brings the marker to the push count");
    heard(2000, 0); gp_tick();
    CHECK(gp_pend == 0 && !strcmp(track_title, "NEW"), "the boundary applies at once after a flush");
    /* S5: no ring / not a library track: no gapless */
    reset(); rg_on = 0; gp_eof_begin(); CHECK(gp_want == 0, "no ring: no gapless");
    reset(); lib_src = 0; gp_eof_begin(); CHECK(gp_want == 0, "not a library track: no gapless");
    reset(); put_pairs(10); gp_eof_begin(); gp_want = 0; gp_begin_load(); CHECK(gp_mode == 0, "no request: begin_load stays ordinary");
    /* S6: FLAC records the FLAC rate */
    reset(); track_fmt = FMT_FLAC; fl.rate = 48000; put_pairs(10); gp_eof_begin(); CHECK(gp_old_rate == 48000 && gp_old_fmt == FMT_FLAC, "FLAC: the codec and the decoder's rate are recorded");
    /* S7: the ring going off clears every gapless state */
    reset(); put_pairs(10); gp_eof_begin(); gp_pend = 1; rg_off(); CHECK(gp_want == 0 && gp_pend == 0 && gp_mode == 0 && rg_on == 0, "rg_off clears the gapless state");
    /* G2c: the MP3 loop draws at most every 20 ms while the ring fills */
    cyc = 10000000u; rg_ui_last = 0u;
    CHECK(rg_ui_due() == 1, "the first draw after a long pause is due");
    CHECK(rg_ui_due() == 0, "a call right after a draw is skipped");
    cyc += (CLK_HZ / 50u) + 5000u; CHECK(rg_ui_due() == 1, "a call 20 ms later is due again");
    cyc += (CLK_HZ / 1000u) * 26u; CHECK(rg_ui_due() == 1, "frames at real time (26 ms) are never skipped");
    printf("%s: %d failures\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
