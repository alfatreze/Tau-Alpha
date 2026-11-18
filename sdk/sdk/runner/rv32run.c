/* rv32run.c: runs a meter pack (the `meter.bin` of a .tmeter) on an RV32IM interpreter with the player's memory map and host table, for `tau-meter test`. MIT licence.
 * Written from the RISC-V unprivileged specification (RV32IM). Built on the fly by the SDK with the pinned clang (tau_meter/runner.py); the strike and watchdog verdicts use the player's own
 * pack_strike_core.h (a byte copy, kept identical by a test), so the verdict is the firmware's arithmetic, not a model of it.
 *
 * What it models (all of it is what the player does around a pack, fw2/ui/meters.c and fw2/cores/meter_pack_core.h):
 *   the 64 KiB window (code fetched at 0x24970000, rodata and the pack's own state read through the data alias 0xA4970000; the state is word access only), the 4 KiB scratch for .data/.bss at 0x00026C00,
 *   a stack, the read-only input structures, and the host table, whose entries are addresses that trap here (an entry the declared ABI minor does not have is a null pointer: a call faults).
 * What it reports: one line per host-table call ("c name args"), then "r status cycles instructions commands return stack_bytes" per tick; a fault prints "f reason pc addr" first.
 * The cycle count is an ESTIMATE (cost model v1): 1.5 cycles per instruction, a 4 KiB direct-mapped instruction cache with 32-byte lines that costs 253 cycles per line fill (measured on a Pocket, B-045),
 * 60 cycles per draw command (the register pushes alone). It is not a measurement: it is pessimistic about cache misses (cold at every tick) and optimistic about engine stalls.
 *
 * Commands on stdin (one per line):  load FILE | profile author|shipped | abi MINOR | blend 0|1 | limit SOFT_CYCLES | params N v... | roles v... | tick ARGS... | quit */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pack_strike_core.h"

#define WIN_ORG   0x24970000u
#define WIN_DATA  0xA4970000u
#define WIN_SIZE  0x10000u
#define SCR_ORG   0x00026C00u
#define SCR_SIZE  0x1000u
#define STK_TOP   0x0001B400u
#define STK_SIZE  0x1400u
#define HOST_ORG  0x00030000u
#define HOST_SIZE 0x1000u
#define STUB_BASE 0xFFFF0000u
#define RET_ADDR  0xFFFFFFF0u
#define MMIO_LO   0x80000000u
#define MMIO_HI   0x80001000u
#define O_IN 0x000u
#define O_SPEC 0x100u
#define O_ROLE 0x200u
#define O_PARAMS 0x300u
#define O_STATS 0x400u
#define O_API 0x500u
#define N_API 22u

static uint8_t win[WIN_SIZE], scr[SCR_SIZE], stk[STK_SIZE], host[HOST_SIZE];
static uint16_t plane[512 * 1024];                    /* the engine's memory, only for the mailbox read-back path */
static uint32_t reg[32], pc;
static struct { uint32_t size, entry, data_off, data_size, scr_org, bss, ps_off, ps_size; } hdr;
static int stackfill_v, loaded, profile_shipped, abi_minor = 2, blend_ready_v = 1, soft_limit;
static ps_t ps; static int struck;
/* per tick */
static uint64_t instr, cyc_extra; static uint32_t calls, cmds, stack_lo; static int faulted; static char fault_msg[200]; static uint32_t fault_pc, fault_addr;
static uint32_t icache_tag[128]; static uint32_t misses;
static uint32_t dev_arms, dev_idx;

static uint32_t cycles_now(void) { return (uint32_t)(instr * 3u / 2u + cyc_extra + (uint64_t)misses * 253u); }
static void fault(const char *msg, uint32_t addr) { if (!faulted) { faulted = 1; snprintf(fault_msg, sizeof fault_msg, "%s", msg); fault_pc = pc; fault_addr = addr; } }

/* ---- the device model of the shipped profile (the registers the first-party meters read; sim/p2_dev_model.py is the Python twin) ---- */
static uint32_t dm_data(uint32_t a, uint32_t i)
{
    uint32_t x = a * 0x9E3779B1u + i * 0x85EBCA6Bu + 12345u; x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    const uint32_t mode = a % 4u, sc = mode == 3u ? 32000u : 6000u;
    int32_t m = (int32_t)((x >> 16) % sc), lo = -(int32_t)((x & 0xFFFFu) % sc);
    if (mode == 1u) { lo += 12000; m += 12000; }
    if (mode == 2u) lo = 0;
    if (lo < -32768) lo = -32768; if (lo > 32767) lo = 32767; if (m < -32768) m = -32768; if (m > 32767) m = 32767;
    return ((uint32_t)(lo & 0xFFFF) << 16) | (uint32_t)(m & 0xFFFF);
}
static int mmio_known(uint32_t off) { return off == 0x0C || off == 0x88 || off == 0xA0 || off == 0xEC || off == 0xF0 || off == 0xF4 || off == 0xFC; }
static uint32_t mmio_rd(uint32_t a)
{
    const uint32_t off = a & 0xFFFu;
    if (!profile_shipped || !mmio_known(off)) { fault("reads a hardware register: a meter may use only the host table", a); return 0; }
    if (off == 0x0C) return cycles_now();
    if (off == 0xFC) return 1u;
    if (off == 0x88) return 0x50535231u;
    if (off == 0xA0) return 0x10000u;
    if (off == 0xF4) return dm_data(dev_arms, dev_idx & 255u);
    return 0u;
}
static void mmio_wr(uint32_t a, uint32_t v)
{
    const uint32_t off = a & 0xFFFu;
    if (!profile_shipped || !mmio_known(off)) { fault("writes a hardware register: a meter may use only the host table", a); return; }
    if (off == 0xF0) dev_idx = v; else if (off == 0xEC && (v & 2u)) dev_arms++;
}

/* ---- memory ---- */
static uint32_t rd_le(const uint8_t *p, int n) { uint32_t v = 0; for (int i = 0; i < n; i++) v |= (uint32_t)p[i] << (8 * i); return v; }
static void wr_le(uint8_t *p, int n, uint32_t v) { for (int i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t in_reads_new;                          /* bytes read from the mtr_in_t fields added in ABI 1.1 (offsets 40..51) */
static uint32_t load_mem(uint32_t a, int n, int host_side)
{
    if (a & (uint32_t)(n - 1)) { fault("a misaligned load", a); return 0; }
    if (a >= WIN_DATA && a - WIN_DATA + n <= WIN_SIZE) {
        const uint32_t o = a - WIN_DATA;
        if (!host_side && n < 4 && o >= hdr.ps_off && o < hdr.ps_off + hdr.ps_size) { fault("a byte or halfword read of its PSRAM state: the state takes whole 32-bit words only", a); return 0; }
        return rd_le(win + o, n);
    }
    if (a >= WIN_ORG && a - WIN_ORG < WIN_SIZE) { fault("a data read through the instruction alias (the window's code alias can be fetched from, not read)", a); return 0; }
    if (a >= SCR_ORG && a - SCR_ORG + n <= SCR_SIZE) {
        if (!host_side && (a < hdr.scr_org || a + n > hdr.scr_org + ((hdr.data_size + 3u) & ~3u) + hdr.bss)) { fault("reads scratch memory that is not its own .data or .bss", a); return 0; }
        return rd_le(scr + (a - SCR_ORG), n);
    }
    if (a >= STK_TOP - STK_SIZE && a + n <= STK_TOP) { if (a < stack_lo) stack_lo = a; return rd_le(stk + (a - (STK_TOP - STK_SIZE)), n); }
    if (a >= HOST_ORG && a - HOST_ORG + n <= HOST_SIZE) {
        const uint32_t o = a - HOST_ORG;
        if (!host_side && o >= 40u && o < 52u) in_reads_new += (uint32_t)n;
        return rd_le(host + o, n);
    }
    if (a >= MMIO_LO && a < MMIO_HI && n == 4) return mmio_rd(a);
    fault("reads an address that is not the meter's memory (not its window, its own data, the stack or the inputs)", a);
    return 0;
}
static void store_mem(uint32_t a, int n, uint32_t v)
{
    if (a & (uint32_t)(n - 1)) { fault("a misaligned store", a); return; }
    if (a >= WIN_DATA && a - WIN_DATA + n <= WIN_SIZE) {
        const uint32_t o = a - WIN_DATA;
        if (o >= hdr.ps_off && o + n <= hdr.ps_off + hdr.ps_size) { if (n != 4) { fault("a byte or halfword write to its PSRAM state: the state takes whole 32-bit words only", a); return; } wr_le(win + o, 4, v); return; }
        fault("writes outside its own state (its code and constants are read-only)", a); return;
    }
    if (a >= WIN_ORG && a - WIN_ORG < WIN_SIZE) { fault("writes through the instruction alias (its code is read-only)", a); return; }
    if (a >= SCR_ORG && a - SCR_ORG + n <= SCR_SIZE) {
        if (a < hdr.scr_org || a + n > hdr.scr_org + ((hdr.data_size + 3u) & ~3u) + hdr.bss) { fault("writes scratch memory that is not its own .data or .bss", a); return; }
        wr_le(scr + (a - SCR_ORG), n, v); return;
    }
    if (a >= STK_TOP - STK_SIZE && a + n <= STK_TOP) { if (a < stack_lo) stack_lo = a; wr_le(stk + (a - (STK_TOP - STK_SIZE)), n, v); return; }
    if (a >= HOST_ORG + O_STATS && a - HOST_ORG + n <= O_STATS + 16u) { wr_le(host + (a - HOST_ORG), n, v); return; }
    if (a >= HOST_ORG && a - HOST_ORG < HOST_SIZE) { fault("writes into the inputs the player passed (they are read-only)", a); return; }
    if (a >= MMIO_LO && a < MMIO_HI && n == 4) { mmio_wr(a, v); return; }
    fault("writes an address that is not the meter's memory", a);
}

/* ---- the host table ---- */
static const char *NAMES[N_API] = { "abi", "rect", "role", "params", "stats", "text", "rect_stash", "blend_ready", "blend_on", "blend_blit", "blend_off", "mb_write", "mb_read", "fence", "blit", "blit_abs", "sblit", "bar", "scroll_down", "clut_load", "cblit", "scroll_left" };
static const int NARGS[N_API] = { 0, 5, 0, 0, 0, 5, 5, 0, 1, 6, 0, 2, 2, 0, 6, 6, 7, 7, 5, 3, 6, 5 };
static const int DRAWS[N_API] = { 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 1, 1 };
static void put_plane(uint32_t a, uint16_t v) { if (a < 512u * 1024u) plane[a] = v; }
static void host_call(uint32_t k)
{
    uint32_t a[8]; for (int i = 0; i < 8; i++) a[i] = reg[10 + i];
    calls++; cyc_extra += DRAWS[k] ? 60u : 12u; if (DRAWS[k]) cmds++;
    printf("c %s", NAMES[k]);
    for (int i = 0; i < NARGS[k]; i++) {
        if (k == 5 && i == 2) { printf(" "); for (uint32_t s = a[2], n = 0; n < 200; s++, n++) { uint32_t c = load_mem(s, 1, 1); if (faulted || !c) break; printf("%02x", c); } printf("."); }
        else if (k == 19 && i == 1) printf(" %u", a[1]);
        else printf(" %u", a[i]);
    }
    printf("\n");
    uint32_t ret = 0;
    switch (k) {
    case 1: case 6: for (uint32_t y = a[1]; y < a[1] + a[3] && y < 1024u; y++) for (uint32_t x = a[0]; x < a[0] + a[2] && x < 512u; x++) put_plane(y * 512u + x, (uint16_t)a[4]); break;
    case 5: ret = a[0]; break;
    case 7: ret = (uint32_t)blend_ready_v; break;
    case 11: put_plane(a[0], (uint16_t)a[1]); put_plane(a[0] + 1u, (uint16_t)(a[1] >> 16)); break;
    case 12: { uint32_t v = (uint32_t)plane[a[0] < 512u * 1024u ? a[0] : 0] | ((uint32_t)plane[a[0] + 1u < 512u * 1024u ? a[0] + 1u : 0] << 16); store_mem(a[1], 4, v); break; }
    case 15: for (uint32_t y = 0; y < a[5] && y < 1024u; y++) for (uint32_t x = 0; x < a[4]; x++) { const uint32_t s = (a[1] + y) * 512u + a[0] + x, d = (a[3] + y) * 512u + a[2] + x; if (s < 512u * 1024u && d < 512u * 1024u) plane[d] = plane[s]; } break;
    case 19: { uint32_t n = a[2] > 256u ? 256u : a[2]; printf("p"); for (uint32_t i = 0; i < n; i++) printf(" %u", load_mem(a[1] + 2u * i, 2, 1)); printf("\n"); break; }
    default: break;
    }
    reg[10] = ret;
}

/* ---- the interpreter ---- */
static int32_t sx(uint32_t v, int bits) { return (int32_t)(v << (32 - bits)) >> (32 - bits); }
static int run_tick(uint64_t cyc_cap)
{
    /* returns 0 finished, 1 cut by the watchdog, 2 fault */
    for (;;) {
        if (pc == RET_ADDR) return 0;
        if (pc >= STUB_BASE && pc < STUB_BASE + 16u * N_API && !(pc & 15u)) { host_call((pc - STUB_BASE) >> 4); if (faulted) return 2; pc = reg[1]; continue; }
        if (pc < WIN_ORG || pc - WIN_ORG >= hdr.size || (pc & 3u)) { fault("jumps to an address outside its own code (a wild or null function pointer, or a call the player's ABI does not offer)", pc); return 2; }
        { const uint32_t li = ((pc - WIN_ORG) >> 5) & 127u, tag = ((pc - WIN_ORG) >> 12) + 1u; if (icache_tag[li] != tag) { icache_tag[li] = tag; misses++; } }     /* 4 KiB direct mapped, 32-byte lines */
        const uint32_t ins = rd_le(win + (pc - WIN_ORG), 4);
        instr++;
        if (cycles_now() > cyc_cap) return 1;
        if ((ins & 3u) != 3u) { fault("a compressed (16-bit) instruction: build for rv32im without the C extension", pc); return 2; }
        const uint32_t op = ins & 0x7Fu, rd = (ins >> 7) & 31u, f3 = (ins >> 12) & 7u, rs1 = (ins >> 15) & 31u, rs2 = (ins >> 20) & 31u, f7 = ins >> 25;
        const uint32_t a = reg[rs1], b = reg[rs2];
        uint32_t next = pc + 4u, v = 0; int wr = 0;
        const int32_t immi = (int32_t)ins >> 20;
        switch (op) {
        case 0x37: v = ins & 0xFFFFF000u; wr = 1; break;
        case 0x17: v = pc + (ins & 0xFFFFF000u); wr = 1; break;
        case 0x6F: { int32_t o = (int32_t)(((ins >> 31) << 20) | (((ins >> 12) & 0xFF) << 12) | (((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3FF) << 1)); o = sx((uint32_t)o, 21); v = next; wr = 1; next = pc + (uint32_t)o; break; }
        case 0x67: v = next; wr = 1; next = (a + (uint32_t)immi) & ~1u; break;
        case 0x63: {
            int32_t o = (int32_t)(((ins >> 31) << 12) | (((ins >> 7) & 1) << 11) | (((ins >> 25) & 0x3F) << 5) | (((ins >> 8) & 0xF) << 1)); o = sx((uint32_t)o, 13);
            int t = 0;
            switch (f3) { case 0: t = a == b; break; case 1: t = a != b; break; case 4: t = (int32_t)a < (int32_t)b; break; case 5: t = (int32_t)a >= (int32_t)b; break; case 6: t = a < b; break; case 7: t = a >= b; break; default: fault("an illegal branch instruction", pc); return 2; }
            if (t) next = pc + (uint32_t)o; break; }
        case 0x03: {
            const uint32_t ad = a + (uint32_t)immi;
            switch (f3) { case 0: v = (uint32_t)sx(load_mem(ad, 1, 0), 8); break; case 1: v = (uint32_t)sx(load_mem(ad, 2, 0), 16); break; case 2: v = load_mem(ad, 4, 0); break; case 4: v = load_mem(ad, 1, 0); break; case 5: v = load_mem(ad, 2, 0); break; default: fault("an illegal load instruction", pc); return 2; }
            wr = 1; break; }
        case 0x23: {
            const int32_t o = sx(((ins >> 25) << 5) | ((ins >> 7) & 31u), 12);
            const uint32_t ad = a + (uint32_t)o;
            if (f3 == 0) store_mem(ad, 1, b); else if (f3 == 1) store_mem(ad, 2, b); else if (f3 == 2) store_mem(ad, 4, b); else { fault("an illegal store instruction", pc); return 2; }
            break; }
        case 0x13: {
            const uint32_t sh = (ins >> 20) & 31u;
            switch (f3) {
            case 0: v = a + (uint32_t)immi; break; case 2: v = (int32_t)a < immi; break; case 3: v = a < (uint32_t)immi; break; case 4: v = a ^ (uint32_t)immi; break; case 6: v = a | (uint32_t)immi; break; case 7: v = a & (uint32_t)immi; break;
            case 1: v = a << sh; break; case 5: v = f7 & 0x20 ? (uint32_t)((int32_t)a >> sh) : a >> sh; break; }
            wr = 1; break; }
        case 0x33:
            if (f7 == 1) {
                switch (f3) {
                case 0: v = a * b; break;
                case 1: v = (uint32_t)(((int64_t)(int32_t)a * (int64_t)(int32_t)b) >> 32); break;
                case 2: v = (uint32_t)(((int64_t)(int32_t)a * (int64_t)b) >> 32); break;
                case 3: v = (uint32_t)(((uint64_t)a * (uint64_t)b) >> 32); break;
                case 4: v = b == 0 ? 0xFFFFFFFFu : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? a : (uint32_t)((int32_t)a / (int32_t)b); break;
                case 5: v = b == 0 ? 0xFFFFFFFFu : a / b; break;
                case 6: v = b == 0 ? a : ((int32_t)a == INT32_MIN && (int32_t)b == -1) ? 0 : (uint32_t)((int32_t)a % (int32_t)b); break;
                case 7: v = b == 0 ? a : a % b; break;
                }
            } else {
                switch (f3) {
                case 0: v = f7 & 0x20 ? a - b : a + b; break; case 1: v = a << (b & 31u); break; case 2: v = (int32_t)a < (int32_t)b; break; case 3: v = a < b; break; case 4: v = a ^ b; break;
                case 5: v = f7 & 0x20 ? (uint32_t)((int32_t)a >> (b & 31u)) : a >> (b & 31u); break; case 6: v = a | b; break; case 7: v = a & b; break;
                }
            }
            wr = 1; break;
        case 0x0F: if (f3 == 1) { fault("a fence.i instruction: instruction-cache control belongs to the player", pc); return 2; } break;
        case 0x73: fault(f3 ? "a CSR instruction (a meter has no access to processor state)" : "a system call (ecall or ebreak): a meter may call only the host table", pc); return 2;
        default: fault("an instruction the RV32IM core does not have", pc); return 2;
        }
        if (faulted) return 2;
        if (wr && rd) reg[rd] = v;
        pc = next;
    }
}

static void put_in(uint32_t frame, uint32_t dt, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t bg, uint32_t force, uint32_t paused, uint32_t pl, uint32_t pr, uint32_t degrade,
                   uint32_t rmsl, uint32_t rmsr, int32_t corr, uint32_t cen, uint32_t crest, uint32_t flux, uint32_t onset, uint32_t sok, const uint8_t *spec)
{
    uint8_t *p = host + O_IN; memset(p, 0, 64);
    wr_le(p + 0, 4, HOST_ORG + O_SPEC); wr_le(p + 4, 4, frame); wr_le(p + 8, 2, dt); wr_le(p + 10, 2, x); wr_le(p + 12, 2, y); wr_le(p + 14, 2, w); wr_le(p + 16, 2, h); wr_le(p + 18, 2, bg);
    p[20] = (uint8_t)force; p[21] = (uint8_t)paused; wr_le(p + 24, 4, pl); wr_le(p + 28, 4, pr); p[32] = (uint8_t)degrade; wr_le(p + 36, 4, 0);
    wr_le(p + 40, 2, rmsl); wr_le(p + 42, 2, rmsr); wr_le(p + 44, 2, (uint32_t)corr); p[46] = (uint8_t)cen; p[47] = (uint8_t)crest; p[48] = (uint8_t)flux; p[49] = (uint8_t)onset; p[50] = (uint8_t)sok;
    memcpy(host + O_SPEC, spec, 16);
}
static void build_api(void)
{
    uint8_t *t = host + O_API;
    memset(t, 0, 4 * N_API);
    wr_le(t, 4, (1u << 8) | (uint32_t)abi_minor);
    for (uint32_t k = 1; k < N_API; k++) {
        uint32_t val = STUB_BASE + 16u * k;
        if (k == 2) val = HOST_ORG + O_ROLE; else if (k == 3) val = HOST_ORG + O_PARAMS; else if (k == 4) val = HOST_ORG + O_STATS;
        if (k == 21 && abi_minor < 2) val = 0;                 /* scroll_left is ABI 1.2: a meter that declares less must not call it */
        wr_le(t + 4 * k, 4, val);
    }
}
static int do_load(const char *file)
{
    FILE *f = fopen(file, "rb"); if (!f) { printf("L err cannot open the pack file\n"); fflush(stdout); return 0; }
    static uint8_t buf[48 + WIN_SIZE + 16]; size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
    if (n < 48 || memcmp(buf, "TMPK", 4)) { printf("L err not a TMPK pack\n"); fflush(stdout); return 0; }
    hdr.size = rd_le(buf + 8, 4); hdr.entry = rd_le(buf + 16, 4); hdr.data_off = rd_le(buf + 24, 4); hdr.data_size = rd_le(buf + 28, 4);
    hdr.scr_org = rd_le(buf + 32, 4); hdr.bss = rd_le(buf + 36, 4); hdr.ps_off = rd_le(buf + 40, 4); hdr.ps_size = rd_le(buf + 44, 4);
    if (!hdr.size || hdr.size > WIN_SIZE || 48u + hdr.size > n || hdr.ps_off < hdr.size || hdr.ps_off > WIN_SIZE || hdr.ps_size > WIN_SIZE - hdr.ps_off || hdr.data_off > hdr.size || hdr.data_size > hdr.size - hdr.data_off ||
        hdr.entry >= hdr.size || hdr.scr_org < SCR_ORG || hdr.scr_org + ((hdr.data_size + 3u) & ~3u) + hdr.bss > SCR_ORG + SCR_SIZE) { printf("L err header fields outside the window or scratch\n"); fflush(stdout); return 0; }
    memset(win, 0, sizeof win); memcpy(win, buf + 48, hdr.size);
    memset(scr, 0, sizeof scr); memcpy(scr + (hdr.scr_org - SCR_ORG), win + hdr.data_off, hdr.data_size);
    memset(stk, stackfill_v, sizeof stk); memset(plane, 0, sizeof plane); ps_reset(&ps); struck = 0; dev_arms = dev_idx = 0; loaded = 1;
    printf("L ok\n"); fflush(stdout); return 1;
}

int main(void)
{
    char line[1024];
    setvbuf(stdout, NULL, _IOFBF, 1 << 16);
    memset(host, 0, sizeof host);
    while (fgets(line, sizeof line, stdin)) {
        char cmd[32]; if (sscanf(line, "%31s", cmd) != 1) continue;
        if (!strcmp(cmd, "quit")) break;
        if (!strcmp(cmd, "load")) { char *p = strchr(line, ' '); if (!p) continue; while (*p == ' ') p++; p[strcspn(p, "\r\n")] = 0; do_load(p); }
        else if (!strcmp(cmd, "profile")) { char w[16]; sscanf(line, "%*s %15s", w); profile_shipped = !strcmp(w, "shipped"); }
        else if (!strcmp(cmd, "abi")) { sscanf(line, "%*s %d", &abi_minor); }
        else if (!strcmp(cmd, "blend")) { sscanf(line, "%*s %d", &blend_ready_v); }
        else if (!strcmp(cmd, "stackfill")) { sscanf(line, "%*s %d", &stackfill_v); }
        else if (!strcmp(cmd, "limit")) { sscanf(line, "%*s %d", &soft_limit); }
        else if (!strcmp(cmd, "params")) { char *p = line + 6; int n = (int)strtol(p, &p, 10); for (int i = 0; i < n && i < 48; i++) wr_le(host + O_PARAMS + 2 * i, 2, (uint32_t)strtol(p, &p, 10)); }
        else if (!strcmp(cmd, "roles")) { char *p = line + 5; for (int i = 0; i < 21; i++) wr_le(host + O_ROLE + 2 * i, 2, (uint32_t)strtol(p, &p, 10)); }
        else if (!strcmp(cmd, "tick")) {
            unsigned v[20]; char hex[64] = "";
            int forced_known = 0; (void)forced_known;
            if (sscanf(line, "%*s %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d %u %u %u %u %u %63s", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13], (int *)&v[14], &v[15], &v[16], &v[17], &v[18], &v[19], hex) != 21 || strlen(hex) != 32) { printf("r bad 0 0 0 0 0\n"); continue; }
            if (!loaded) { printf("r bad 0 0 0 0 0\n"); continue; }
            if (struck) { printf("r struck 0 0 0 0 0\n"); fflush(stdout); continue; }
            uint8_t spec[16]; for (int i = 0; i < 16; i++) { unsigned b; sscanf(hex + 2 * i, "%2x", &b); spec[i] = (uint8_t)b; }
            put_in(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], (int32_t)v[14], v[15], v[16], v[17], v[18], v[19], spec);
            build_api(); memset(host + O_STATS, 0, 16);
            memset(reg, 0, sizeof reg); reg[2] = STK_TOP; reg[10] = HOST_ORG + O_IN; reg[11] = HOST_ORG + O_API; reg[1] = RET_ADDR; pc = WIN_ORG + hdr.entry;
            instr = cyc_extra = calls = cmds = misses = 0; faulted = 0; fault_msg[0] = 0; stack_lo = STK_TOP; in_reads_new = 0; memset(icache_tag, 0, sizeof icache_tag);
            const uint32_t wd = wd_limit((uint32_t)soft_limit);
            const int st = run_tick(wd);
            const uint32_t spent = cycles_now(), used = STK_TOP - stack_lo;
            const char *verdict = st == 0 ? "ok" : st == 1 ? "cut" : "fault";
            if (st == 2) printf("f %u %u %s\n", fault_pc - WIN_ORG < WIN_SIZE ? fault_pc - WIN_ORG : fault_pc, fault_addr, fault_msg);
            if (in_reads_new && abi_minor < 1) printf("n in-fields-1.1 %u\n", in_reads_new);
            int judged = 0;
            if (st == 1) { struck = 1; }
            else if (st == 0 && !v[7]) { judged = 1; if (ps_judge(&ps, spent, (uint32_t)soft_limit)) struck = 1; }
            printf("r %s %u %llu %u %u %u wd=%u strikes=%u clean=%u judged=%d struck=%d\n", verdict, spent, (unsigned long long)instr, cmds, reg[10], used, wd, ps.strikes, ps.clean, judged, struck);
            fflush(stdout);
        }
    }
    fflush(stdout);
    return 0;
}
