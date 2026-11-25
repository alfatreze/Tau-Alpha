/* rvprof: an RV32IM instruction-set simulator with a per-PC profile and a documented VexRiscv-like cycle model.
 * Written from the RISC-V unprivileged spec for the MP3 v2 scoping (docs/features/MP3_DECODER_V2_SCOPING_2026-11-25.md).
 * MEASUREMENT TOOL ONLY: it contains no decoder. Usage: rvprof <elf> <datafile> <profile-out>
 * MMIO (same as tools/rv32sim.py): 0xF0000000 putchar, +4 exit, +0x10/0x14/0x18 DMA dst/src/len, +0x1C file size.
 * Cycle model (an ESTIMATE; parameters below): ALU/LUI/AUIPC/store 1; load 1, +1 if the next instruction uses the loaded
 * register; MUL* 1, +2 if the very next instruction uses it, +1 if the one after does; DIV/REM 34; taken branch +2;
 * JAL +1; JALR +2. Instruction-fetch and data-cache misses are NOT modelled (hot code and data are on-chip RAM). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define MEMSZ (16u << 20)
#define MMIO 0xF0000000u
static uint8_t mem[MEMSZ];
static uint32_t reg[32], pc;
static uint64_t *cnt, *cyc, *tak;
static uint8_t *file;
static uint32_t fsize, dma_dst, dma_src, dma_len;
static uint8_t eb[8 << 20];
static uint32_t rd32(uint32_t a) { uint32_t v; memcpy(&v, mem + a, 4); return v; }
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
    if (argc < 4) { fprintf(stderr, "usage: rvprof elf data profile-out\n"); return 2; }
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
    cnt = calloc(MEMSZ / 4, 8); cyc = calloc(MEMSZ / 4, 8); tak = calloc(MEMSZ / 4, 8);
    pc = entry; uint64_t ninstr = 0, ncyc = 0; int pl = 0, pm = 0, pm2 = 0, halted = 0; uint32_t code = 0;
    while (!halted) {
        uint32_t ins = rd32(pc), op = ins & 0x7f, rd = (ins >> 7) & 31, f3 = (ins >> 12) & 7, rs1 = (ins >> 15) & 31, rs2 = (ins >> 20) & 31, npc = pc + 4;
        uint32_t a = reg[rs1], b = reg[rs2]; int32_t sa = (int32_t)a, sb = (int32_t)b; uint32_t v = 0; int c = 1, taken = 0, wr = 0, nl = 0, nm = 0;
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
            else switch (f3) {
                case 0: v = (uint32_t)(int8_t)mem[ad]; break;
                case 1: { int16_t h; memcpy(&h, mem + ad, 2); v = (uint32_t)(int32_t)h; break; }
                case 2: v = rd32(ad); break;
                case 4: v = mem[ad]; break;
                default: { uint16_t h; memcpy(&h, mem + ad, 2); v = h; }
                }
            break; }
        case 0x23: {
            int32_t imm = (((int32_t)ins >> 25) << 5) | ((ins >> 7) & 31); uint32_t ad = a + imm;
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
        cnt[pc / 4]++; cyc[pc / 4] += c; tak[pc / 4] += taken; ninstr++; ncyc += c; pc = npc;
    }
    FILE *o = fopen(argv[3], "w");
    for (uint32_t i = 0; i < MEMSZ / 4; i++) if (cnt[i]) fprintf(o, "%x %x %llu %llu %llu\n", i * 4, rd32(i * 4), (unsigned long long)cnt[i], (unsigned long long)cyc[i], (unsigned long long)tak[i]);
    fclose(o);
    fprintf(stderr, "[instr %llu cycles(model) %llu exit %u]\n", (unsigned long long)ninstr, (unsigned long long)ncyc, code);
    return code ? 1 : 0;
}
