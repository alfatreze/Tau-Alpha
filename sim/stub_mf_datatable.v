// Simulation stand-in for the APF datatable RAM (src/fpga/apf/mf_datatable.v is an altsyncram wrapper that Icarus does not have): core_bridge_cmd.v instantiates it for the data slot table,
// which the benches that drive the bridge for other purposes (sim/tb_rtc_cdc.v) never read. Same ports; a plain dual-port memory.
module mf_datatable (
    input  wire [9:0]  address_a, address_b,
    input  wire        clock_a, clock_b,
    input  wire [31:0] data_a, data_b,
    input  wire        wren_a, wren_b,
    output reg  [31:0] q_a, q_b
);
    reg [31:0] mem [0:1023];
    always @(posedge clock_a) begin if (wren_a) mem[address_a] <= data_a; q_a <= mem[address_a]; end
    always @(posedge clock_b) begin if (wren_b) mem[address_b] <= data_b; q_b <= mem[address_b]; end
endmodule
