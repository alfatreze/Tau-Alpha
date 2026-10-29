// Synthesis + fit probe of Tau Alpha's scheduler path (tau_sdram_sched_a = PHY + sched_v with 3 ports + the graphics-master
// adapter). MIT licence. NOT part of any core: compiled by `tools/sdram_probe.py launch NAME --variant sched_a` only, which
// stages this file next to the base probe and points the QSF's TOP_LEVEL_ENTITY here. Same method as tau_sdram_probe_top.sv:
// the real dram_* pins and SDC, the core's PLL (mf_pllbase, 100 MHz outclk_3), every client signal one register away from a
// virtual pin (v_*), the slow statistics straight to virtual pins. Read the area from the rows u_sa|u_phy, u_sa|u_sched and
// u_sa|u_gfx of the fitter's entity table (the tool prints the u_phy / u_sched rows; u_sa itself holds the adapter and the
// reset synchroniser). The CPU mailbox half in clk_sys (tau_sdram_a_cpu, about 130 flops) is NOT in this probe: port 2 is
// driven directly in the scheduler protocol.
`default_nettype none
/* verilator lint_off PROCASSINIT */
module tau_sdram_probe_a_top (
    input  wire         clk_74a,
    input  wire         v_rst,
    // graphics master (p0 protocol + sideband)
    input  wire [24:0]  v_p0_addr,
    input  wire [15:0]  v_p0_data,
    input  wire [1:0]   v_p0_byte_en,
    input  wire [10:0]  v_p0_wr_len,
    input  wire         v_p0_wr_stream,
    input  wire         v_p0_wr_req,
    input  wire         v_p0_rd_req,
    input  wire         v_p0_end_burst_req,
    input  wire         v_p0_is_fill,
    input  wire [9:0]   v_p0_rd_len,
    output wire [15:0]  v_p0_q,
    output wire         v_p0_available,
    output wire         v_p0_ready,
    output wire         v_p0_data_available,
    output wire [10:0]  v_wsrc_addr,
    input  wire [15:0]  v_wsrc_q,
    // CPU port (scheduler protocol)
    input  wire         v_cpu_req_valid,
    output wire         v_cpu_req_ready,
    input  wire         v_cpu_req_we,
    input  wire [24:0]  v_cpu_req_addr,
    input  wire [9:0]   v_cpu_req_len,
    output wire         v_cpu_w_next,
    output wire [9:0]   v_cpu_w_idx,
    input  wire [15:0]  v_cpu_w_data,
    input  wire [1:0]   v_cpu_w_be,
    output wire         v_cpu_r_valid,
    output wire [15:0]  v_cpu_r_data,
    output wire         v_cpu_r_last,
    output wire         v_cpu_done,
    // slow monitor outputs
    output wire         v_init_done,
    output wire         v_busy,
    output wire [95:0]  v_cnt_requests,
    output wire [95:0]  v_cnt_words,
    output wire [95:0]  v_cnt_busy,
    output wire [47:0]  v_cnt_bad,
    output wire [47:0]  v_cnt_worst_wait,
    output wire [47:0]  v_cnt_urgent_over,
    output wire [15:0]  v_dbg_pmiss,
    output wire         v_phy_fault,
    output wire [15:0]  v_fault_count,
    output wire [31:0]  v_refreshes,
    // the real SDRAM pins
    output wire [12:0]  dram_a,
    output wire [1:0]   dram_ba,
    inout  wire [15:0]  dram_dq,
    output wire [1:0]   dram_dqm,
    output wire         dram_clk,
    output wire         dram_cke,
    output wire         dram_ras_n,
    output wire         dram_cas_n,
    output wire         dram_we_n
);
    wire clk_unused0, clk_unused1, clk_unused2, clk, pll_locked;
    mf_pllbase u_pll (.refclk(clk_74a), .rst(1'b0), .outclk_0(clk_unused0), .outclk_1(clk_unused1),
                      .outclk_2(clk_unused2), .outclk_3(clk), .locked(pll_locked));

    // ---- client inputs: one register each (the reset is the PLL lock, as in the core, plus the virtual pin)
    reg         rst_i;
    reg [24:0]  p0_addr_q; reg [15:0] p0_data_q; reg [1:0] p0_be_q; reg [10:0] p0_wlen_q; reg p0_str_q, p0_wr_q, p0_rd_q, p0_end_q, p0_fill_q;
    reg [9:0]   p0_rlen_q; reg [15:0] wsq_q;
    reg         c_valid_q, c_we_q; reg [24:0] c_addr_q; reg [9:0] c_len_q; reg [15:0] c_wdata_q; reg [1:0] c_wbe_q;
    always @(posedge clk) begin
        rst_i <= ~pll_locked | v_rst;
        p0_addr_q <= v_p0_addr; p0_data_q <= v_p0_data; p0_be_q <= v_p0_byte_en; p0_wlen_q <= v_p0_wr_len; p0_str_q <= v_p0_wr_stream;
        p0_wr_q <= v_p0_wr_req; p0_rd_q <= v_p0_rd_req; p0_end_q <= v_p0_end_burst_req; p0_fill_q <= v_p0_is_fill; p0_rlen_q <= v_p0_rd_len;
        wsq_q <= v_wsrc_q;
        c_valid_q <= v_cpu_req_valid; c_we_q <= v_cpu_req_we; c_addr_q <= v_cpu_req_addr; c_len_q <= v_cpu_req_len;
        c_wdata_q <= v_cpu_w_data; c_wbe_q <= v_cpu_w_be;
    end

    wire [15:0] p0_q; wire p0_av, p0_rdy, p0_da; wire [10:0] wsa;
    wire c_ready, c_wnext, c_rvalid, c_rlast, c_done; wire [9:0] c_widx; wire [15:0] c_rdata;
    wire init_done, busy, phy_fault, rst_out;

    tau_sdram_sched_a #(.CNT_EN(1)) u_sa (
        .clk(clk), .rst_in(rst_i), .rst_out(rst_out), .init_done(init_done), .busy(busy),
        .p0_addr(p0_addr_q), .p0_data(p0_data_q), .p0_byte_en(p0_be_q), .p0_wr_len(p0_wlen_q), .p0_wr_stream(p0_str_q),
        .p0_wr_req(p0_wr_q), .p0_rd_req(p0_rd_q), .p0_end_burst_req(p0_end_q), .p0_is_fill(p0_fill_q), .p0_rd_len(p0_rlen_q),
        .p0_q(p0_q), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq_q),
        .cpu_req_valid(c_valid_q), .cpu_req_ready(c_ready), .cpu_req_we(c_we_q), .cpu_req_addr(c_addr_q), .cpu_req_len(c_len_q),
        .cpu_w_next(c_wnext), .cpu_w_idx(c_widx), .cpu_w_data(c_wdata_q), .cpu_w_be(c_wbe_q), .cpu_r_valid(c_rvalid),
        .cpu_r_data(c_rdata), .cpu_r_last(c_rlast), .cpu_done(c_done),
        .cnt_requests(v_cnt_requests), .cnt_words(v_cnt_words), .cnt_busy(v_cnt_busy), .cnt_bad(v_cnt_bad),
        .cnt_worst_wait(v_cnt_worst_wait), .cnt_urgent_over(v_cnt_urgent_over), .dbg_pmiss(v_dbg_pmiss),
        .phy_fault(v_phy_fault), .phy_fault_count(v_fault_count), .refreshes(v_refreshes),
        .dram_a(dram_a), .dram_ba(dram_ba), .dram_dq(dram_dq), .dram_dqm(dram_dqm), .dram_clk(dram_clk),
        .dram_cke(dram_cke), .dram_ras_n(dram_ras_n), .dram_cas_n(dram_cas_n), .dram_we_n(dram_we_n));
    assign v_init_done = init_done;
    assign v_busy = busy;

    // ---- client outputs: one register each
    reg [15:0] p0_q_q; reg p0_av_q, p0_rdy_q, p0_da_q; reg [10:0] wsa_q;
    reg c_ready_q, c_wnext_q, c_rvalid_q, c_rlast_q, c_done_q; reg [9:0] c_widx_q; reg [15:0] c_rdata_q;
    always @(posedge clk) begin
        p0_q_q <= p0_q; p0_av_q <= p0_av; p0_rdy_q <= p0_rdy; p0_da_q <= p0_da; wsa_q <= wsa;
        c_ready_q <= c_ready; c_wnext_q <= c_wnext; c_rvalid_q <= c_rvalid; c_rlast_q <= c_rlast; c_done_q <= c_done;
        c_widx_q <= c_widx; c_rdata_q <= c_rdata;
    end
    assign v_p0_q = p0_q_q; assign v_p0_available = p0_av_q; assign v_p0_ready = p0_rdy_q; assign v_p0_data_available = p0_da_q;
    assign v_wsrc_addr = wsa_q;
    assign v_cpu_req_ready = c_ready_q; assign v_cpu_w_next = c_wnext_q; assign v_cpu_w_idx = c_widx_q;
    assign v_cpu_r_valid = c_rvalid_q; assign v_cpu_r_data = c_rdata_q; assign v_cpu_r_last = c_rlast_q; assign v_cpu_done = c_done_q;
endmodule
`default_nettype wire
