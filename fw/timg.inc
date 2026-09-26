/* ==================================================================== TIM1 cover reader (TAU_ART_TIMG), B-285
 * Shows a pre-converted cover (<album>/tau-art/cover_128.pal256.timg, written by tools/sync_media.py --art-variants) instead of
 * decoding the track's embedded JPEG (2.6 to 15.8 s). Design, memory map, error handling and cost: docs/COVER_TIMG_READER.md.
 *
 *   1. open the file by absolute path into data slot 7 (a new slot, same 0192 mechanism as the playlist slot)
 *   2. read the 16-byte header, validate; read the 256-entry RGB565 CLUT straight into the draw engine's CLUT
 *   3. stream the index bytes (4 KB at a time through the tag buffer) into an INDEX PLANE in off-screen framebuffer rows through
 *      the SDRAM mailbox, two pixels per 32-bit write (the CPU window cannot reach these rows)
 *   4. OP_CBLIT the plane (through the CLUT) into the art panel in the art stash
 * All cold code. Nothing here writes the sticky blit fields. Guarded by its own known-answer probe, so a bitstream without the
 * blit engine or a broken mailbox falls back to the embedded cover. Default OFF. */
#include "timg_core.h"

#define TIMG_SLOT_ID   7u
#define TIMG_PLANE_Y   (ART_STASH_Y + ART_H + THUMB_LIVE_SLOTS * 32u)    /* 128 rows, after the compacted thumbnail stash: ends at 984 */
#define TIMG_PROBE_ROW 1023u                                            /* the same probe cell fw/chladni.inc uses (rows 984..1023 are its) */
#define TIMG_PROBE_DX  496u
#define TIMG_PROBE_DY  2u

static uint8_t  timg_ok;             /* 1 = the mailbox half order is known (the probe passed); anything else is probed again */
static uint8_t  timg_swap;
static uint8_t  timg_last_err;       /* TIMG_E_* of the last attempt, for the Info page */
static uint32_t timg_miss_sig;       /* cover path known to be absent/bad: not retried for every track of an album */
static uint16_t timg_w, timg_h;
static uint32_t timg_ms;             /* last load time, ms */
static uint32_t timg_loaded_n, timg_fail_n;   /* covers shown from a TIM1 file / attempts that fell back (Info > TIM1 COVER) */

static int timg_mb_write(uint32_t word_addr, uint32_t data)
{
    REG(R_SDR_ADDR) = word_addr; REG(R_SDR_DATA) = data;
    REG(R_SDR_CTRL) = 0x3Fu;
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u) if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    return 0;
}
static int timg_mb_read(uint32_t word_addr, uint32_t *out)
{
    REG(R_SDR_ADDR) = word_addr; REG(R_SDR_CTRL) = 0x3Du;
    uint32_t t = cycles();
    while ((uint32_t)(cycles() - t) < 128u) { }
    while (REG(R_SDR_STATUS) & 1u) if ((uint32_t)(cycles() - t) > CLK_HZ / 4u) return -1;
    *out = REG(R_SDR_RDATA);
    return 0;
}

/* Known-answer probe (see chl_probe in fw/chladni.inc): proves the mailbox half order against the engine's own addressing.
 *
 * B-325 (the reader had never loaded a cover on a Pocket: Info showed E6 on every run): the probe used to read the destination a fixed
 * 512 cycles after queueing the copy. That is enough in steady state (Chladni's own probe runs with an empty command queue) but a track
 * load has just queued a burst of chrome drawing ahead of the probe's copy, and fb_wait() only says the command was ACCEPTED. So the read
 * came back before the copy landed, the probe failed, and the failure was remembered for the whole session. Now: the destination is
 * pre-loaded with a sentinel, the read is polled until it changes (up to about 50 ms) and only then judged; a failure is no longer
 * remembered (the next track probes again) and says why. Results: 0 ok, TIMG_E_ENGINE no blit engine, TIMG_E_MAILBOX the mailbox timed out,
 * TIMG_E_PROBE the copy never landed or landed with an unexpected value (timg_probe_raw holds what was read). */
static uint32_t timg_probe_raw;
static uint8_t  timg_probe_fails;    /* consecutive probe failures: after 3 the reader stays off for the session, so a bitstream that cannot do it costs three probes, not one per track */

COLD_FN3 static int timg_probe(void)
{
    const uint32_t src = TIMG_PROBE_ROW * FB_STRIDE, dst = TIMG_PROBE_DY * FB_STRIDE + TIMG_PROBE_DX;
    const uint32_t sentinel = 0xA5C33C5Au;
    uint32_t r = 0;
    if (!BLIT_READY()) return TIMG_E_ENGINE;
    if (timg_mb_write(src, 0xF80007E0u) || timg_mb_write(dst, sentinel)) return TIMG_E_MAILBOX;
    fb_blit(0u, TIMG_PROBE_ROW, TIMG_PROBE_DX, TIMG_PROBE_DY, 1u, 1u);
    fb_wait();
    const uint32_t t0 = cycles();
    do {
        uint32_t t = cycles();
        while ((uint32_t)(cycles() - t) < 512u) { }
        if (timg_mb_read(dst, &r)) return TIMG_E_MAILBOX;
        if ((r & 0xFFFFu) != (sentinel & 0xFFFFu)) break;               /* the copy has landed (the low half changed) */
    } while ((uint32_t)(cycles() - t0) < CLK_HZ / 20u);                  /* about 50 ms */
    timg_probe_raw = r;
    if ((r & 0xFFFFu) == 0x07E0u)  { timg_swap = 0u; timg_ok = 1u; return 0; }
    if ((r >> 16) == 0xF800u)      { timg_swap = 1u; timg_ok = 1u; return 0; }
    return TIMG_E_PROBE;
}

/* Load `path` (absolute, NUL-terminated) into the index plane and the CLUT. Returns TIMG_OK or a TIMG_E_* code; on any error
 * nothing has been drawn. */
COLD_FN3 static int timg_load(const char *path)
{
    const uint32_t t0 = cycles();
    blit_probe_ensure();
    if (timg_ok != 1u) {
        if (timg_probe_fails >= 3u) return TIMG_E_ENGINE;                 /* gave up for this session */
        int pe = timg_probe();
        if (pe) { timg_probe_fails++; return pe; }                       /* not remembered as a bad file: the next track probes again (up to 3 times) */
        timg_probe_fails = 0u;
    }
    if (!pl_open_into(TIMG_SLOT_ID, path)) return TIMG_E_OPEN;

    timg_hdr_t h;
    if (!target_read_slot(TIMG_SLOT_ID, 0u, TAG_OFF, TIMG_HDR_BYTES)) return TIMG_E_READ;
    int e = timg_parse((const uint8_t *)tagbuf, &h);
    if (e) return e;

    if (!target_read_slot(TIMG_SLOT_ID, TIMG_HDR_BYTES, TAG_OFF, TIMG_CLUT_BYTES)) return TIMG_E_READ;
    fb_wait();
    REG(R_CLUT_IDX) = 0u;
    for (uint32_t i = 0; i < 256u; i++) REG(R_CLUT_DATA) = timg_le16((const uint8_t *)tagbuf + 2u * i);

    const uint32_t total = (uint32_t)h.w * h.h, chunk_max = TAG_SIZE & ~1u;
    uint32_t pos = 0, col = 0, row = 0, pend = 0;
    while (pos < total) {
        uint32_t n = total - pos; if (n > chunk_max) n = chunk_max;
        if (!target_read_slot(TIMG_SLOT_ID, TIMG_HDR_BYTES + TIMG_CLUT_BYTES + pos, TAG_OFF, n)) return TIMG_E_READ;
        for (uint32_t i = 0; i < n; i++) {
            const uint32_t b = tagbuf[i];
            if (!(col & 1u)) pend = b;
            if ((col & 1u) || col + 1u == h.w) {
                const uint32_t w32 = (col & 1u) ? timg_pair(pend, b, timg_swap) : timg_pair(pend, 0u, timg_swap);
                if (timg_mb_write((TIMG_PLANE_Y + row) * FB_STRIDE + (col & ~1u), w32)) return TIMG_E_READ;
            }
            if (++col == h.w) { col = 0u; row++; }
        }
        pos += n;
    }
    timg_w = h.w; timg_h = h.h;
    timg_ms = (uint32_t)(cycles() - t0) / (CLK_HZ / 1000u);
    return TIMG_OK;
}

/* Draw the loaded image, through the CLUT, into the cover area of the art stash (ART_IMG is 128 px now, the size the covers are made at,
 * so nothing is cropped). A smaller image (a non-square cover is scaled to 128 on its LONG side) is centred. */
COLD_FN3 static void timg_draw_art(void)
{
    const uint32_t cw = (timg_w < ART_IMG) ? timg_w : ART_IMG, ch = (timg_h < ART_IMG) ? timg_h : ART_IMG;
    const uint32_t sx = (timg_w - cw) / 2u, sy = (timg_h - ch) / 2u;
    const uint32_t dx = ART_PAD + (ART_IMG - cw) / 2u, dy = ART_STASH_Y + ART_PAD + (ART_IMG - ch) / 2u;
    for (uint32_t x = 0; x < cw; x += 120u) {                        /* a CBLIT is at most 127 words wide */
        const uint32_t w = (cw - x < 120u) ? cw - x : 120u;
        fb_cblit(sx + x, TIMG_PLANE_Y + sy, dx + x, dy, w, ch);
    }
    fb_wait();
}

/* FNV-1a of the cover path for the current LIBRARY track (0 when there is none): the same-album reuse key, so the 13 tracks of an
 * album load the cover once. `out` receives the path. */
COLD_FN3 static uint32_t timg_cover_path(char *out, uint32_t cap)
{
    if (!lib_src || !lib_qn) return 0u;
    char p[LIB_MAX_PATH + 8u];
    if (!lib_path(&lib, LIB_Q[lib_qpos], p, sizeof(p))) return 0u;
    uint32_t last = 0u, n = 0u;
    for (; p[n]; n++) if (p[n] == '/') last = n + 1u;
    static const char tail[] = "tau-art/cover_128.pal256.timg";
    if (!last || last + sizeof(tail) > cap) return 0u;
    for (uint32_t i = 0; i < last; i++) out[i] = p[i];
    for (uint32_t i = 0; i < sizeof(tail); i++) out[last + i] = tail[i];
    uint32_t hsh = 2166136261u;
    for (uint32_t i = 0; out[i]; i++) { hsh ^= (uint8_t)out[i]; hsh *= 16777619u; }
    return hsh ? hsh : 1u;
}

/* The art step of load_track(), for a library track: 1 = the cover is in the stash (drawn, panel rounded by the caller),
 * 0 = use the embedded-cover path. `sig` is the reuse key (timg_cover_path). */
COLD_FN3 static int timg_cover(uint32_t sig, const char *path)
{
    if (sig == timg_miss_sig) return 0;
    /* The cover is drawn into the OFF-SCREEN art stash, so it is safe while a menu or fullscreen is up; without this exemption every engine
     * command below is dropped when FB_HELD() is true (B-325). */
    const uint8_t ov_saved = ov_draw; ov_draw = 1u;
    /* B-328: NOT mounted here. ui_art_mount() wipes the stash, and a failed attempt returns 0 so the caller falls back to the embedded-cover
     * path, whose 'same picture' reuse test then trusted a stash this function had just destroyed (covers vanished after the first JPEG
     * cover, the B-075 symptom again). Mount only once the image is loaded and about to be drawn. */
    int e = timg_load(path);
    timg_last_err = (uint8_t)e;
    if (e) {
        ov_draw = ov_saved;
        if (e != TIMG_E_ENGINE && e != TIMG_E_MAILBOX && e != TIMG_E_PROBE) timg_miss_sig = sig;   /* only a bad or missing FILE is remembered */
        timg_fail_n++;
        return 0;
    }
    ui_art_mount();
    timg_draw_art();
    ov_draw = ov_saved;
    timg_loaded_n++;
    return 1;
}
