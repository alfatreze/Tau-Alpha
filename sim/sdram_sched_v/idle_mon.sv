// Idle-bus classification monitor for the replay (simulation only): attach as an extra top, `-s idle_mon`.
// Every cycle after init is DATA (a burst is active in the scheduler) or idle; idle cycles are classified by what the scheduler waits for.
`timescale 1ps/1ps
module idle_mon;
    integer c_total = 0, c_data = 0, c_refresh = 0, c_nowork = 0, c_blank = 0, c_pipe = 0, c_wdata = 0, c_row = 0, c_perm = 0, c_other = 0;
    integer n_chain = 0, n_bst = 0, n_piece = 0, n_short = 0, n_rw = 0, n_wr_after_rd = 0;
    reg last_w = 0; reg last_v = 0;
    always @(posedge tb_replay.clk) if (tb_replay.init_done && !tb_replay.rst) begin
        c_total = c_total + 1;
        if (tb_replay.dut.issue) begin
            n_piece = n_piece + 1;
            if (tb_replay.dut.t_n < 6) n_short = n_short + 1;
            if (tb_replay.dut.boundary) n_chain = n_chain + 1;
            if (last_v && !last_w && tb_replay.dut.t_we) n_wr_after_rd = n_wr_after_rd + 1;
            last_v = 1; last_w = tb_replay.dut.t_we;
        end
        if (tb_replay.cmd == 3'd7) n_bst = n_bst + 1;
        if (tb_replay.dut.cur_v) c_data = c_data + 1;
        else if (tb_replay.dut.rfm != 0 || tb_replay.dut.hold_q != 0 || tb_replay.dut.rf_go_q) c_refresh = c_refresh + 1;
        else if (!(|tb_replay.dut.hv)) c_nowork = c_nowork + 1;
        else if (tb_replay.dut.blank_q != 0) c_blank = c_blank + 1;
        else if (!tb_replay.dut.td_v) c_pipe = c_pipe + 1;
        else if (tb_replay.dut.t_we && !tb_replay.dut.wr_ok) c_wdata = c_wdata + 1;
        else if (!tb_replay.dut.t_hit) c_row = c_row + 1;
        else if (!tb_replay.dut.issue) c_perm = c_perm + 1;
        else c_other = c_other + 1;
    end
    final begin
        $display("IDLE total=%0d data=%0d refresh=%0d nowork=%0d blank=%0d pipe=%0d wdata=%0d row=%0d perm=%0d other=%0d", c_total, c_data, c_refresh, c_nowork, c_blank, c_pipe, c_wdata, c_row, c_perm, c_other);
        $display("IDLE pieces=%0d short(<6)=%0d chained=%0d bst=%0d wr_after_rd=%0d", n_piece, n_short, n_chain, n_bst, n_wr_after_rd);
    end
endmodule
