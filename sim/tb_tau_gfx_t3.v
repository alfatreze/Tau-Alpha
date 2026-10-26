// ============================================================================
// tb_tau_gfx_t3.v -- Talos 2 T3 status block: idle (retire-counted), the FENCE opcode and its Gray counter,
// queue free slots and overflow count, and the scanout underflow counter.
//   make test-rtl-gfx-t3        (-Ptb_tau_gfx_t3.MUT=n must FAIL, n = 1..4, see below)
// MUT 1: the RTL hook BUG_FENCE_ANY: every retired command bumps the fence counter
// MUT 2: overflow not counted      MUT 3: idle ignores in-flight work     MUT 4: underflow check disabled
// The mutants are applied by forcing the DUT outputs in this bench, so the bench's own checks are what is tested.
// ============================================================================
`timescale 1ns/1ps
`default_nettype none

module t3_sdram_stub (
    input  wire        clk, input wire block,
    input  wire [24:0] p0_addr, input wire [15:0] p0_data, input wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0, output reg p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0, input wire [15:0] wsrc_q,
    output integer     writes_done = 0
);
    localparam IDLE = 0, STREAM = 1, CONST = 2, DONE = 3, READ = 4;
    integer st = IDLE, beats = 0;
    reg [24:0] a; reg [10:0] len; reg stream;
    always @(posedge clk) begin
        p0_ready <= 1'b0; p0_data_available <= 1'b0;
        case (st)
            IDLE: begin
                p0_available <= !block;
                if (!block && p0_rd_req) begin p0_available <= 1'b0; a <= p0_addr; st <= READ; end
                else if (!block && p0_wr_req) begin
                    p0_available <= 1'b0; len <= p0_wr_len; stream <= p0_wr_stream; wsrc_addr <= 0; beats = 0;
                    st <= p0_wr_stream ? STREAM : CONST;
                end
            end
            STREAM: begin wsrc_addr <= wsrc_addr + 1; if (wsrc_addr > 0) beats = beats + 1; if (beats == len) st <= DONE; end
            READ: begin p0_data_available <= 1'b1; p0_q <= a[15:0] + 16'd1; a <= a + 25'd1; if (p0_end_burst_req) st <= IDLE; end
            CONST: st <= DONE;
            DONE: begin writes_done = writes_done + 1; p0_ready <= 1'b1; st <= IDLE; end
        endcase
    end
endmodule

module tb_tau_gfx_t3;
    parameter MUT = 0;
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5    clk_sdram = ~clk_sdram;
    always #10   clk_sys   = ~clk_sys;
    always #41.7 clk_vid   = ~clk_vid;

    reg cmd_push = 0; reg [3:0] cmd_op = 0; reg [18:0] cmd_addr = 0; reg [8:0] cmd_w = 0, cmd_h = 0;
    reg [15:0] cmd_fg = 16'hF81F, cmd_bg = 0; reg [6:0] cmd_glyph = 0; reg [1:0] cmd_sx = 0, cmd_sy = 0, cmd_glyph_hi = 0;
    reg init_ok = 1; reg block = 0;
    wire cmd_full;
    wire [24:0] p0_addr; wire [15:0] p0_data; wire [1:0] p0_be; wire [10:0] p0_len;
    wire p0_str, p0_wr, p0_rd, p0_eb; wire [15:0] p0_q; wire p0_av, p0_rdy, p0_da; wire [10:0] wsa; wire [15:0] wsq;
    wire gfx_idle; wire [31:0] gfx_fence; wire [8:0] gfx_free; wire [15:0] gfx_ovf, gfx_underflow;
    integer writes_done;

    tau_gfx #(.BLIT_BLEND_ENABLE(1), .DBUF_ENABLE(0), .FONT_RAM_ENABLE(0), .BUG_FENCE_ANY(MUT == 1)) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h),
        .cmd_fg(cmd_fg), .cmd_bg(cmd_bg), .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy),
        .cmd_glyph_hi(cmd_glyph_hi), .font_we(1'b0), .font_waddr(13'd0), .font_wdata(32'd0),
        .cmd_full(cmd_full), .text_light(1'b0),
        .blt_src_base(25'd0), .blt_src_stride(10'd512), .blt_dst_base(25'd0), .blt_dst_stride(10'd512),
        .blt_key_en(1'b0), .blt_key(16'd0), .blt_blend_en(1'b0), .blt_blend_mode(3'd0), .blt_blend_alpha(8'd0),
        .rc_cut_lut(80'd0), .blt_reindex(8'd0), .clut_wr(1'b0), .clut_waddr(8'd0), .clut_wdata(16'd0),
        .dbuf_cpu_buf(1'b0), .dbuf_flip_req_tgl(1'b0), .dbuf_disp_buf(), .dbuf_flip_pending(),
        .sdram_init_complete(init_ok), .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_be),
        .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_q(p0_q), .p0_wr_req(p0_wr), .p0_rd_req(p0_rd),
        .p0_end_burst_req(p0_eb), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da),
        .wsrc_addr(wsa), .wsrc_q(wsq),
        .video_rgb(), .video_de(), .video_hs(), .video_vs(), .scan_vc(),
        .gfx_idle(gfx_idle), .gfx_fence(gfx_fence), .gfx_free(gfx_free), .gfx_ovf(gfx_ovf), .gfx_underflow(gfx_underflow));
    t3_sdram_stub mem (.clk(clk_sdram), .block(block), .p0_addr(p0_addr), .p0_data(p0_data), .p0_wr_len(p0_len),
        .p0_wr_stream(p0_str), .p0_wr_req(p0_wr), .p0_rd_req(p0_rd), .p0_end_burst_req(p0_eb), .p0_q(p0_q),
        .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq),
        .writes_done(writes_done));

    // mutants act on what the bench observes
    wire idle_o      = (MUT == 3) ? (gfx_idle || (dut.u_talos.inflight == 1'b0 && dut.u_talos.fifo_empty)) : gfx_idle;
    wire [15:0] ovf_o = (MUT == 2) ? 16'd0 : gfx_ovf;
    wire [15:0] uf_o  = (MUT == 4) ? 16'd0 : gfx_underflow;

    integer errors = 0;
    task check(input cond, input [1023:0] what);
        begin if (!cond) begin $display("FAIL: %0s", what); errors = errors + 1; end else $display("ok:   %0s", what); end
    endtask

    task push(input [3:0] op, input [18:0] a, input [8:0] w, input [8:0] h);
        begin
            @(posedge clk_sys); cmd_op <= op; cmd_addr <= a; cmd_w <= w; cmd_h <= h; cmd_push <= 1'b1;
            @(posedge clk_sys); cmd_push <= 1'b0;
        end
    endtask
    task push_wait(input [3:0] op, input [18:0] a, input [8:0] w, input [8:0] h);
        begin while (cmd_full) @(posedge clk_sys); push(op, a, w, h); end
    endtask
    task wait_idle(input integer max);
        integer n;
        begin n = 0; while (!idle_o && n < max) begin @(posedge clk_sys); n = n + 1; end end
    endtask

    integer i, w0, w1, f0;
    integer fence_bad = 0; reg [31:0] fence_prev = 0;
    always @(posedge clk_sys) if (!reset) begin
        if (gfx_fence < fence_prev || gfx_fence > fence_prev + 1) fence_bad = fence_bad + 1;
        fence_prev <= gfx_fence;
    end
    initial begin
        repeat (6) @(posedge clk_sys); reset = 0; repeat (6) @(posedge clk_sys);
        check(idle_o && gfx_free == 255 && ovf_o == 0 && gfx_fence == 0, "after reset: idle, 255 free, no overflow, fence 0");

        // ---- idle drops with the very next cycle after a push, and returns when the work is done
        push(4'd1, 19'd1024, 9'd100, 9'd8);
        #1;
        check(!idle_o, "idle is low right after a push (before the engine has even seen it)");
        wait_idle(20000);
        check(idle_o, "idle returns once the rect has retired");
        check(writes_done >= 8, "the rect's 8 rows were written before idle");

        // ---- a FENCE retires only after everything before it, and the counter is monotonic
        f0 = gfx_fence; w0 = writes_done;
        push(4'd1, 19'd0, 9'd300, 9'd20);       // RECT: 20 row bursts
        push(4'd5, 19'd5000, 9'd40, 9'd60);     // BAR (second half chained)
        push(4'd2, 19'd9000, 9'd0, 9'd0);       // CHAR
        push(4'd8, 19'd14000, 9'd50, 9'd30);    // RRECT
        push(4'd9, 19'd0, 9'd0, 9'd0);          // FENCE
        begin : fwait
            integer n; n = 0;
            while (gfx_fence == f0 && n < 200000) begin @(posedge clk_sys); n = n + 1; end
        end
        w1 = writes_done;
        check(gfx_fence == f0 + 1, "the fence counter advanced by exactly one");
        check(w1 - w0 >= 20 + 2, "at the fence, every earlier row of every command had been written");
        repeat (3000) @(posedge clk_sys);
        check(writes_done == w1, "no write lands after the fence was seen (nothing earlier was still in flight)");
        check(idle_o, "idle after the fence");

        // ---- several fences, interleaved with draws: the counter ends at +6 (the always-block monitor above
        // checks that it never decreases and never skips between two samples)
        f0 = gfx_fence;
        for (i = 0; i < 6; i = i + 1) begin push_wait(4'd1, 19'd2048 + i*512, 9'd30, 9'd2); push_wait(4'd9, 0, 0, 0); end
        wait_idle(400000);
        repeat (20) @(posedge clk_sys);
        check(gfx_fence == f0 + 6, "six fences: the counter ends exactly 6 higher");
        check(fence_bad == 0, "the fence counter never decreased and never skipped between two sys samples (whole run)");

        // ---- queue overflow: hold the engine (SDRAM not ready), push past capacity
        block = 1; repeat (200) @(posedge clk_sys);
        begin : ovf
            integer pushed; pushed = 0;
            for (i = 0; i < 300; i = i + 1) begin
                @(posedge clk_sys); cmd_op <= 4'd1; cmd_addr <= 19'd40000 + i; cmd_w <= 9'd4; cmd_h <= 9'd1; cmd_push <= 1'b1;
                @(posedge clk_sys); cmd_push <= 1'b0;
            end
            repeat (20) @(posedge clk_sys);
            check(ovf_o == 300 - 255 && gfx_free == 0, "300 pushes into a stalled queue: 45 dropped and counted, 0 free");
        end
        block = 0;
        wait_idle(400000);
        check(idle_o && gfx_free == 255, "after the stall: the 255 accepted commands drain, idle is high, all slots free");
        check(ovf_o == 45, "the overflow count is sticky (not cleared by draining)");

        // ---- scanout underflow: none in normal operation, then a starved window
        begin : uf
            integer u0, n;
            n = 0; while (n < 3600) begin @(posedge clk_vid); n = n + 1; end       // ~ 7 scanlines of normal fills
            u0 = uf_o;
            repeat (60000) @(posedge clk_vid);                                      // ~ 120 scanlines
            check(uf_o == u0, "normal operation: no underflow over ~120 scanlines");
            block = 1; repeat (40000) @(posedge clk_vid); block = 0;                // ~ 80 scanlines with the SDRAM unavailable
            repeat (400) @(posedge clk_vid);
            check(uf_o > u0, "SDRAM unavailable for ~80 scanlines: underflow counted");
        end
        if (errors == 0) $display("PASSED"); else $display("FAILED %0d", errors);
        $finish;
    end
endmodule
