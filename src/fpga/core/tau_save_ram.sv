// =============================================================================
// tau_save_ram.sv -- the RAM behind the meter-settings save slot (docs/features/meters/METER_SAVE_FILE_SPEC.md).
//
// One dual-clock RAM shared by two masters that never run at the same time:
//   * the APF bridge (clk_74a): at boot it WRITES the slot's file into the RAM (the slot is `nonvolatile`, with an `address` in the
//     0x3xxxxxxx region); at Quit it READS the RAM back, word by word, and the Pocket writes those words over the file.
//   * the CPU (clk_sys), through three MMIO registers (index, data with auto-increment, status): the firmware reads the settings at boot
//     and writes every change at once ("write-through"), so Quit may come at any moment.
// They cannot collide by construction, not by arbitration: the CPU is held in reset until APF has delivered every slot
// (dataslot_allcomplete), and at Quit APF sends 0010 Reset Enter BEFORE it reads the slots back. The memory is true dual port,
// so even a stray overlap on DIFFERENT addresses is harmless; the same address at the same instant is undefined and the protocol forbids it.
//
// BRIDGE SIDE (the read path is the first thing that could have killed the design): core_top.v owns the bridge_rd_data mux (it already serves
// the interact persist words at 0x2xxxxxxx from a register file in clk_74a). The shell latches the address, waits four clk_74a cycles, then samples
// bridge_rd_data (src/fpga/apf/io_bridge_peripheral.v, ST_READ_0). So the sampling itself is not the issue; the PROTOCOL is: reads are buffered by one word, see READ below.
//   bridge_hit   = the address is in this module's region and inside the RAM (above it: reads return 0, writes are dropped)
//   bridge_rdata = the word (valid when bridge_hit, otherwise 0)
// File byte order: bridge_endian_little = 0, so the first file byte is bits [31:24] of the bridge word. The RAM keeps the bridge words as they come;
// the CPU port swaps the bytes (CPU_LE = 1), so a little-endian CPU reading a word sees file bytes b0 b1 b2 b3 as 0xb3b2b1b0, i.e. memcpy order.
//
// CPU SIDE: IDX (write: word index, read: current index), DATA (read: the word at the index, write: store it; both step the index by one),
// ST (read only). Reads are sampled by the bus one cycle after the request, so the index steps one cycle after the request pulse (the word that is
// read is the word at the index the CPU set; the data register lags the index by one cycle, which is exactly the zero slack back-to-back MMIO accesses leave,
// so the firmware accessor puts one status read between an index write and the first data read, and the real-CPU bench sim/tb_save_soc.v proves both ways). See sim/tb_tau_save_ram.v for the exact bus cycles it models.
// =============================================================================
`default_nettype none

module tau_save_ram #(
    parameter AW        = 11,          // words = 2**AW: 11 -> 8 KB (8 M10K blocks at 32 bits), 10 -> 4 KB
    parameter REGION    = 4'h3,        // bridge_addr[31:28]
    parameter CPU_LE    = 1,
    parameter BUG       = 0            // mutation hooks for sim/tb_tau_save_ram.v (non-zero must make the bench fail)
) (
    // ---- APF bridge, clk_74a ----
    input  wire        clk_74a,
    input  wire [31:0] bridge_addr,
    input  wire        bridge_rd,
    input  wire        bridge_wr,
    input  wire [31:0] bridge_wr_data,
    output wire        bridge_hit,
    output wire [31:0] bridge_rdata,

    // ---- CPU, clk_sys (all strobes are one-cycle pulses from tau_soc's MMIO decode) ----
    input  wire        clk_sys,
    input  wire        rst_sys,        // active high: the CPU reset
    input  wire        idx_we,
    input  wire [31:0] idx_wd,
    input  wire        data_we,        // the registered write pulse; data_wd is valid in the same cycle
    input  wire [31:0] data_wd,
    input  wire        data_req,       // the read REQUEST pulse (the bus samples the data one cycle later)
    output wire [31:0] idx_rd,
    output wire [31:0] data_rd,
    output wire [31:0] status          // [31] present, [30] the bridge has written this RAM, [28:24] AW (words = 2**AW), [15:0] words the bridge wrote
);
    localparam WORDS = 1 << AW;

    // ---------------------------------------------------------------- the memory ---
    (* ramstyle = "M10K, no_rw_check" *) reg [31:0] mem [0:WORDS-1];
    integer ii;
    initial for (ii = 0; ii < WORDS; ii = ii + 1) mem[ii] = 32'd0;

    // ---- port A: the bridge, clk_74a ----
    wire        a_region = (bridge_addr[31:28] == REGION);
    wire        a_inside = (BUG == 5) ? 1'b1 : (bridge_addr[27:AW+2] == {(28-AW-2){1'b0}});     // BUG 5: no range check (a write past the end wraps onto word 0)
    wire [AW-1:0] a_idx  = bridge_addr[AW+1:2];
    assign      bridge_hit = a_region & a_inside;
    wire        a_we     = bridge_wr & ((BUG == 2) ? 1'b1 : a_region) & a_inside;                // BUG 2: no region check (any bridge write lands here)
    // READ: buffered by ONE word, as the APF shell defines it (src/fpga/apf/io_bridge_peripheral.v header, docs "Bus Communication", agg23's data_unloader): in a read
    // transaction the shell hands the host the value bridge_rd_data holds AFTER four clk_74a cycles, then pulses bridge_rd (the strobe) with the address still on the bus;
    // the strobe is the request for THAT address, and the core has until the next transaction to drive the word. So the word returned in a transaction is the one of the
    // PREVIOUS strobe's address, and the Pocket reads a file with one extra, discarded transaction. This module latches the address at the strobe (only a strobe in its own
    // region) and returns that word. Answering for the LIVE address instead delivered file word j = RAM word j+1 on a Pocket (TAU DEV 227, B-1123 to B-1125; BUG 1 below).
    reg           brd_d = 1'b0;
    reg  [AW-1:0] r_idx = {AW{1'b0}};
    reg           r_inside = 1'b0;                                                                // the latched address is inside the RAM (past the end: the word is zero)
    reg  [31:0]   a_q;
    wire          rd_strobe = bridge_rd & ~brd_d;
    // ONE address per port, or Quartus cannot map the array to an M10K (the first version of this fix gave port A a write address AND a separate read address: synthesis failed with
    // "multiple constant drivers", B-1129). A bridge write and a bridge read never overlap (the load is before dataslot_allcomplete, the flush after Reset Enter), so the port's address
    // is the write address during a write and the latched read address otherwise.
    wire [AW-1:0] a_addr = a_we ? a_idx : r_idx;
    always @(posedge clk_74a) begin
        if (a_we) mem[a_addr] <= bridge_wr_data;
        a_q <= mem[a_addr];                                                                       // the word of the latched address, one stage after the latch; the next transaction is at least four cycles away
        brd_d <= bridge_rd;
        if ((BUG == 1) ? 1'b1 : (rd_strobe && ((BUG == 7) ? 1'b1 : a_region))) begin r_idx <= a_idx; r_inside <= a_inside; end   // BUG 1: the address follows the live bus (the old answer); BUG 7: a strobe in ANY region latches
    end
    assign bridge_rdata = (a_region && r_inside) ? a_q : 32'd0;                                   // the live region only selects whether this RAM answers (core_top's mux does the same on the top nibble)

    // ---- bridge-side evidence for the CPU: how many words the bridge wrote, and a sticky 'written' flag ----
    wire        a_wr_count = a_we & a_region;
    wire [15:0] bwr_n;
    tau_cdc_gray_ctr #(.WIDTH(16)) u_bwr (.clk_src(clk_74a), .rst_src(1'b0), .inc(a_wr_count), .clk_dst(clk_sys), .rst_dst(1'b0), .count_dst(bwr_n));

    // ---- port B: the CPU, clk_sys ----
    reg  [AW-1:0] idx = {AW{1'b0}};
    reg           rd_d = 1'b0;                                                                    // the read request, one cycle later (the bus samples the data in that cycle)
    reg  [31:0]   b_q;
    wire [31:0]   b_wd = CPU_LE ? ((BUG == 3) ? data_wd : {data_wd[7:0], data_wd[15:8], data_wd[23:16], data_wd[31:24]}) : data_wd;   // BUG 3: no byte swap on writes
    always @(posedge clk_sys) begin
        if (data_we) mem[idx] <= b_wd;
        b_q <= mem[idx];
    end
    always @(posedge clk_sys) begin
        rd_d <= data_req;
        if (rst_sys)              idx <= {AW{1'b0}};
        else if (idx_we)          idx <= idx_wd[AW-1:0];
        else if (data_we)         begin if (BUG != 6) idx <= idx + 1'b1; end                       // BUG 6: a write does not step the index
        else if (rd_d)            begin if (BUG != 4) idx <= idx + 1'b1; end                       // BUG 4: a read does not step the index
    end
    assign idx_rd  = {{(32-AW){1'b0}}, idx};
    assign data_rd = CPU_LE ? {b_q[7:0], b_q[15:8], b_q[23:16], b_q[31:24]} : b_q;

    // ---- status ----
    reg [2:0] wr_seen_s = 3'd0;
    always @(posedge clk_sys) wr_seen_s <= {wr_seen_s[1:0], (bwr_n != 16'd0)};
    assign status = {1'b1, wr_seen_s[2], 1'b0, AW[4:0], 8'd0, bwr_n};
endmodule

`default_nettype wire
