/* Player 2 register map (hardware facts: docs/MMIO_ALLOCATION.md). Only what a stage uses is listed; a stage adds its registers here and nowhere else. */
#ifndef P2_MMIO_H
#define P2_MMIO_H
#include <stdint.h>
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define CLK_HZ 66666667u                 /* Player 2 requires the CLK66 bitstream */

#define R_CYCLES     0x8000000Cu         /* free-running clk_sys cycle counter */
#define R_STAT0      0x80000010u         /* the four status words are visible to debug tooling; the interlock failure paints a pattern here */
#define R_STAT1      0x80000014u
#define R_STAT2      0x80000018u
#define R_STAT3      0x8000001Cu
#define R_PCM_ST     0x80000034u         /* [11:0] FIFO level, bit 16 empty, 17 full, 18 underrun (sticky) */
#define R_INPUT      0x8000003Cu         /* [15:0] buttons, active high */
#define R_VERSION    0x80000040u         /* CORE_VERSION */
#define R_FB_ADDR    0x80000048u
#define R_FB_SIZE    0x8000004Cu         /* {h[17:9], w[8:0]} */
#define R_FB_COLOR   0x80000050u         /* {bg[31:16], fg[15:0]} RGB565 */
#define R_FB_GO      0x80000054u         /* write: {sy[13:12], sx[11:10], glyph[9:3], op[1:0]}; read bit 0 = the command FIFO is full */
#define R_SPEC_ST    0x800000E4u
#define R_WAVE_ST    0x800000FCu
#define R_POLY_ST    0x80000110u
#define R_DBUF_DISP  0x8000011Cu
#define R_LPC_STATUS 0x8000013Cu
#define R_CYMO_STATUS 0x8000015Cu
#define R_GAIN_CTRL  0x80000168u
#define R_GAIN_CUR   0x80000170u
#define R_HAL_CTRL   0x80000178u
#define R_RING_BASE  0x80000184u
#define R_RING_SIZE  0x80000188u
#define R_RING_WPTR  0x8000018Cu
#define R_RING_RIDX  0x80000190u
#define R_RING_CTRL  0x80000194u         /* read bit 31 = the reader is built in */
#define R_RING_CNT   0x80000198u         /* pairs pushed into the FIFO by the reader */

#define KEY_UP     (1u << 0)
#define KEY_DOWN   (1u << 1)
#define KEY_LEFT   (1u << 2)
#define KEY_RIGHT  (1u << 3)
#define KEY_A      (1u << 4)
#define KEY_B      (1u << 5)
#define KEY_X      (1u << 6)
#define KEY_Y      (1u << 7)
#define KEY_START  (1u << 15)

#define PCM_LEVEL(s) ((s) & 0xFFFu)
static inline uint32_t cycles(void) { return REG(R_CYCLES); }
#endif
