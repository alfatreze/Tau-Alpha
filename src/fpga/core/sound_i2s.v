// MIT License
// Copyright (c) 2022 Adam Gastineau
//
// A very simple audio i2s bridge to APF, based on Analogue example code.
//
// Usage:
//   - CHANNEL_WIDTH: width of the per-channel audio bus, any value >= 1.
//                   Values < 15 are zero-padded LSB. Values > 15 keep top 15 bits.
//   - SIGNED_INPUT:  0 for unsigned positive-only audio (silence at 0),
//                   1 for signed two's-complement audio (silence at 0).
//   - clk_audio is the game core clock domain (clk_sys)
//   - clk_mclk is a PLL-synthesised 12.288 MHz (mf_pllbase_mclk, its own dedicated PLL -- B-460/B-462:
//     it cannot share the main mf_pllbase's VCO with clk_vid/clk_sdram) driving the serializer and the
//     MCLK output pin directly.
//
// B-457 (Cymo 44.1 kHz investigation): this used to synthesise ~12.288 MHz
// itself, from clk_74a, via a phase accumulator (DDA) -- `audgen_accum`
// overflowing a 742500 threshold at an average rate of 245760/742500 per
// clk_74a cycle, a ratio that does not reduce to a power of two (GCD 6,
// 245760/6=40960, 742500/6=123750 -- still not clean). That is a REAL,
// hardware-only source of jitter on every MCLK edge -- and since SCLK/LRCK/
// the serializer below were all derived from detecting MCLK's own toggle,
// that jitter propagated straight through to every DAC bit-clock edge. RTL
// simulation (checking logical VALUES, not real inter-edge timing) could
// never have caught this -- exactly why the decisive digital-domain SINAD
// test (sim/test_cymo_i2s_rate.py) matched the ideal-hold prediction almost
// exactly while real hardware measured ~17 dB worse. Replaced with a real
// PLL output using fractional-N (sigma-delta, noise-shaped) synthesis --
// the same mechanism outclk_1/2's 12.000 MHz already relies on -- which
// pushes quantisation noise to frequencies far outside the audio band,
// instead of a naive accumulator's much coarser, in-band error. The
// serializer below now runs entirely inside this one clean clock domain
// (previously it ran in clk_74a, edge-detecting the accumulator's own
// jittery toggle -- a second layer of indirection this removes too).

`default_nettype none

module sound_i2s #(
    parameter CHANNEL_WIDTH = 8,
    parameter SIGNED_INPUT  = 0
) (
    input wire clk_mclk,
    input wire clk_audio,

    input wire [CHANNEL_WIDTH-1:0] audio_l,
    input wire [CHANNEL_WIDTH-1:0] audio_r,

    output wire audio_mclk,
    output reg  audio_lrck,
    output reg  audio_dac
);

  // MCLK is the PLL clock itself now -- nothing left to synthesise here.
  assign audio_mclk = clk_mclk;

  // ----------------------------------------------------------------
  // Pack audio channels into 32-bit sample word.
  // Each channel occupies a 16-bit signed slot ([15] sign, [14:0] magnitude).
  //   SIGNED_INPUT=1: audio_l/r is already signed -- pass through MSB-aligned.
  //   SIGNED_INPUT=0: audio_l/r is unsigned positive-only (silence at 0).
  //                   Map to a positive signed value: top bit forced 0,
  //                   audio bits placed in the 15-bit magnitude slot.
  //
  // NOTE: the earlier version of this module hardcoded an 8-bit slice
  // (`audgen_sampdata[14:7] = audio_l`). With CHANNEL_WIDTH > 8 Verilog
  // silently truncated to the low byte, dropping the actual signal and
  // leaving only LSB noise at full DAC range (crunchy / clipped sound).
  // ----------------------------------------------------------------
  // 15-bit magnitude, MSB-aligned from audio_l/r:
  wire [14:0] left_mag  = (CHANNEL_WIDTH >= 15)
        ? audio_l[CHANNEL_WIDTH-1 -: 15]
        : { audio_l, {(15 - CHANNEL_WIDTH){1'b0}} };
  wire [14:0] right_mag = (CHANNEL_WIDTH >= 15)
        ? audio_r[CHANNEL_WIDTH-1 -: 15]
        : { audio_r, {(15 - CHANNEL_WIDTH){1'b0}} };

  // For SIGNED_INPUT, the top bit of audio_l/r is the sign; keep it.
  // For unsigned (default), force sign bit to 0 (positive-only).
  wire left_sign  = (SIGNED_INPUT != 0) ? audio_l[CHANNEL_WIDTH-1] : 1'b0;
  wire right_sign = (SIGNED_INPUT != 0) ? audio_r[CHANNEL_WIDTH-1] : 1'b0;

  wire [31:0] audgen_sampdata;
  assign audgen_sampdata[15]    = left_sign;
  assign audgen_sampdata[14:0]  = left_mag;
  assign audgen_sampdata[31]    = right_sign;
  assign audgen_sampdata[30:16] = right_mag;

  // ----------------------------------------------------------------
  // Cross from clk_audio (game domain) to clk_mclk (serializer domain)
  // via sync_fifo. Write whenever sample changes.
  // ----------------------------------------------------------------
  reg write_en = 0;
  reg [CHANNEL_WIDTH-1:0] prev_left  = 0;
  reg [CHANNEL_WIDTH-1:0] prev_right = 0;

  always @(posedge clk_audio) begin
    prev_left  <= audio_l;
    prev_right <= audio_r;
    write_en   <= 0;
    if (audio_l != prev_left || audio_r != prev_right)
      write_en <= 1;
  end

  wire [31:0] audgen_sampdata_s;

  sync_fifo #(
      .WIDTH(32)
  ) i_sync_fifo (
      .clk_write(clk_audio),
      .clk_read (clk_mclk),
      .write_en (write_en),
      .data_in  (audgen_sampdata),
      .data_out (audgen_sampdata_s)
  );

  // ----------------------------------------------------------------
  // SCLK = MCLK / 4 = 3.072 MHz, a plain synchronous counter in the SAME
  // clock domain as MCLK itself (no more cross-domain edge-detection of a
  // jittery toggle -- clk_mclk already IS the clean clock). sclk_div == 3
  // is the clk_mclk cycle right before SCLK's own bit (sclk_div[1]) falls
  // 3->0 on the next edge -- i.e. exactly SCLK's falling edge, the same
  // instant the original design's `prev_audgen_sclk && ~audgen_sclk` fired.
  //
  // Serialize: shift out on falling edge of SCLK
  // 32 bits per channel (16 active + 16 padding), stereo = 64 SCLK cycles
  // LRCK toggles every 32 SCLK cycles -> 3.072MHz / 64 = 48kHz
  // ----------------------------------------------------------------
  reg [1:0]  sclk_div = 0;
  reg [31:0] audgen_sampshift = 0;
  reg [4:0]  audio_lrck_cnt  = 0;

  always @(posedge clk_mclk) begin
    sclk_div <= sclk_div + 1'b1;

    if (sclk_div == 2'd3) begin
      // Output next bit on falling SCLK edge
      audio_dac <= audgen_sampshift[31];

      audio_lrck_cnt <= audio_lrck_cnt + 1'b1;

      if (audio_lrck_cnt == 31) begin
        // Toggle LRCK, reload sample at start of left channel
        audio_lrck <= ~audio_lrck;
        if (~audio_lrck)
          audgen_sampshift <= audgen_sampdata_s;
      end else if (audio_lrck_cnt < 16) begin
        // Shift for first 16 clocks of each channel, pad rest with 0
        audgen_sampshift <= {audgen_sampshift[30:0], 1'b0};
      end
    end
  end

endmodule
