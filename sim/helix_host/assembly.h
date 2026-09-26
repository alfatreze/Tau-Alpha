/* Portable host replacement for third_party/libhelix-mp3/real/assembly.h (which only knows ARM, x86-Windows and RISC-V inline asm).
 * Each function is the exact integer operation the RISC-V build performs: MULSHIFT32 = mulh, MADD64 = mulh:mul + add, SAR64 = arithmetic
 * shift, so the host results are bit-identical to the firmware's. Used only by sim/mp3_poly_probe.c. */
#ifndef _ASSEMBLY_H
#define _ASSEMBLY_H
#include <stdint.h>
typedef long long Word64;
static inline int MULSHIFT32(int x, int y) { return (int)(((Word64)x * (Word64)y) >> 32); }
static inline int FASTABS(int x) { int s = x >> 31; x ^= s; x -= s; return x; }
static inline int CLZ(int x) { int n = 0; if (!x) return 32; while (!(x & 0x80000000)) { n++; x <<= 1; } return n; }
static inline Word64 MADD64(Word64 sum, int a, int b) { return sum + (Word64)a * (Word64)b; }
static inline Word64 SHL64(Word64 x, int n) { return x << n; }
static inline Word64 SAR64(Word64 x, int n) { return x >> n; }
#endif
