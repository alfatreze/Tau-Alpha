/* Local addition (not part of the upstream Helix drop): a cycle-counting hook
 * for mp3dec.c, mirroring fw/flac.c's FLAC_PROFILE mechanism exactly so the
 * two decoders can be compared on the same instrument.
 *
 * Three buckets, not five:
 *   - UnpackScaleFactors is folded into the Huffman bucket. It is on the same
 *     bitstream-parsing side as DecodeHuffman and small next to it, and
 *     Phase F step 1 only needs "bit reading" as one number, the way FLAC's
 *     R already is.
 *   - Alias reduction is folded into the IMDCT bucket. IMDCT() does it
 *     inline, before the transform, in the same function call -- splitting
 *     it apart would mean editing the transform, which this measurement
 *     does not need to do.
 *
 * Present when mp3dec.c is built with MP3_PROFILE=1 (default 0; must be 0 in
 * a shipped build -- see the guard in mp3dec.c). Firmware points mp3_tick at
 * its cycle counter (R_CYCLES via player.c's cycles()); leave it null to
 * disable, same convention as flac_tick. */
#ifndef MP3_PROFILE_H
#define MP3_PROFILE_H

#include <stdint.h>

/* MPROF_T0()/MPROF_ADD(A, AT): shared by mp3dec.c and imdct.c (B-345) so both use one definition
 * instead of two copies drifting. Two targets, one tick() read: the per-second screen counter (A)
 * and the Check-record run total (AT) advance by the SAME delta, so a build with both features on
 * never has them drift apart from being sampled twice. */
#if MP3_PROFILE
#define MPROF_T0()   uint32_t mprof_t0 = mp3_tick ? mp3_tick() : 0u
#define MPROF_ADD(A, AT) do { if (mp3_tick) { uint32_t _mprof_d = mp3_tick() - mprof_t0; (A) += _mprof_d; (AT) += _mprof_d; } } while (0)
#else
#define MPROF_T0()   do {} while (0)
#define MPROF_ADD(A, AT) do {} while (0)
#endif

extern uint32_t (*mp3_tick)(void);
extern uint32_t mp3_huff_cyc;   /* UnpackScaleFactors + DecodeHuffman, both channels    */
extern uint32_t mp3_imdct_cyc;  /* IMDCT() only now -- see B-345 below for the split out of what used to be folded in here */
extern uint32_t mp3_sub_cyc;    /* Subband -- the polyphase synthesis filterbank        */

/* B-345 (docs/AUDIT_TRAIL.md): mp3_imdct_cyc used to fold Dequantize + all of IMDCT() (AntiAlias, both
 * transforms, windowing/overlap-add) into one number -- the same "I" ambiguity B-341's IMDCT scoping
 * pass flagged (docs/MP3_IMDCT_KERNEL_SCOPING.md). Split at the two boundaries cheap enough to
 * instrument without perturbing anything: Dequantize is its own call per granule (mp3dec.c); inside
 * IMDCT() itself, AntiAlias is one call per (gr,ch) and HybridTransform (both transform kinds, the
 * windowing/overlap tail) is the rest -- at most ~32 loop iterations, nowhere near FLAC's unary()
 * call-count problem (thousands/frame), so a plain tick()-around-the-call split is fine here, unlike
 * unary()'s call-counter-plus-calibration approach. mp3_imdct_cyc is now IMDCT() alone (AntiAlias +
 * HybridTransform combined, still comparable to its old meaning minus Dequantize); mp3_alias_cyc and
 * mp3_xform_cyc are the finer split WITHIN it. mp3_alias_cyc + mp3_xform_cyc should sum to very close
 * to mp3_imdct_cyc (the difference is IMDCT()'s own small bookkeeping outside both calls). */
extern uint32_t mp3_dequant_cyc;
extern uint32_t mp3_alias_cyc;
extern uint32_t mp3_xform_cyc;   /* HybridTransform: both transform kinds + windowing/overlap-add, not yet split further */

/* B-088/B-089 (docs/TEST_SUITE_SPEC.md section 11): a SECOND, independent set
 * of accumulators for the Check/QR record (fw/suite.inc's CT_AUD test), so
 * that feature and the UI_SHOW_DECODE_PROFILE screen row above share the
 * instrumented call sites but no mutable state -- the screen row resets
 * every second, these are reset once when a Check audio window starts and
 * read once when it ends. Same fields, same meaning, just not on a clock. */
extern uint32_t mp3_huff_total_cyc;
extern uint32_t mp3_imdct_total_cyc;
extern uint32_t mp3_sub_total_cyc;
extern uint32_t mp3_dequant_total_cyc;
extern uint32_t mp3_alias_total_cyc;
extern uint32_t mp3_xform_total_cyc;

#endif
