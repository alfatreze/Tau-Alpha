#pragma GCC optimize ("Os")
#include "save_probe.h"
#include "../platform/mmio.h"
#include "../platform/platform.h"
#define SVP_FN P2_COLD_TEXT static                   /* the pure core's functions go to the cold image too: the self-test adds nothing to the hot code */
#include "../cores/save_probe_core.h"
#define SV_SLOT_ID 11u                       /* the save slot's id in data.json (tools/tau_data_slots.py add_save_probe_slot) */
#ifdef P2_HOST
#define SV_COLD
#else
#define SV_COLD __attribute__((section(".cold_data")))   /* writable, in the cold image: the diagnostic state costs no hot RAM */
#endif
SV_COLD sv_state_t sv_state = { 0 };

P2_COLD_TEXT static void io_seek(uint32_t w) { REG_WR(R_SAVE_IDX, w); (void)REG_RD(R_SAVE_ST); }      /* the status read gives the RAM's output register its cycle */
P2_COLD_TEXT static uint32_t io_rd(void) { return REG_RD(R_SAVE_DATA); }
P2_COLD_TEXT static void io_wr(uint32_t v) { REG_WR(R_SAVE_DATA, v); }

/* APF's slot table (words at R_DT_ADDR: id, size, id, size, ...): the size entry for the save slot, or 0. Read-only here: the size is APF's own bookkeeping (KB-001, KB-002). */
P2_COLD_TEXT static uint32_t table_size(void)
{
    for (uint32_t i = 0; i < 32u; i++) {
        REG_WR(R_DT_ADDR, 2u * i); (void)REG_RD(R_STAT0);
        const uint32_t id = REG_RD(R_DT_DATA);
        if (id == SV_SLOT_ID) { REG_WR(R_DT_ADDR, 2u * i + 1u); (void)REG_RD(R_STAT0); return REG_RD(R_DT_DATA); }
    }
    return 0;
}

P2_COLD_TEXT void save_probe_run(void)           /* cold code: it runs from the PSRAM image, so it costs no hot RAM */
{
    if (sv_state.runs) return;
    sv_state.runs = 1;
    const uint32_t st = REG_RD(R_SAVE_ST);
    sv_state.present = st >> 31;
    if (!sv_state.present) return;                                   /* a bitstream without TAU_SAVE: nothing to do */
    sv_state.bridge_words = st & 0xFFFFu;
    sv_state.table_bytes = table_size();
    const uint32_t t0 = cycles();
    const uint32_t epoch = (REG_RD(R_RTC_ST) & 1u) ? REG_RD(R_RTC_EPOCH) : 0u;
    svp_io_t io; svp_res_t r; io.seek = io_seek; io.rd = io_rd; io.wr = io_wr;      /* built at run time: a const initialiser would put 12 bytes of .rodata in the HOT image */
    svp_run(&io, epoch, &r);
    sv_state.boots = r.boots; sv_state.crc = r.crc_state; sv_state.pat_bad = r.pat_bad; sv_state.last = r.last_value_loaded; sv_state.written = r.last_value_written; sv_state.prev_epoch = r.prev_epoch;
    sv_state.ms = (cycles() - t0) / (CLK_HZ / 1000u);
}
