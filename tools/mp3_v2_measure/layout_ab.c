/* MP3 v2 WP-05: synthetic loops for the cache model (tools/mp3_v2_measure/rvprof.c --cache). Own code (Tau Alpha, MIT). Contains no decoder.
 *
 * Two experiments, selected at compile time, each printing a checksum and exiting through the simulator's MMIO exit:
 *
 * (1) D$ layout A/B (-DEXP=1): the IMDCT -> DCT32 handoff of one channel. 18 slots x 32 subbands = 576 words.
 *       -DLAYOUT=0  "column-strided": the IMDCT writes each subband's 18 values contiguously ([subband][slot]) and the DCT32 reads slot s as the column
 *                   out[sb * 18 + s], sb = 0..31, a stride of 72 bytes (the layout Helix and minimp3 read);
 *       -DLAYOUT=1  "[slot][subband]": the IMDCT writes with stride (out[s * 32 + sb]) and the DCT32 reads 32 contiguous words per slot (the v2 plan, section 3.1).
 *     Stores are write-through and never allocate, so a strided store costs what a sequential one costs; only the loads of the DCT32 pass differ. Each slot also reads a
 *     32-word coefficient table (4 lines, sets 124..127: never in the way) and, optionally, a SCRATCH area of 24 lines read once per slot (the decoder's per-slot stack
 *     arrays, the window words, the log): -DSCR_SET=n puts its lines at sets n..n+23 of the 128. The output array starts at set 0 and covers sets 0..71, so SCR_SET=0 makes the
 *     scratch alias 24 lines of it and SCR_SET=100 does not; -DSCR_SET=-1 leaves the scratch out. Two channels run in turn through the same output array (a second array would cover the other sets and leave no set clear).
 *
 * (2) I$ alias (-DEXP=2): two straight-line functions of 1,024 bytes called alternately 4,000 times. -DIALIAS=1 starts both at the same offset modulo 4,096
 *     (the same 32 sets of the 128: they evict each other on every call), -DIALIAS=0 places the second 2,048 bytes further on (disjoint sets: after the first pass every
 *     fetch hits). Symbols f1 and f2 are what tools/mp3_v2_measure/alias_check.py finds in the ELF. */
#include <stdint.h>
#define IO(a) (*(volatile uint32_t *)(a))
static void pr(const char *s) { while (*s) IO(0xF0000000) = (uint8_t)*s++; }
static void pn(unsigned v) { char b[12]; int i = 11; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + v % 10; v /= 10; } pr(b + i); }

#ifndef EXP
#define EXP 1
#endif
#ifndef NGR
#define NGR 200
#endif
#ifndef LAYOUT
#define LAYOUT 1
#endif
#ifndef SCR_SET
#define SCR_SET -1
#endif
#ifndef IALIAS
#define IALIAS 1
#endif

#if EXP == 1
static uint32_t src[2][576], ovl[2][576], vout[64];
static uint32_t out[576] __attribute__((aligned(4096)));          /* one array for both channels in turn: sets 0..71 */
static struct { uint8_t pad[124 * 32]; uint32_t t[32]; } tblw __attribute__((aligned(4096)));
#if SCR_SET >= 0
static struct { uint8_t pad[SCR_SET * 32]; uint32_t t[192]; } scr __attribute__((aligned(4096)));
#endif
static inline int ix(int sb, int s) { return LAYOUT == 0 ? sb * 18 + s : s * 32 + sb; }
int main(void)
{
    uint32_t sum = 0;
    for (int i = 0; i < 32; i++) tblw.t[i] = 0x10001u * (uint32_t)(i + 1);
#if SCR_SET >= 0
    for (int i = 0; i < 192; i++) scr.t[i] = 0x9E3779B9u * (uint32_t)(i + 1);     /* written, so the compiler cannot fold the reads */
#endif
    for (int c = 0; c < 2; c++) for (int i = 0; i < 576; i++) { src[c][i] = (uint32_t)i * 2654435761u; ovl[c][i] = (uint32_t)i * 40503u; }
    for (int g = 0; g < NGR; g++) {
        for (int c = 0; c < 2; c++) {
            for (int sb = 0; sb < 32; sb++)                        /* the IMDCT-like pass: sequential reads of src and ovl, one store per value */
                for (int s = 0; s < 18; s++) {
                    uint32_t v = src[c][sb * 18 + s] + ovl[c][sb * 18 + s];
                    out[ix(sb, s)] = v;
                    ovl[c][sb * 18 + s] = v >> 1;
                }
            for (int s = 0; s < 18; s++) {                         /* the DCT32-like pass: 32 loads of out, 32 of the table, one multiply-add each */
                uint32_t acc = 0;
                for (int sb = 0; sb < 32; sb++) acc += out[ix(sb, s)] * tblw.t[sb];
#if SCR_SET >= 0
                for (int i = 0; i < 192; i += 8) acc += scr.t[i];
#endif
                vout[(c * 32 + s) & 63] = acc;                     /* a store: write-through, no allocate */
                sum += acc;
            }
        }
    }
    pr("sum="); pn(sum); pr("\n");
    return 0;
}
#else
#define ICALL 4000
/* two 1,024-byte straight-line functions (256 instructions each, the last is the return) */
#define BODY ".rept 255\n\taddi a0, a0, 1\n\t.endr\n\tret\n"
__asm__(".section .text.ialias,\"ax\",@progbits\n.balign 4096\n.global f1\n.type f1,@function\nf1:\n" BODY ".size f1, .-f1\n"
#if IALIAS
        ".balign 4096\n"
#else
        ".balign 4096\n.skip 2048\n"
#endif
        ".global f2\n.type f2,@function\nf2:\n" BODY ".size f2, .-f2\n.text\n");
extern unsigned f1(unsigned), f2(unsigned);
int main(void)
{
    unsigned a = 0;
    for (int i = 0; i < ICALL; i++) { a = f1(a); a = f2(a); }
    pr("a="); pn(a); pr("\n");
    return 0;
}
#endif
