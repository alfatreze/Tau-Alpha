/* Loader for meter packs (.tmpk), portable like fw/cold_core.h: no MMIO, no libc, tested on the host under tools/rv32sim.py against files made by
 * tools/pack_meter.py. Header, 48 bytes little endian:
 *   0 magic 'TMPK'   4 abi u16   6 meter id u16   8 image size u32 (code + read-only data + the initial values of .data; copied into the slot, CRC'd)
 *   12 origin u32 (the slot address the pack was linked at)   16 entry offset u32   20 image crc32   24 .data offset in the image   28 .data size
 *   32 scratch origin u32 (where .data and .bss run: the on-chip meter scratch)   36 .bss size (zeroed after .data in scratch)
 *   40 pstate offset (state kept in the slot's PSRAM, zeroed)   44 pstate size
 * Refusal order: header (E21), ABI (E22), meter id (E23), origin (E24), sizes (E25: file length exact, image and pstate inside the slot, .data inside the
 * image), CRC (E26), entry inside the image (E27), scratch origin (E28), state larger than the scratch area (E29). A refusal leaves the meter unavailable
 * and the firmware on its built-in meters; nothing is executed. The scratch area is the caller's: only the active meter's working state is resident. */
#ifndef TAU_METER_PACK_CORE_H
#define TAU_METER_PACK_CORE_H

#include "library_core.h"
#include "meter_pack.h"

#define MPK_MAGIC 0x4B504D54u
#define MPK_HDR   48u
enum { MPK_OK = 0, MPK_E_NOFILE = 20, MPK_E_HDR = 21, MPK_E_ABI = 22, MPK_E_ID = 23, MPK_E_ORIGIN = 24, MPK_E_SIZE = 25, MPK_E_CRC = 26, MPK_E_ENTRY = 27, MPK_E_SCRATCH_ORG = 28, MPK_E_SCRATCH_CAP = 29 };

static int mpk_load(lib_read_fn rd, void *ctx, uint8_t *win, volatile uint8_t *slot, uint32_t slot_origin, uint32_t slot_cap,
                    volatile uint8_t *scratch, uint32_t scratch_origin, uint32_t scratch_cap, uint32_t want_meter, uint32_t *entry_addr)
{
    if (!rd(ctx, 0u, win, MPK_HDR)) return rd(ctx, 0u, win, 1u) ? MPK_E_HDR : MPK_E_NOFILE;
    if (lib_ld32(win) != MPK_MAGIC) return MPK_E_HDR;
    const uint32_t abi = lib_ld16(win + 4), id = lib_ld16(win + 6), size = lib_ld32(win + 8), origin = lib_ld32(win + 12), entry = lib_ld32(win + 16);
    const uint32_t crc0 = lib_ld32(win + 20), data_off = lib_ld32(win + 24), data_size = lib_ld32(win + 28), s_org = lib_ld32(win + 32), bss = lib_ld32(win + 36);
    const uint32_t ps_off = lib_ld32(win + 40), ps_size = lib_ld32(win + 44);
    if (abi != MTR_PACK_ABI) return MPK_E_ABI;
    if (id != want_meter) return MPK_E_ID;
    if (origin != slot_origin) return MPK_E_ORIGIN;
    if (size == 0u || size > slot_cap || ps_off < size || ps_off > slot_cap || ps_size > slot_cap - ps_off) return MPK_E_SIZE;
    if (data_off > size || data_size > size - data_off) return MPK_E_SIZE;
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
    if (s_org != scratch_origin) return MPK_E_SCRATCH_ORG;
    const uint32_t bss_off = (data_size + 3u) & ~3u;
    if (bss_off > scratch_cap || bss > scratch_cap - bss_off) return MPK_E_SCRATCH_CAP;
    for (uint32_t i = 0; i < data_size; i++) scratch[i] = slot[data_off + i];
    for (uint32_t i = 0; i < bss; i++) scratch[bss_off + i] = 0u;
    for (uint32_t i = 0; i < ps_size; i++) slot[ps_off + i] = 0u;
    *entry_addr = slot_origin + entry;
    return MPK_OK;
}
#endif
