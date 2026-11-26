// ============================================================================
// tb_talos_v2_diff.v -- ORACLE-SIDE differential replay bench for the Talos v2 contract (audit B-1181).
// NEVER give this file, its outputs or tools/talos_v2_model/run_diff.py's reports to the clean-room RTL author:
// it instantiates the OLD engine (src/fpga/core/tau_talos.sv, or Tau Gallery's copy with -DGAL) and reads a
// few of its internal signals (push_cnt, rd_ptr, inflight) to attribute memory writes to commands.
//
// It replays one G1 vector, translated by tools/talos_v2_model/run_diff.py into a plain stimulus file
// (+stim=<file>), through the old engine on a functional SDRAM stub and writes a log (+out=<file>):
//   A <ci> <acc>                  a push and whether the engine accepted it
//   D <ci> <op>                   command ci dispatched (popped from the queue)
//   C <ci> <op> <busy> <rq> <rw> <wq> <ww>   command ci retired: clk_sdram cycles in flight, read requests/words,
//                                            write requests/words (functional-stub cost)
//   W <ci> <addr> <data>          one word written to memory (ci -1: backdoor/CPU write)
//   P <kind> <ci> <len>           a protocol violation (request length 0 or above 512)
//   F <tag> <line> <hexwords>     a fill finished (tag = action index); its line-buffer words
//   G <tag> <line>                a fill was not completed within the timeout (lost)
//   Q <tag> <full> <idle> <free> <fence> <ovf> <disp> <pend> <vact>   engine outputs at a register read
//   H <tag>                       a wait-for-idle timed out (hang)
//   Z <full> <idle> <free> <fence> <ovf> <disp> <pend> <fdl> <fseen>  final state
// Stimulus lines (one per line, decimal):
//   P op addr w h fg bg glyph sx sy ghi   raw push (one clk_sys cycle; no wait for cmd_full: a drop is a drop)
//   S field value                         a quasi-static input (see the case below)
//   C idx data / F addr data              CLUT / font RAM write through the engine's ports
//   B addr data / O addr data / U idx d   backdoor: memory word / font ROM word / CLUT entry / (N addr data font RAM)
//   L tag line                            request a fill of line and wait for it
//   V / X                                 vertical-blank toggle / flip-request toggle
//   R                                     reset pulse (both domains)
//   I tag                                 wait for idle (H on timeout)
//   T v                                   memory stall (p0_available held low) on/off
//   Q tag                                 sample the status outputs
//   D n                                   wait n clk_sdram cycles
//   K n                                   wait until n commands have retired since the last reset (the model's "step")
//   J line                                toggle a fill request for line and do not wait (C-FILL-3 experiments)
//   Y tag                                 print "Y tag <fills finished> <fill_done_line>"
//   E                                     end: drain unless stalled, print Z, finish
//
//   iverilog -g2012 -I src/fpga/core -o build/rtl/v2diff.vvp sim/tb_talos_v2_diff.v src/fpga/core/tau_talos.sv \
//       src/fpga/core/tau_cdc_gray_ctr.sv src/fpga/core/font_rom.v src/fpga/core/font_ram.v
//   (-DGAL with work/talos_v2_diff/gal/tau_talos_gal.sv instead: Gallery's engine at 26d9b1c, copied, never edited)
//   parameters: -Ptb_talos_v2_diff.BLEND=1 .DBUF=1 .FONTRAM=1 .FILLW=400 .VIEW=0
// The stub is the probe's (sim/tb_talos_v2_contract_probe.v): one read word per clock, a constant write in one
// clock plus a done clock, a streamed write one word per clock. Memory: the full 2^25-word space, paged (pages
// allocated on first write, unwritten words read the probe pattern k * 0x9E37 + 0x1234).
// ============================================================================
`timescale 1ns/1ps
`default_nettype none
module v2diff_stub #(parameter NPHYS = 4096) (
    input  wire        clk,
    input  wire        stall,
    input  wire [24:0] p0_addr, input wire [15:0] p0_data, input wire [10:0] p0_wr_len,
    input  wire        p0_wr_stream, p0_wr_req, p0_rd_req, p0_end_burst_req,
    output reg  [15:0] p0_q = 0, output reg p0_available = 0, p0_ready = 0, p0_data_available = 0,
    output reg  [10:0] wsrc_addr = 0, input wire [15:0] wsrc_q
);
    reg [15:0] pmem [0:NPHYS*1024-1];
    integer ptab [0:32767];
    integer nalloc = 0, k0;
    integer fout = 0;               // set by the bench
    integer cur_ci = -1;            // the command the engine is executing (set by the bench)
    integer nrq = 0, nrw = 0, nwq = 0, nww = 0;   // per-command counters (cleared by the bench)
    initial for (k0 = 0; k0 < 32768; k0 = k0 + 1) ptab[k0] = -1;
    function [15:0] patt(input [24:0] a); patt = a[15:0] * 16'h9E37 + 16'h1234; endfunction
    task mwrite(input [24:0] a, input [15:0] d, input integer ci);
        integer pg, i;
        begin
            pg = a >> 10;
            if (ptab[pg] < 0) begin
                if (nalloc >= NPHYS) begin $display("STUB OUT OF PAGES"); $finish; end
                ptab[pg] = nalloc;
                for (i = 0; i < 1024; i = i + 1) pmem[nalloc * 1024 + i] = patt(pg * 1024 + i);
                nalloc = nalloc + 1;
            end
            pmem[ptab[pg] * 1024 + (a & 1023)] = d;
            if (fout) $fdisplay(fout, "W %0d %0d %0d", ci, a, d);
        end
    endtask
    function [15:0] mread(input [24:0] a);
        integer pg;
        begin pg = a >> 10; mread = (ptab[pg] < 0) ? patt(a) : pmem[ptab[pg] * 1024 + (a & 1023)]; end
    endfunction
    localparam IDLE = 0, STREAM = 1, CONST = 2, DONE = 3, READ = 4;
    integer st = IDLE, beats = 0;
    reg [24:0] a; reg [10:0] len; reg [15:0] cdata;
    always @(posedge clk) begin
        p0_ready <= 1'b0; p0_data_available <= 1'b0;
        case (st)
            IDLE: begin
                p0_available <= !stall;
                if (p0_rd_req) begin p0_available <= 1'b0; a <= p0_addr; st <= READ; nrq = nrq + 1; end
                else if (p0_wr_req) begin
                    p0_available <= 1'b0; a <= p0_addr; len <= p0_wr_len; cdata <= p0_data; wsrc_addr <= 0; beats = 0;
                    nwq = nwq + 1;
                    if (p0_wr_len == 0 || p0_wr_len > 512) begin if (fout) $fdisplay(fout, "P wlen %0d %0d", cur_ci, p0_wr_len); end
                    st <= (p0_wr_len == 0) ? DONE : (p0_wr_stream ? STREAM : CONST);
                end
            end
            STREAM: begin
                wsrc_addr <= wsrc_addr + 1;
                if (wsrc_addr > 0) begin mwrite((a + beats) & 25'h1FFFFFF, wsrc_q, cur_ci); nww = nww + 1; beats = beats + 1; end
                if (beats == len) st <= DONE;
            end
            READ: begin
                p0_data_available <= 1'b1; p0_q <= mread(a); a <= a + 25'd1; nrw = nrw + 1;
                if (p0_end_burst_req) st <= IDLE;
            end
            CONST: begin
                for (beats = 0; beats < len; beats = beats + 1) begin mwrite((a + beats) & 25'h1FFFFFF, cdata, cur_ci); nww = nww + 1; end
                st <= DONE;
            end
            DONE: begin p0_ready <= 1'b1; st <= IDLE; end
        endcase
    end
endmodule

module tb_talos_v2_diff;
    parameter BLEND = 1, DBUF = 1, FONTRAM = 1, FILLW = 400, VIEW = 0;
    reg clk_sdram = 0, clk_sys = 0, clk_vid = 0, reset = 1;
    always #5 clk_sdram = ~clk_sdram; always #7.5 clk_sys = ~clk_sys; always #41.7 clk_vid = ~clk_vid;
    reg cmd_push = 0; reg [3:0] cmd_op = 0; reg [18:0] cmd_addr = 0; reg [8:0] cmd_w = 0, cmd_h = 0; reg [15:0] cmd_fg = 0, cmd_bg = 0;
    reg [6:0] cmd_glyph = 0; reg [1:0] cmd_sx = 0, cmd_sy = 0, cmd_glyph_hi = 0;
    reg [24:0] src_base = 0, dst_base = 0; reg [9:0] src_stride = 512, dst_stride = 512;
    reg key_en = 0; reg [15:0] key = 0; reg blend_en = 0; reg [2:0] blend_mode = 0; reg [7:0] alpha = 0; reg [7:0] reindex = 0;
    reg [79:0] rc_lut = 0; reg clut_wr = 0; reg [7:0] clut_waddr = 0; reg [15:0] clut_wdata = 0;
    reg font_we = 0; reg [12:0] font_waddr = 0; reg [31:0] font_wdata = 0;
    reg text_light = 0, cpu_buf = 0, flip_tgl = 0, vblank_tgl = 0, fill_tgl = 0; reg [9:0] fill_line = 0;
    reg view_en = 0, view_dbl = 0; reg [24:0] view_base = 0; reg [15:0] view_stride = 1200;
    reg stall = 0;
    wire cmd_full, idle; wire [31:0] fence; wire [8:0] free; wire [15:0] ovf; wire disp, pend;
    wire [24:0] p0_addr; wire [15:0] p0_data; wire [10:0] p0_len; wire p0_str, p0_wr, p0_rd, p0_eb;
    wire [15:0] p0_q; wire p0_av, p0_rdy, p0_da; wire [10:0] wsa; wire [15:0] wsq; wire is_fill; wire [9:0] rd_len;
    wire lb_we; wire [9:0] lb_waddr; wire [15:0] lb_wdata; wire fdi; wire [9:0] fdl; wire fseen;
    wire vlb_we; wire [11:0] vlb_waddr; wire [15:0] vlb_wdata; wire vact;
    tau_talos #(.BLIT_BLEND_ENABLE(BLEND), .DBUF_ENABLE(DBUF), .FONT_RAM_ENABLE(FONTRAM), .FILL_WORDS(FILLW)
`ifdef GAL
        , .VIEW_ENABLE(VIEW)
`endif
    ) dut (
        .reset(reset), .clk_sys(clk_sys), .clk_sdram(clk_sdram), .clk_vid(clk_vid),
        .cmd_push(cmd_push), .cmd_op(cmd_op), .cmd_addr(cmd_addr), .cmd_w(cmd_w), .cmd_h(cmd_h), .cmd_fg(cmd_fg), .cmd_bg(cmd_bg),
        .cmd_glyph(cmd_glyph), .cmd_sx(cmd_sx), .cmd_sy(cmd_sy), .cmd_glyph_hi(cmd_glyph_hi),
        .font_we(font_we), .font_waddr(font_waddr), .font_wdata(font_wdata), .cmd_full(cmd_full), .text_light(text_light),
        .blt_src_base(src_base), .blt_src_stride(src_stride), .blt_dst_base(dst_base), .blt_dst_stride(dst_stride),
        .blt_key_en(key_en), .blt_key(key), .blt_blend_en(blend_en), .blt_blend_mode(blend_mode), .blt_blend_alpha(alpha),
        .rc_cut_lut(rc_lut), .blt_reindex(reindex), .clut_wr(clut_wr), .clut_waddr(clut_waddr), .clut_wdata(clut_wdata),
        .dbuf_cpu_buf(cpu_buf), .dbuf_flip_req_tgl(flip_tgl), .dbuf_disp_buf(disp), .dbuf_flip_pending(pend),
        .sdram_init_complete(1'b1), .p0_addr(p0_addr), .p0_data(p0_data), .p0_byte_en(), .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_q(p0_q),
        .p0_wr_req(p0_wr), .p0_rd_req(p0_rd), .p0_end_burst_req(p0_eb), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da),
        .wsrc_addr(wsa), .wsrc_q(wsq), .p0_is_fill(is_fill), .p0_rd_len(rd_len),
        .fill_req_tgl(fill_tgl), .fill_line_req(fill_line), .vblank_tgl(vblank_tgl),
        .lb_we(lb_we), .lb_waddr(lb_waddr), .lb_wdata(lb_wdata), .fill_done_inc(fdi), .fill_done_line(fdl), .fill_seen(fseen),
        .gfx_idle(idle), .gfx_fence(fence), .gfx_free(free), .gfx_ovf(ovf)
`ifdef GAL
        , .view_en(view_en), .view_base(view_base), .view_stride(view_stride), .view_dbl(view_dbl),
        .vlb_we(vlb_we), .vlb_waddr(vlb_waddr), .vlb_wdata(vlb_wdata), .view_active(vact)
`endif
    );
`ifndef GAL
    assign vlb_we = 1'b0; assign vlb_waddr = 12'd0; assign vlb_wdata = 16'd0; assign vact = 1'b0;
`endif
    v2diff_stub mem (.clk(clk_sdram), .stall(stall), .p0_addr(p0_addr), .p0_data(p0_data), .p0_wr_len(p0_len), .p0_wr_stream(p0_str), .p0_wr_req(p0_wr), .p0_rd_req(p0_rd),
        .p0_end_burst_req(p0_eb), .p0_q(p0_q), .p0_available(p0_av), .p0_ready(p0_rdy), .p0_data_available(p0_da), .wsrc_addr(wsa), .wsrc_q(wsq));

    integer fo, fi, r, c, i, tag, nfill_done = 0, tmo;
    integer a1, a2, a3, a4, a5, a6, a7, a8, a9, a10;

    // ---- line buffers as the scan-out would hold them ----
    reg [15:0] lb [0:1023];
    reg [15:0] vlb [0:4095];
    always @(posedge clk_sdram) begin
        if (lb_we) lb[lb_waddr] <= lb_wdata;
        if (vlb_we) vlb[vlb_waddr] <= vlb_wdata;
        if (fdi) nfill_done = nfill_done + 1;
    end

    // ---- command attribution: accepted pushes queue their index; a pop takes the head ----
    integer accq [0:131071];
    integer qh = 0, qt = 0, next_ci = 0;
    reg [8:0] rd_ptr_prev = 0;
    integer cur_op = 0, busy = 0, nret = 0;      // nret: commands retired since the last reset (for K)
    // a pop is seen at the falling edge after it: rd_ptr has stepped and cmd_q still holds the popped entry
    always @(negedge clk_sdram) begin
        if (!reset && dut.rd_ptr != rd_ptr_prev) begin
            mem.cur_ci = (qh < qt) ? accq[qh] : -9;
            if (qh < qt) qh = qh + 1;
            cur_op = dut.q_op;
            busy = 0; mem.nrq = 0; mem.nrw = 0; mem.nwq = 0; mem.nww = 0;
            $fdisplay(fo, "D %0d %0d", mem.cur_ci, cur_op);
        end
        rd_ptr_prev = dut.rd_ptr;
    end
    always @(posedge clk_sdram) begin
        if (dut.inflight) busy = busy + 1;
        if (dut.retire_now) nret = nret + 1;
        if (dut.retire_now) $fdisplay(fo, "C %0d %0d %0d %0d %0d %0d %0d", mem.cur_ci, cur_op, busy, mem.nrq, mem.nrw, mem.nwq, mem.nww);
        if (p0_rd && (rd_len == 0 || rd_len > 512)) $fdisplay(fo, "P rlen %0d %0d", mem.cur_ci, rd_len);
    end

    task st(input integer f, input integer v);
        case (f)
            0: src_base = v; 1: src_stride = v; 2: dst_base = v; 3: dst_stride = v;
            4: key_en = v; 5: key = v; 6: blend_en = v; 7: blend_mode = v; 8: alpha = v; 9: reindex = v;
            10: text_light = v; 11: cpu_buf = v; 12: rc_lut[(v >> 8) * 5 +: 5] = v & 31;
            13: view_en = v; 14: view_base = v; 15: view_stride = v; 16: view_dbl = v;
            default: $display("bad S field %0d", f);
        endcase
    endtask
    task qline(input [7:0] kind, input integer t);
        $fdisplay(fo, "%s %0d %0d %0d %0d %0d %0d %0d %0d %0d", kind, t, cmd_full, idle, free, fence, ovf, disp, pend, vact);
    endtask

    reg [8*1024-1:0] fn_in, fn_out;
    initial begin
        if (!$value$plusargs("stim=%s", fn_in) || !$value$plusargs("out=%s", fn_out)) begin $display("usage: +stim=<file> +out=<file>"); $finish; end
        fi = $fopen(fn_in, "r"); fo = $fopen(fn_out, "w");
        if (fi == 0 || fo == 0) begin $display("cannot open files"); $finish; end
        mem.fout = fo;
        for (i = 0; i < 1024; i = i + 1) lb[i] = 0;
        for (i = 0; i < 4096; i = i + 1) vlb[i] = 0;
        repeat (10) @(posedge clk_sdram); reset = 0; repeat (20) @(posedge clk_sdram);
        forever begin
            c = $fgetc(fi);
            if (c == -1) begin $display("stimulus without E"); $finish; end
            case (c)
                "P": begin
                    r = $fscanf(fi, "%d %d %d %d %d %d %d %d %d %d\n", a1, a2, a3, a4, a5, a6, a7, a8, a9, a10);
                    @(posedge clk_sys); #1;
                    cmd_op = a1; cmd_addr = a2; cmd_w = a3; cmd_h = a4; cmd_fg = a5; cmd_bg = a6; cmd_glyph = a7; cmd_sx = a8; cmd_sy = a9; cmd_glyph_hi = a10;
                    // accepted at the next clk_sys edge iff cmd_full is 0 then (cmd_full only changes on clk_sys edges)
                    $fdisplay(fo, "A %0d %0d", next_ci, !cmd_full);
                    if (!cmd_full) begin accq[qt] = next_ci; qt = qt + 1; end
                    next_ci = next_ci + 1;
                    cmd_push = 1; @(posedge clk_sys); #1; cmd_push = 0;
                end
                "S": begin r = $fscanf(fi, "%d %d\n", a1, a2); st(a1, a2); end
                "C": begin r = $fscanf(fi, "%d %d\n", a1, a2); @(posedge clk_sys); #1; clut_waddr = a1; clut_wdata = a2; clut_wr = 1; @(posedge clk_sys); #1; clut_wr = 0; end
                "F": begin r = $fscanf(fi, "%d %d\n", a1, a2); @(posedge clk_sys); #1; font_waddr = a1; font_wdata = a2; font_we = 1; @(posedge clk_sys); #1; font_we = 0; end
                "B": begin r = $fscanf(fi, "%d %d\n", a1, a2); mem.mwrite(a1, a2, -1); end
                "U": begin r = $fscanf(fi, "%d %d\n", a1, a2); dut.clut[a1] = a2; end
`ifdef NO_FONT_ROM_BD
`else
                "O": begin r = $fscanf(fi, "%d %d\n", a1, a2); if (FONTRAM == 0) dut.g_font_rom.u_font.mem[a1] = a2; end
                "N": begin r = $fscanf(fi, "%d %d\n", a1, a2); if (FONTRAM != 0) dut.g_font_ram.u_font.mem[a1] = a2; end
`endif
                "L": begin
                    r = $fscanf(fi, "%d %d\n", tag, a1);
                    a2 = nfill_done;
                    @(posedge clk_vid); fill_line = a1; @(posedge clk_vid); fill_tgl = ~fill_tgl;
                    tmo = 0; while (nfill_done == a2 && tmo < 2000000) begin @(posedge clk_sdram); tmo = tmo + 1; end
                    repeat (2) @(posedge clk_sdram);
                    if (nfill_done == a2) $fdisplay(fo, "G %0d %0d", tag, a1);
                    else begin
                        $fwrite(fo, "F %0d %0d %0d ", tag, fdl, vact);
                        if (vact) for (i = 0; i < 2048; i = i + 1) $fwrite(fo, "%04x", vlb[{fdl[0], i[10:0]}]);
                        else for (i = 0; i < 512; i = i + 1) $fwrite(fo, "%04x", lb[{fdl[0], i[8:0]}]);
                        $fwrite(fo, "\n");
                    end
                end
                "V": begin @(posedge clk_vid); vblank_tgl = ~vblank_tgl; repeat (8) @(posedge clk_sdram); end
                "X": begin @(posedge clk_sys); flip_tgl = ~flip_tgl; repeat (8) @(posedge clk_sdram); end
                "R": begin
                   
                    @(posedge clk_sys); reset = 1; repeat (8) @(posedge clk_sys); reset = 0;
                    qh = qt;                  // pending commands are discarded
                    nret = 0;
                    mem.cur_ci = -3;          // an abandoned command's late writes are not attributed to it
                    repeat (8) @(posedge clk_sys);
                    rd_ptr_prev = dut.rd_ptr;
                end
                "I": begin
                    r = $fscanf(fi, "%d\n", tag);
                    repeat (4) @(posedge clk_sys);
                    tmo = 0; while (!idle && tmo < 20000000) begin @(posedge clk_sys); tmo = tmo + 1; end
                    if (!idle) $fdisplay(fo, "H %0d", tag);
                    repeat (4) @(posedge clk_sdram);
                end
                "T": begin r = $fscanf(fi, "%d\n", a1); @(posedge clk_sdram); stall = a1; repeat (2) @(posedge clk_sdram); end
                "Q": begin r = $fscanf(fi, "%d\n", tag); @(posedge clk_sys); #1; qline("Q", tag); end
                "D": begin r = $fscanf(fi, "%d\n", a1); repeat (a1) @(posedge clk_sdram); end
                "K": begin r = $fscanf(fi, "%d\n", a1); tmo = 0; while (nret < a1 && tmo < 3000000) begin @(posedge clk_sdram); tmo = tmo + 1; end repeat (10) @(posedge clk_sys); end
                "J": begin r = $fscanf(fi, "%d\n", a1); @(posedge clk_vid); fill_line = a1; @(posedge clk_vid); fill_tgl = ~fill_tgl; end
                "Y": begin r = $fscanf(fi, "%d\n", tag); $fdisplay(fo, "Y %0d %0d %0d", tag, nfill_done, fdl); end
                "E": begin
                    if (!stall) begin
                        repeat (4) @(posedge clk_sys);
                        tmo = 0; while (!idle && tmo < 20000000) begin @(posedge clk_sys); tmo = tmo + 1; end
                        if (!idle) $fdisplay(fo, "H end");
                    end
                    repeat (40) @(posedge clk_sdram);
                    $fdisplay(fo, "Z %0d %0d %0d %0d %0d %0d %0d %0d %0d", cmd_full, idle, free, fence, ovf, disp, pend, fdl, fseen);
                    $fclose(fo); $finish;
                end
                "\n", " ": ;
                default: begin $display("bad stimulus char %0d", c); $finish; end
            endcase
        end
    end
endmodule
`default_nettype wire
