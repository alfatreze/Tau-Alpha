/* rvprof: an RV32IM instruction-set simulator with a per-PC profile and a documented VexRiscv-like cycle model.
 * Written from the RISC-V unprivileged spec for the MP3 v2 scoping (docs/features/MP3_DECODER_V2_SCOPING_2026-11-25.md).
 * MEASUREMENT TOOL ONLY: it contains no decoder. Usage: rvprof <elf> <datafile> <profile-out> [--cache] [--imiss N] [--dmiss N] [--icache-bytes N] [--dcache-bytes N]
 * MMIO (same as tools/rv32sim.py): 0xF0000000 putchar, +4 exit, +0x10/0x14/0x18 DMA dst/src/len, +0x1C file size.
 * Cycle model (an ESTIMATE; parameters below): ALU/LUI/AUIPC/store 1; load 1, +1 if the next instruction uses the loaded
 * register; MUL* 1, +2 if the very next instruction uses it, +1 if the one after does; DIV/REM 34; taken branch +2;
 * JAL +1; JALR +2.
 * CACHE MODEL (--cache; WP-05, from the expert review Q1: the Player 2 CPU has a 4 KiB direct-mapped I$ and a 4 KiB direct-mapped D$ in front of the 192 KB BRAM,
 * VexRiscv_Full.v lines 5721-5725 and 6467-6468): both caches have 32-byte lines (128 lines), index = address bits 11:5, tag = the rest; both start cold.
 * Instruction fetch: a miss when the line is not the one in its slot; the line is then allocated. Load: the same (read-allocate). Store: WRITE-THROUGH and NO WRITE-ALLOCATE:
 * a store never allocates and never stalls beyond its 1 cycle (a write buffer is assumed; a store to a line that is present updates it, which changes nothing here).
 * Each miss adds a penalty (default 12 cycles, the review's 10-15; --imiss / --dmiss) to the instruction's cycles. Without --cache the model is the old cacheless one.
 * Addresses of the MMIO window (0xF0000000 and up) are never cached. Instruction counts do not depend on the cache model. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define MEMSZ (16u << 20)
#define MMIO 0xF0000000u
static uint8_t mem[MEMSZ];
static uint32_t reg[32], pc;
static uint64_t *cnt, *cyc, *tak, *imc, *dmc;
static uint8_t *file;
static uint32_t fsize, dma_dst, dma_src, dma_len;
static uint8_t eb[8 << 20];
static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, mem + a, 4); return v; }
/* cache model */
static int CACHE = 0, P_IMISS = 12, P_DMISS = 12; static uint32_t ICACHE_BYTES = 4096, DCACHE_BYTES = 4096;
static uint32_t *itag, *dtag; static uint32_t inl, dnl;
static uint64_t n_imiss, n_dmiss, n_loads, n_stores;
static int P_LOADUSE = 1, P_MUL1 = 2, P_MUL2 = 1, P_DIV = 34, P_BR = 2, P_JAL = 1, P_JALR = 2;
static int uses(uint32_t ins, int r)
{
    if (!r) return 0;
    uint32_t op = ins & 0x7f; int rs1 = (ins >> 15) & 31, rs2 = (ins >> 20) & 31;
    switch (op) {
    case 0x33: case 0x23: case 0x63: return rs1 == r || rs2 == r;
    case 0x13: case 0x03: case 0x67: return rs1 == r;
    default: return 0;
    }
}
int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: rvprof elf data profile-out [--cache] [--imiss N] [--dmiss N] [--icache-bytes N] [--dcache-bytes N]\n"); return 2; }
    for (int i = 4; i < argc; i++) {
        if (!strcmp(argv[i], "--cache")) CACHE = 1;
        else if (!strcmp(argv[i], "--imiss") && i + 1 < argc) P_IMISS = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dmiss") && i + 1 < argc) P_DMISS = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--icache-bytes") && i + 1 < argc) ICACHE_BYTES = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dcache-bytes") && i + 1 < argc) DCACHE_BYTES = (uint32_t)atoi(argv[++i]);
        else { fprintf(stderr, "bad option %s\n", argv[i]); return 2; }
    }
    inl = ICACHE_BYTES / 32; dnl = DCACHE_BYTES / 32;
    if (inl == 0 || (inl & (inl - 1)) || dnl == 0 || (dnl & (dnl - 1))) { fprintf(stderr, "cache sizes must be powers of two, 32 bytes or more\n"); return 2; }
    itag = malloc(inl * 4); dtag = malloc(dnl * 4); memset(itag, 0xFF, inl * 4); memset(dtag, 0xFF, dnl * 4);
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror("elf"); return 2; }
    size_t n = fread(eb, 1, sizeof eb, f); fclose(f); (void)n;
    uint32_t entry, phoff; uint16_t phes, phn;
    memcpy(&entry, eb + 24, 4); memcpy(&phoff, eb + 28, 4); memcpy(&phes, eb + 42, 2); memcpy(&phn, eb + 44, 2);
    for (int i = 0; i < phn; i++) {
        uint8_t *p = eb + phoff + i * phes; uint32_t t, off, va, fs, ms;
        memcpy(&t, p, 4); memcpy(&off, p + 4, 4); memcpy(&va, p + 8, 4); memcpy(&fs, p + 16, 4); memcpy(&ms, p + 20, 4);
        if (t != 1) continue;
        if (va + ms > MEMSZ) { fprintf(stderr, "segment too high\n"); return 2; }
        memcpy(mem + va, eb + off, fs); memset(mem + va + fs, 0, ms - fs);
    }
    f = fopen(argv[2], "rb");
    if (f) { fseek(f, 0, SEEK_END); fsize = ftell(f); fseek(f, 0, SEEK_SET); file = malloc(fsize); if (fread(file, 1, fsize, f) != fsize) return 2; fclose(f); }
    cnt = calloc(MEMSZ / 4, 8); cyc = calloc(MEMSZ / 4, 8); tak = calloc(MEMSZ / 4, 8); imc = calloc(MEMSZ / 4, 8); dmc = calloc(MEMSZ / 4, 8);
    pc = entry; uint64_t ninstr = 0, ncyc = 0; int pl = 0, pm = 0, pm2 = 0, halted = 0; uint32_t code = 0;
    while (!halted) {
        uint32_t ins = rd32(pc), op = ins & 0x7f, rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7, rs1 = (ins >> 15) & 31, rs2 = (ins >> 20) & 31, npc = pc + 4;
        uint32_t a = reg[rs1], b = reg[rs2]; int32_t sa = (int32_t)a, sb = (int32_t)b; uint32_t v = 0; int c = 1, taken = 0, wr = 0, nl = 0, nm = 0;
        int im = 0, dm = 0;
        if (CACHE) { uint32_t ln = pc >> 5, ix = ln & (inl - 1); if (itag[ix] != ln) { itag[ix] = ln; c += P_IMISS; im = 1; n_imiss++; } }
        if (uses(ins, pl)) c += P_LOADUSE;
        if (uses(ins, pm)) c += P_MUL1; else if (uses(ins, pm2)) c += P_MUL2;
        switch (op) {
        case 0x13: {
            int32_t imm = (int32_t)ins >> 20; wr = 1;
            switch (f3) {
            case 0: v = a + imm; break; case 1: v = a << rs2; break; case 2: v = sa < imm; break; case 3: v = a < (uint32_t)imm; break;
            case 4: v = a ^ imm; break; case 5: v = (ins & 0x40000000) ? (uint32_t)(sa >> rs2) : (a >> rs2); break; case 6: v = a | imm; break; default: v = a & imm;
            }
            break; }
        case 0x33:
            wr = 1;
            if (ins & 0x02000000) {
                switch (f3) {
                case 0: v = a * b; nm = 1; break;
                case 1: v = (uint32_t)(((int64_t)sa * sb) >> 32); nm = 1; break;
                case 2: v = (uint32_t)(((int64_t)sa * (int64_t)b) >> 32); nm = 1; break;
                case 3: v = (uint32_t)(((uint64_t)a * b) >> 32); nm = 1; break;
                case 4: c += P_DIV - 1; v = b == 0 ? ~0u : (sa == INT32_MIN && sb == -1) ? 0x80000000u : (uint32_t)(sa / sb); break;
                case 5: c += P_DIV - 1; v = b == 0 ? ~0u : a / b; break;
                case 6: c += P_DIV - 1; v = b == 0 ? a : (sa == INT32_MIN && sb == -1) ? 0 : (uint32_t)(sa % sb); break;
                default: c += P_DIV - 1; v = b == 0 ? a : a % b;
                }
            } else {
                int alt = ins & 0x40000000;
                switch (f3) {
                case 0: v = alt ? a - b : a + b; break; case 1: v = a << (b & 31); break; case 2: v = sa < sb; break; case 3: v = a < b; break;
                case 4: v = a ^ b; break; case 5: v = alt ? (uint32_t)(sa >> (b & 31)) : a >> (b & 31); break; case 6: v = a | b; break; default: v = a & b;
                }
            }
            break;
        case 0x03: {
            int32_t imm = (int32_t)ins >> 20; uint32_t ad = a + imm; wr = 1; nl = 1;
            if (ad >= MMIO) { if (ad == MMIO + 0x18) v = dma_len; else if (ad == MMIO + 0x1c) v = fsize; else { fprintf(stderr, "bad mmio ld %x\n", ad); return 3; } }
            else if (ad + 4 > MEMSZ) { fprintf(stderr, "oob ld %x pc %x\n", ad, pc); return 3; }
            else {
                n_loads++;
                if (CACHE) { uint32_t ln = ad >> 5, ix = ln & (dnl - 1); if (dtag[ix] != ln) { dtag[ix] = ln; c += P_DMISS; dm = 1; n_dmiss++; } }
                switch (f3) {
                case 0: v = (uint32_t)(int8_t)mem[ad]; break;
                case 1: { int16_t h; memcpy(&h, mem + ad, 2); v = (uint32_t)(int32_t)h; break; }
                case 2: v = rd32(ad); break;
                case 4: v = mem[ad]; break;
                default: { uint16_t h; memcpy(&h, mem + ad, 2); v = h; }
                } }
            break; }
        case 0x23: {
            int32_t imm = (((int32_t)ins >> 25) << 5) | ((ins >> 7) & 31); uint32_t ad = a + imm; if (ad < MMIO) n_stores++;   /* write-through, no write-allocate: no cache state changes, no stall */
            if (ad >= MMIO) {
                uint32_t o = ad - MMIO;
                if (o == 0) putchar(b & 255); else if (o == 4) { halted = 1; code = b; } else if (o == 0x10) dma_dst = b; else if (o == 0x14) dma_src = b;
                else if (o == 0x18) { uint32_t k = b; if (dma_src + k > fsize) k = fsize > dma_src ? fsize - dma_src : 0; memcpy(mem + dma_dst, file + dma_src, k); dma_len = k; }
                else { fprintf(stderr, "bad mmio st\n"); return 3; }
            } else if (ad + 4 > MEMSZ) { fprintf(stderr, "oob st %x pc %x\n", ad, pc); return 3; }
            else { if (f3 == 0) mem[ad] = b; else if (f3 == 1) memcpy(mem + ad, &b, 2); else memcpy(mem + ad, &b, 4); }
            break; }
        case 0x63: {
            int t;
            switch (f3) { case 0: t = a == b; break; case 1: t = a != b; break; case 4: t = sa < sb; break; case 5: t = sa >= sb; break; case 6: t = a < b; break; default: t = a >= b; }
            if (t) { int32_t imm = (((int32_t)ins >> 31) << 12) | (((ins >> 7) & 1) << 11) | (((ins >> 25) & 0x3f) << 5) | (((ins >> 8) & 0xf) << 1); npc = pc + imm; taken = 1; c += P_BR; }
            break; }
        case 0x6f: { int32_t imm = (((int32_t)ins >> 31) << 20) | (((ins >> 12) & 0xff) << 12) | (((ins >> 20) & 1) << 11) | (((ins >> 21) & 0x3ff) << 1); v = npc; wr = 1; npc = pc + imm; c += P_JAL; taken = 1; break; }
        case 0x67: { int32_t imm = (int32_t)ins >> 20; v = npc; wr = 1; npc = (a + imm) & ~1u; c += P_JALR; taken = 1; break; }
        case 0x37: v = ins & 0xfffff000; wr = 1; break;
        case 0x17: v = pc + (ins & 0xfffff000); wr = 1; break;
        case 0x0f: break;
        case 0x73:
            if (f3 == 0) { halted = 1; code = reg[10]; }
            else { uint32_t csr = ins >> 20; v = (csr == 0xC00 || csr == 0xB00) ? (uint32_t)ncyc : (csr == 0xC02 || csr == 0xB02) ? (uint32_t)ninstr : 0; wr = 1; }
            break;
        default: fprintf(stderr, "illegal op %02x at %x\n", op, pc); return 3;
        }
        if (wr && rd) reg[rd] = v;
        pm2 = pm; pm = (nm && rd) ? (int)rd : 0; pl = (nl && rd) ? (int)rd : 0;
        cnt[pc / 4]++; cyc[pc / 4] += c; tak[pc / 4] += taken; imc[pc / 4] += im; dmc[pc / 4] += dm; ninstr++; ncyc += c; pc = npc;
    }
    FILE *o = fopen(argv[3], "w");
    for (uint32_t i = 0; i < MEMSZ / 4; i++) if (cnt[i]) fprintf(o, "%x %x %llu %llu %llu %llu %llu\n", i * 4, rd32(i * 4), (unsigned long long)cnt[i], (unsigned long long)cyc[i], (unsigned long long)tak[i], (unsigned long long)imc[i], (unsigned long long)dmc[i]);
    fclose(o);
    fprintf(stderr, "[instr %llu cycles(model) %llu exit %u imiss %llu dmiss %llu loads %llu stores %llu cache %d]\n", (unsigned long long)ninstr, (unsigned long long)ncyc, code,
            (unsigned long long)n_imiss, (unsigned long long)n_dmiss, (unsigned long long)n_loads, (unsigned long long)n_stores, CACHE);
    return code ? 1 : 0;
}
