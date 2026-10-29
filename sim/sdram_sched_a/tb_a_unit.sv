// Unit / system bench of tau_sdram_sched_a: PHY + sched_v (3 ports) + chip model, driven through the player's own client
// protocols: a stand-in for tau_gfx (the p0 protocol with the sideband) and the real tau_sdram_a_cpu (the CPU mailbox).
// MIT licence. Simulation only (iverilog -g2012). vvp tb.vvp +TEST=<name>:
//   cpu | engine | readsizes | mixed | urgent | refresh | idle | latency
// Legal tests end with phy_fault 0, zero chip errors, dbg_pmiss 0.
`timescale 1ps/1ps
`include "sdram_chip_err.vh"
module tb_a_unit #(
    parameter integer QD = 1,
    parameter integer REFIPS = 7_800_000,
    parameter integer BANK_HASH = 2
);
    localparam P_PS = 10000, INITCYC = 200;
    reg clk = 0; always #5000 clk = ~clk;
    reg clk_sys = 0; always #8333 clk_sys = ~clk_sys;
    reg rst_in = 1; integer cyc = 0; always @(posedge clk) cyc <= cyc + 1;
    reg rst_sys = 1;

    // ---- master (tau_gfx stand-in)
    reg [24:0] m_addr = 0; reg [15:0] m_data = 0; reg [1:0] m_be = 2'b11; reg [10:0] m_wlen = 1;
    reg m_stream = 0, m_wr = 0, m_rd = 0, m_end = 0, m_fill = 0; reg [9:0] m_rlen = 1;
    wire [15:0] p0_q; wire p0_av, p0_rdy, p0_da; wire [10:0] wsa;
    reg [15:0] gly [0:2047]; reg [15:0] wsq = 0; always @(posedge clk) wsq <= gly[wsa];

    // ---- CPU mailbox
    reg sys_start = 0, sys_write = 0; reg [24:0] sys_addr = 0; reg [31:0] sys_wdata = 0; reg [3:0] sys_be = 4'hf;
    wire sys_busy, sys_done; wire [31:0] sys_rdata;
    wire c_req_valid, c_req_ready, c_req_we, c_w_next, c_r_valid, c_r_last, c_done; wire [24:0] c_req_addr; wire [9:0] c_req_len, c_w_idx;
    wire [15:0] c_w_data, c_r_data; wire [1:0] c_w_be;
    wire rst_out, init_done, busy, phy_fault; wire [15:0] fault_count, dbg_pmiss; wire [31:0] refreshes;
    wire [95:0] cnt_requests, cnt_words, cnt_busy; wire [47:0] cnt_bad, cnt_worst_wait, cnt_urgent_over;
    wire [12:0] c_a; wire [1:0] c_ba, c_dqm; wire c_clk, c_cke, c_ras, c_cas, c_we; wire [15:0] dq;

    tau_sdram_sched_a #(.QDEPTH(QD), .BANK_HASH(BANK_HASH), .CNT_EN(1), .INIT_WAIT_CYCLES(INITCYC), .SIM_SHORT_INIT(1), .REFRESH_INTERVAL_PS(REFIPS)) dut (
        .clk(clk), .rst_in(rst_in), .rst_out(rst_out), .init_done(init_done), .busy(busy),
        .p0_addr(m_addr), .p0_data(m_data), .p0_byte_en(m_be), .p0_wr_len(m_wlen), .p0_wr_stream(m_stream), .p0_wr_req(m_wr),
        .p0_rd_req(m_rd), .p0_end_burst_req(m_end), .p0_is_fill(m_fill), .p0_rd_len(m_rlen), .p0_q(p0_q), .p0_available(p0_av),
        .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq),
        .cpu_req_valid(c_req_valid), .cpu_req_ready(c_req_ready), .cpu_req_we(c_req_we), .cpu_req_addr(c_req_addr), .cpu_req_len(c_req_len),
        .cpu_w_next(c_w_next), .cpu_w_idx(c_w_idx), .cpu_w_data(c_w_data), .cpu_w_be(c_w_be), .cpu_r_valid(c_r_valid), .cpu_r_data(c_r_data),
        .cpu_r_last(c_r_last), .cpu_done(c_done),
        .cnt_requests(cnt_requests), .cnt_words(cnt_words), .cnt_busy(cnt_busy), .cnt_bad(cnt_bad), .cnt_worst_wait(cnt_worst_wait),
        .cnt_urgent_over(cnt_urgent_over), .dbg_pmiss(dbg_pmiss), .phy_fault(phy_fault), .phy_fault_count(fault_count), .refreshes(refreshes),
        .dram_a(c_a), .dram_ba(c_ba), .dram_dq(dq), .dram_dqm(c_dqm), .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras),
        .dram_cas_n(c_cas), .dram_we_n(c_we));
    tau_sdram_a_cpu cpu (
        .clk_sys(clk_sys), .rst_sys(rst_sys), .sys_start(sys_start), .sys_wb_start(1'b0), .sys_write(sys_write), .sys_addr(sys_addr),
        .sys_wdata(sys_wdata), .sys_byte_en(sys_be), .sys_busy(sys_busy), .sys_done(sys_done), .sys_rdata(sys_rdata),
        .clk_sdram(clk), .rst_sdram(rst_out), .req_valid(c_req_valid), .req_ready(c_req_ready), .req_we(c_req_we), .req_addr(c_req_addr),
        .req_len(c_req_len), .w_next(c_w_next), .w_idx(c_w_idx), .w_data(c_w_data), .w_be(c_w_be), .r_valid(c_r_valid), .r_data(c_r_data),
        .r_last(c_r_last), .done(c_done));
    sdram_chip_model #(.CL_DEFAULT(2), .INIT_NOP_MIN(INITCYC), .INIT_WAIT_PS(INITCYC * P_PS), .MEM_LOG2(20), .MEM_COMPACT(1)) chip (
        .dram_clk(c_clk), .dram_cke(c_cke), .dram_ras_n(c_ras), .dram_cas_n(c_cas), .dram_we_n(c_we), .dram_dqm(c_dqm),
        .dram_ba(c_ba), .dram_a(c_a), .dram_dq(dq), .fpga_oe(dut.u_phy.u_core.dq_oe), .cap_clk(clk));

    // ---- shadow memory (the layout of the other benches: region[3:0] in addr[23:20], offset in addr[15:0])
    reg [15:0] sh_d [0:(1<<20)-1]; reg [1:0] sh_v [0:(1<<20)-1];
    integer ii; initial for (ii = 0; ii < (1<<20); ii = ii + 1) sh_v[ii] = 2'b00;
    function integer sidx(input [24:0] a); begin sidx = {a[23:20], a[15:0]}; end endfunction
    function [24:0] A(input integer region, input integer off); begin A = {1'b0, region[3:0], 4'b0000, off[15:0]}; end endfunction
    function [15:0] pat(input [24:0] a, input integer g); reg [31:0] x; begin
        x = {7'd0, a} * 32'h9E3779B1 + g * 32'h7F4A7C15; x = x ^ (x >> 15); x = x * 32'h2C1B3C6D; pat = x[31:16] ^ x[15:0]; end endfunction
    integer fail = 0; reg [8*40-1:0] tname;
    task fail1(input [8*60-1:0] m); begin $display("  FAIL cyc=%0d: %0s", cyc, m); fail = fail + 1; end endtask
    task sh_wr(input [24:0] a, input [15:0] d, input [1:0] m); begin
        if (m[0]) sh_d[sidx(a)][7:0] = d[7:0]; if (m[1]) sh_d[sidx(a)][15:8] = d[15:8]; sh_v[sidx(a)] = sh_v[sidx(a)] | m; end endtask
    task sh_chk(input [24:0] a, input [15:0] g, input [8*20-1:0] who); reg [15:0] e; reg [1:0] v; begin
        e = sh_d[sidx(a)]; v = sh_v[sidx(a)];
        if ((v[0] && g[7:0] !== e[7:0]) || (v[1] && g[15:8] !== e[15:8])) begin
            if (fail < 12) $display("  MISMATCH %0s addr %h got %h want %h (cyc %0d)", who, a, g, e, cyc); fail = fail + 1; end end endtask

    // ---- master tasks (the p0 protocol, one request at a time)
    integer got, m_t_req, m_t_first, m_t_done; reg collect = 0; reg [15:0] rbuf [0:1023];
    always @(posedge clk) if (collect && p0_da) begin if (got == 0) m_t_first = cyc; if (got < 1024) rbuf[got] = p0_q; got = got + 1; end
    task m_wait_av; begin while (p0_av !== 1'b1) @(posedge clk); end endtask
    // constant write (stream = 0) or streamed from gly[0..len-1] (stream = 1)
    task m_write(input [24:0] addr, input integer len, input [15:0] data, input [1:0] be, input stream);
        integer k; begin
            m_wait_av; @(posedge clk);
            m_addr <= addr; m_data <= data; m_be <= be; m_wlen <= len; m_stream <= stream; m_wr <= 1; m_fill <= 0; m_rlen <= 1;
            m_t_req = cyc + 1;
            @(posedge clk); m_wr <= 0; m_stream <= 0;
            begin : w forever begin @(posedge clk); if (p0_rdy === 1'b1) disable w; end end
            m_t_done = cyc;
            for (k = 0; k < len; k = k + 1) sh_wr(addr + k, stream ? gly[k] : data, stream ? 2'b11 : be);
        end endtask
    task m_read(input [24:0] addr, input integer len, input fill, input check);
        integer k; begin
            m_wait_av; @(posedge clk);
            got = 0; collect = 1;
            m_addr <= addr; m_rd <= 1; m_fill <= fill; m_rlen <= len; m_wr <= 0;
            m_t_req = cyc + 1; m_t_first = -1;
            @(posedge clk); m_rd <= 0;
            while (got < len) @(posedge clk);
            collect = 0; m_end <= 1; @(posedge clk); m_end <= 0;
            begin : w forever begin @(posedge clk); if (p0_rdy === 1'b1 || p0_av === 1'b1) disable w; end end
            m_t_done = cyc;
            if (check) for (k = 0; k < len; k = k + 1) sh_chk(addr + k, rbuf[k], "engine read");
        end endtask

    // ---- CPU tasks (one 32-bit access through the mailbox)
    realtime t_cpu0; realtime cpu_lat_w_sum = 0, cpu_lat_r_sum = 0; integer cpu_n_w = 0, cpu_n_r = 0; realtime cpu_lat_max = 0;
    reg [31:0] cpu_rd;
    task cpu_op(input w, input [24:0] a, input [31:0] d, input [3:0] be);
        realtime l; begin
            @(posedge clk_sys); sys_write <= w; sys_addr <= a; sys_wdata <= d; sys_be <= be; sys_start <= 1; t_cpu0 = $realtime;
            @(posedge clk_sys); sys_start <= 0;
            begin : w2 forever begin @(posedge clk_sys); if (sys_done === 1'b1) disable w2; end end
            cpu_rd = sys_rdata; l = ($realtime - t_cpu0) / 1000.0;
            if (w) begin cpu_lat_w_sum = cpu_lat_w_sum + l; cpu_n_w = cpu_n_w + 1; end else begin cpu_lat_r_sum = cpu_lat_r_sum + l; cpu_n_r = cpu_n_r + 1; end
            if (l > cpu_lat_max) cpu_lat_max = l;
            if (w) begin sh_wr(a, d[15:0], be[1:0]); sh_wr(a + 1, d[31:16], be[3:2]); end
            else begin sh_chk(a, cpu_rd[15:0], "cpu read lo"); sh_chk(a + 1, cpu_rd[31:16], "cpu read hi"); end
        end endtask

    task end_checks; begin
        repeat (8) @(posedge clk);
        if (phy_fault !== 1'b0) begin $display("  PHY fault count=%0d", fault_count); fail = fail + 1; end
        if (chip.err_count != 0) begin $display("  chip errors %0d mask %h", chip.err_count, chip.err_mask); fail = fail + 1; end
        if (dbg_pmiss) begin $display("  timing model mismatch"); fail = fail + 1; end
    end endtask

    integer seed = 5;
    function integer urand(input integer m); integer v; begin v = $random(seed); if (v < 0) v = -v; urand = v % m; end endfunction
    integer L, r, n, off, k2, lens_n, rc, offc; integer lens [0:15];
    integer mix_done_m, mix_done_c; integer fill_max_first, fill_max_total, fill_n, fill_late;
    integer idle_p0; integer t_first_acc;
    initial begin
        tname = "cpu"; if (!$value$plusargs("TEST=%s", tname)) tname = "cpu";
        for (ii = 0; ii < 2048; ii = ii + 1) gly[ii] = pat(ii, 77);
        repeat (4) @(posedge clk); rst_in <= 0; repeat (4) @(posedge clk_sys); rst_sys <= 0;
        n = 0; while (!init_done && n < 40000) begin @(posedge clk); n = n + 1; end
        repeat (8) @(posedge clk);
        case (tname)
            "cpu": begin     // every byte-enable combination, read-after-write, random words
                for (r = 0; r < 16; r = r + 1) begin
                    cpu_op(1, A(1, 100 + 2 * r), 32'hFFFFFFFF, 4'hF); cpu_op(1, A(1, 100 + 2 * r), {16'h0000 + r[15:0], 16'hA000 + r[15:0]}, r[3:0]);
                    cpu_op(0, A(1, 100 + 2 * r), 0, 0);
                end
                for (r = 0; r < 300; r = r + 1) begin
                    off = 2 * urand(20000);
                    cpu_op(1, A(2 + (r % 3), off), $random(seed), 4'hF);
                    if (urand(2)) cpu_op(1, A(2 + (r % 3), off), $random(seed), urand(16));
                    cpu_op(0, A(2 + (r % 3), off), 0, 0);
                end
                $display("  CPU latency: write avg %0.1f ns (n %0d), read avg %0.1f ns (n %0d), max %0.1f ns", cpu_lat_w_sum / cpu_n_w, cpu_n_w, cpu_lat_r_sum / cpu_n_r, cpu_n_r, cpu_lat_max);
            end
            "engine": begin  // const writes, streamed writes, reads, a copy (read then streamed write), partial byte enables
                lens[0] = 1; lens[1] = 2; lens[2] = 3; lens[3] = 7; lens[4] = 31; lens[5] = 64; lens[6] = 127; lens[7] = 128;
                lens[8] = 129; lens[9] = 255; lens[10] = 256; lens[11] = 400; lens[12] = 480; lens[13] = 511; lens[14] = 512; lens[15] = 100;
                for (r = 0; r < 16; r = r + 1) begin
                    off = (r * 4099) % 40000;
                    m_write(A(r % 8, off), lens[r], 16'h1111 * (r + 1), 2'b11, 0);
                    m_read(A(r % 8, off), lens[r], 0, 1);
                    m_write(A(r % 8, off + 700), lens[r] > 128 ? 128 : lens[r], 0, 2'b11, 1);   // stream (glyph rows are at most 128 words)
                    m_read(A(r % 8, off + 700), lens[r] > 128 ? 128 : lens[r], 0, 1);
                    m_write(A(r % 8, off + 3), 5, 16'hC3C3, 2'b01, 0); m_write(A(r % 8, off + 3), 5, 16'h3C3C, 2'b10, 0);
                    m_read(A(r % 8, off), 16, 0, 1);
                end
                for (r = 0; r < 60; r = r + 1) begin      // copy: read a row, stream it out to another row
                    L = 1 + urand(127); off = urand(30000);
                    m_write(A(1, off), L, 16'hD000 + r, 2'b11, 0);
                    m_read(A(1, off), L, 0, 1);
                    for (k2 = 0; k2 < L; k2 = k2 + 1) gly[k2] = rbuf[k2];
                    m_write(A(2, off + 64), L, 0, 2'b11, 1); m_read(A(2, off + 64), L, 0, 1);
                end
                $display("  counters: requests p0 %0d p1 %0d p2 %0d", cnt_requests[31:0], cnt_requests[63:32], cnt_requests[95:64]);
                if (cnt_requests[31:0] != 0) fail1("port 0 saw requests with no fill");
            end
            "readsizes": begin  // the lengths the engine really asks for: 1 (pixel blit), char_w (<=128), 400 (fill) and 256 (char_w 0)
                for (r = 0; r < 20; r = r + 1) m_write(A(3, 0 + 512 * r), 512, 16'h2000 + r, 2'b11, 0);
                for (L = 1; L <= 130; L = L + 1) m_read(A(3, 100 * (L % 7) + 512 * (L % 20)), L, 0, 1);
                m_read(A(3, 0), 400, 1, 1); m_read(A(3, 512), 256, 0, 1); m_read(A(3, 1024), 400, 1, 1);
                if (cnt_requests[31:0] != 2) begin $display("  fill requests %0d", cnt_requests[31:0]); fail1("fill requests counted on port 0"); end
            end
            "mixed": begin   // master, CPU and fills at once, disjoint regions, short refresh interval from the parameter
                mix_done_m = 0; mix_done_c = 0;
                fork
                    begin : mm
                        for (r = 0; r < 120; r = r + 1) begin
                            L = 1 + urand(127); off = urand(30000);
                            m_write(A(8 + r % 3, off), L, 16'h4000 + r, 2'b11, 0); m_read(A(8 + r % 3, off), L, 0, 1);
                            for (k2 = 0; k2 < L; k2 = k2 + 1) gly[k2] = pat(off + k2, 900 + r);
                            m_write(A(8 + r % 3, off + 40000), L, 0, 2'b11, 1); m_read(A(8 + r % 3, off + 40000), L, 0, 1);
                            if (r % 7 == 0) begin m_read(A(11, 0), 400, 1, 0); end
                        end
                        mix_done_m = 1;
                    end
                    begin : cc
                        for (rc = 0; rc < 400; rc = rc + 1) begin
                            offc = 2 * urand(20000);
                            cpu_op(1, A(12, offc), $random(seed), 4'hF); cpu_op(1, A(12, offc), $random(seed), urand(16)); cpu_op(0, A(12, offc), 0, 0);
                        end
                        mix_done_c = 1;
                    end
                join
                $display("  refreshes %0d, cycles %0d", refreshes, cyc);
            end
            "urgent": begin  // scan-out fills every ~6350 cycles (one line) while the CPU hammers and the engine writes rows
                fill_max_first = 0; fill_max_total = 0; fill_n = 0; fill_late = 0; mix_done_m = 0;
                for (r = 0; r < 20; r = r + 1) m_write(A(13, 0 + 512 * r), 512, 16'h5000 + r, 2'b11, 0);
                fork
                    begin : cc2
                        rc = 0; while (!mix_done_m) begin cpu_op(rc % 2, A(12, 2 * urand(30000)), $random(seed), 4'hF); rc = rc + 1; end
                    end
                    begin : mm2
                        for (r = 0; r < 60; r = r + 1) begin
                            // engine work: two 512-word constant rows and a 128-word copy, as the draw engine would
                            m_write(A(14, 512 * (r % 20)), 480, 16'h6000 + r, 2'b11, 0);
                            m_write(A(14, 512 * (r % 20) + 3000), 480, 16'h6100 + r, 2'b11, 0);
                            m_read(A(13, 512 * (r % 20)), 128, 0, 0);
                            // the fill (always wins at the master's idle point, as in tau_talos)
                            m_read(A(13, 512 * (r % 20)), 400, 1, 0);
                            fill_n = fill_n + 1;
                            if (m_t_first - m_t_req > fill_max_first) fill_max_first = m_t_first - m_t_req;
                            if (m_t_done - m_t_req > fill_max_total) fill_max_total = m_t_done - m_t_req;
                            if (m_t_done - m_t_req > 1000) fill_late = fill_late + 1;
                            repeat (urand(300)) @(posedge clk);
                        end
                        mix_done_m = 1;
                    end
                join
                $display("  fills %0d: first word max %0d cycles, total max %0d cycles, over 10 us %0d, CPU ops %0d (max latency %0.0f ns)", fill_n, fill_max_first, fill_max_total, fill_late, cpu_n_w + cpu_n_r, cpu_lat_max);
                if (fill_max_first > 170) fail1("fill first word beyond the urgent bound + adapter");
                if (fill_late != 0) fail1("a fill took more than 10 us");
                if (cnt_requests[31:0] != fill_n) fail1("fills not counted on the urgent port 0");
            end
            "refresh": begin  // sustained load against a refresh interval of REFIPS: the debt never overruns (PHY fault), data intact
                for (r = 0; r < 150; r = r + 1) begin
                    L = 1 + urand(300); off = urand(30000);
                    m_write(A(5 + r % 3, off), L, 16'h7000 + r, 2'b11, 0); m_read(A(5 + r % 3, off), L, 0, 1);
                    cpu_op(1, A(9, 2 * (r * 13 % 20000)), $random(seed), 4'hF); cpu_op(0, A(9, 2 * (r * 13 % 20000)), 0, 0);
                end
                $display("  refreshes %0d cycles %0d", refreshes, cyc);
                if (refreshes < (cyc * 10000 / REFIPS) / 2) fail1("too few refreshes for the load");
            end
            "idle": begin   // available/busy semantics: busy while a request is outstanding, idle after; a second request waits
                if (p0_av !== 1'b1 || busy !== 1'b0) fail1("not idle after init");
                m_wait_av; @(posedge clk);
                m_addr <= A(4, 0); m_data <= 16'hABCD; m_be <= 2'b11; m_wlen <= 300; m_stream <= 0; m_wr <= 1; m_fill <= 0;
                @(posedge clk); m_wr <= 0;
                @(posedge clk); if (p0_av !== 1'b0) fail1("p0_available still high with a request outstanding");
                if (busy !== 1'b1) fail1("busy low with a request outstanding");
                begin : w forever begin @(posedge clk); if (p0_rdy === 1'b1) disable w; end end
                @(posedge clk); if (p0_av !== 1'b1) fail1("p0_available not back one cycle after p0_ready");
                @(posedge clk); @(posedge clk); if (busy !== 1'b0) fail1("busy high after done");
                for (k2 = 0; k2 < 300; k2 = k2 + 1) sh_wr(A(4, k2), 16'hABCD, 2'b11);
                m_read(A(4, 0), 300, 0, 1);
                // after done the chip memory holds the data (no write lands after p0_ready)
                begin : chk integer j; reg [16:0] pk; reg [24:0] a; for (j = 0; j < 300; j = j + 1) begin
                    a = A(4, j); pk = chip.peek(a[11:10] ^ a[21:20] ^ a[23:22], a[24:12], a[9:0]); if (pk[15:0] !== 16'hABCD) fail1("memory not written at done"); end end
            end
            "latency": begin  // idle-system latencies: CPU word, engine 1-word read, engine 64-word read, engine 64-word write (cycles)
                for (r = 0; r < 20; r = r + 1) begin cpu_op(1, A(1, 2 * r), r, 4'hF); cpu_op(0, A(1, 2 * r), 0, 0); end
                $display("  CPU word write %0.1f ns, read %0.1f ns (idle system, avg of %0d)", cpu_lat_w_sum / cpu_n_w, cpu_lat_r_sum / cpu_n_r, cpu_n_w);
                for (r = 0; r < 8; r = r + 1) m_write(A(1, 5000 + 100 * r), 64, 16'h1234, 2'b11, 0);
                m_read(A(1, 5000), 1, 0, 0); $display("  engine read 1 word: first %0d, done %0d cycles", m_t_first - m_t_req, m_t_done - m_t_req);
                m_read(A(1, 5000), 64, 0, 0); $display("  engine read 64 words: first %0d, done %0d cycles", m_t_first - m_t_req, m_t_done - m_t_req);
                m_write(A(1, 5100), 64, 16'h4321, 2'b11, 0); $display("  engine write 64 words: done %0d cycles", m_t_done - m_t_req);
                m_write(A(1, 5200), 1, 16'h4321, 2'b11, 0); $display("  engine write 1 word: done %0d cycles", m_t_done - m_t_req);
            end
            default: begin $display("unknown test"); fail = fail + 1; end
        endcase
        end_checks;
        if (fail == 0) $display("TEST %0s PASS", tname); else $display("TEST %0s FAIL (%0d)", tname, fail);
        $finish;
    end
    initial begin #(64'd1_000_000 * 40_000); $display("TEST %0s FAIL (global time-out)", tname); $finish; end
endmodule
