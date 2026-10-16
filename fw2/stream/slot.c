#pragma GCC optimize ("Os")   /* not on a hot path: smaller code is what the 192 KB link needs */
#include "slot.h"
#include "../platform/mmio.h"

#define TGT_READ 0u
#define TGT_OPEN 1u
#define TGT_GET  2u
#define DT_RESP_W  64u       /* where APF writes the 0190 response */
#define DT_PARAM_W 128u      /* where APF reads the 0192 parameters */
#define DT_WORDS   64u

uint8_t slot_err, slot_errcode;
uint32_t slot_opens, slot_open_fails, slot_open_ms_last, slot_open_ms_max;
static uint8_t  pending;
static uint32_t seq0;

int slot_busy(void) { return pending; }

void slot_read_start(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len)
{
    seq0 = (REG_RD(R_TGT_GO) >> 8) & 0xFFu;
    REG_WR(R_TGT_ID, slot); REG_WR(R_TGT_OFF, off); REG_WR(R_TGT_ADR, dst_off); REG_WR(R_TGT_LEN, len);
    REG_WR(R_TGT_GO, TGT_READ);
    pending = 1;
}

/* Completion is the SEQUENCE COUNTER moving, never the `done` level (it stays asserted from the previous command). */
io_result_t slot_read_poll(void)
{
    if (!pending) return IO_PENDING;
    uint32_t s = REG_RD(R_TGT_GO);
    if (((s >> 8) & 0xFFu) == seq0) return IO_PENDING;
    pending = 0;
    return ((s >> 2) & 7u) == 0u ? IO_OK : IO_FAIL;
}

int slot_read_blocking(uint32_t slot, uint32_t off, uint32_t dst_off, uint32_t len, uint32_t timeout_cycles)
{
    slot_read_start(slot, off, dst_off, len);
    uint32_t t0 = cycles();
    for (;;) {
        io_result_t r = slot_read_poll();
        if (r != IO_PENDING) return r == IO_OK;
        if ((int32_t)(cycles() - t0 - timeout_cycles) >= 0) return 0;
    }
}

static int cmd(uint32_t sel, uint32_t slot, uint32_t timeout_cycles, int *timedout)
{
    uint32_t s0 = (REG_RD(R_TGT_GO) >> 8) & 0xFFu, t0 = cycles();
    *timedout = 0;
    REG_WR(R_TGT_ID, slot); REG_WR(R_TGT_GO, sel);
    for (;;) {
        uint32_t st = REG_RD(R_TGT_GO);
        if (((st >> 8) & 0xFFu) != s0) return ((st >> 2) & 7u) == 0u;
        if ((int32_t)(cycles() - t0 - timeout_cycles) >= 0) { *timedout = 1; return 0; }
    }
}

static inline void dt_wr(uint32_t w, uint32_t v) { REG_WR(R_DT_ADDR, w); REG_WR(R_DT_DATA, v); }
static inline uint32_t dt_rd(uint32_t w) { REG_WR(R_DT_ADDR, w); return REG_RD(R_DT_DATA); }

/* The 0192 parameter struct is built from a template APF itself produced (0190 on a slot that already holds a file): the printable path in it is replaced by ours and
 * the length word immediately before it is patched when it holds the old length. This is the mechanism the old player proved on hardware (fw/playlist.inc). */
int slot_open_path(uint32_t slot, const char *path)
{
    uint32_t t0 = cycles();
    uint8_t buf[DT_WORDS * 4u];
    int to;
    slot_err = SLOT_E_OK; slot_errcode = 0;
    if (!cmd(TGT_GET, SLOT_LIB, CLK_HZ, &to)) { slot_err = SLOT_E_NOTEMPLATE; goto fail; }
    for (uint32_t w = 0; w < DT_WORDS; w++) { uint32_t v = dt_rd(DT_RESP_W + w); buf[w*4] = (uint8_t)(v >> 24); buf[w*4+1] = (uint8_t)(v >> 16); buf[w*4+2] = (uint8_t)(v >> 8); buf[w*4+3] = (uint8_t)v; }
    uint32_t best = 0, best_len = 0, i = 0;
    while (i < sizeof buf) {
        uint32_t st = i;
        while (i < sizeof buf && buf[i] >= 0x20u && buf[i] < 0x7Fu) i++;
        if (i - st > best_len) { best_len = i - st; best = st; }
        i++;
    }
    if (!best_len) { slot_err = SLOT_E_NOTEMPLATE; goto fail; }
    uint32_t n = 0; while (path[n]) n++;
    if (path[0] != '/' || n >= sizeof buf - best) { slot_err = SLOT_E_TOOLONG; goto fail; }
    int patch = 0;
    if (best >= 4u) {
        uint32_t lw = (best - 4u) / 4u;
        uint32_t v = ((uint32_t)buf[lw*4] << 24) | ((uint32_t)buf[lw*4+1] << 16) | ((uint32_t)buf[lw*4+2] << 8) | buf[lw*4+3];
        patch = (v == best_len) ? (int)(lw + 1u) : 0;
        if (patch) { uint32_t L = (uint32_t)patch - 1u; buf[L*4] = (uint8_t)(n >> 24); buf[L*4+1] = (uint8_t)(n >> 16); buf[L*4+2] = (uint8_t)(n >> 8); buf[L*4+3] = (uint8_t)n; }
    }
    for (uint32_t k = 0; k < best_len || k < n; k++) { if (best + k >= sizeof buf) break; buf[best + k] = k < n ? (uint8_t)path[k] : 0u; }
    if (best + n < sizeof buf) buf[best + n] = 0u;
    for (uint32_t w = 0; w < DT_WORDS; w++)
        dt_wr(DT_PARAM_W + w, ((uint32_t)buf[w*4] << 24) | ((uint32_t)buf[w*4+1] << 16) | ((uint32_t)buf[w*4+2] << 8) | buf[w*4+3]);
    if (!cmd(TGT_OPEN, slot, CLK_HZ * 3u, &to)) { slot_err = to ? SLOT_E_TIMEOUT : SLOT_E_OPEN; slot_errcode = (uint8_t)((REG_RD(R_TGT_GO) >> 2) & 7u); goto fail; }
    slot_opens++;
    slot_open_ms_last = (cycles() - t0) / (CLK_HZ / 1000u); if (slot_open_ms_last > slot_open_ms_max) slot_open_ms_max = slot_open_ms_last;
    return 1;
fail:
    slot_open_fails++;
    slot_open_ms_last = (cycles() - t0) / (CLK_HZ / 1000u); if (slot_open_ms_last > slot_open_ms_max) slot_open_ms_max = slot_open_ms_last;
    return 0;
}
