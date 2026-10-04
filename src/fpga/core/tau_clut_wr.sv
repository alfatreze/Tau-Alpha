// tau_clut_wr.sv -- the CLUT write port of mp3_soc (R_CLUT_IDX / R_CLUT_DATA, Phase F B8), as a module so it can be simulated.
//
// Contract (what firmware relies on): a write to R_CLUT_IDX sets the index; every write to R_CLUT_DATA stores its value in the slot the
// index named AT THAT WRITE and advances the index by one (8-bit, wraps 255 -> 0). So a load that starts with IDX = s puts its n-th
// entry in slot (s + n) mod 256.
//
// B-575: the previous inline version raised the one-cycle write pulse and advanced the index on the same edge, with the address output
// simply following the index register: the pulse was seen a cycle later, after the index had already moved on, so every entry landed
// one slot too high (B-569). Here the address is REGISTERED with the pulse (waddr, wr and wdata all change on the same edge), so the
// consumer sees a consistent triple. BUG = 1 restores the old behaviour for the mutation test only.
module tau_clut_wr #(
    parameter BUG = 0
) (
    input  wire        clk,
    input  wire        idx_we,         // strobe: write to R_CLUT_IDX
    input  wire [7:0]  idx_d,
    input  wire        data_we,        // strobe: write to R_CLUT_DATA
    input  wire [15:0] data_d,
    output reg         wr    = 1'b0,   // one-cycle pulse per data write
    output wire [7:0]  waddr,
    output reg  [15:0] wdata = 16'd0
);
    reg [7:0] idx = 8'd0;
    reg [7:0] waddr_r = 8'd0;
    assign waddr = (BUG != 0) ? idx : waddr_r;
    always @(posedge clk) begin
        wr <= 1'b0;
        if (idx_we) idx <= idx_d;
        if (data_we) begin
            wdata   <= data_d;
            waddr_r <= idx;
            wr      <= 1'b1;
            idx     <= idx + 8'd1;
        end
    end
endmodule
