// =============================================================================
// tau_signaltap_tap.sv -- fixed-name register bank for SignalTap (diagnostic builds only).
//
// Instantiated inside mp3_soc under `ifdef TAU_SIGNALTAP` and NOT listed in ap_core.qsf:
// the staged qsf of a SignalTap build adds it (tools/signaltap_proof_qsf_append.txt).
// Purpose: SignalTap needs stable node names; module ports and wires get merged away or
// renamed by synthesis, a preserved register named `tap[n]` does not. One extra clock of
// latency on every captured signal; no logic depends on it.
// =============================================================================
`default_nettype none
module tau_signaltap_tap (
    input  wire        clk,
    input  wire [31:0] d
);
    (* preserve, noprune *) reg [31:0] tap = 32'd0;
    always @(posedge clk) tap <= d;
endmodule
`default_nettype wire
