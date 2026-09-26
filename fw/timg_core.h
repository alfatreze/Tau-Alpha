/* TIM1 cover-image container, portable part (no hardware): header validation and the mailbox word packing. Format and encoder:
 * tools/tau_image.py (docstring), design: docs/COVER_TIMG_READER.md. Tested on the host against real files from the Python
 * encoder (sim/test_tau_timg.py). Only the palette-256 / 8-bit variant is read; anything else is refused with a code. */
#ifndef TIMG_CORE_H
#define TIMG_CORE_H
#include <stdint.h>

#define TIMG_HDR_BYTES   16u
#define TIMG_CLUT_BYTES  512u
#define TIMG_MAX_DIM     128u       /* the plane is 128 rows; a wider image would also not fit one CBLIT (127-word limit) unsplit */

enum { TIMG_OK = 0, TIMG_E_MAGIC = 1, TIMG_E_FORMAT = 2, TIMG_E_SIZE = 3, TIMG_E_OPEN = 4, TIMG_E_READ = 5, TIMG_E_ENGINE = 6 };

typedef struct { uint16_t w, h; uint32_t payload; } timg_hdr_t;

static inline uint32_t timg_le16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static inline uint32_t timg_le32(const uint8_t *p) { return timg_le16(p) | (timg_le16(p + 2) << 16); }

/* b: the first 16 bytes of the file. Returns TIMG_OK or why not. */
static int timg_parse(const uint8_t *b, timg_hdr_t *h)
{
    if (b[0] != 'T' || b[1] != 'I' || b[2] != 'M' || b[3] != '1') return TIMG_E_MAGIC;
    const uint32_t fmt = b[4], bpp = b[5], w = timg_le16(b + 6), ht = timg_le16(b + 8), nc = timg_le16(b + 10), pl = timg_le32(b + 12);
    if (fmt != 2u || bpp != 8u || nc != 256u) return TIMG_E_FORMAT;
    if (!w || !ht || w > TIMG_MAX_DIM || ht > TIMG_MAX_DIM || pl != TIMG_CLUT_BYTES + w * ht) return TIMG_E_SIZE;
    h->w = (uint16_t)w; h->h = (uint16_t)ht; h->payload = pl;
    return TIMG_OK;
}

/* Two neighbouring indices as one 32-bit mailbox word (two 16-bit SDRAM words). `swap` = the mailbox's high half is the LOWER
 * address (probed at runtime, same convention as fw/chladni.inc). A row's odd last pixel is paired with 0. */
static inline uint32_t timg_pair(uint32_t p0, uint32_t p1, uint32_t swap)
{
    return swap ? ((p0 << 16) | p1) : ((p1 << 16) | p0);
}
#endif
