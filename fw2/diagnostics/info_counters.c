#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "info_counters.h"
#include "../platform/platform.h"
#include "../aout/aout.h"
#include "../transport/tone.h"
#include "../cores/monoclock_core.h"
#ifdef P2_MP3PLAY
#include "../settings/settings.h"
#include "../art/art.h"
#include "../stream/skiplist.h"
#include "../transport/mp3play.h"
#define tone mp
#define tone_tl mp_tl
#define tone_gov mp_gov
#endif
#ifdef P2_LISTPLAY
#include "../transport/listplay.h"
#include "../stream/slot.h"
#include "../stream/library.h"
#endif

/* Shadow values: written only here (one writer, P2), read through the table. */
static struct { uint32_t hw_under_ev, hw_under_tk, aout_init, tone_state, tone_rate, heard_seg, heard_pos_ms, heard_valid, put_seg, swaps, acct_err, acct_bad, tl_overflow, tl_empty, tl_flushes, ring_fill, ring_min, ring_max,
                under_ring, dry, put_stalls, put_max_cyc, put_pairs, put_blocks, gov_mode, gov_n1, gov_n2, gov_n3, tone_stalls; int32_t last_delta;
                uint32_t uptime_s, loops, loop_max_cyc, pcm_level, pcm_flags, ring_cnt, ring_ridx, ring_wptr, ring_ctrl, ring_hold, ring_empty, gain_cur, cymo_st, keys, stack_peak, image_b, free_b; } v;
uint32_t counters_loops, counters_loop_max_cyc, counters_keys, counters_aout_init;            /* the loop's own measurements (app writes, this file publishes) */
static mc_t uptime_clk;                  /* monotone seconds from the 32-bit cycle counter (it wraps every 64.4 s) */

#define U32(nm, unit, p) { nm, CK_U32, unit, (const volatile void *)(p) }
const cnt_t counters_table[] = {
    { "FIRMWARE",     CK_STR,   CU_NONE,  p2_ver_field + 7 },       /* read through a volatile pointer at run time: the packagers patch these bytes after the build */
    { "CORE VERSION", CK_HEX32, CU_NONE,  &caps.core_version },
#ifdef P2_MP3PLAY
    { "MP STATE",     CK_U32, CU_NONE,   &mp.state },             /* 0 idle, 1 decoding, 2 ending, 3 waiting to change rate */
    { "MP RATE",      CK_U32, CU_NONE,   &mp.rate },
    { "MP CH",        CK_U32, CU_NONE,   &mp.ch },
    { "MP FRAMES",    CK_U32, CU_NONE,   &mp.frames },
    { "MP ERRORS",    CK_U32, CU_NONE,   &mp.errors },
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
    { "MP INIT FAIL", CK_U32, CU_NONE,   &mp.init_fail },
    { "MP NON-MP3",   CK_U32, CU_NONE,   &mp.non_mp3 },
    { "MP SEEKS",     CK_U32, CU_NONE,   &mp.seeks },
    { "MP SEEK FAIL", CK_U32, CU_NONE,   &mp.seek_fail },
    { "MP SEEK IGN",  CK_U32, CU_NONE,   &mp.seek_ign },
    { "MP SEEK TO",   CK_U32, CU_PAIRS,  &mp.seek_to },
    { "MP VOLUME",    CK_U32, CU_NONE,   &mp.volume },
    { "MP RG MODE",   CK_U32, CU_NONE,   &mp.rg_mode },          /* 0 off, 1 track, 2 album (Select cycles) */
    { "MP RG FACTOR", CK_U32, CU_NONE,   &mp.rg_factor },        /* Q15, 32768 = 0 dB */
    { "MP RG CAP",    CK_U32, CU_NONE,   &mp.rg_cap },
    { "MP GAIN TGT",  CK_U32, CU_NONE,   &mp.gain_tgt },
    { "MP DEC AVG",   CK_U32, CU_NONE,   &mp.dec_avg_us },        /* microseconds of CPU per decoded block (a 1152-pair block lasts 26,122 us at 44.1 kHz) */
    { "MP POLY ON",   CK_U32, CU_NONE,   &mp.poly_on },           /* the window unit is in use */
    { "MP POLY SLOTS",CK_U32, CU_NONE,   &mp.poly_slots },
    { "MP POLY BAD",  CK_U32, CU_NONE,   &mp.poly_mismatch },     /* self-check mismatches against the software window (must stay 0) */
    { "MP POLY TMO",  CK_U32, CU_NONE,   &mp.poly_timeout },
    { "MP POLY SILENT", CK_U32, CU_NONE, &mp.poly_silent },     /* stereo slots output as silence because the required unit failed (must stay 0) */
    { "MP TEMPO IDX", CK_U32, CU_NONE,   &mp.tempo_idx },         /* 0.75 0.85 1.00 1.10 1.25 1.50 1.75 2.00 (Select+L1/R1) */
    { "MP TEMPO OUT", CK_U32, CU_PAIRS,  &mp.tempo_out },
    { "FLAC REFUSED", CK_U32, CU_NONE,   &mp.flac_refused },      /* rate over 48 kHz, more than 2 channels, blocks too large or a bad header */
    { "FLAC ERR",     CK_U32, CU_NONE,   &mp.flac_err },
    { "FLAC WAITS",   CK_U32, CU_NONE,   &mp.flac_waits },        /* times the decoder had to wait for the card (open only; must not grow while playing) */
    { "LPC SAMPLES",  CK_U32, CU_NONE,   &mp.lpc_slots },
    { "LPC TMO",      CK_U32, CU_NONE,   &mp.lpc_timeout },
    { "F2 BAD FRAME ", CK_U32, CU_NONE,   &mp.f2_ferr },
    { "F2 RESYNC    ", CK_U32, CU_NONE,   &mp.f2_resync },
    { "F2 SILENT    ", CK_U32, CU_NONE,   &mp.f2_silent },
    { "F2 WRAPS     ", CK_U32, CU_NONE,   &mp.f2_wraps },
    { "F2 ROLLBACK  ", CK_U32, CU_NONE,   &mp.f2_rollbacks },
    { "F2 SEEKPROBE", CK_U32, CU_NONE,   &mp.f2_probes },
    { "F2 REPOS", CK_U32, CU_NONE,   &mp.f2_repos },
    { "F2 STAGEMAX", CK_U32, CU_NONE,   &mp.f2_staged_max },
    { "SET STORES",   CK_U32, CU_NONE,   &settings_stats.stores },
    { "ART LOADED",   CK_U32, CU_NONE,   &art_loaded },
    { "ART FAILED",   CK_U32, CU_NONE,   &art_failed },
    { "ART ERR",      CK_U32, CU_NONE,   &art_last_err },           /* TIMG_E_*: 1 magic 2 format 3 size 4 open 5 read 7 mailbox 8 probe */
    { "ART MS",       CK_U32, CU_MS,     &art_ms },
    { "ART CLUT",     CK_U32, CU_NONE,   &art_clut_start },         /* palette load start index the probe chose: 0 registered, 255 legacy */
    { "SKIP ON",      CK_U32, CU_NONE,   &mp.skip_on },
    { "SKIP HITS",    CK_U32, CU_NONE,   &skiplist_hits },
    { "SKIP MISSES",  CK_U32, CU_NONE,   &skiplist_misses },
    { "MP STALLS",    CK_U32, CU_NONE,   &mp.stalls },
#endif
#ifdef P2_LISTPLAY
    { "LP STATE",     CK_U32, CU_NONE,   &lp.state },             /* 0 idle, 1 streaming, 2 opening */
    { "LIB STATE",    CK_U8,  CU_NONE,   &lib_state },            /* 1 loaded, 2 failed */
    { "LIB ERR",      CK_U8,  CU_NONE,   &lib_err },
    { "LIB TRACKS",   CK_U32, CU_NONE,   &lib_n_tracks },
    { "LP QUEUE",     CK_U32, CU_NONE,   &lp.qn },
    { "LP TRACK ID",  CK_U32, CU_NONE,   &lp.cur_id },
#ifndef P2_MP3PLAY
    { "LP SHUFFLE",   CK_U32, CU_NONE,   &lp.shuffle },
#endif
#ifndef P2_MP3PLAY
    { "LP REPEAT",    CK_U32, CU_NONE,   &lp.repeat },
#endif
#ifndef P2_MP3PLAY
    { "LP RATE",      CK_U32, CU_BYTES,  &lp.rate_bps },          /* bytes per second the consumer takes, 0 = flat out */
#endif
#ifndef P2_MP3PLAY
    { "LP DONE",      CK_U32, CU_NONE,   &lp.tracks_done },
#endif
#ifndef P2_MP3PLAY
    { "LP OK",        CK_U32, CU_NONE,   &lp.tracks_ok },         /* ended with exactly the library's size */
#endif
#ifndef P2_MP3PLAY
    { "LP SIZE BAD",  CK_U32, CU_NONE,   &lp.size_bad },
#endif
#ifndef P2_MP3PLAY
    { "LP HASH SAME", CK_U32, CU_NONE,   &lp.hash_seen },         /* a track replayed with the identical content hash */
#endif
#ifndef P2_MP3PLAY
    { "LP HASH BAD",  CK_U32, CU_NONE,   &lp.hash_bad },
#endif
#ifndef P2_MP3PLAY
    { "LP HASH LAST", CK_HEX32, CU_NONE, &lp.hash_last },
#endif
    { "LP OPEN ERR",  CK_U32, CU_NONE,   &lp.open_err },
#ifndef P2_MP3PLAY
    { "LP SKIPPED",   CK_U32, CU_NONE,   &lp.skipped },
#endif
    { "SLOT ERR",     CK_U8,  CU_NONE,   &slot_err },
    { "SLOT OPENS",   CK_U32, CU_NONE,   &slot_opens },
    { "SLOT OPEN FAIL", CK_U32, CU_NONE, &slot_open_fails },
    { "OPEN LAST",    CK_U32, CU_MS,     &slot_open_ms_last },
    { "ST READS",     CK_U32, CU_NONE,   &lp_stream.reads },
    { "ST BYTES",     CK_U32, CU_NONE,   &lp_stream.bytes },
    { "ST TO OFF",    CK_U32, CU_NONE,   &lp_stream.to_off },
    { "ST TO LEN",    CK_U32, CU_NONE,   &lp_stream.to_len },
    { "ST FAILS",     CK_U32, CU_NONE,   &lp_stream.fails_total },
    { "ST RETRIES",   CK_U32, CU_NONE,   &lp_stream.retries },
    { "ST STALE",     CK_U32, CU_NONE,   &lp_stream.stale },
    { "ST TIMEOUTS",  CK_U32, CU_NONE,   &lp_stream.timeouts },
    { "ST STARVED",   CK_U32, CU_NONE,   &lp_stream.starved },
    { "ST AVAIL",     CK_U32, CU_BYTES,  &lp_stream.avail },
    { "ST MIN AVAIL", CK_U32, CU_BYTES,  &lp_stream.min_avail },
    { "ST MAX LAT",   CK_U32, CU_CYC,    &lp_stream.max_lat },    /* cycles: /66,667 = ms */
#ifndef P2_MP3PLAY
    { "ST SEEKS",     CK_U32, CU_NONE,   &lp_stream.seeks },
#endif
#ifndef P2_MP3PLAY
    { "ST OPENS",     CK_U32, CU_NONE,   &lp_stream.opens },
#endif
#ifndef P2_MP3PLAY
    { "LP KB/S",      CK_U32, CU_NONE,   &lp.kbps },
#endif
#endif
    U32("AOUT INIT",    CU_NONE,   &v.aout_init),
#ifndef P2_MP3PLAY
    U32("TONE STATE",   CU_NONE,   &v.tone_state),          /* 0 stopped, 1 playing, 2 held */
#endif
#ifndef P2_MP3PLAY
    U32("TONE RATE",    CU_NONE,   &v.tone_rate),
#endif
    U32("HEARD POS",    CU_MS,     &v.heard_pos_ms),
    U32("HEARD VALID",  CU_NONE,   &v.heard_valid),
    U32("SWAPS",        CU_NONE,   &v.swaps),
    U32("ACCT ERR",     CU_NONE,   &v.acct_err),
    { "LAST DELTA",   CK_I32,   CU_PAIRS, &v.last_delta },
    U32("ACCT BAD",     CU_NONE,   &v.acct_bad),
    U32("RING FILL",    CU_PAIRS,  &v.ring_fill),
    U32("RING MIN",     CU_PAIRS,  &v.ring_min),
    U32("RING MAX",     CU_PAIRS,  &v.ring_max),
    U32("UNDER RING",   CU_NONE,   &v.under_ring),          /* the reader fell behind: must stay 0 */
    U32("DRY",          CU_NONE,   &v.dry),                 /* the producer fell behind */
    U32("PUT STALLS",   CU_NONE,   &v.put_stalls),
    U32("PUT MAX",      CU_CYC,    &v.put_max_cyc),
    U32("TL OVERFLOW",  CU_NONE,   &v.tl_overflow),
    U32("GOV MODE",     CU_NONE,   &v.gov_mode),
    U32("GOV SHED",     CU_NONE,   &v.gov_n1),
    U32("GOV AUDIO",    CU_NONE,   &v.gov_n2),
    U32("GOV PANIC",    CU_NONE,   &v.gov_n3),
#ifndef P2_MP3PLAY
    U32("TONE STALLS",  CU_NONE,   &v.tone_stalls),
#endif
    { "CAP RING",     CK_U8,    CU_NONE,  &caps.ring },
    { "CAP GAIN",     CK_U8,    CU_NONE,  &caps.gain },
    { "CAP CYMO",     CK_U8,    CU_NONE,  &caps.cymo },
    { "CAP HALCYON",  CK_U8,    CU_NONE,  &caps.halcyon },
    { "CAP POLY",     CK_U8,    CU_NONE,  &caps.poly },
    { "CAP LPC",      CK_U8,    CU_NONE,  &caps.lpc },
    { "CAP WAVE",     CK_U8,    CU_NONE,  &caps.wave },
    { "CAP SPEC",     CK_U8,    CU_NONE,  &caps.spec },
    { "CAP DBUF",     CK_U8,    CU_NONE,  &caps.dbuf },
    { "CAP UNDER",    CK_U8,    CU_NONE,  &caps.under },
    U32("HW UNDER EV",  CU_NONE,   &v.hw_under_ev),          /* P6: runs of output ticks that found the FIFO empty (free-running since reset; the polling counters UNDER RING / DRY cannot see a short one) */
    U32("HW UNDER TK",  CU_NONE,   &v.hw_under_tk),          /* each is one lost sample pair */
    U32("UPTIME",      CU_NONE,   &v.uptime_s),
    U32("LOOPS",       CU_NONE,   &v.loops),
    U32("LOOP MAX",    CU_CYC,    &v.loop_max_cyc),
    U32("KEYS",        CU_NONE,   &v.keys),
    U32("RING CNT",    CU_PAIRS,  &v.ring_cnt),
    U32("RING WPTR",   CU_NONE,   &v.ring_wptr),
    { "RING CTRL",    CK_HEX32, CU_NONE,  &v.ring_ctrl },
    U32("RING HOLD",    CU_NONE,   &v.ring_hold),            /* RING_CTRL read: bit 31 present, 3 hold, 2 busy, 1 empty (read index == write index), 0 enable */
    U32("GAIN CUR",    CU_NONE,   &v.gain_cur),
    { "CYMO STATUS",  CK_HEX32, CU_NONE,  &v.cymo_st },
    U32("STACK PEAK",  CU_BYTES,  &v.stack_peak),
    U32("IMAGE",       CU_BYTES,  &v.image_b),
    U32("FREE",        CU_BYTES,  &v.free_b),
};
const uint32_t counters_count = sizeof counters_table / sizeof counters_table[0];

void counters_init(void) { mc_update(&uptime_clk, cycles(), CLK_HZ); }

void counters_refresh(void)
{
    {   /* the audio side: one writer here, read through the table */
        extern uint32_t counters_aout_init;
        aout_snap_t s = aout_snap(); const aout_stats_t *a = &aout_stats;
        v.aout_init = counters_aout_init;
        v.tone_state = !tone.playing ? 0u : tone.held ? 2u : 1u; v.tone_rate = tone.rate;
        v.heard_seg = tone.cur_valid ? tone.cur_gen : 0u; v.heard_valid = tone.cur_valid;
        v.heard_pos_ms = tone.cur_valid && tone.rate ? (uint32_t)(((uint64_t)tone.cur_pos * 1000u) / tone.rate) : 0u;
        v.put_seg = tone.gen; v.swaps = tone_tl.swaps; v.acct_err = tone_tl.acct_err; v.acct_bad = tone.acct_bad; v.last_delta = tone_tl.last_delta;
        v.tl_overflow = tone_tl.overflow; v.tl_empty = tone_tl.empty; v.tl_flushes = tone_tl.flushes;
        v.ring_fill = aout_fill(&s); v.ring_min = a->ring_min == 0xFFFFFFFFu ? 0u : a->ring_min; v.ring_max = a->ring_max;
        v.under_ring = a->under_ring; v.dry = a->dry; v.put_stalls = a->put_stalls; v.put_max_cyc = a->put_max_cyc; v.put_pairs = a->put_pairs; v.put_blocks = a->put_blocks;
        v.gov_mode = tone.gov_mode; v.gov_n1 = tone_gov.entered[1]; v.gov_n2 = tone_gov.entered[2]; v.gov_n3 = tone_gov.entered[3]; v.tone_stalls = tone.stalls;
    }
    v.uptime_s = mc_update(&uptime_clk, cycles(), CLK_HZ);
    v.loops = counters_loops; v.loop_max_cyc = counters_loop_max_cyc; v.keys = counters_keys;
    uint32_t st = REG(R_PCM_ST);
    v.pcm_level = PCM_LEVEL(st); v.pcm_flags = (st >> 16) & 7u;
    v.ring_cnt = REG(R_RING_CNT); v.ring_ridx = REG(R_RING_RIDX); v.ring_wptr = REG(R_RING_WPTR); v.ring_ctrl = REG(R_RING_CTRL); v.ring_hold = (v.ring_ctrl >> 3) & 1u; v.ring_empty = (v.ring_ctrl >> 1) & 1u;
    { uint32_t u = REG(R_UNDER); v.hw_under_ev = (u >> 16) & 0x7FFFu; v.hw_under_tk = u & 0xFFFFu; }
    v.gain_cur = REG(R_GAIN_CUR); v.cymo_st = REG(R_CYMO_STATUS);
    v.stack_peak = platform_stack_peak(); v.image_b = platform_image_bytes(); v.free_b = platform_free_bytes();
}
