/* fw2/cores/save_probe_core.h -- the self-test of the meter-settings save slot (S1 spike of docs/features/meters/METER_SAVE_FILE_SPEC.md; test: docs/features/meters/METER_SAVE_FILE_TEST.md).
 * Pure logic, host-tested (sim/test_p2_save_probe.py) and run on the real CPU in simulation (sim/fw_save/main.c with sim/tb_save_soc.v): the RAM is reached only through the small
 * accessor struct `svp_io_t`, so the same code runs against a model, against the simulated SoC and on the Pocket.
 *
 * The probe file `tau-save-probe.bin` is 8,192 bytes, zero when the installer creates it. Layout in 32-bit little-endian words (the byte order of the file; the RTL swaps the bridge's big-endian words):
 *   w0 magic 'T','S','A','V' (0x56415354)   w1 version 1   w2 boots (1 on the first boot)   w3 last value written (a hash chain: proves THIS boot's write came back)
 *   w4 seed of the pattern   w5 CRC-32 of the body (bytes 32..8191, zlib.crc32)   w6 wall-clock epoch of that boot (0 if the host's clock had not arrived)   w7 reserved 0
 *   w8..w2047 the pattern svp_pat(seed, i).
 * Every boot: read the header; a zero or unknown file is "first boot" (no verdict on the CRC or the pattern: crc 0 = not checked); otherwise verify the body against its CRC and against
 * the pattern of the seed in the header, then write a NEW seed, pattern, CRC, value, epoch and boot count. The body is written first and the header LAST, so a Quit that tears the update
 * shows up at the next boot as a CRC mismatch instead of a wrong boot count. One Quit and one boot are enough to prove the whole round trip; a Quit in the middle of the self-test is a test case of its own.
 * The CRC and the pattern are mirrored in tools/decode_save_probe.py (which also reads the file back on the Mac) and compared in sim/test_p2_save_probe.py. */
#ifndef SAVE_PROBE_CORE_H
#define SAVE_PROBE_CORE_H
#include <stdint.h>
#ifndef BUG
#define BUG 0
#endif
#ifndef SVP_FN
#define SVP_FN static inline        /* the firmware build places these in the cold image (fw2/diagnostics/save_probe.c) */
#endif
#ifndef SVP_WORDS
#define SVP_WORDS     2048u          /* the file is 8 KB; the simulation of the SoC with the real CPU shortens it (sim/fw_save/main.c) because a gate-level-slow CPU would need minutes for 2,040 words */
#endif
#define SVP_HDR       8u
#define SVP_MAGIC     0x56415354u
#define SVP_VERSION   1u
enum { SVP_CRC_NA = 0, SVP_CRC_OK = 1, SVP_CRC_BAD = 2 };

typedef struct {
    void (*seek)(uint32_t word);        /* position the index (and let the RAM settle) */
    uint32_t (*rd)(void);               /* the word at the index, the index steps */
    void (*wr)(uint32_t v);             /* store at the index, the index steps */
} svp_io_t;

typedef struct {
    uint32_t boots;                     /* this boot's number (the file's count + 1; 1 on a first boot) */
    uint32_t crc_state;                 /* SVP_CRC_* of the file as LOADED */
    uint32_t pat_bad;                   /* words of the loaded body that differ from the pattern of the loaded seed (0 = pattern ok; 0 on a first boot) */
    uint32_t first;                     /* 1: the file was zero or unknown (nothing to verify) */
    uint32_t last_value_loaded;         /* w3 as loaded: the value the PREVIOUS boot wrote */
    uint32_t last_value_written;        /* w3 as written now */
    uint32_t prev_epoch;                /* w6 as loaded */
    uint32_t seed;                      /* the seed written now */
    uint32_t crc_written;
} svp_res_t;

SVP_FN uint32_t svp_crc_word(uint32_t crc, uint32_t w)       /* zlib CRC-32 (reflected 0xEDB88320), the four bytes of the word in file order (little-endian) */
{
    for (uint32_t b = 0; b < 4u; b++) {
#if BUG == 2
        crc ^= (w >> (24u - 8u * b)) & 0xFFu;                           /* mutant: the bytes in big-endian order */
#else
        crc ^= (w >> (8u * b)) & 0xFFu;
#endif
        for (uint32_t k = 0; k < 8u; k++) crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return crc;
}
SVP_FN uint32_t svp_pat(uint32_t seed, uint32_t i)
{
    uint32_t x = seed * 0x9E3779B1u + i * 0x85EBCA6Bu;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return x;
}
SVP_FN uint32_t svp_next_value(uint32_t last, uint32_t boots) { return (last ^ 0xA5A5A5A5u) * 1664525u + 1013904223u + boots; }

/* Run one boot. `epoch` is the host's wall clock (0 if unknown). Returns 0, or 1 if the header could not be read at all (never: kept for the accessor to refuse). */
SVP_FN void svp_run(const svp_io_t *io, uint32_t epoch, svp_res_t *r)
{
    uint32_t h[SVP_HDR];
    io->seek(0);
    for (uint32_t i = 0; i < SVP_HDR; i++) h[i] = io->rd();
    const uint32_t valid = (h[0] == SVP_MAGIC && h[1] == SVP_VERSION);
    r->first = !valid; r->crc_state = SVP_CRC_NA; r->pat_bad = 0; r->prev_epoch = 0; r->last_value_loaded = 0;
    uint32_t boots = 1, seed = 1, last = 0;
    if (valid) {
        uint32_t crc = 0xFFFFFFFFu, bad = 0;
        io->seek(SVP_HDR);                                                       /* the body is read in one sweep: the CRC and the pattern are both checked on the same words */
        for (uint32_t i = SVP_HDR; i < SVP_WORDS; i++) { const uint32_t w = io->rd(); crc = svp_crc_word(crc, w); if (w != svp_pat(h[4], i)) bad++; }
        r->crc_state = ((crc ^ 0xFFFFFFFFu) == h[5]) ? SVP_CRC_OK : SVP_CRC_BAD; r->pat_bad = bad;
        r->prev_epoch = h[6]; r->last_value_loaded = h[3];
        boots = h[2] + 1u; seed = h[4] + (BUG == 1 ? 0u : 1u); last = h[3];      /* BUG 1: the seed never advances */
    }
    const uint32_t value = svp_next_value(last, boots);
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = SVP_HDR; i < SVP_WORDS; i++) crc = svp_crc_word(crc, svp_pat(seed, i));
    crc ^= 0xFFFFFFFFu;
    io->seek(SVP_HDR);
    for (uint32_t i = SVP_HDR; i < SVP_WORDS; i++) io->wr(svp_pat(seed, i));
    io->seek(0);                                                                 /* the header last: a torn update then shows as a CRC mismatch at the next boot */
    io->wr(SVP_MAGIC); io->wr(SVP_VERSION); io->wr(boots); io->wr(value); io->wr(seed); io->wr(crc); io->wr(epoch); io->wr(0u);
    r->boots = boots; r->seed = seed; r->last_value_written = value; r->crc_written = crc;
}
#endif
