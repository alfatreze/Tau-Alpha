// =============================================================================
// tau_sdram_addr_decode.sv -- Phase 2 CPU address-map contract.
//
// VexRiscv presents word addresses.  This module deliberately replaces the
// old broad "RAM or MMIO" classification with mutually exclusive windows.
// It is not connected to the CPU bus yet: the bounded Wishbone adapter is a
// separate Phase 2 preflight item.  Keeping decode standalone makes the
// no-alias property directly testable before any bus transaction is enabled.
// =============================================================================
`default_nettype none

module tau_sdram_addr_decode (
    input  wire [29:0] dadr,
    output wire        bram_cached,
    output wire        bram_uncached,
    output wire        mmio,
    output wire        sdram_cached,
    output wire        sdram_uncached,
    // SDRAM controller uses 16-bit-word addresses; one CPU word is two of
    // these. Valid only while either SDRAM output is asserted.
    output wire [24:0] sdram_addr,
    // PSRAM uncached window (B-016): 32 MiB, no cached counterpart. psram_word is the
    // 23-bit CPU-word offset the PSRAM bus wrapper takes ([22] chip, [21] die).
    output wire        psram_uncached,
    output wire [22:0] psram_word
);
    // Byte map, written as word addresses because that is the CPU interface:
    // 0x0000_0000..0003_FFFF cached BRAM     => 0x00000..0x0FFFF
    // 0x8000_0000..8000_0FFF MMIO page       => 0x20000000..200003FF
    // 0xA010_0000..A3FF_FFFF uncached SDRAM  => 0x28040000..28FFFFFF
    // 0xC000_0000..C003_FFFF uncached BRAM   => 0x30000000..3000FFFF
    // 0x4010_0000..43FF_FFFF cached SDRAM    => 0x10040000..10FFFFFF
    // 0xA400_0000..A5FF_FFFF uncached PSRAM  => 0x29000000..297FFFFF (adjacent to the
    //                                           SDRAM window, no overlap; no cached alias)
    // First 1 MiB of SDRAM remains a framebuffer/guard region.
    localparam [29:0] MMIO_BASE      = 30'h20000000;
    localparam [29:0] MMIO_LIMIT     = 30'h20000400;
    localparam [29:0] SDRAM_C_BASE   = 30'h10040000;
    localparam [29:0] SDRAM_C_LIMIT  = 30'h11000000;
    localparam [29:0] SDRAM_U_BASE   = 30'h28040000;
    localparam [29:0] SDRAM_U_LIMIT  = 30'h29000000;
    localparam [29:0] PSRAM_BASE     = 30'h29000000;
    localparam [29:0] PSRAM_LIMIT    = 30'h29800000;

    assign bram_cached   = (dadr[29:16] == 14'h0000);
    assign bram_uncached = (dadr[29:16] == 14'h3000);
    assign mmio          = (dadr >= MMIO_BASE) && (dadr < MMIO_LIMIT);
    assign sdram_cached  = (dadr >= SDRAM_C_BASE) && (dadr < SDRAM_C_LIMIT);
    assign sdram_uncached= (dadr >= SDRAM_U_BASE) && (dadr < SDRAM_U_LIMIT);
    assign psram_uncached= (dadr >= PSRAM_BASE)   && (dadr < PSRAM_LIMIT);
    assign psram_word    = dadr[22:0];

    // Both SDRAM aliases preserve the low 24 CPU-word offset. Convert it to
    // a 16-bit SDRAM-word address for the existing two-halfword bridge.
    assign sdram_addr = {dadr[23:0], 1'b0};
endmodule

`default_nettype wire
