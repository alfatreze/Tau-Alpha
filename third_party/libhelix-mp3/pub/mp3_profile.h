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

extern uint32_t (*mp3_tick)(void);
extern uint32_t mp3_huff_cyc;   /* UnpackScaleFactors + DecodeHuffman, both channels    */
extern uint32_t mp3_imdct_cyc;  /* Dequantize + IMDCT (alias reduction is inside IMDCT) */
extern uint32_t mp3_sub_cyc;    /* Subband -- the polyphase synthesis filterbank        */

/* B-088/B-089 (docs/TEST_SUITE_SPEC.md section 11): a SECOND, independent set
 * of accumulators for the Check/QR record (fw/suite.inc's CT_AUD test), so
 * that feature and the UI_SHOW_DECODE_PROFILE screen row above share the
 * instrumented call sites but no mutable state -- the screen row resets
 * every second, these are reset once when a Check audio window starts and
 * read once when it ends. Same fields, same meaning, just not on a clock. */
extern uint32_t mp3_huff_total_cyc;
extern uint32_t mp3_imdct_total_cyc;
extern uint32_t mp3_sub_total_cyc;

#endif
