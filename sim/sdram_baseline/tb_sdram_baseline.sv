// Baseline bench: the OLD SDRAM controller (tau_sdram_ctrl, as the player ships it) through the new chip model.
// MIT licence. Simulation only, iverilog -g2012 -DSIM (4-state needed).
//
// Everything about the controller is instantiation glue and a client-side driver (the p0 port protocol of the player:
// CPU word access, scanout fill, engine copy, framebuffer clear chunk). The controller and the chip model are not modified.
// Board / FPGA output delays are applied by this bench (transport delays) and are parameters, see README.md.
//
// Log lines starting with [BENCH] are summary facts; [CHIPERR ...] lines come from the chip model (aggregate.py counts them).
`timescale 1ps/1ps

module tb_sdram_baseline #(
    parameter integer RUN_US     = 8000,   // simulated traffic time after init completes (8000 = 8 ms, 64000 = a full tREF)
    parameter integer SHORT      = 0,      // 1: init, one of each access and a 100 us idle only (for the sweeps)
    // ---- output side of the FPGA (ps, from the ideal controller clock edge) ----
    parameter integer CLK_DLY    = 0,      // controller clock to the chip clock pin (the DDIO output)
    parameter integer CMD_DLY    = 0,      // controller register to pin: RAS/CAS/WE, DQM, address, bank, CKE
    parameter integer DQ_DLY     = 0,      // controller register to pin: write data
    parameter integer OE_MIN     = 0,      // output-enable envelope at the pin: on after OE_MIN, off after OE_MAX
    parameter integer OE_MAX     = 0,
    // ---- input side ----
    parameter integer DMIN       = 0,      // board delay chip to FPGA pin, min / max (chip model DMIN_PS / DMAX_PS)
    parameter integer DMAX       = 0,
    parameter integer RET_DLY    = 0,      // extra pin-to-capture-register delay (also applied to the data the controller sees)
    parameter integer CAP_CHECK  = 1,      // chip model analytic capture-window check on/off
    // ---- bench options ----
    parameter integer EMRS_WAIVE = 1,      // 1: poke the model's init state to "done" after the MRS (the old controller sends no EMRS)
    parameter integer INIT_REGS  = 1,      // 1: SDRAM_A/BA/DQM start at 0 (Cyclone V register power-up value); 0: X as in 4-state simulation
    parameter integer WRITE_HEAVY = 0,     // 1: after the initial fill only writes (clear chunks, streamed copies, CPU words): the worst case for refresh spacing
    parameter integer SEED       = 1
);
    localparam longint RUN_PS = RUN_US * 64'd1000000;
    // ------------------------------------------------------------------------------------------------------
    // clock, reset
    // ------------------------------------------------------------------------------------------------------
    reg clk = 1'b0;
    always #5000 clk = ~clk;                       // 100 MHz
    reg reset = 1'b1;
    initial begin
        repeat (4) @(posedge clk);
        @(negedge clk) reset = 1'b0;
    end

    // ------------------------------------------------------------------------------------------------------
    // the old controller
    // ------------------------------------------------------------------------------------------------------
    reg  [24:0] p0_addr = 25'd0;
    reg  [15:0] p0_data = 16'd0;
    reg  [1:0]  p0_byte_en = 2'b11;
    reg  [10:0] p0_wr_len = 11'd1;
    reg         p0_wr_stream = 1'b0;
    reg         p0_wr_req = 1'b0, p0_rd_req = 1'b0, p0_end_burst_req = 1'b0;
    reg  [15:0] wsrc_q = 16'd0;
    wire [10:0] wsrc_addr;
    wire [15:0] p0_q;
    wire        init_complete, p0_available, p0_data_available;
    wire        p0_ready;
    wire [15:0] dq_ctrl;
    wire [12:0] c_a;
    wire [1:0]  c_dqm, c_ba;
    wire        c_ncs, c_nwe, c_nras, c_ncas, c_cke, c_clk;

    tau_sdram_ctrl #(.CLOCK_SPEED_MHZ(100), .BURST_TYPE(0), .CAS_LATENCY(2), .WRITE_BURST(1)) dut (
        .clk(clk), .reset(reset), .init_complete(init_complete),
        .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(p0_byte_en), .p0_wr_len(p0_wr_len), .p0_q(p0_q),
        .p0_wr_stream(p0_wr_stream), .wsrc_addr(wsrc_addr), .wsrc_q(wsrc_q),
        .p0_wr_req(p0_wr_req), .p0_rd_req(p0_rd_req), .p0_end_burst_req(p0_end_burst_req),
        .p0_available(p0_available), .p0_ready(p0_ready), .p0_data_available(p0_data_available),
        .SDRAM_DQ(dq_ctrl), .SDRAM_A(c_a), .SDRAM_DQM(c_dqm), .SDRAM_BA(c_ba),
        .SDRAM_nCS(c_ncs), .SDRAM_nWE(c_nwe), .SDRAM_nRAS(c_nras), .SDRAM_nCAS(c_ncas),
        .SDRAM_CKE(c_cke), .SDRAM_CLK(c_clk)
    );

    initial if (INIT_REGS != 0) begin
        dut.SDRAM_A = 13'd0; dut.SDRAM_BA = 2'b00; dut.SDRAM_DQM = 2'b00;
    end

    // ------------------------------------------------------------------------------------------------------
    // board / FPGA output delays (transport, one corner per run)
    // ------------------------------------------------------------------------------------------------------
    reg        chip_clk_en = 1'b0;                 // the chip clock starts after the controller has seen reset (no edges on undriven pins)
    initial begin repeat (3) @(posedge clk); chip_clk_en = 1'b1; end
    reg        dram_clk = 1'b0;
    wire       clk_g = c_clk & chip_clk_en;
    always @(clk_g) dram_clk <= #(CLK_DLY) clk_g;

    reg        dram_cke, dram_ras_n, dram_cas_n, dram_we_n;
    reg  [1:0] dram_dqm, dram_ba;
    reg [12:0] dram_a;
    always @(c_cke)  dram_cke   <= #(CMD_DLY) c_cke;
    always @(c_nras) dram_ras_n <= #(CMD_DLY) c_nras;
    always @(c_ncas) dram_cas_n <= #(CMD_DLY) c_ncas;
    always @(c_nwe)  dram_we_n  <= #(CMD_DLY) c_nwe;
    always @(c_dqm)  dram_dqm   <= #(CMD_DLY) c_dqm;
    always @(c_ba)   dram_ba    <= #(CMD_DLY) c_ba;
    always @(c_a)    dram_a     <= #(CMD_DLY) c_a;
    // (the chip has no chip-select pin in the model; core_game.vh leaves SDRAM_nCS unconnected)

    // write path: read the controller's data register and driver enable (hierarchical, glue only)
    reg [15:0] dq_w;
    reg        dq_w_en = 1'b0;
    always @(dut.sdram_data) dq_w    <= #(DQ_DLY) dut.sdram_data;
    always @(dut.dq_output)  dq_w_en <= #(DQ_DLY) dut.dq_output;
    wire [15:0] dq_pin;
    assign dq_pin = dq_w_en ? dq_w : 16'hzzzz;
    // read path: pin to the controller (only while the controller is not driving)
    reg [15:0] dq_ret;
    always @(dq_pin) dq_ret <= #(RET_DLY) dq_pin;
    assign dq_ctrl = dut.dq_output ? 16'hzzzz : dq_ret;
    // output-enable envelope for the model's bus-conflict check
    reg [15:0] fpga_oe = 16'h0000;
    always @(dut.dq_output) begin
        if (dut.dq_output) fpga_oe <= #(OE_MIN) 16'hffff; else fpga_oe <= #(OE_MAX) 16'h0000;
    end

    // ------------------------------------------------------------------------------------------------------
    // the chip model
    // ------------------------------------------------------------------------------------------------------
    sdram_chip_model #(
        .DMIN_PS(DMIN), .DMAX_PS(DMAX), .CAP_MODE(0), .CAP_SKEW_PS(-RET_DLY), .CAP_CHECK_EN(CAP_CHECK),
        .MEM_LOG2(20), .MEM_COMPACT(1), .VERBOSE(1)
    ) u_chip (
        .dram_clk(dram_clk), .dram_cke(dram_cke), .dram_ras_n(dram_ras_n), .dram_cas_n(dram_cas_n), .dram_we_n(dram_we_n),
        .dram_dqm(dram_dqm), .dram_ba(dram_ba), .dram_a(dram_a), .dram_dq(dq_pin), .fpga_oe(fpga_oe), .cap_clk(clk)
    );

    // The old controller sends PREA, REF, REF, MRS and no EMRS. The model then stays in "init" and flags every later
    // command, and stops checking tREF. With EMRS_WAIVE the bench marks init done right after the MRS so the other checks run.
    integer kk;
    initial if (EMRS_WAIVE != 0) begin
        wait (u_chip.init_stage == 4);
        #1;
        u_chip.init_stage = 5; u_chip.init_done = 1'b1;
        for (kk = 0; kk < 8192; kk = kk + 1) u_chip.row_ts[kk] = $time;
        $display("[BENCH] EMRS waived at t=%0t (the controller sends none)", $time);
    end

    // ------------------------------------------------------------------------------------------------------
    // refresh monitor on the controller's command pins (what the player's controller really issues)
    // ------------------------------------------------------------------------------------------------------
    wire [3:0] c_cmd = {c_ncs, c_nras, c_ncas, c_nwe};
    longint ref_last, ref_sum, ref_max, ref_min; integer ref_n; longint ref_t0;
    initial begin ref_last = 0; ref_sum = 0; ref_max = 0; ref_min = 64'sd1_000_000_000_000; ref_n = 0; ref_t0 = 0; end
    always @(negedge clk) if (init_complete === 1'b1 && c_cmd === 4'b0001) begin
        if (ref_last != 0) begin
            ref_sum = ref_sum + ($time - ref_last); ref_n = ref_n + 1;
            if ($time - ref_last > ref_max) ref_max = $time - ref_last;
            if ($time - ref_last < ref_min) ref_min = $time - ref_last;
        end else ref_t0 = $time;
        ref_last = $time;
    end

    // ------------------------------------------------------------------------------------------------------
    // reference memory (the scoreboard) and helpers
    // ------------------------------------------------------------------------------------------------------
    reg [15:0] rdat [0:65535];
    reg [1:0]  rvld [0:65535];
    integer ii;
    initial for (ii = 0; ii < 65536; ii = ii + 1) begin rdat[ii] = 16'd0; rvld[ii] = 2'b00; end
    function [15:0] ridx(input [24:0] a);          // bank (2) row[3:0] col (10): the rows this bench uses
        ridx = {a[24:23], a[13:10], a[9:0]};
    endfunction
    function [24:0] A(input integer bank, input integer row, input integer col);
        A = {bank[1:0], row[12:0], col[9:0]};
    endfunction
    reg [31:0] rs = 32'h1badb002;
    function [31:0] rnd(input integer n);          // xorshift, 0..n-1
        begin
            rs = rs ^ (rs << 13); rs = rs ^ (rs >> 17); rs = rs ^ (rs << 5);
            rnd = rs % n;
        end
    endfunction

    integer n_ops, n_words_rd, n_words_wr, n_mismatch, n_trunc;
    initial begin n_ops = 0; n_words_rd = 0; n_words_wr = 0; n_mismatch = 0; n_trunc = 0; end

    // streaming-write source (a caller BRAM, one cycle latency)
    reg [15:0] src_mem [0:2047];
    always @(posedge clk) wsrc_q <= src_mem[wsrc_addr];

    // read collection
    reg [15:0] rbuf [0:2047];
    integer    got, tgt;
    reg        collecting = 1'b0;
    always @(posedge clk) if (collecting && p0_data_available === 1'b1) begin
        if (got < 2048) rbuf[got] = p0_q;
        got = got + 1;
    end
    always @(negedge clk) p0_end_burst_req <= collecting && (got >= tgt);

    integer guard;
    task wait_avail;
        begin
            @(negedge clk); guard = 0;
            while (p0_available !== 1'b1) begin @(negedge clk); guard = guard + 1; if (guard > 200000) begin $display("[BENCH] TIMEOUT waiting for p0_available"); $finish; end end
        end
    endtask
    task wait_ready;
        begin
            @(posedge clk); guard = 0;
            while (p0_ready !== 1'b1) begin @(posedge clk); guard = guard + 1; if (guard > 200000) begin $display("[BENCH] TIMEOUT waiting for p0_ready"); $finish; end end
        end
    endtask

    // one write request: constant word (stream = 0) or from src_mem[0..len-1] (stream = 1)
    task wr_op(input [24:0] addr, input [15:0] data, input [1:0] be, input integer len, input stream);
        integer w; reg [15:0] d; reg [1:0] m;
        begin
            wait_avail;
            p0_addr = addr; p0_data = data; p0_byte_en = be; p0_wr_len = len; p0_wr_stream = stream; p0_wr_req = 1'b1;
            @(negedge clk); p0_wr_req = 1'b0;
            wait_ready;
            p0_wr_stream = 1'b0;
            for (w = 0; w < len; w = w + 1) begin
                d = stream ? src_mem[w] : data; m = stream ? 2'b11 : be;
                if (m[0]) rdat[ridx(addr + w)][7:0]  = d[7:0];
                if (m[1]) rdat[ridx(addr + w)][15:8] = d[15:8];
                rvld[ridx(addr + w)] = rvld[ridx(addr + w)] | m;
            end
            n_ops = n_ops + 1; n_words_wr = n_words_wr + len;
        end
    endtask

    // one read request, "want" words; the end-burst level is raised once want words arrived
    task rd_op(input [24:0] addr, input integer want);
        begin
            wait_avail;
            got = 0; tgt = want; collecting = 1'b1;
            p0_addr = addr; p0_rd_req = 1'b1;
            @(negedge clk); p0_rd_req = 1'b0;
            wait_ready;
            collecting = 1'b0; p0_end_burst_req = 1'b0;
            n_ops = n_ops + 1;
        end
    endtask

    // read n words (re-requesting after a refresh or page-end truncation) and check them against the scoreboard
    task rd_check(input [24:0] addr, input integer n);
        integer done, take, w, lane; reg [15:0] exp, g; reg [24:0] a;
        begin
            done = 0;
            while (done < n) begin
                rd_op(addr + done, n - done);
                take = (got < n - done) ? got : (n - done);
                if (got < n - done) n_trunc = n_trunc + 1;
                if (take == 0) begin $display("[BENCH] read returned no data at t=%0t addr=%h", $time, addr + done); take = 1; end
                for (w = 0; w < take; w = w + 1) begin
                    a = addr + done + w; g = rbuf[w]; exp = rdat[ridx(a)];
                    for (lane = 0; lane < 2; lane = lane + 1)
                        if (rvld[ridx(a)][lane]) begin
                            if ((^g[lane*8 +: 8]) === 1'bx || g[lane*8 +: 8] !== exp[lane*8 +: 8]) begin
                                n_mismatch = n_mismatch + 1;
                                if (n_mismatch <= 8) $display("[BENCH] MISMATCH t=%0t addr=%h word=%0d got=%h exp=%h", $time, a, done + w, g, exp);
                            end
                        end
                end
                done = done + take; n_words_rd = n_words_rd + take;
            end
        end
    endtask

    // ------------------------------------------------------------------------------------------------------
    // the traffic of the player
    // ------------------------------------------------------------------------------------------------------
    integer w, r, k, c0;
    reg [24:0] a0;
    task fill_row(input integer bank, input integer row, input integer salt);   // framebuffer clear chunks: two 512-word constant writes
        begin
            wr_op(A(bank, row, 0),   16'h1000 + salt * 16'h0101 + row, 2'b11, 512, 1'b0);
            wr_op(A(bank, row, 512), 16'h8000 + salt * 16'h0303 + row, 2'b11, 512, 1'b0);
        end
    endtask
    task cpu_words(input integer nwords);       // CPU: single halfword writes (full and byte-masked) and reads
        integer q; reg [24:0] ad;
        begin
            for (q = 0; q < nwords; q = q + 1) begin
                ad = rnd(2) ? A(1, rnd(4), rnd(1000)) : A(0, rnd(8), rnd(1000));   // only rows the initial fill wrote
                case (rnd(4))
                    0: wr_op(ad, rnd(65536), 2'b11, 1, 1'b0);
                    1: wr_op(ad, rnd(65536), 2'b01, 1, 1'b0);
                    2: wr_op(ad, rnd(65536), 2'b10, 1, 1'b0);
                    default: ;
                endcase
                rd_check(ad, 1);
            end
        end
    endtask
    task engine_copy(input integer n);          // read n words of bank 1, write them (streamed) to bank 0
        integer q, sr, dr, sc, dc;
        begin
            sr = rnd(4); dr = rnd(8); sc = rnd(1000 - n); dc = rnd(1000 - n);
            rd_op(A(1, sr, sc), n);            // collect (not checked here, checked by the read-back below)
            for (q = 0; q < n; q = q + 1) src_mem[q] = (q < got) ? rbuf[q] : 16'h0000;
            wr_op(A(0, dr, dc), 16'h0000, 2'b11, n, 1'b1);
            rd_check(A(0, dr, dc), n);
        end
    endtask

    initial begin : main
        wait (init_complete === 1'b1);
        repeat (20) @(negedge clk);
        $display("[BENCH] init_complete at t=%0t", $time);
        // initial content: banks 0 and 1, rows 0..7 (bank 0) and 0..3 (bank 1)
        for (r = 0; r < ((SHORT != 0) ? 2 : 8); r = r + 1) fill_row(0, r, 1);
        for (r = 0; r < ((SHORT != 0) ? 2 : 4); r = r + 1) fill_row(1, r, 2);
        if (SHORT != 0) begin
            cpu_words(8);
            rd_check(A(0, 0, 100), 64);
            engine_copy(32);
            repeat (10000) @(negedge clk);                       // 100 us idle: refresh while idle
        end else begin
            k = 0;
            while ($time < (64'd101_000_000 + RUN_PS)) begin
                if (WRITE_HEAVY != 0) begin
                    fill_row(0, k % 8, k);                                  // two 512-word constant bursts
                    for (w = 0; w < 64; w = w + 1) src_mem[w] = rnd(65536);
                    wr_op(A(0, rnd(8), rnd(900)), 16'h0, 2'b11, 64, 1'b1);   // a streamed copy burst
                    wr_op(A(1, rnd(4), rnd(1000)), rnd(65536), 2'b11, 1, 1'b0);
                end else begin
                // scanout fill: a long read burst, sometimes up to the page end, refresh may cut it short
                c0 = (k % 50 == 7) ? 900 : rnd(700);
                rd_check(A(0, k % 8, c0), (k % 50 == 7) ? 200 : 256);
                cpu_words(3);
                engine_copy(64);
                if (k % 16 == 0) fill_row(0, rnd(8), k);
                end
                if (k == 100 && WRITE_HEAVY == 0) repeat (30000) @(negedge clk);     // 300 us idle: refresh while the controller is idle
                repeat (rnd(20)) @(negedge clk);
                k = k + 1;
            end
        end
        $display("[BENCH] traffic done at t=%0t, ops=%0d read_words=%0d write_words=%0d mismatches=%0d truncated_reads=%0d",
                 $time, n_ops, n_words_rd, n_words_wr, n_mismatch, n_trunc);
        if (ref_n > 0)
            $display("[BENCH] refresh: n=%0d avg=%0d ps min=%0d ps max=%0d ps (8192 rows x avg = %0d us; tREF = 64000 us)",
                     ref_n + 1, ref_sum / ref_n, ref_min, ref_max, (ref_sum / ref_n) * 8192 / 1000000);
        else $display("[BENCH] refresh: fewer than two refreshes seen");
        u_chip.report;
        $finish;
    end

    initial begin                               // watchdog
        #(64'd200_000_000 + RUN_PS * 3);
        $display("[BENCH] WATCHDOG at t=%0t", $time);
        u_chip.report;
        $finish;
    end
endmodule
