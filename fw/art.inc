/* ======================================================================= ART
 * Embedded cover art: locate the ID3 APIC frame, decode the JPEG inside it,
 * and scale the result into the off-screen SDRAM stash the panel blits from.
 *
 * Runs ONCE per track, before prefill -- so the blocking reads it needs cannot
 * starve the decoder, because playback has not started yet. Decoding later,
 * during playback, would put hundreds of milliseconds of SD reads directly in
 * the path that keeps the PCM FIFO fed.
 *
 * picojpeg's REDUCE mode decodes one pixel per 8x8 block, skipping AC
 * dequantisation, IDCT and chroma upsampling entirely. A 1280 px cover
 * collapses to 160x160 -- comfortably above panel size -- for a fraction of the
 * work, and downscaling from there is sharp.
 *
 * But reduce mode was used UNCONDITIONALLY, and below 736 px it lands under the
 * panel: a 455 px cover became 56x56 and was then MAGNIFIED to 92, which is why
 * cover art was the softest thing on screen. Measured across a real library:
 * 300 px -> 37, 455 px -> 56, 1280 px -> 160.
 *
 * So the mode is now chosen per image, and the choice pays for itself. Full
 * decode costs an IDCT per block, making it affordable only on SMALL images --
 * which are precisely the ones reduce mode fails. Anything from 736 px up
 * already reduces to >= 92 and keeps the cheap path, so the expensive path is
 * never taken on an expensive image.
 *
 * Scaling is a box filter over whichever source arrives, not the nearest-pixel
 * pick this used to do. Downscaling by point sampling throws away most of the
 * image and aliases what is left, which would waste the resolution the full
 * decode just paid for.
 */
#include "picojpeg.h"

#define ART_MAX_SRC_W 320u              /* reduce mode: art up to ~2560 px wide */

/* Largest APIC frame we will decode. This is NOT a memory limit -- the image
 * streams through a 4 KB window and is never held whole -- it only bounds how
 * long a track takes to load.
 *
 * It was 384 KB, described as refusing the absurd, and it was silently refusing
 * the GOOD art: measured on this library, a 1494x1497 cover is an 847 KB frame
 * and a 1280x1280 one is comparable. Those are ordinary sizes for high
 * resolution covers, not absurd ones, and the symptom was a track that simply
 * showed no art with no indication why. Large covers also take the CHEAP decode
 * path (they reduce to well above panel size), so the only real cost is reading
 * the bytes off SD. Prefer slower-but-shown over instant-and-missing. */
#define ART_MAX_BYTES (2048u * 1024u)

/* Full decode is capped so the accumulator below cannot overflow -- see the
 * bound there. 1024 is far above the sizes that ever need this path. */
#define ART_FULL_MAX  1024u

/* Destination rows held in flight by the box filter. One MCU row spans at most
 * 16 source rows, which map to at most 16 destination rows when the source only
 * just exceeds the panel, plus one row that straddles the boundary into the
 * next MCU row and so cannot be emitted yet. 20 leaves margin over that 17.
 *
 * Accumulator width bound: each destination pixel sums at most
 * ceil(1024/92)^2 = 144 source pixels of max value 255 = 36,720, inside
 * uint16. ART_FULL_MAX is what holds that true -- do not raise one without
 * the other. */
#define ART_ACC_ROWS  20u

static uint16_t art_acc[ART_ACC_ROWS][ART_IMG][3] ART_PSRAM;

#define ART_ERR_PSRAM  0xE1u
#if TAU_ART_PSRAM
/* The accumulator lives at 0xA4000000.. (PSRAM window). On a bitstream without the window
 * that address is not RAM, so no store may be issued until it is proven: the PSRAM
 * expansion ID must read back, the build must report the CPU window present, and the first
 * and last word of the section must hold two patterns. Runs once; the verdict is kept.
 * Failure leaves cover art off for the session (art_decode returns 0, code ART_ERR_PSRAM). */
#define ART_PS_ID      0x80000088u
#define ART_PS_CFG     0x800000A0u
#define ART_PS_ID_VAL  0x50535231u
static uint8_t  art_ps_state;                    /* 0 untested, 1 proven, 2 failed */
static uint32_t art_ps_rd __attribute__((unused));   /* cycles for one window read */

static int art_psram_prove(void)
{
    static const uint32_t pat[2] = { 0xA5C33C5Au, 0x5A3CC3A5u };
    if (REG(ART_PS_ID) != ART_PS_ID_VAL) return 0;
    if (!((REG(ART_PS_CFG) >> 16) & 1u)) return 0;
    volatile uint32_t *w0 = (volatile uint32_t *)(uintptr_t)&art_acc[0][0][0];
    volatile uint32_t *w1 = (volatile uint32_t *)((uintptr_t)&art_acc[0][0][0] + sizeof(art_acc) - 4u);
    uint32_t t = cycles();
    *w0 = pat[0];
    uint32_t got = *w0;
    art_ps_rd = cycles() - t;
    if (got != pat[0]) return 0;
    for (uint32_t k = 0; k < 2u; k++) {
        *w0 = pat[k]; *w1 = ~pat[k];
        if (*w0 != pat[k] || *w1 != ~pat[k]) return 0;
    }
    return 1;
}

static int art_psram_ready(void)
{
    if (!art_ps_state) art_ps_state = art_psram_prove() ? 1u : 2u;
    return art_ps_state == 1u;
}
#else
#define art_psram_ready() 1
#endif
/* B-567: the embedded-JPEG fallback is rare (TIM1 covers are the normal path), so its low-traffic work buffers live in PSRAM
 * (.psram_state, uninitialised: every one is written before it is read, proved by the poisoned-buffer run of
 * sim/test_art_decode.py) instead of hot RAM. They are only touched after art_psram_ready() has proved the window. The
 * high-traffic ones stay hot: picojpeg's MCU and coefficient buffers take 20+ M accesses on a 1024 px cover, which at about
 * 48 cycles per uncached access would add 15+ s to the load. Measured accesses and the sums in docs/features/RAM_BSS_AUDIT.md. */
#if TAU_ART_PSRAM
#define ART_COLD_BSS __attribute__((section(".psram_state.art")))
#else
#define ART_COLD_BSS
#endif
static uint16_t art_rowcnt[ART_IMG] ART_COLD_BSS;    /* source rows feeding each dest row */
static uint16_t art_colcnt[ART_IMG] ART_COLD_BSS;    /* source cols feeding each dest col */
static uint16_t art_line[ART_IMG] ART_COLD_BSS;      /* the row currently being emitted   */

/* Source coordinate -> accumulator cell, precomputed.
 *
 * These replace `sx * aw / sw` in the innermost loop, which ran ONCE PER SOURCE
 * PIXEL: 207,025 divisions for a 455 px cover, and VexRiscv takes ~34 cycles for
 * a 32-bit divide -- about 117 ms, on its own more than the IDCT that full
 * decode was added for. The row map absorbs the `% ART_ACC_ROWS` too, a second
 * divide by a non-power-of-two. Both maps depend only on the axis, so the whole
 * cost collapses into sw+sh divisions done once. */
static uint8_t art_xmap[ART_FULL_MAX] ART_COLD_BSS;  /* sx -> accumulator column   */
static uint8_t art_yslot[ART_FULL_MAX] ART_COLD_BSS; /* sy -> accumulator row slot */
static uint32_t art_jpg_pos, art_jpg_end;   /* file offsets of the image data */

/* Why the last art_decode() gave up, as picojpeg reported it. 0 means either
 * success or no picture to decode at all -- the two cases nobody needs telling
 * about. Kept because the answer was already known and thrown away: a
 * progressive cover comes back as PJPG_UNSUPPORTED_MODE and the user saw an
 * empty frame with no reason for it. */
static uint8_t art_fail_code;
#define ART_ERR_NOFRAME 0xF0u   /* B-073: a valid ID3 tag was present but no APIC/FLAC picture frame was found in it */
#define ART_ERR_LOOP    0xF1u   /* the frame walk fell off the end of the tag before finding one */
static uint32_t art_win_len, art_win_pos;   /* window currently held in tagbuf */

/* picojpeg asks for at most 255 bytes at a time. Serving each of those with its
 * own target read would be hundreds of SD commands per image, so a 4 KB window
 * is refilled instead and the callback served from that. */
static unsigned char art_need_bytes(unsigned char *pBuf, unsigned char buf_size,
                                    unsigned char *pBytes, void *d)
{
    (void)d;
    uint32_t n = 0;
    while (n < buf_size) {
        if (art_win_pos >= art_win_len) {
            if (art_jpg_pos >= art_jpg_end) break;
            uint32_t len = art_jpg_end - art_jpg_pos;
            if (len > TAG_SIZE) len = TAG_SIZE;
            if (!target_read_slot(MP3_SLOT_ID, art_jpg_pos, TAG_OFF, len)) break;
            art_jpg_pos += len;
            art_win_len = len;
            art_win_pos = 0;
        }
        pBuf[n++] = tagbuf[art_win_pos++];
    }
    *pBytes = (unsigned char)n;
    return n ? 0 : PJPG_STREAM_READ_ERROR;
}

/* Walk the tag's frame list looking for APIC. Reads each 10-byte frame header
 * on its own and skips by the declared size rather than scanning a buffer:
 * cover art is often hundreds of KB, so the frame we want usually sits well
 * past anything worth holding in RAM. */
COLD_FN2 static int art_find_apic(uint32_t tag_len)
{
    if (tag_len < 20u) return 0;
    if (!target_read_slot(MP3_SLOT_ID, 0, TAG_OFF, 16u)) return 0;
    if (tagbuf[0] != 'I' || tagbuf[1] != 'D' || tagbuf[2] != '3') return 0;
    uint8_t major = tagbuf[3];

    uint32_t p = 10;
    while (p + 10u <= tag_len) {
        if (!target_read_slot(MP3_SLOT_ID, p, TAG_OFF, 16u)) return 0;
        if (tagbuf[0] == 0) { art_fail_code = ART_ERR_NOFRAME; return 0; }  /* padding reached, no picture frame */

        uint32_t fsize = (major >= 4)
            ? (((uint32_t)(tagbuf[4] & 0x7Fu) << 21) | ((uint32_t)(tagbuf[5] & 0x7Fu) << 14) |
               ((uint32_t)(tagbuf[6] & 0x7Fu) << 7)  |  (uint32_t)(tagbuf[7] & 0x7Fu))
            : (((uint32_t)tagbuf[4] << 24) | ((uint32_t)tagbuf[5] << 16) |
               ((uint32_t)tagbuf[6] << 8)  |  (uint32_t)tagbuf[7]);
        if (!fsize || p + 10u + fsize > tag_len) { art_fail_code = ART_ERR_LOOP; return 0; }

        if (tagbuf[0] == 'A' && tagbuf[1] == 'P' &&
            tagbuf[2] == 'I' && tagbuf[3] == 'C') {
            if (fsize > ART_MAX_BYTES) { art_fail_code = ART_ERR_NOFRAME; return 0; }          /* refuse the absurd */

            /* Payload: encoding, MIME (NUL-terminated), picture type,
             * description (NUL-terminated; two NULs when UTF-16), then image. */
            uint32_t body = p + 10u;
            uint32_t win  = fsize < 256u ? fsize : 256u;
            if (!target_read_slot(MP3_SLOT_ID, body, TAG_OFF, win)) return 0;

            uint8_t  enc  = tagbuf[0];
            uint32_t i    = 1;
            uint32_t mime = i;
            while (i < win && tagbuf[i]) i++;

            /* picojpeg is baseline JPEG only, and PNG cover art is common
             * enough that checking the MIME beats feeding a PNG to it. */
            int jpg = 0;
            for (uint32_t k = mime; k + 2u < i; k++) {
                uint8_t a = tagbuf[k] | 0x20u, b2 = tagbuf[k+1] | 0x20u,
                        c = tagbuf[k+2] | 0x20u;
                if (a == 'j' && b2 == 'p' && (c == 'e' || c == 'g')) { jpg = 1; break; }
            }
            if (!jpg) { art_fail_code = ART_ERR_NOFRAME; return 0; }

            i++;                                     /* past the MIME NUL     */
            i++;                                     /* picture type byte     */
            if (enc == 1u || enc == 2u) {            /* UTF-16 description    */
                while (i + 1u < win && (tagbuf[i] || tagbuf[i+1])) i += 2;
                i += 2;
            } else {
                while (i < win && tagbuf[i]) i++;
                i++;
            }
            if (i >= win) { art_fail_code = ART_ERR_LOOP; return 0; }

            art_jpg_pos = body + i;
            art_jpg_end = p + 10u + fsize;
            art_win_len = 0;
            art_win_pos = 0;
            return 1;
        }
        p += 10u + fsize;
    }
    art_fail_code = ART_ERR_NOFRAME;   /* walked to the end of the tag: no APIC frame in it */
    return 0;
}

/* One destination row, run-length encoded: adjacent pixels of downscaled cover
 * art are very often identical, and a run costs one engine command whatever
 * its length. */
static void art_emit_row(const uint16_t *line, uint32_t aw, uint32_t dy)
{
    uint32_t run_x = 0;
    uint16_t run_c = line[0];
    for (uint32_t dx = 1; dx <= ART_IMG; dx++) {
        uint16_t c = (dx < ART_IMG) ? line[dx * aw / ART_IMG]
                                    : (uint16_t)(run_c ^ 1u);
        if (c != run_c) {
            fb_rect(ART_PAD + run_x, ART_STASH_Y + ART_PAD + dy,
                    dx - run_x, 1, run_c);
            run_x = dx;
            run_c = c;
        }
    }
}

/* Last source row feeding accumulator row ay. Rows map by ay = sy*ah/sh, so ay
 * claims every sy with ay*sh <= sy*ah < (ay+1)*sh -- i.e. sy < ceil((ay+1)*sh/ah).
 * Needed because an accumulator row can straddle two MCU rows and must not be
 * emitted half-accumulated: that ships a row averaged from part of its input,
 * which reads as a faint horizontal seam rather than as an obvious fault. */
static uint32_t art_last_sy(uint32_t ay, uint32_t sh, uint32_t ah)
{
    return ((ay + 1u) * sh + ah - 1u) / ah - 1u;
}

/* Average and pack one finished accumulator row, emit every destination row it
 * covers, then clear the slot.
 *
 * Counts are separable -- a cell sums exactly rowcnt[ay]*colcnt[ax] source
 * pixels -- so no per-cell counter is needed. Neither count can be zero: the
 * accumulator is never larger than the source on either axis, so the map onto
 * it is onto. Magnification happens on the way OUT instead, in art_emit_row. */
COLD_FN2 static void art_flush_row(uint32_t ay, uint32_t aw, uint32_t ah)
{
    uint32_t slot = ay % ART_ACC_ROWS;
    uint32_t rc   = art_rowcnt[ay];

    for (uint32_t ax = 0; ax < aw; ax++) {
        uint32_t n = rc * art_colcnt[ax];
        uint32_t r = art_acc[slot][ax][0] / n;
        uint32_t g = art_acc[slot][ax][1] / n;
        uint32_t b = art_acc[slot][ax][2] / n;
        art_line[ax] = (uint16_t)(((r & 0xF8u) << 8) |
                                  ((g & 0xFCu) << 3) | (b >> 3));
        art_acc[slot][ax][0] = art_acc[slot][ax][1] = art_acc[slot][ax][2] = 0u;
    }

    /* Every destination row that samples this accumulator row. One when
     * downscaling; several when the cover is smaller than the panel. */
    uint32_t d0 = (ay * ART_IMG + ah - 1u) / ah;
    uint32_t d1 = ((ay + 1u) * ART_IMG + ah - 1u) / ah;
    if (d1 > ART_IMG) d1 = ART_IMG;
    for (uint32_t dy = d0; dy < d1; dy++)
        art_emit_row(art_line, aw, dy);
}

/* ---- FLAC: METADATA_BLOCK_PICTURE (type 6) -----------------------------
 *
 * Only the FINDER differs from the ID3 path. Both set art_jpg_pos/art_jpg_end
 * to an absolute byte range in the file, and everything downstream -- the
 * windowed reader, picojpeg, the scaler, the stash -- is untouched.
 *
 * The block is all BIG-endian (unlike the Vorbis comment block next to it,
 * which is little-endian; FLAC mixes the two):
 *   u32 picture type, u32 mime len + mime, u32 desc len + desc,
 *   u32 width, height, depth, colours, u32 data len, then the image.
 *
 * Prefers picture type 3 (front cover): the Psychedelic Furs file on the test
 * card carries TWO picture blocks, and taking the first would be a coin toss.
 */
static uint32_t art_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

COLD_FN2 static int art_find_flac_picture(void)
{
    if (!target_read_slot(MP3_SLOT_ID, 0, TAG_OFF, 4u)) return 0;
    if (tagbuf[0] != 'f' || tagbuf[1] != 'L' ||
        tagbuf[2] != 'a' || tagbuf[3] != 'C') return 0;

    uint32_t off = 4u, best_pos = 0, best_end = 0;
    int best_front = 0;

    /* Bounded: a corrupt length must not turn this into an unbounded walk. */
    for (uint32_t guard = 0; guard < 64u; guard++) {
        if (!target_read_slot(MP3_SLOT_ID, off, TAG_OFF, 4u)) break;
        uint32_t last = (uint32_t)(tagbuf[0] >> 7);
        uint32_t type = (uint32_t)(tagbuf[0] & 0x7Fu);
        uint32_t len  = ((uint32_t)tagbuf[1] << 16)
                      | ((uint32_t)tagbuf[2] << 8) | (uint32_t)tagbuf[3];
        uint32_t body = off + 4u;

        if (type == 6u && len > 32u && len <= ART_MAX_BYTES) {
            /* Headers only. 1 KB covers any sane MIME and description, and
             * bounds the read against a 59 KB block. */
            uint32_t win = len < 1024u ? len : 1024u;
            if (!target_read_slot(MP3_SLOT_ID, body, TAG_OFF, win)) break;

            uint32_t ptype = art_be32(&tagbuf[0]);
            uint32_t mlen  = art_be32(&tagbuf[4]);
            uint32_t q     = 8u;

            /* picojpeg is baseline JPEG only, and PNG cover art is common. */
            int jpg = 0;
            if (mlen && q + mlen + 4u <= win) {
                for (uint32_t k = q; k + 2u < q + mlen; k++) {
                    uint8_t a = tagbuf[k] | 0x20u, b = tagbuf[k+1] | 0x20u,
                            c = tagbuf[k+2] | 0x20u;
                    if (a == 'j' && b == 'p' && (c == 'e' || c == 'g')) { jpg = 1; break; }
                }
                q += mlen;

                uint32_t dlen = art_be32(&tagbuf[q]); q += 4u + dlen;
                /* width/height/depth/colours, then the data length. */
                if (jpg && q + 20u <= win) {
                    uint32_t dl = art_be32(&tagbuf[q + 16u]);
                    uint32_t dp = body + q + 20u;
                    if (dl && dl <= ART_MAX_BYTES && (!best_end || (ptype == 3u && !best_front))) {
                        best_pos   = dp;
                        best_end   = dp + dl;
                        best_front = (ptype == 3u);
                    }
                }
            }
        }

        off = body + len;
        if (last) break;
    }

    if (!best_end) return 0;
    art_jpg_pos = best_pos;
    art_jpg_end = best_end;
    art_win_len = 0;
    art_win_pos = 0;
    return 1;
}

/* A cheap fingerprint of the embedded image, without decoding it.
 *
 * MEASURED, which is the only reason this exists: album art is 2801 ms of a
 * 3731 ms FLAC load -- 75% of it -- and every track of an album pays it again.
 * On the test album all twelve tracks carry a byte-identical 259318-byte JPEG,
 * so eleven of those decodes produce a picture the stash already holds.
 *
 * The cost is Huffman decoding 259 KB of entropy data, not scaling: the image
 * is 1200x1200 and already takes picojpeg's 1/8 path, so a smaller panel would
 * save nothing. Not decoding it is the only thing that helps.
 *
 * Length, the HEAD of the image and the TAIL of it.
 *
 * The head alone is not enough, and the reason is worth stating because it is
 * the one way this could show the WRONG cover rather than merely miss. The
 * first kilobyte of a JPEG is nearly all encoder boilerplate -- SOI, the JFIF
 * header, the quantisation tables, the Huffman tables. Two different covers
 * exported by the same tool at the same size and quality have IDENTICAL first
 * kilobytes, and then only the length separates them. Two unrelated photographs
 * compressing to exactly the same byte count is unlikely but it is the kind of
 * unlikely that eventually happens to somebody, and the failure is silent: the
 * previous album's cover, sitting next to the right title.
 *
 * The last 512 bytes are entropy-coded image data, which differs between any
 * two pictures. Sampling both ends costs one extra small read against a 2801 ms
 * decode.
 *
 * Returns 0 when there is no image, which never matches a cached signature. */
COLD_FN2 static uint32_t art_sig_of(uint32_t tag_len)
{
    int found = (track_fmt == FMT_FLAC)
              ? art_find_flac_picture()
              : (tag_len && art_find_apic(tag_len));
    if (!found || art_jpg_end <= art_jpg_pos) return 0;

    uint32_t len = art_jpg_end - art_jpg_pos;
    uint32_t h   = 2166136261u;

    uint32_t win = len < 512u ? len : 512u;
    if (!target_read_slot(MP3_SLOT_ID, art_jpg_pos, TAG_OFF, win)) return 0;
    for (uint32_t i = 0; i < win; i++) { h ^= tagbuf[i]; h *= 16777619u; }

    if (len > win) {
        uint32_t tail = (len - win) < 512u ? (len - win) : 512u;
        if (!target_read_slot(MP3_SLOT_ID, art_jpg_end - tail, TAG_OFF, tail))
            return 0;
        for (uint32_t i = 0; i < tail; i++) { h ^= tagbuf[i]; h *= 16777619u; }
    }

    h ^= len; h *= 16777619u;
    return h ? h : 1u;
}

/* B-735 (gapless G1b): the decode is split into begin / run. art_decode_begin() finds the picture, initialises picojpeg and primes the job; art_decode_run() decodes MCUs until a
 * budget is spent and can be called again later (the state lives in art_job, in PSRAM); art_decode() is begin plus one unlimited run, so the blocking path and the stepped path are
 * the SAME code and sim/test_art_decode.py requires identical stashes for every budget. The caller guarantees the slot still serves the same file between steps (the player compares
 * the 0190 file identity before each step) and that nothing else needs tagbuf's window: it is dropped after every step and the unread part re-read next time. */
typedef struct {
    uint8_t  reduce, mono, running;
    uint16_t my, mx, mcus_row, mcus_col;
    uint32_t next_ay, aw, ah, sw, sh, mcu_w, mcu_h, bxn, byn;
    unsigned char *pr, *pg, *pb;      /* picojpeg's MCU buffers (static in picojpeg) */
    uint32_t t0, sig, last;           /* when the job began (cycles), the cover's signature for the cache, and the cycles the last step took */
} art_job_t;
static art_job_t art_job ART_COLD_BSS;

COLD_FN2 static int art_decode_begin(uint32_t tag_len)
{
    art_job.running = 0u;
    /* Same decoder, different finder. */
    art_fail_code = 0;
    int found = (track_fmt == FMT_FLAC)
              ? art_find_flac_picture()
              : (tag_len && art_find_apic(tag_len));
    if (!found) return 0;                    /* no picture: nothing to explain */
    if (!art_psram_ready()) { art_fail_code = ART_ERR_PSRAM; return 0; }   /* P5: window not proven */
    uint32_t jpg_start = art_jpg_pos;        /* to rewind for a second init */

    pjpeg_image_info_t info;
    {
        unsigned char st = pjpeg_decode_init(&info, art_need_bytes, 0, 1 /* reduce */);
        if (st) { art_fail_code = st; return 0; }
    }

    uint32_t iw = (uint32_t)info.m_width, ih = (uint32_t)info.m_height;
    if (!iw || !ih) return 0;

    /* Take the cheap path only when it genuinely lands at or above panel size
     * on BOTH axes -- otherwise that axis would be magnified. */
    int reduce = (((iw + 7u) / 8u) >= ART_IMG) && (((ih + 7u) / 8u) >= ART_IMG);
    if (!reduce && (iw > ART_FULL_MAX || ih > ART_FULL_MAX))
        reduce = 1;                          /* keeps the accumulator in range */

    if (!reduce) {
        /* Re-init at full resolution. The stream must be rewound to the first
         * byte of the image: decode_init consumed the headers. */
        art_jpg_pos = jpg_start;
        art_win_len = 0;
        art_win_pos = 0;
        unsigned char st = pjpeg_decode_init(&info, art_need_bytes, 0, 0 /* full */);
        if (st) { art_fail_code = st; return 0; }
    }

    uint32_t bxn = (uint32_t)info.m_MCUWidth / 8u;
    uint32_t byn = (uint32_t)info.m_MCUHeight / 8u;
    uint32_t mcu_w = reduce ? bxn : (uint32_t)info.m_MCUWidth;
    uint32_t mcu_h = reduce ? byn : (uint32_t)info.m_MCUHeight;

    /* Clamp to the real image: the last MCU row and column are padded out to a
     * whole MCU, and averaging that padding in would smear the edges. */
    uint32_t sw = (uint32_t)info.m_MCUSPerRow * mcu_w;
    uint32_t sh = (uint32_t)info.m_MCUSPerCol * mcu_h;
    uint32_t rw = reduce ? (iw + 7u) / 8u : iw;
    uint32_t rh = reduce ? (ih + 7u) / 8u : ih;
    if (sw > rw) sw = rw;
    if (sh > rh) sh = rh;
    /* art_xmap/art_yslot are indexed by SOURCE coordinate, so both axes must fit
     * them. sh was previously unguarded -- only sw was checked -- which an
     * absurdly tall image in reduce mode (over 8192 px) would have walked off
     * the end of. Full decode is already gated to ART_FULL_MAX above. */
    if (!sw || !sh || sw > ART_FULL_MAX || sh > ART_FULL_MAX) return 0;
    if (reduce && sw > ART_MAX_SRC_W) return 0;

    /* The accumulator is never larger than the source. Averaging down to
     * min(panel, source) and magnifying on the way out keeps the map onto it
     * surjective -- no empty cells -- and, decisively, BOUNDS how many rows one
     * MCU row can touch. Accumulating straight into 92 rows from a source
     * shorter than 92 lets a single 16-row MCU span 23 rows, which overruns the
     * 20 slots and makes two rows add into each other: silent corruption, not a
     * crash. Caught by tools/art_scale_model.py before it reached hardware. */
    uint32_t aw = (sw < ART_IMG) ? sw : ART_IMG;
    uint32_t ah = (sh < ART_IMG) ? sh : ART_IMG;

    for (uint32_t i = 0; i < ART_IMG; i++) art_rowcnt[i] = art_colcnt[i] = 0u;
    /* Build the coordinate maps and the counts in the same pass: sw+sh divides
     * done once, in place of one divide per source PIXEL in the loop below. */
    for (uint32_t sy = 0; sy < sh; sy++) {
        uint32_t ay = sy * ah / sh;
        art_yslot[sy] = (uint8_t)(ay % ART_ACC_ROWS);
        art_rowcnt[ay]++;
    }
    for (uint32_t sx = 0; sx < sw; sx++) {
        uint32_t ax = sx * aw / sw;
        art_xmap[sx] = (uint8_t)ax;
        art_colcnt[ax]++;
    }
    for (uint32_t s = 0; s < ART_ACC_ROWS; s++)
        for (uint32_t dx = 0; dx < ART_IMG; dx++)
            art_acc[s][dx][0] = art_acc[s][dx][1] = art_acc[s][dx][2] = 0u;
    art_line[0] = 0u;

    art_job.reduce = (uint8_t)reduce; art_job.mono = (uint8_t)(info.m_comps == 1);
    art_job.my = 0; art_job.mx = 0;
    art_job.mcus_row = (uint16_t)info.m_MCUSPerRow; art_job.mcus_col = (uint16_t)info.m_MCUSPerCol;
    art_job.next_ay = 0; art_job.aw = aw; art_job.ah = ah; art_job.sw = sw; art_job.sh = sh;
    art_job.mcu_w = mcu_w; art_job.mcu_h = mcu_h; art_job.bxn = bxn; art_job.byn = byn;
    art_job.pr = info.m_pMCUBufR; art_job.pg = info.m_pMCUBufG; art_job.pb = info.m_pMCUBufB;
    art_job.running = 1u;
    return 1;
}

/* Decode MCUs until max_mcus (0 = no limit) or budget_cycles (0 = no limit) is spent, at least one per call. Returns 0 = more to do, 1 = finished (the stash holds the cover),
 * -1 = failed (art_fail_code says why). Between calls the window of tagbuf is given back (the unread part is re-read next time). */
COLD_FN2 static int art_decode_run(uint32_t max_mcus, uint32_t budget_cycles)
{
    if (!art_job.running) return -1;
    const art_job_t j = art_job;                 /* one PSRAM read; the loop works on the copy */
    const uint32_t reduce = j.reduce, bxn = j.bxn, byn = j.byn, mcu_w = j.mcu_w, mcu_h = j.mcu_h;
    const uint32_t sw = j.sw, sh = j.sh, aw = j.aw, ah = j.ah;
    const int mono = j.mono;
    unsigned char *const pr = j.pr, *const pg = j.pg, *const pb = j.pb;
    uint32_t next_ay = j.next_ay, n = 0;
    const uint32_t t0 = budget_cycles ? cycles() : 0u;
    uint32_t my = j.my, mx = j.mx;
    while (my < j.mcus_col) {
        if (n && ((max_mcus && n >= max_mcus) || (budget_cycles && (uint32_t)(cycles() - t0) >= budget_cycles))) {
            art_job.my = (uint16_t)my; art_job.mx = (uint16_t)mx; art_job.next_ay = next_ay;
            art_jpg_pos -= art_win_len - art_win_pos; art_win_len = 0; art_win_pos = 0;   /* give the window back */
            return 0;
        }
        {
            unsigned char st = pjpeg_decode_mcu();
            if (st) { art_fail_code = st; art_job.running = 0u; return -1; }
        }
            for (uint32_t by = 0; by < byn; by++) {
                for (uint32_t bx = 0; bx < bxn; bx++) {
                    /* Blocks sit in a fixed 2x2 grid of 64-byte slots, so a
                     * block ROW is always 128 apart regardless of how many
                     * blocks are actually across. This was (by*bxn + bx)*64,
                     * which agrees for H1V1/H2V1/H2V2 but puts H1V2's second
                     * block at 64 where picojpeg writes it to 128 (confirmed in
                     * picojpeg.c: copyY(0) then copyY(128)) -- so on H1V2 art
                     * the bottom half of every MCU read a block nothing had
                     * written. Latent before; full decode reads all 64 bytes of
                     * each block, which would have made it obvious. */
                    uint32_t blk = by * 128u + bx * 64u;
                    /* Reduce mode leaves ONE valid pixel per block, at the
                     * block's base; full decode fills all 64, row-major. */
                    uint32_t pn = reduce ? 1u : 8u;
                    for (uint32_t j = 0; j < pn; j++) {
                        uint32_t sy = reduce ? (my * byn + by)
                                             : (my * mcu_h + by * 8u + j);
                        if (sy >= sh) break;
                        /* Hoisted: the accumulator row, the row base pointer and
                         * the block's row base are all constant across the pixel
                         * loop, which is the only loop here that runs per pixel. */
                        uint16_t (*arow)[3] = art_acc[art_yslot[sy]];
                        uint32_t obase = blk + (reduce ? 0u : j * 8u);
                        uint32_t sxbase = reduce ? (mx * bxn + bx)
                                                 : (mx * mcu_w + bx * 8u);
                        for (uint32_t i = 0; i < pn; i++) {
                            uint32_t sx = sxbase + (reduce ? 0u : i);
                            if (sx >= sw) break;
                            uint32_t o = obase + (reduce ? 0u : i);
                            uint8_t r = pr[o];
                            uint8_t g = mono ? r : pg[o];
                            uint8_t b = mono ? r : pb[o];
                            uint16_t *cell = arow[art_xmap[sx]];
                            cell[0] += r;
                            cell[1] += g;
                            cell[2] += b;
                        }
                    }
                }
            }
        n++;
        if (++mx == (uint32_t)j.mcus_row) {
            mx = 0;
            /* Emit every accumulator row whose last source row has now arrived. */
            uint32_t sy_done = (my + 1u) * mcu_h;
            if (sy_done > sh) sy_done = sh;
            while (next_ay < ah && art_last_sy(next_ay, sh, ah) < sy_done)
                art_flush_row(next_ay++, aw, ah);
            my++;
        }
    }
    while (next_ay < ah) art_flush_row(next_ay++, aw, ah);
    art_job.running = 0u;

    /* Deliberately does NOT derive an accent from the cover. That was tried:
     * the accent then changed on every track, which read as inconsistency
     * rather than as a feature. One fixed accent everywhere. */
    return 1;
}

/* The blocking decode: begin, then everything in one run. */
COLD_FN2 static int art_decode(uint32_t tag_len)
{
    if (!art_decode_begin(tag_len)) return 0;
    return art_decode_run(0u, 0u) == 1;
}

#ifndef ART_NO_JOB          /* the host harness (tools/host/art_decode_harness.c) has no player around it: it tests begin/run only */
/* ---- B-735 (gapless G1b): the cover decodes in the background -------------------------------------------------------------------------------------------------------
 * art_job_launch() is what load_track() calls instead of the blocking decode: it finds the picture and primes the job (begin), then returns at once. art_job_tick() runs from
 * ui_draw_dynamic_cold(), once per decoded frame (MP3) or UI period (FLAC), and decodes MCUs for a few ms when it is safe: the audio FIFO holds at least ART_JOB_MIN_PAIRS,
 * no load or reload is in progress, no overlay holds the draw engine (the stash rows are written with engine commands, which an overlay drops), and no asynchronous audio read is
 * pending (the job's own reads are synchronous and drain it first, but there is no reason to queue behind it). Anything that can switch the slot's file clears art_job_on
 * (load_track, the 0192 open in pl_cmd); a reload request is also checked here. A step costs at most the budget, one MCU and one 4 KB window read (about 13 ms worst), against a FIFO
 * of at least 23 ms. When the job ends the stash holds the cover: it is rounded, drawn and cached under its signature exactly as the blocking path did. */
#define ART_JOB_MIN_PAIRS  1000u                       /* a frame's decode and UI (about 17 ms) plus margin must still fit after a step */
#define ART_JOB_MAX_NEED   1900u                       /* never ask for more than the FIFO normally holds, or a slow step would stop the job for good */
#define ART_JOB_BUDGET     (CLK_HZ / 333u)             /* 3 ms of decoding per step (plus one MCU and the 4 KB window read the step must repeat) */
COLD_FN2 static int art_job_launch(uint32_t tag_len, uint32_t sig)
{
    if (!art_decode_begin(tag_len)) return 0;
    art_job.t0 = cycles(); art_job.sig = sig; art_job.last = 0u;
    art_job_on = 1u;
    return 1;
}
COLD_FN2 static void art_job_tick(void)
{
    if (reload_pending || reload_armed || ui_loader_on || FB_HELD() || rd_pending) return;
    /* B-735 (DEV 119 hardware): a fixed 1,024-pair floor let a step end with 6 ms of audio left and the next frame's decode underrun it (24 FIFO stalls in a 5 s cover job).
     * The floor is now the frame time plus what the LAST step actually cost (its window re-read varies from 5 to 13 ms), so a slow step makes the next one wait for a fuller FIFO. */
    const uint32_t hz = track_hz ? track_hz : 44100u;
    uint32_t need = ART_JOB_MIN_PAIRS + (art_job.last / (CLK_HZ / 1000u)) * hz / 1000u;
    if (need > ART_JOB_MAX_NEED) need = ART_JOB_MAX_NEED;
    if (pcm_level() < need) return;
    const uint32_t t_step = cycles();
    const int r = art_decode_run(0u, ART_JOB_BUDGET);
    art_job.last = cycles() - t_step;
    if (r == 0) return;
    art_job_on = 0u;
    ld_art = LD_MS(cycles() - art_job.t0);           /* LOAD MS' cover field now shows the whole background decode */
    if (r == 1) {
        art_sig = art_job.sig;
        ui_art_round();
        art_ready = 1;
        if (art_shown) ui_art_draw();
    } else {                                          /* present and unreadable (the blocking path's `art_bad` branch) */
        art_bad = 1; art_bad_code = art_fail_code; art_bad_sig = art_job.sig; art_have = 0;
        ui_art_mount();
        ui_art_reason(art_bad_code == PJPG_UNSUPPORTED_MODE);
        ui_art_round();
        ui_toast_msg(art_bad_code == PJPG_UNSUPPORTED_MODE ? "COVER: PROGRESSIVE JPEG" : "COVER: CANNOT BE READ");
        if (art_shown) ui_art_draw();
    }
}
#endif /* ART_NO_JOB */
