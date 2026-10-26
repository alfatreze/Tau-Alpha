/* fw/lame_tag.h -- the LAME/Info tag's encoder delay and end padding (gapless groundwork, parallel plan A4, B-624).
 *
 * The LAME extension follows the Xing/Info header. From the first byte of the encoder string ("LAME4.0 ", 9 bytes) the layout is: +9 revision/VBR method, +10 lowpass, +11..14 peak,
 * +15..18 ReplayGain, +19 flags, +20 bitrate, then THREE bytes holding two 12-bit numbers: encoder delay (+21, +22 high nibble) and end padding (+22 low nibble, +23). A gapless player
 * skips `delay + 529` samples at the start of the file (529 = the decoder's own delay) and drops `padding - 529` at the end. Pure code, host-tested against real files and mutagen. */
#ifndef LAME_TAG_H
#define LAME_TAG_H
#include <stdint.h>

/* ext points at the encoder string; avail is how many bytes are readable. Returns 1 and the two numbers when it looks like a LAME-style extension (four printable characters first). */
static inline int lame_ext_gapless(const uint8_t *ext, uint32_t avail, uint16_t *delay, uint16_t *padding)
{
    if (avail < 24u) return 0;
    for (uint32_t k = 0; k < 4u; k++)
        if (ext[k] < 0x20u || ext[k] > 0x7Eu) return 0;
    *delay   = (uint16_t)(((uint16_t)ext[21] << 4) | (uint16_t)(ext[22] >> 4));
    *padding = (uint16_t)(((uint16_t)(ext[22] & 0x0Fu) << 8) | (uint16_t)ext[23]);
    return 1;
}

/* Host helper (tests and later tools): find the Xing/Info header in the first bytes of the audio (after any ID3v2 tag the caller skipped) and read the two numbers. */
static inline int lame_scan(const uint8_t *buf, uint32_t len, uint16_t *delay, uint16_t *padding)
{
    for (uint32_t i = 0; i + 16u < len && i < 64u; i++) {
        const uint8_t a = buf[i], b = buf[i + 1], c = buf[i + 2], d = buf[i + 3];
        if (!((a == 'X' && b == 'i' && c == 'n' && d == 'g') || (a == 'I' && b == 'n' && c == 'f' && d == 'o'))) continue;
        const uint32_t flags = ((uint32_t)buf[i + 4] << 24) | ((uint32_t)buf[i + 5] << 16) | ((uint32_t)buf[i + 6] << 8) | (uint32_t)buf[i + 7];
        uint32_t e = i + 8u;
        if (flags & 1u) e += 4u;
        if (flags & 2u) e += 4u;
        if (flags & 4u) e += 100u;
        if (flags & 8u) e += 4u;
        if (e + 24u > len) return 0;
        return lame_ext_gapless(buf + e, len - e, delay, padding);
    }
    return 0;
}
#endif
