/* Loader for meter packs (.tmpk) and for the bundle file that carries them (tau-packs.bin), portable like fw/cold_core.h: no MMIO, no libc, tested on the host under
 * tools/rv32sim.py against files made by tools/pack_meter.py. Pack header, 48 bytes little endian:
 *   0 magic 'TMPK'   4 abi u16   6 meter id u16   8 image size u32 (code + read-only data + the initial values of .data; copied into the slot, CRC'd)
 *   12 origin u32 (the slot address the pack was linked at)   16 entry offset u32   20 image crc32   24 .data offset in the image   28 .data size
 *   32 scratch origin u32 (where .data and .bss run: the on-chip meter scratch)   36 .bss size (zeroed after .data in scratch)
 *   40 pstate offset (state kept in the slot's PSRAM, zeroed)   44 pstate size
 * Refusal order: header (E21), ABI (E22), meter id (E23), origin (E24), sizes (E25: file length exact, image and pstate inside the slot, .data inside the
 * image), CRC (E26), entry inside the image (E27), scratch origin (E28), state larger than the scratch area (E29). A refusal leaves the meter unavailable
 * and the firmware on its built-in meters; nothing is executed.
 * Two steps: mpk_install() verifies a pack and copies its image into its slot (done once, at boot, for every pack in the bundle); mpk_activate() prepares the shared
 * scratch for ONE meter (copies its .data, zeroes its .bss and its slot state), done whenever that meter becomes the active one -- so only the active meter's
 * working state is resident, and switching meters costs a few hundred bytes of copying.
 * Bundle (tau-packs.bin): 'TPKB' u32 | version u16 (1) | count u16 | the packs back to back (each is its own 48-byte header plus image, so its length is
 * 48 + image size). Errors in one pack never affect the others. */
#ifndef TAU_METER_PACK_CORE_H
#define TAU_METER_PACK_CORE_H

#include "library_core.h"
#include "meter_pack.h"

#define MPK_MAGIC 0x4B504D54u
#define MPK_HDR   48u
#define MPKB_MAGIC 0x424B5054u
#define MPKB_HDR  8u
enum { MPK_OK = 0, MPK_E_NOFILE = 20, MPK_E_HDR = 21, MPK_E_ABI = 22, MPK_E_ID = 23, MPK_E_ORIGIN = 24, MPK_E_SIZE = 25, MPK_E_CRC = 26, MPK_E_ENTRY = 27, MPK_E_SCRATCH_ORG = 28, MPK_E_SCRATCH_CAP = 29,
       MPKB_E_HDR = 30, MPKB_E_FULL = 31 };

typedef struct {                  /* what an installed pack needs to be activated and called */
    uint32_t meter, size, entry, data_off, data_size, bss, ps_off, ps_size;
} mpk_info_t;

static int mpk_install(lib_read_fn rd, void *ctx, uint8_t *win, volatile uint8_t *slot, uint32_t slot_origin, uint32_t slot_cap,
                       uint32_t scratch_origin, uint32_t scratch_cap, uint32_t want_meter, mpk_info_t *info)
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
    if (!rd(ctx, total - 1u, win, 1u)) return MPK_E_SIZE;       /* the last byte must exist; one past it is the caller's business (a bundle continues there) */
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
    info->meter = id; info->size = size; info->entry = slot_origin + entry; info->data_off = data_off; info->data_size = data_size; info->bss = bss; info->ps_off = ps_off; info->ps_size = ps_size;
    return MPK_OK;
}

/* Prepares the scratch and the slot's state for this pack: .data copied from the slot, .bss and the slot state zeroed. Cannot fail: mpk_install checked every size. */
static void mpk_activate(const mpk_info_t *info, volatile uint8_t *slot, volatile uint8_t *scratch)
{
    const uint32_t bss_off = (info->data_size + 3u) & ~3u;
    for (uint32_t i = 0; i < info->data_size; i++) scratch[i] = slot[info->data_off + i];
    for (uint32_t i = 0; i < info->bss; i++) scratch[bss_off + i] = 0u;
    for (uint32_t i = 0; i < info->ps_size; i++) slot[info->ps_off + i] = 0u;
}

/* One pack, installed and activated in one call (the test harnesses, and a firmware that has a single pack). */
static int mpk_load(lib_read_fn rd, void *ctx, uint8_t *win, volatile uint8_t *slot, uint32_t slot_origin, uint32_t slot_cap,
                    volatile uint8_t *scratch, uint32_t scratch_origin, uint32_t scratch_cap, uint32_t want_meter, uint32_t *entry_addr)
{
    mpk_info_t info;
    const int e = mpk_install(rd, ctx, win, slot, slot_origin, slot_cap, scratch_origin, scratch_cap, want_meter, &info);
    if (e) return e;
    if (rd(ctx, MPK_HDR + info.size, win, 1u)) return MPK_E_SIZE;      /* a single-pack file ends exactly at the image: a trailing byte is a malformed file */
    mpk_activate(&info, slot, scratch);
    *entry_addr = info.entry;
    return MPK_OK;
}

/* ---- the bundle ------------------------------------------------------------------------------------------------------------------------------- */
typedef struct { lib_read_fn rd; void *ctx; uint32_t base; } mpkb_rd_t;
static int mpkb_rd(void *c, uint32_t off, uint8_t *dst, uint32_t len) { const mpkb_rd_t *r = (const mpkb_rd_t *)c; return r->rd(r->ctx, r->base + off, dst, len); }

/* Per meter slot: the state of the pack for it. status MPK_OK + info valid = installed; anything else = refused (the code says why) or 0xFF = no pack in the bundle for it. */
typedef struct { uint8_t status; mpk_info_t info; } mpkb_slot_t;

/* Reads the bundle of `file_len` bytes and installs every pack it finds. `slot_data` is the first slot's data-alias address (slot i is at slot_data + i * MTR_PACK_SLOT_SIZE).
 * Returns MPK_OK (even if some packs were refused: see slots[i].status), MPK_E_NOFILE (no file) or MPKB_E_HDR (not a bundle). `seen` receives the number of packs in the file. */
static int mpkb_install_all(lib_read_fn rd, void *ctx, uint8_t *win, uint32_t file_len, volatile uint8_t *slot_data,
                            uint32_t scratch_origin, uint32_t scratch_cap, mpkb_slot_t slots[MTR_PACK_SLOTS], uint32_t *seen)
{
    for (uint32_t i = 0; i < MTR_PACK_SLOTS; i++) slots[i].status = 0xFFu;
    *seen = 0u;
    if (file_len == 0u || !rd(ctx, 0u, win, MPKB_HDR)) return MPK_E_NOFILE;
    if (lib_ld32(win) != MPKB_MAGIC || lib_ld16(win + 4) != 1u) return MPKB_E_HDR;
    const uint32_t count = lib_ld16(win + 6);
    uint32_t off = MPKB_HDR;
    for (uint32_t k = 0; k < count; k++) {
        if (off + MPK_HDR > file_len || !rd(ctx, off, win, MPK_HDR)) return MPK_OK;       /* a truncated bundle: what came before stays */
        const uint32_t id = lib_ld16(win + 6), size = lib_ld32(win + 8);
        const uint32_t next = off + MPK_HDR + size;
        if (lib_ld32(win) != MPK_MAGIC || size == 0u || next > file_len) return MPK_OK;    /* cannot find the next pack: stop here */
        (*seen)++;
        const int s = mtr_pack_slot_of(id);
        if (s < 0 || (uint32_t)s >= MTR_PACK_SLOTS) { off = next; continue; }               /* a meter that cannot be a pack here: skipped */
        mpkb_rd_t r = { rd, ctx, off };
        slots[s].status = (uint8_t)mpk_install(mpkb_rd, &r, win, slot_data + (uint32_t)s * MTR_PACK_SLOT_SIZE, MTR_PACK_SLOT_BASE + (uint32_t)s * MTR_PACK_SLOT_SIZE,
                                               MTR_PACK_SLOT_SIZE, scratch_origin, scratch_cap, id, &slots[s].info);
        off = next;
    }
    return MPK_OK;
}
#endif
