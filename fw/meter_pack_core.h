/* Loader for meter packs (.tmpk), portable like fw/cold_core.h: no MMIO, no libc, tested on the host under tools/rv32sim.py against files made by
 * tools/pack_meter.py. Header, 32 bytes little endian:
 *   0 magic 'TMPK'   4 abi u16   6 meter id u16   8 load size u32 (code + read-only data + data, copied)   12 bss size u32 (zeroed after)
 *   16 origin u32 (the address the pack was linked at = its slot)   20 entry offset u32   24 body crc32   28 bss offset u32
 * Refusal order: header (E21), ABI (E22), meter id (E23), origin (E24), sizes (E25: file length exact, body and bss inside the slot), CRC (E26),
 * entry inside the body (E27). Any refusal leaves the meter unavailable and the firmware on its built-in meters; nothing is executed. */
#ifndef TAU_METER_PACK_CORE_H
#define TAU_METER_PACK_CORE_H

#include "library_core.h"
#include "meter_pack.h"

#define MPK_MAGIC 0x4B504D54u
#define MPK_HDR   32u
enum { MPK_OK = 0, MPK_E_NOFILE = 20, MPK_E_HDR = 21, MPK_E_ABI = 22, MPK_E_ID = 23, MPK_E_ORIGIN = 24, MPK_E_SIZE = 25, MPK_E_CRC = 26, MPK_E_ENTRY = 27 };

static int mpk_load(lib_read_fn rd, void *ctx, uint8_t *win, volatile uint8_t *slot, uint32_t slot_origin, uint32_t slot_cap,
                    uint32_t want_meter, uint32_t *entry_addr)
{
    if (!rd(ctx, 0u, win, MPK_HDR)) return rd(ctx, 0u, win, 1u) ? MPK_E_HDR : MPK_E_NOFILE;
    if (lib_ld32(win) != MPK_MAGIC) return MPK_E_HDR;
    const uint32_t abi = lib_ld16(win + 4), id = lib_ld16(win + 6), size = lib_ld32(win + 8), bss = lib_ld32(win + 12), origin = lib_ld32(win + 16);
    const uint32_t entry = lib_ld32(win + 20), crc0 = lib_ld32(win + 24), bss_off = lib_ld32(win + 28);
    if (abi != MTR_PACK_ABI) return MPK_E_ABI;
    if (id != want_meter) return MPK_E_ID;
    if (origin != slot_origin) return MPK_E_ORIGIN;
    if (size == 0u || size > slot_cap || bss_off < size || bss_off > slot_cap || bss > slot_cap - bss_off) return MPK_E_SIZE;
    const uint32_t total = MPK_HDR + size;
    if (!rd(ctx, total - 1u, win, 1u) || rd(ctx, total, win, 1u)) return MPK_E_SIZE;
    uint32_t crc = LIB_CRC_INIT;
    for (uint32_t off = 0; off < size; ) {
        uint32_t n = size - off;
        if (n > LIB_WIN) n = LIB_WIN;
        if (!rd(ctx, MPK_HDR + off, win, n)) return MPK_E_SIZE;
        crc = lib_crc_update(crc, win, n);
        volatile uint8_t *d = slot + off;
        for (uint32_t i = 0; i < n; i++) d[i] = win[i];
        off += n;
    }
    if (LIB_CRC_DONE(crc) != crc0) return MPK_E_CRC;
    if (entry >= size) return MPK_E_ENTRY;
    for (uint32_t i = 0; i < bss; i++) slot[bss_off + i] = 0u;
    *entry_addr = slot_origin + entry;
    return MPK_OK;
}
#endif
