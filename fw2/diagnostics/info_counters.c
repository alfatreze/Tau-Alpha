#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "info_counters.h"
#ifndef P2_DIAG
#define P2_DIAG 1      /* 1 = the Diagnostic Build (every counter, the Check, the raw Info page); 0 = the normal build (the support set only, the About page) */
#endif
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../ui/helios.h"
#include "../ui/meter.h"
#include "../generated/meter_registry.h"
#include "check_page.h"
extern uint32_t gfx_blend_state, gfx_blend_tries;
#include "../gfx/font.h"
#include "../gfx/gfx.h"
#if P2_STREAM_LAT
extern uint32_t bt_n, bt_wait_n, bt_wait_max, bt_max;   /* fw2/transport/mp3play.c */
#endif
extern uint32_t report_rows, report_len;   /* fw2/diagnostics/report_page.c */
extern uint32_t gfx_flip_n, gfx_flip_cyc, gfx_flip_max; extern uint32_t gfx_fence_n[7], gfx_fence_c[7]; extern uint32_t gfx_bench[4], blit_check_mask; uint32_t blit_check_bad_px(void);   /* fw2/diagnostics/check_page.c */
extern uint32_t gfx_polls, gfx_stalls, gfx_stall_cyc, gfx_fences, gfx_fence_cyc, gfx_fence_tmo; extern uint32_t gfx_hw_ovf, gfx_hw_uf, gfx_hw_fence_st;
#if P2_DIAG
#include "../cores/meter_cost_core.h"
#if P2_DIAG
#include "sdram_soak.h"
static uint32_t soak_lvl_row;   /* refreshed with the table (counters_refresh) */
#endif
mcost_t meter_cost[8];           /* per meter (the index of meter_stats.max_cyc): the cost of its ordinary ticks, forced repaints left out; rows "M ..." */
extern uint32_t gfx_stall_cyc, gfx_fence_cyc;   /* fw2/gfx/gfx.c, T0 counters */
static uint32_t mc_t0, mc_stall0, mc_fence0;
void meter_cost_mark(void) { mc_t0 = cycles(); mc_stall0 = gfx_stall_cyc; mc_fence0 = gfx_fence_cyc; }                       /* strong: overrides the weak no-op in fw2/ui/meters.c */
void meter_cost_note(uint32_t meter, uint32_t cmds) { if (meter < 8u) mcost_add(&meter_cost[meter], cmds, cycles() - mc_t0, gfx_stall_cyc - mc_stall0, gfx_fence_cyc - mc_fence0); }   /* fw2/gfx/gfx.c, T0 of the Talos plan (not in gfx.h: the wasm meter preview hashes that header) */
#endif
#include "../stream/cold.h"
#include "../aout/halcyon.h"
#include "../stream/assets.h"
#include "../ui/theme.h"
#include "../cores/monoclock_core.h"
#include "../settings/settings.h"
#include "../art/art.h"
#include "../stream/skiplist.h"
#include "../transport/mp3play.h"
#include "../transport/player_state.h"
#include "../stream/slot.h"
#include "../stream/library.h"

/* Shadow values: written only here (one writer, P2), read through the table. */
static struct { uint32_t hw_under_ev, hw_under_tk, aout_init, tone_state, tone_rate, heard_seg, heard_pos_ms, heard_valid, put_seg, swaps, acct_err, acct_bad, tl_overflow, tl_empty, tl_flushes, ring_fill, ring_min, ring_max,
                under_ring, dry, put_stalls, put_max_cyc, put_pairs, put_blocks, gov_mode, gov_n1, gov_n2, gov_n3, tone_stalls; int32_t last_delta;
                uint32_t pack_run, pack_err, pack_file, pack_seen, pack_src, meter_cur, uptime_s, loops, loop_max_cyc, pcm_level, pcm_flags, ring_cnt, ring_ridx, ring_wptr, ring_ctrl, ring_hold, ring_empty, gain_cur, cymo_st, keys, stack_peak, image_b, free_b; } v;
uint32_t counters_loops, counters_loop_max_cyc, counters_keys, counters_aout_init;            /* the loop's own measurements (app writes, this file publishes) */
static mc_t uptime_clk;                  /* monotone seconds from the 32-bit cycle counter (it wraps every 64.4 s) */

#define U32(nm, unit, p) { nm, CK_U32, unit, (const volatile void *)(p) }
#define METER_IDX_X(key, nm, id, lc, UC, np, cmp) mtr_idx_##key,
enum { METER_LIST(METER_IDX_X) };
#define METER_MAX_ROW(key, nm, id, lc, UC, np, cmp) U32("METER MAX " #UC, CU_CYC, &meter_stats.max_cyc[mtr_idx_##key]),
const cnt_t counters_table[] = {
    { "FIRMWARE",     CK_STR,   CU_NONE,  p2_ver_field + 7 },       /* read through a volatile pointer at run time: the packagers patch these bytes after the build */
    { "CORE VERSION", CK_HEX32, CU_NONE,  &caps.core_version },
    { "MP STATE",     CK_U32, CU_NONE,   &mp.state },             /* 0 idle, 1 decoding, 2 ending, 3 waiting to change rate */
    { "MP RATE",      CK_U32, CU_NONE,   &mp.rate },
    { "MP CH",        CK_U32, CU_NONE,   &mp.ch },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "MP FRAMES",    CK_U32, CU_NONE,   &mp.frames },
#endif
    { "MP ERRORS",    CK_U32, CU_NONE,   &mp.errors },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "MP RESYNCS",   CK_U32, CU_NONE,   &mp.resyncs },
    { "MP PAIRS OUT", CK_U32, CU_PAIRS,  &mp.pairs_out },
    { "MP PAIRS TOT", CK_U32, CU_PAIRS,  &mp.pairs_total },       /* the P3 expectation of the track being decoded (0 = unknown) */
    { "MP ENDED",     CK_U32, CU_NONE,   &mp.ended },
    { "MP GAPLESS",   CK_U32, CU_NONE,   &mp.gapless_next },      /* natural ends continued with no flush */
    { "MP DRAINED",   CK_U32, CU_NONE,   &mp.drain_next },        /* natural ends that waited for a rate change */
    { "MP RATE CHG",  CK_U32, CU_NONE,   &mp.rate_changes },
    { "MP NEED DATA", CK_U32, CU_NONE,   &mp.need_data },
    { "MP DEC MAX",   CK_U32, CU_CYC,    &mp.dec_max_cyc },       /* the longest atomic decode step */
    { "MP DEC STEPS", CK_U32, CU_NONE,   &mp.dec_steps },
#endif
    { "MP INIT FAIL", CK_U32, CU_NONE,   &mp.init_fail },
    { "MP NON-MP3",   CK_U32, CU_NONE,   &mp.non_mp3 },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "MP SEEKS",     CK_U32, CU_NONE,   &mp.seeks },
    { "MP SEEK FAIL", CK_U32, CU_NONE,   &mp.seek_fail },
    { "MP SEEK IGN",  CK_U32, CU_NONE,   &mp.seek_ign },
    { "MP SEEK TO",   CK_U32, CU_PAIRS,  &mp.seek_to },
#endif
    { "MP VOLUME",    CK_U32, CU_NONE,   &mp.volume },
    { "MP RG MODE",   CK_U32, CU_NONE,   &mp.rg_mode },          /* 0 off, 1 track, 2 album (Select cycles) */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "MP RG FACTOR", CK_U32, CU_NONE,   &mp.rg_factor },        /* Q15, 32768 = 0 dB */
    { "MP RG CAP",    CK_U32, CU_NONE,   &mp.rg_cap },
    { "MP GAIN TGT",  CK_U32, CU_NONE,   &mp.gain_tgt },
    { "MP DEC AVG",   CK_U32, CU_NONE,   &mp.dec_avg_us },        /* microseconds of CPU per decoded block (a 1152-pair block lasts 26,122 us at 44.1 kHz) */
    { "MP POLY ON",   CK_U32, CU_NONE,   &mp.poly_on },           /* the window unit is in use */
    { "MP POLY SLOTS",CK_U32, CU_NONE,   &mp.poly_slots },
    { "MP POLY BAD",  CK_U32, CU_NONE,   &mp.poly_mismatch },     /* self-check mismatches against the software window (must stay 0) */
    { "MP POLY TMO",  CK_U32, CU_NONE,   &mp.poly_timeout },
    { "MP POLY SILENT", CK_U32, CU_NONE, &mp.poly_silent },     /* stereo slots output as silence because the required unit failed (must stay 0) */
    { "MP SPEED IDX", CK_U32, CU_NONE,   &mp.speed_idx },         /* 0.85 0.95 1.00 1.10 1.20 1.30 1.50 1.75 2.00 (2 = normal) */
    { "MP SPEED IDX", CK_U32, CU_NONE,   &mp.speed_idx },         /* 0.85 0.95 1.00 1.10 1.20 1.30 1.50 1.75 2.00 (2 = normal) */
    { "MP TEMPO IDX", CK_U32, CU_NONE,   &mp.tempo_idx },         /* table index: 0.75 0.85 1.00 1.10 1.25 1.50 1.75 2.00, 2 = off */
    { "MP TEMPO OUT", CK_U32, CU_PAIRS,  &mp.tempo_out },
#endif
    { "FLAC REFUSED", CK_U32, CU_NONE,   &mp.flac_refused },      /* rate over 48 kHz, more than 2 channels, blocks too large or a bad header */
    { "FLAC ERR",     CK_U32, CU_NONE,   &mp.flac_err },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "FLAC WAITS",   CK_U32, CU_NONE,   &mp.flac_waits },        /* times the decoder had to wait for the card (open only; must not grow while playing) */
    { "LPC SAMPLES",  CK_U32, CU_NONE,   &mp.lpc_slots },
    { "LPC TMO",      CK_U32, CU_NONE,   &mp.lpc_timeout },
    { "SET WIDE",     CK_U32, CU_NONE,   &settings_stats.wide },       /* the 32-word register file is present (word 17 is usable) */
    { "F2 BAD FRAME ", CK_U32, CU_NONE,   &mp.f2_ferr },
    { "F2 RESYNC    ", CK_U32, CU_NONE,   &mp.f2_resync },
    { "F2 SILENT    ", CK_U32, CU_NONE,   &mp.f2_silent },
    { "F2 WRAPS     ", CK_U32, CU_NONE,   &mp.f2_wraps },
    { "F2 ROLLBACK  ", CK_U32, CU_NONE,   &mp.f2_rollbacks },
    { "F2 SEEKPROBE", CK_U32, CU_NONE,   &mp.f2_probes },
    { "F2 REPOS", CK_U32, CU_NONE,   &mp.f2_repos },
    { "F2 STAGEMAX", CK_U32, CU_NONE,   &mp.f2_staged_max },
#endif
    { "SET STORES",   CK_U32, CU_NONE,   &settings_stats.stores },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "SET MENU",     CK_U32, CU_NONE,   &settings_stats.menu_changes },
    { "SET DIRTY",    CK_U32, CU_NONE,   &settings_stats.dirty },
#endif
    { "SET MIGRATED", CK_U32, CU_NONE,   &settings_stats.migrated },     /* 1: this boot converted a legacy-layout card (the settings of TAU DEV 181 and older) to the layout by area */
    { "SET LAYOUT",   CK_U32, CU_NONE,   &settings_stats.layout },       /* the layout version the register file holds: 1 = by area (0 would be legacy) */
    { "LAST STATE",   CK_U32, CU_NONE,   &last_state },                /* the last played track at boot: 0 not tried, 1 restored, 2 no record saved, 3 record failed its check, 4 track not in that queue, 5 no library */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "LAST W0",      CK_HEX32, CU_NONE, &last_boot[0] },             /* the two persisted words as read at boot (0x4... = a record; ids 31 and 32) */
    { "LAST W1",      CK_HEX32, CU_NONE, &last_boot[1] },
#endif
    { "ART LOADED",   CK_U32, CU_NONE,   &art_loaded },
    { "ART FAILED",   CK_U32, CU_NONE,   &art_failed },
    { "ART ERR",      CK_U32, CU_NONE,   &art_last_err },           /* TIMG_E_*: 1 magic 2 format 3 size 4 open 5 read 7 mailbox 8 probe */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "ART MS",       CK_U32, CU_MS,     &art_ms },
    { "ART PROBE RAW", CK_HEX32, CU_NONE, &art_probe_raw },    /* what the palette probe read back: 1004..1007 (low or high half) means it worked */
    { "ART CLUT",     CK_U32, CU_NONE,   &art_clut_start },         /* palette load start index the probe chose: 0 registered, 255 legacy */
    { "GFX CMDS",     CK_U32, CU_NONE,   &gfx_cmds },               /* draw-engine commands issued since boot */
    { "GFX POLLS",    CK_U32, CU_NONE,   &gfx_polls },              /* command-FIFO "full" reads (one per command, plus the waits) */
    { "GFX STALLS",   CK_U32, CU_NONE,   &gfx_stalls },             /* of those, the times the FIFO was full and the CPU had to wait */
    { "GFX STALL CYC", CK_U32, CU_NONE,  &gfx_stall_cyc },          /* CPU cycles spent waiting for a free FIFO slot */
    { "GFX FENCES",   CK_U32, CU_NONE,   &gfx_fences },
    { "GFX FENCE CYC", CK_U32, CU_NONE,  &gfx_fence_cyc },          /* CPU cycles spent in fences (the marker round trip, 256-cycle polls) */
    { "GFX FENCE TMO", CK_U32, CU_NONE,  &gfx_fence_tmo },          /* fences that gave up after 50 ms */
    { "GFX HW FENCE", CK_U32, CU_NONE,   &gfx_hw_fence_st },        /* Talos T3: 0 not probed, 1 the mailbox fence (no T3 block, or the hardware fence timed out), 2 the hardware FENCE command */
    { "SOAK LEVEL",   CK_U32, CU_NONE,   &soak_lvl_row },            /* the SDRAM soak (Check page, X): 0 off, 1..3 = 16 k / 64 k / 128 k operations a second beside whatever plays */
    { "SOAK PASSES",  CK_U32, CU_NONE,   &soak_st.pass },
    { "SOAK OPS",     CK_U32, CU_NONE,   &soak_st.ops },
    { "SOAK FAILS",   CK_U32, CU_NONE,   &soak_st.fails },            /* words that read back wrong: must stay 0 */
    { "SOAK TMO",     CK_U32, CU_NONE,   &soak_st.tmo },              /* mailbox timeouts */
    { "SOAK 1ST ADR", CK_HEX32, CU_NONE, &soak_st.first_addr },
    { "SOAK 1ST EXP", CK_HEX32, CU_NONE, &soak_st.first_exp },
    { "SOAK 1ST GOT", CK_HEX32, CU_NONE, &soak_st.first_got },
    { "GFX Q OVF",    CK_U32, CU_NONE,   &gfx_hw_ovf },             /* Talos T3: commands dropped because the queue was full (read after each hardware fence; 0 = the poll before every push works) */
    { "GFX SCAN LATE", CK_U32, CU_NONE,  &gfx_hw_uf },              /* Talos T3: scanout lines whose fill was late (the instrument for "scanout always wins"; 0 on a healthy build) */
    { "GFX B1 ISSUE", CK_U32, CU_NONE,   &gfx_bench[0] },           /* Check > GFX ISSUE: CPU cycles per command to issue 1,000 one-pixel rectangles (the CPU's cost per command) */
    { "GFX B1 DRAIN", CK_U32, CU_NONE,   &gfx_bench[1] },           /* cycles per command still queued when the issue loop ended, drained by the fence */
    { "GFX B100 ISSUE", CK_U32, CU_NONE, &gfx_bench[2] },           /* the same with 100-pixel rectangles: higher means the engine, not the CPU, is the limit */
    { "GFX B100 DRAIN", CK_U32, CU_NONE, &gfx_bench[3] },
    { "BLIT BAD",     CK_HEX32, CU_NONE, &blit_check_mask },        /* Check > BLIT OPS: the engine tests that failed, as bits: 0 RECT 1 BAR 2 COPY 3 PALETTE 4 SCALED 5 BLEND 6 CLUT ORDER 7 WIDE COPY (0 = all pass) */
    /* fences by reason (count, CPU cycles): OTHER = callers of the public gfx_fence (meters, the checks, the font hook), FRAME = end of a double-buffered frame, CLUT = a palette load, BASES = a change of the blit base pair, COMP1/COMP2 = before/after an off-screen compose is copied on, BLEND = blend turned off */
    { "FNC OTHER N", CK_U32, CU_NONE, &gfx_fence_n[0] },
    { "FNC OTHER CYC", CK_U32, CU_NONE, &gfx_fence_c[0] },
    { "FNC FRAME N", CK_U32, CU_NONE, &gfx_fence_n[1] },
    { "FNC FRAME CYC", CK_U32, CU_NONE, &gfx_fence_c[1] },
    { "FNC CLUT N", CK_U32, CU_NONE, &gfx_fence_n[2] },
    { "FNC CLUT CYC", CK_U32, CU_NONE, &gfx_fence_c[2] },
    { "FNC BASES N", CK_U32, CU_NONE, &gfx_fence_n[3] },
    { "FNC BASES CYC", CK_U32, CU_NONE, &gfx_fence_c[3] },
    { "FNC COMP1 N", CK_U32, CU_NONE, &gfx_fence_n[4] },
    { "FNC COMP1 CYC", CK_U32, CU_NONE, &gfx_fence_c[4] },
    { "FNC COMP2 N", CK_U32, CU_NONE, &gfx_fence_n[5] },
    { "FNC COMP2 CYC", CK_U32, CU_NONE, &gfx_fence_c[5] },
    { "FNC BLEND N", CK_U32, CU_NONE, &gfx_fence_n[6] },
    { "FNC BLEND CYC", CK_U32, CU_NONE, &gfx_fence_c[6] },
    { "FNC FLIP N",   CK_U32, CU_NONE, &gfx_flip_n },               /* frames flipped: after the fence the CPU waits for the flip to land at a vblank */
    { "FNC FLIP CYC", CK_U32, CU_NONE, &gfx_flip_cyc },             /* CPU cycles spent in that wait */
    { "FNC FLIP MAX", CK_U32, CU_CYC,  &gfx_flip_max },             /* the longest one (a vblank is 16.7 ms = about 1.1 M cycles) */
    { "REPORT ROWS",  CK_U32, CU_NONE,   &report_rows },            /* the last report: counters in it */
    { "REPORT BYTES", CK_U32, CU_NONE,   &report_len },             /* and the bytes of its record (0 = it did not fit the buffer) */
#if P2_STREAM_LAT
    /* stream read latency (B-879): finished reads by time, stale ones (issued before a skip) included */
    { "ST LAT <5MS", CK_U32, CU_NONE, &lp_stream.lat_hist[0] },
    { "ST LAT <20MS", CK_U32, CU_NONE, &lp_stream.lat_hist[1] },
    { "ST LAT <50MS", CK_U32, CU_NONE, &lp_stream.lat_hist[2] },
    { "ST LAT <200MS", CK_U32, CU_NONE, &lp_stream.lat_hist[3] },
    { "ST LAT <1S", CK_U32, CU_NONE, &lp_stream.lat_hist[4] },
    { "ST LAT <3S", CK_U32, CU_NONE, &lp_stream.lat_hist[5] },
    { "ST LAT >=3S", CK_U32, CU_NONE, &lp_stream.lat_hist[6] },
    { "ST FRESH MAX", CK_U32, CU_CYC,  &lp_stream.max_lat_fresh },    /* the slowest read of the generation that was current when it finished */
    { "ST STALE MAX", CK_U32, CU_CYC,  &lp_stream.max_lat_stale },    /* the slowest read that finished after a skip or open had moved on */
    { "ST SLOW OFF",  CK_U32, CU_NONE, &lp_stream.slow_off },         /* the slowest read overall: its file offset, length, and 1 when it was stale */
    { "ST SLOW LEN",  CK_U32, CU_NONE, &lp_stream.slow_len },
    { "ST SLOW STALE", CK_U32, CU_NONE, &lp_stream.slow_stale },
    { "BT N",         CK_U32, CU_NONE, &bt_n },                       /* track starts */
    { "BT WAIT N",    CK_U32, CU_NONE, &bt_wait_n },                  /* ... that waited for the read in flight (the one card channel) */
    { "BT WAIT MAX",  CK_U32, CU_CYC,  &bt_wait_max },                /* the longest such wait: the main loop does nothing else meanwhile */
    { "BT MAX",       CK_U32, CU_CYC,  &bt_max },                     /* the longest whole track start */
#endif
    { "HELIOS BEAM",     CK_U32, CU_NONE,  &helios_beam },        /* 1: the bitstream has the video line counter, regions wait for the beam to leave their rows */
    { "HELIOS DRAWN",    CK_U32, CU_NONE,  &helios.drawn },       /* beam-checked region draws that went ahead */
    { "HELIOS DEFERRED", CK_U32, CU_NONE,  &helios.deferred },    /* ... and those that waited a pass because the beam was in their rows */
    { "COLD MS",         CK_U32, CU_MS,    &cold_ms },            /* boot time to load and verify the cold image */
    { "COLD BYTES",      CK_U32, CU_BYTES, &cold_bytes },
#endif
    { "COLD ERR",        CK_U32, CU_NONE,  &cold_err },           /* 0 = ok (a failure stops the boot with the code on screen) */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "ACCEPT RATES",    CK_U8,  CU_NONE,  &mp3play_accept_all },  /* Settings > Diagnostics: FLAC above 48 kHz is decoded */
#endif
    { "HAL PRESENT",     CK_U8,  CU_NONE,  &halcyon_present },     /* the bitstream has the Halcyon engine */
    { "HAL SEL",         CK_U8,  CU_NONE,  &halcyon_sel },         /* 0 off, 1..8 built-in, 9.. user, 200 custom */
    { "HAL USER",        CK_U8,  CU_NONE,  &halcyon_user_n },      /* user presets loaded from tau-assets.bin */
    { "ASSETS STATE",    CK_U8,  CU_NONE,  &assets_state },        /* 0 not tried, 1 no file, 2 loaded, 3 error */
    { "ASSETS ERR",      CK_U32, CU_NONE,  &assets_err },
    { "THEME",           CK_U8,  CU_NONE,  &th_theme },            /* the selected theme: built-in ones first, then the file themes */
    { "MODE",            CK_U8,  CU_NONE,  &th_pol },              /* 0 dark, 1 light */
    { "COLOUR",          CK_U8,  CU_NONE,  &th_pal },              /* the accent colour index (0..18) */
    { "THEME FILES",     CK_U8,  CU_NONE,  &th_file_n },           /* themes read from tau-assets.bin */
    { "THEME ERR",       CK_U8,  CU_NONE,  &th_file_err },         /* 0 none, else an AS_E_* code */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "SKIP ON",      CK_U32, CU_NONE,   &mp.skip_on },
    { "SKIP LEAD",    CK_U32, CU_PAIRS,  &mp.skip_lead },
    { "SKIP TAIL",    CK_U32, CU_PAIRS,  &mp.skip_tail },
    { "SKIP APPLIED", CK_U32, CU_NONE,   &mp.skip_applied },
    { "SKIP HITS",    CK_U32, CU_NONE,   &skiplist_hits },
    { "SKIP MISSES",  CK_U32, CU_NONE,   &skiplist_misses },
    { "MP STALLS",    CK_U32, CU_NONE,   &mp.stalls },
    { "LP STATE",     CK_U32, CU_NONE,   &lp.state },             /* 0 idle, 1 streaming, 2 opening */
#endif
    { "LIB STATE",    CK_U8,  CU_NONE,   &lib_state },            /* 1 loaded, 2 failed */
    { "LIB ERR",      CK_U8,  CU_NONE,   &lib_err },
    { "LIB TRACKS",   CK_U32, CU_NONE,   &lib_n_tracks },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "LIB LOAD",     CK_U32, CU_MS,     &lib_ms },
    { "LP QUEUE",     CK_U32, CU_NONE,   &lp.qn },
    { "LP POS",       CK_U32, CU_NONE,   &lp.cur_pos },
    { "LP TRACK ID",  CK_U32, CU_NONE,   &lp.cur_id },
#endif
    { "LP OPEN ERR",  CK_U32, CU_NONE,   &lp.open_err },
    { "LP SKIPPED",   CK_U32, CU_NONE,   &lp.skipped },
    { "SLOT ERR",     CK_U8,  CU_NONE,   &slot_err },
    { "SLOT ERRCODE", CK_U8,  CU_NONE,   &slot_errcode },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "SLOT OPENS",   CK_U32, CU_NONE,   &slot_opens },
#endif
    { "SLOT OPEN FAIL", CK_U32, CU_NONE, &slot_open_fails },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "OPEN LAST",    CK_U32, CU_MS,     &slot_open_ms_last },
    { "OPEN MAX",     CK_U32, CU_MS,     &slot_open_ms_max },
    { "ST READS",     CK_U32, CU_NONE,   &lp_stream.reads },
    { "ST BYTES",     CK_U32, CU_NONE,   &lp_stream.bytes },
    { "ST TO OFF",    CK_U32, CU_NONE,   &lp_stream.to_off },
    { "ST TO LEN",    CK_U32, CU_NONE,   &lp_stream.to_len },
#endif
    { "ST FAILS",     CK_U32, CU_NONE,   &lp_stream.fails_total },
    { "ST RETRIES",   CK_U32, CU_NONE,   &lp_stream.retries },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "ST STALE",     CK_U32, CU_NONE,   &lp_stream.stale },
#endif
    { "ST TIMEOUTS",  CK_U32, CU_NONE,   &lp_stream.timeouts },
    { "ST STARVED",   CK_U32, CU_NONE,   &lp_stream.starved },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "ST AVAIL",     CK_U32, CU_BYTES,  &lp_stream.avail },
    { "ST MIN AVAIL", CK_U32, CU_BYTES,  &lp_stream.min_avail },
    { "ST MAX LAT",   CK_U32, CU_CYC,    &lp_stream.max_lat },    /* cycles: /66,667 = ms */
    { "ST SEEKS",     CK_U32, CU_NONE,   &lp_stream.seeks },
    { "ST OPENS",     CK_U32, CU_NONE,   &lp_stream.opens },
    U32("AOUT INIT",    CU_NONE,   &v.aout_init),
    U32("HEARD SEG",    CU_NONE,   &v.heard_seg),
    U32("HEARD POS",    CU_MS,     &v.heard_pos_ms),
    U32("HEARD VALID",  CU_NONE,   &v.heard_valid),
    U32("PUT SEG",      CU_NONE,   &v.put_seg),
    U32("SWAPS",        CU_NONE,   &v.swaps),
    U32("ACCT ERR",     CU_NONE,   &v.acct_err),
    { "LAST DELTA",   CK_I32,   CU_PAIRS, &v.last_delta },
    U32("ACCT BAD",     CU_NONE,   &v.acct_bad),
    U32("RING FILL",    CU_PAIRS,  &v.ring_fill),
    U32("RING MIN",     CU_PAIRS,  &v.ring_min),
    U32("RING MAX",     CU_PAIRS,  &v.ring_max),
#endif
    U32("UNDER RING",   CU_NONE,   &v.under_ring),          /* the reader fell behind: must stay 0 */
    U32("DRY",          CU_NONE,   &v.dry),                 /* the producer fell behind */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    U32("PUT STALLS",   CU_NONE,   &v.put_stalls),
    U32("PUT MAX",      CU_CYC,    &v.put_max_cyc),
    U32("PUT PAIRS",    CU_NONE,   &v.put_pairs),
    U32("PUT BLOCKS",   CU_NONE,   &v.put_blocks),
    U32("TL OVERFLOW",  CU_NONE,   &v.tl_overflow),
    U32("TL EMPTY",     CU_NONE,   &v.tl_empty),
    U32("TL FLUSHES",   CU_NONE,   &v.tl_flushes),
    U32("GOV MODE",     CU_NONE,   &v.gov_mode),
    U32("GOV SHED",     CU_NONE,   &v.gov_n1),
    U32("GOV AUDIO",    CU_NONE,   &v.gov_n2),
#endif
    U32("GOV PANIC",    CU_NONE,   &v.gov_n3),
    { "CAP RING",     CK_U8,    CU_NONE,  &caps.ring },
    { "CAP GAIN",     CK_U8,    CU_NONE,  &caps.gain },
    { "CAP CYMO",     CK_U8,    CU_NONE,  &caps.cymo },
    { "CAP HALCYON",  CK_U8,    CU_NONE,  &caps.halcyon },
    { "CAP POLY",     CK_U8,    CU_NONE,  &caps.poly },
    { "CAP LPC",      CK_U8,    CU_NONE,  &caps.lpc },
    { "CAP WAVE",     CK_U8,    CU_NONE,  &caps.wave },
    { "CAP STATS",    CK_U8,    CU_NONE,  &caps.stats },          /* the audio statistics block (tau_audio_stats.sv) answered with its signature */
    { "CAP SPEC",     CK_U8,    CU_NONE,  &caps.spec },
    { "CAP DBUF",     CK_U8,    CU_NONE,  &caps.dbuf },
    { "CAP UNDER",    CK_U8,    CU_NONE,  &caps.under },
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    U32("HW UNDER EV",  CU_NONE,   &v.hw_under_ev),          /* P6: runs of output ticks that found the FIFO empty (free-running since reset; the polling counters UNDER RING / DRY cannot see a short one) */
    U32("HW UNDER TK",  CU_NONE,   &v.hw_under_tk),          /* each is one lost sample pair */
#endif
    U32("UPTIME",      CU_NONE,   &v.uptime_s),
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    U32("LOOPS",       CU_NONE,   &v.loops),
    U32("LOOP MAX",    CU_CYC,    &v.loop_max_cyc),
    U32("KEYS",        CU_NONE,   &v.keys),
    U32("PCM LEVEL",   CU_PAIRS,  &v.pcm_level),
    U32("PCM FLAGS",   CU_NONE,   &v.pcm_flags),
    U32("RING CNT",    CU_PAIRS,  &v.ring_cnt),
    U32("RING RIDX",   CU_NONE,   &v.ring_ridx),
    U32("RING WPTR",   CU_NONE,   &v.ring_wptr),
    { "RING CTRL",    CK_HEX32, CU_NONE,  &v.ring_ctrl },
    U32("RING HOLD",    CU_NONE,   &v.ring_hold),            /* RING_CTRL read: bit 31 present, 3 hold, 2 busy, 1 empty (read index == write index), 0 enable */
    U32("RING EMPTY",   CU_NONE,   &v.ring_empty),
    U32("GAIN CUR",    CU_NONE,   &v.gain_cur),
    { "CYMO STATUS",  CK_HEX32, CU_NONE,  &v.cymo_st },
#endif
    U32("STACK PEAK",  CU_BYTES,  &v.stack_peak),
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    U32("IMAGE",       CU_BYTES,  &v.image_b),
#endif
    U32("FREE",        CU_BYTES,  &v.free_b),
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    { "CHECK MASK",   CK_HEX32, CU_NONE,  &check_mask },      /* 2 bits per test: 1 pass 2 fail 3 skipped (AUDIO, TRACKS, STRETCH 1.10/1.50/2.00, PITCH 1.10/1.50/2.00) */
    U32("CHECK STALLS", CU_NONE,  &check_under),
    U32("BOOT ASSETS",  CU_CYC,    &assets_boot.load),        /* reading and validating tau-assets.bin at boot (the staging copy of a file over 4 KiB included) */
    U32("BOOT THEMES",  CU_CYC,    &assets_boot.themes),      /* handing out the THEM section */
    U32("BOOT PRST",    CU_CYC,    &assets_boot.prst),        /* the Halcyon presets section */
    U32("BOOT PACKS",   CU_CYC,    &assets_boot.packs),       /* the MPKB section: every pack verified and copied into its PSRAM slot */
    U32("BOOT PRESETS", CU_CYC,    &assets_boot.presets),     /* the METR section: the meter presets */
    U32("FONT",         CU_NONE,   &font_cur),        /* the font in the engine memory: 0 Tamzen, 1 Tamzen Bold, 2 Grotesk SemiBold, 3 Grotesk Bold */
#endif
    U32("FONT ERR",     CU_NONE,   &font_err),      /* the last refused or failed load: 1 bad record, 2 no font memory, 3 the load counter did not step, 4 engine held */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    U32("FONT LOADS",   CU_NONE,   &font_loads),
    U32("FONT MS",      CU_NONE,   &font_ms),
    U32("BLEND PROBE", CU_NONE,   &gfx_blend_state),     /* 0 not asked yet, 1 the hardware blend works, 2 it answered wrongly (no blend), 3 the mailbox did not answer (asks again) */
    U32("BLEND TRIES", CU_NONE,   &gfx_blend_tries),
#endif
    U32("METER CUR",    CU_NONE,   &v.meter_cur),             /* the selected meter: its index in the table (meters_cur) */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    U32("METER TICKS",  CU_NONE,   &meter_stats.ticks),       /* pictures drawn */
    U32("METER DEGRADED", CU_NONE, &meter_stats.degraded),  /* windows not drawn because the audio's lead was shrinking (degrade level 1 or 2) */
    U32("METER CMDS",   CU_NONE,   &meter_stats.cmds_last),   /* draw-engine commands of the last tick */
    U32("METER CMDS MAX", CU_NONE, &meter_stats.cmds_max),    /* the most commands one tick has issued since boot */
    U32("METER OVER",   CU_NONE,   &meter_stats.over),        /* ticks over the meter's command ceiling (each lowered its picture rate) */
    U32("PACK OVERRUNS", CU_NONE,  &meter_stats.overruns),    /* ticks of a pack over its cycle limit */
#endif
    U32("PACKS STRUCK", CU_NONE,   &meter_stats.struck),      /* packs disabled for the session after three overruns (PACK ERR 40) */
#if P2_DIAG                                                    /* raw counters: the Diagnostic Build only (D-U02); the normal build keeps the support set */
    U32("METER ROWS",   CU_NONE,   &meter_stats.rows),        /* the rows the last tick wrote: first << 16 | end */
    U32("METER DIRECT", CU_NONE,   &meter_stats.direct),      /* ticks drawn straight onto the shown buffer */
    U32("METER EXPOSED", CU_NONE,  &meter_stats.exposed),     /* of those, ticks that began with the beam inside the rows they wrote (a tear risk) */
    U32("METER SHED",   CU_NONE,   &meter_stats.shed),        /* spectrum windows that waited because the governor shed the UI */
    U32("METER LAST",   CU_CYC,    &meter_stats.last_cyc),    /* the last tick, compose copy included */
    U32("STATS WINDOWS", CU_NONE,  &meter_stats.stats_win),   /* hardware statistics windows read (0 on a bitstream without the block) */
    U32("STATS N",      CU_NONE,   &meter_stats.stats_n),     /* samples in the last window (about 1,250 at 26 ms) */
    { "STATS OVF",    CK_U16,   CU_NONE,  &meter_stats.stats_ovf },   /* windows that overflowed the block's sums (should stay 0) */
    { "STATS RMS",    CK_U16,   CU_NONE,  &meter_stats.stats_rms },   /* last RMS, mean of both channels, 0..24575 */
    { "STATS CORR",   CK_I32,   CU_NONE,  &meter_stats.stats_corr },   /* last L/R correlation, Q8: 256 mono, 0 unrelated, -256 opposite phase */
    U32("METER COMPOSE", CU_CYC,   &meter_stats.compose_cyc), /* the last compose copy onto the shown buffer (0: the meter draws directly) */
#define METER_PACK_MAX_ROW(row, nm) U32("METER MAX " nm, CU_CYC, &meter_stats.max_cyc[row]),
#define MCOST_ROWS(row, nm) \
    { "M CMD MAX " nm,   CK_U16, CU_NONE, &meter_cost[row].cmd_max }, { "M CMD AVG16 " nm, CK_U16, CU_NONE, &meter_cost[row].cmd_avg16 }, \
    U32("M CYC AVG " nm, CU_CYC, &meter_cost[row].cyc_avg), U32("M STALL AVG " nm, CU_CYC, &meter_cost[row].stall_avg), U32("M FENCE AVG " nm, CU_CYC, &meter_cost[row].fence_avg),
#define MCOST_X(key, nm, id, lc, UC, np, cmp) MCOST_ROWS(mtr_idx_##key, #UC)
    METER_PACK_LIST(METER_PACK_MAX_ROW)                       /* the same for every pack-only meter, by name */
    METER_LIST(METER_MAX_ROW)                                 /* the slowest tick of each meter since boot, one row per registry entry */
    /* Talos 2 T4 gate: the cost of each meter's ordinary ticks (forced repaints left out) -- commands (max, average x 16), CPU cycles, and of those the cycles spent waiting for a queue slot and in fences. Read in the
     * window and fullscreen: a meter whose commands x 132 cycles (GFX B1 ISSUE) is most of M CYC AVG is limited by command issue; one with a small share is limited by its own arithmetic or by reads. */
    METER_PACK_LIST(MCOST_ROWS)
    METER_LIST(MCOST_X)
#endif
#ifdef P2_PACKS
    { "PACKS RUN",     CK_HEX32, CU_NONE, &v.pack_run },      /* bit per meter (table order): 1 = the meter runs from its pack */
    U32("PACK ERR",     CU_NONE,   &v.pack_err),              /* the first refusal code of a pack (21 header, 22 ABI, 23 id, 24 origin, 25 size, 26 CRC, 27 entry, 28/29 scratch); 0 none */
    U32("PACK FILE",    CU_NONE,   &v.pack_file),             /* the bundle: 0 read, 255 no bundle in tau-assets.bin, 30 not a bundle */
    U32("PACKS SEEN",   CU_NONE,   &v.pack_seen),             /* packs found in the bundle */
    U32("PACK SRC",     CU_NONE,   &v.pack_src),              /* where the bundle came from: 0 none, 1 tau-assets.bin (MPKB), 2 the core's own tau-packs.bin (the release) */
#endif
};
const uint32_t counters_count = sizeof counters_table / sizeof counters_table[0];

void counters_init(void) { mc_update(&uptime_clk, cycles(), CLK_HZ); }

void counters_refresh(void)
{
#if P2_DIAG
    soak_lvl_row = soak_level();
#endif
    {   /* the audio side: one writer here, read through the table */
        extern uint32_t counters_aout_init;
        aout_snap_t s = aout_snap(); const aout_stats_t *a = &aout_stats;
        v.aout_init = counters_aout_init;
        v.tone_state = !mp.playing ? 0u : mp.held ? 2u : 1u; v.tone_rate = mp.rate;
        v.heard_seg = mp.cur_valid ? mp.cur_gen : 0u; v.heard_valid = mp.cur_valid;
        v.heard_pos_ms = mp.cur_valid && mp.rate ? (uint32_t)(((uint64_t)mp.cur_pos * 1000u) / mp.rate) : 0u;
        v.put_seg = mp.gen; v.swaps = mp_tl.swaps; v.acct_err = mp_tl.acct_err; v.acct_bad = mp.acct_bad; v.last_delta = mp_tl.last_delta;
        v.tl_overflow = mp_tl.overflow; v.tl_empty = mp_tl.empty; v.tl_flushes = mp_tl.flushes;
        v.ring_fill = aout_fill(&s); v.ring_min = a->ring_min == 0xFFFFFFFFu ? 0u : a->ring_min; v.ring_max = a->ring_max;
        v.under_ring = a->under_ring; v.dry = a->dry; v.put_stalls = a->put_stalls; v.put_max_cyc = a->put_max_cyc; v.put_pairs = a->put_pairs; v.put_blocks = a->put_blocks;
        v.gov_mode = mp.gov_mode; v.gov_n1 = mp_gov.entered[1]; v.gov_n2 = mp_gov.entered[2]; v.gov_n3 = mp_gov.entered[3]; v.tone_stalls = mp.stalls;
    }
    v.uptime_s = mc_update(&uptime_clk, cycles(), CLK_HZ);
    v.loops = counters_loops; v.loop_max_cyc = counters_loop_max_cyc; v.keys = counters_keys;
    uint32_t st = REG(R_PCM_ST);
    v.pcm_level = PCM_LEVEL(st); v.pcm_flags = (st >> 16) & 7u;
    v.ring_cnt = REG(R_RING_CNT); v.ring_ridx = REG(R_RING_RIDX); v.ring_wptr = REG(R_RING_WPTR); v.ring_ctrl = REG(R_RING_CTRL); v.ring_hold = (v.ring_ctrl >> 3) & 1u; v.ring_empty = (v.ring_ctrl >> 1) & 1u;
    { uint32_t u = REG(R_UNDER); v.hw_under_ev = (u >> 16) & 0x7FFFu; v.hw_under_tk = u & 0xFFFFu; }
    v.gain_cur = REG(R_GAIN_CUR); v.cymo_st = REG(R_CYMO_STATUS);
    v.meter_cur = meters_cur();
#ifdef P2_PACKS
    meters_pack_info(&v.pack_run, &v.pack_err, &v.pack_file, &v.pack_seen); v.pack_src = meters_pack_source();
#endif
    v.stack_peak = platform_stack_peak(); v.image_b = platform_image_bytes(); v.free_b = platform_free_bytes();
}
