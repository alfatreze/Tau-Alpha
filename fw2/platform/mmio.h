/* Player 2 register map (hardware facts: docs/MMIO_ALLOCATION.md). Only what a stage uses is listed; a stage adds its registers here and nowhere else. */
#ifndef P2_MMIO_H
#define P2_MMIO_H
#include <stdint.h>
/* Register access. The host tests build modules with P2_HOST and supply REG_RD/REG_WR themselves (a model of the hardware); on the Pocket they are plain volatile accesses. */
#ifndef P2_HOST
#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG_RD(a) REG(a)
#define REG_WR(a, v) (REG(a) = (v))
#endif
#define CLK_HZ 66666667u                 /* Player 2 requires the CLK66 bitstream */

#define R_CYCLES     0x8000000Cu         /* free-running clk_sys cycle counter */
#define R_STAT0      0x80000010u         /* the four status words are visible to debug tooling; the interlock failure paints a pattern here */
#define R_STAT1      0x80000014u
#define R_STAT2      0x80000018u
#define R_STAT3      0x8000001Cu
#define R_TGT_ID     0x80000020u         /* target (card) command channel: slot id, offset, destination, length, go/status. One command in flight (APF) */
#define R_TGT_OFF    0x80000024u
#define R_TGT_ADR    0x80000028u         /* destination byte offset in core RAM */
#define R_TGT_LEN    0x8000002Cu
#define R_TGT_GO     0x80000030u         /* write: 0 read(0180), 1 open file(0192), 2 get file(0190); read: [15:8] sequence counter, [4:2] result (0 = ok) */
#define R_DT_ADDR    0x80000060u         /* bridge datatable: word address, then data */
#define R_DT_DATA    0x80000064u
#define R_PS_ID      0x80000088u         /* 0x50535231 when the PSRAM expansion window is built in */
#define R_PS_CFG     0x800000A0u         /* bit 16: the CPU window to PSRAM is present */
#define R_PCM_RATE   0x80000038u         /* output tick increment: hz * 2^32 / CLK_HZ */
#define R_PCM_ST     0x80000034u         /* [11:0] FIFO level, bit 16 empty, 17 full, 18 underrun (sticky) */
#define R_INPUT      0x8000003Cu         /* [15:0] buttons, active high */
#define R_VERSION    0x80000040u         /* CORE_VERSION */
#define R_FB_ADDR    0x80000048u
#define R_FB_SIZE    0x8000004Cu         /* {h[17:9], w[8:0]} */
#define R_FB_COLOR   0x80000050u         /* {bg[31:16], fg[15:0]} RGB565 */
#define R_FB_GO      0x80000054u         /* write: {sy[13:12], sx[11:10], glyph[9:3], op[1:0]}; read bit 0 = the command FIFO is full */
#define R_SDR_ADDR   0x80000074u         /* SDRAM mailbox: the CPU writes one framebuffer word (two RGB565 pixels) at a pixel address */
#define R_SDR_DATA   0x80000078u
#define R_SDR_CTRL   0x8000007Cu         /* write 0x3F = a full-word write */
#define R_SDR_STATUS 0x80000084u         /* bit 0 = busy */
#define R_SPEC_ST    0x800000E4u
#define R_WAVE_ST    0x800000FCu
#define R_POLY_ST    0x80000110u
#define R_DBUF_DISP  0x8000011Cu
#define R_LPC_STATUS 0x8000013Cu
#define R_CYMO_STATUS 0x8000015Cu
#define R_CYMO_CTRL  0x80000150u         /* bit 2 = LIVE_ENABLE (sticky): hands the real audio path to the resampler */
#define R_GAIN_CTRL  0x80000168u
#define R_GAIN_TARGET 0x8000016Cu        /* Q15, 32768 = unity */
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
#define KEY_L1     (1u << 8)
#define KEY_R1     (1u << 9)
#define KEY_SELECT (1u << 14)
#define KEY_START  (1u << 15)

#define PCM_LEVEL(s) ((s) & 0xFFFu)
/* output tick increment for a rate, evaluated by the compiler (no runtime 64-bit divide on the target) */
#define RATE_INC(hz) ((uint32_t)(((uint64_t)(hz) << 32) / CLK_HZ))
static inline uint32_t cycles(void) { return REG_RD(R_CYCLES); }
#endif
