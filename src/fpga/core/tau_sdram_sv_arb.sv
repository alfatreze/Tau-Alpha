// Tau V SDRAM scheduler: port arbiter. MIT licence.
// Clean-room, from docs/features/SDRAM_CONTROLLER_CONTRACT.md v0.6 section 5 (Arbitration).
//
// Order of preference: (1) an urgent port (lowest index among the urgent candidates); (2) the same port as the last
// grant when its next piece is in the same row (`cont`), unless another port has waited longer than the starvation
// limit; (3) weighted round robin per piece. `grant` (with `grant_wrr` = the grant came from rule 3) updates the
// round-robin state. The result `sel_*` is a pure function of the registered state and the inputs.
`default_nettype none
module tau_sdram_sv_arb #(
    parameter integer NP     = 6,
    parameter [8*4-1:0] WEIGHT = {4'd1, 4'd1, 4'd1, 4'd1, 4'd2, 4'd4, 4'd4, 4'd4}   // port 0 is the LSB nibble
) (
    input  wire          clk,
    input  wire          rst,
    input  wire [NP-1:0] cand,          // port has a piece that may be granted now
    input  wire [NP-1:0] urgent,        // urgent & URGENT_CAPABLE (masked by the caller)
    input  wire          cont_ok,       // the last-granted port's next piece is in the same row and may go back to back
    input  wire [2:0]    last_port,
    input  wire          grant,
    input  wire [2:0]    grant_port,
    input  wire          grant_wrr,
    output reg           sel_v,
    output reg  [2:0]    sel_p,
    output reg           sel_wrr
);
    reg [2:0] ptr_q;
    reg [3:0] rem_q;
    function [3:0] wt(input [2:0] p);
        begin wt = WEIGHT[p*4 +: 4]; end
    endfunction

    integer k;
    reg [2:0] pp;
    reg       found;
    reg       u_found;
    reg [2:0] u_p;
    always @* begin
        sel_v = 1'b0; sel_p = 3'd0; sel_wrr = 1'b0;
        u_found = 1'b0; u_p = 3'd0;
        for (k = NP - 1; k >= 0; k = k - 1) begin
            if (urgent[k] & cand[k]) begin u_found = 1'b1; u_p = k[2:0]; end
        end
        found = 1'b0; pp = 3'd0;
        if (cand[ptr_q] && rem_q != 4'd0) begin found = 1'b1; pp = ptr_q; end
        else begin
            for (k = NP; k >= 1; k = k - 1) begin               // first candidate after ptr, wrapping, ptr itself last
                if (cand[(ptr_q + k) % NP]) begin found = 1'b1; pp = (ptr_q + k) % NP; end
            end
        end
        if (u_found)           begin sel_v = 1'b1; sel_p = u_p;       sel_wrr = 1'b0; end
        else if (cont_ok)      begin sel_v = 1'b1; sel_p = last_port; sel_wrr = 1'b0; end
        else if (found)        begin sel_v = 1'b1; sel_p = pp;        sel_wrr = 1'b1; end
    end

    always @(posedge clk) begin
        if (rst) begin
            ptr_q <= 3'd0; rem_q <= 4'd0;
        end else if (grant && grant_wrr) begin
            if (grant_port == ptr_q && rem_q != 4'd0) rem_q <= rem_q - 4'd1;
            else begin ptr_q <= grant_port; rem_q <= (wt(grant_port) == 4'd0) ? 4'd0 : wt(grant_port) - 4'd1; end
        end
    end
endmodule
`default_nettype wire
