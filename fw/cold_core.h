/* Phase G1: portable loader for the cold image (tau-cold.bin), see docs/PHASE_G_SPEC.md and tools/pack_cold.py.
 * Same style as library_core.h: no MMIO, no libc, tested on the host under tools/rv32sim.py against images written by
 * tools/pack_cold.py. Header, 20 bytes LE: magic 'TCLD', version u16 (1), flags u16, size u32, body crc32, layout id.
 * The firmware passes the size and layout id it was linked with; anything else is refused and the cold features stay off. */
#ifndef TAU_COLD_CORE_H
#define TAU_COLD_CORE_H

#include "library_core.h"

#define COLD_MAGIC 0x444C4354u
#define COLD_HDR   20u
enum { COLD_OK = 0, COLD_E_NOFILE = 10, COLD_E_HDR = 11, COLD_E_SIZE = 12, COLD_E_CRC = 13, COLD_E_ID = 14, COLD_E_PSRAM = 16 };

/* Copies the body into `dst` (`cap` bytes) window by window and checks it. Order: header (E11), size and file length
 * (E12: the last byte reads and one past it does not), layout id (E14), body CRC (E13). `win` is 4-byte aligned, LIB_WIN bytes. */
static int cold_load(lib_read_fn rd, void *ctx, uint8_t *win, volatile uint8_t *dst, uint32_t cap,
                     uint32_t want_size, uint32_t want_id)
{
    if (!rd(ctx, 0u, win, COLD_HDR)) return rd(ctx, 0u, win, 1u) ? COLD_E_HDR : COLD_E_NOFILE;
    if (lib_ld32(win) != COLD_MAGIC || lib_ld16(win + 4) != 1u) return COLD_E_HDR;
    uint32_t size = lib_ld32(win + 8), crc0 = lib_ld32(win + 12), id = lib_ld32(win + 16);
    if (size != want_size || size > cap || size == 0u) return COLD_E_SIZE;
    uint32_t total = COLD_HDR + size;
    if (!rd(ctx, total - 1u, win, 1u) || rd(ctx, total, win, 1u)) return COLD_E_SIZE;
    if (id != want_id) return COLD_E_ID;
    uint32_t crc = LIB_CRC_INIT;
    for (uint32_t off = 0; off < size; ) {
        uint32_t n = size - off;
        if (n > LIB_WIN) n = LIB_WIN;
        if (!rd(ctx, COLD_HDR + off, win, n)) return COLD_E_SIZE;
        crc = lib_crc_update(crc, win, n);
        volatile uint8_t *d = dst + off;
        if (((off | n) & 3u) == 0u) {
            volatile uint32_t *dw = (volatile uint32_t *)d;
            const uint32_t *sw = (const uint32_t *)win;
            for (uint32_t i = 0; i < (n >> 2); i++) dw[i] = sw[i];
        } else {
            for (uint32_t i = 0; i < n; i++) d[i] = win[i];
        }
        off += n;
    }
    return LIB_CRC_DONE(crc) == crc0 ? COLD_OK : COLD_E_CRC;
}

#endif
