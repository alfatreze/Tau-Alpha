// Behavioural model of one Pocket PSRAM chip (two 8 MiB dies) for simulation.
// Timing limits are the AS1C8M16PL datasheet values in docs/PSRAM_TIMING_CONTRACT.md.
// The model is deliberately strict: read data is X until tAADV (from ADV# rising),
// tAA, tCO and tOE have all elapsed,
// and it reports (never silently accepts) CE overlap, DQ changes inside address
// hold, short pulses, missing setup, lane changes during a write, and any
// access to the guard word 3FFFFFh.
`timescale 1ns/1ps
`default_nettype none
module psram_chip_model #(
    parameter NAME = "chip",
    parameter real T_VP  = 5.0,
    parameter real T_AVH = 2.0,
    parameter real T_CVS = 7.0,
    parameter real T_AVS = 5.0,
    parameter real T_DW  = 20.0,
    parameter real T_WP  = 45.0,
    parameter real T_AW  = 70.0,
    parameter real T_AADV = 70.0,   // ADV# access: measured from ADV# RISING (conservative)
    parameter real T_AA   = 70.0,   // address access, from address valid (ADV# falling)
    parameter real T_CO   = 70.0,   // chip-select access, from CE# falling
    parameter real T_OE   = 20.0,   // output enable to valid data
    parameter real T_CW   = 70.0,   // CE# low to end of write
    parameter real T_CPH  = 5.0,    // CE# high between operations
    parameter real T_CEM  = 4000.0  // maximum CE# low
) (
    input  wire [21:16] a,
    inout  wire [15:0]  dq,
    input  wire         adv_n, ce0_n, ce1_n, oe_n, we_n, ub_n, lb_n,
    output reg          drv_en,
    output reg          guard_touched
);
    reg [15:0] mem0 [0:4194303];
    reg [15:0] mem1 [0:4194303];
    integer errors = 0;
    integer ops_write = 0, ops_read = 0;

    reg [15:0] dq_drv;
    assign dq = drv_en ? dq_drv : 16'hZZZZ;

    wire ce_active = (ce0_n ^ ce1_n);              // exactly one low
    wire die       = ~ce0_n ? 1'b0 : 1'b1;

    real t_ce_rise = -1.0e9;
    real t_ce_fall = 0, t_adv_fall = 0, t_adv_rise = 0, t_we_fall = 0,
         t_dq_chg = 0, t_a_chg = 0;
    reg        adv_open = 0, latched = 0, wr_open = 0;
    reg [21:0] laddr;
    reg        ldie;
    reg [1:0]  lane_l;
    integer    gen = 0, my;
    real       rem;

    task err(input [8*48-1:0] msg);
        begin
            errors = errors + 1;
            $display("%0t MODEL[%0s] ERROR: %0s", $time, NAME, msg);
        end
    endtask

    initial begin drv_en = 0; dq_drv = 16'hxxxx; guard_touched = 0; end

    // CE overlap
    always @(ce0_n or ce1_n) begin
        if (!ce0_n && !ce1_n) err("both CE# low on one chip");
    end
    always @(negedge ce0_n or negedge ce1_n) begin
        if ($realtime - t_ce_rise < T_CPH) err("CE# high < t_cph between ops");
        t_ce_fall = $realtime;
    end

    // CE rising aborts everything in flight
    always @(posedge ce0_n or posedge ce1_n) begin
        if (ce0_n && ce1_n) begin
            if ($realtime - t_ce_fall > T_CEM) err("CE# low > t_cem");
            t_ce_rise = $realtime;
            if (wr_open && !we_n) begin
                if (ldie) mem1[laddr] = 16'hxxxx; else mem0[laddr] = 16'hxxxx;
            end
            drv_en = 0; gen = gen + 1;
            adv_open = 0; latched = 0; wr_open = 0;
        end
    end

    // track DQ / A changes made by the controller (not by us)
    always @(dq) if (!drv_en) t_dq_chg = $realtime;
    always @(a)  t_a_chg = $realtime;

    // address phase
    real t_adv_evt;
    always @(negedge adv_n) begin
        t_adv_evt = $realtime;
        #0.001;                       // CE# may change in the same clock step
        if (!ce_active) err("ADV# low without exactly one CE#");
        else begin adv_open = 1; t_adv_fall = t_adv_evt; latched = 0; end
    end
    always @(posedge adv_n) if (adv_open) begin
        if ($realtime - t_adv_fall < T_VP)   err("ADV# pulse < t_vp");
        if ($realtime - t_ce_fall  < T_CVS)  err("CE# low < t_cvs before ADV# rise");
        if ($realtime - t_dq_chg   < T_AVS)  err("address on DQ < t_avs");
        if ($realtime - t_a_chg    < T_AVS)  err("address on A < t_avs");
        laddr = {a, dq}; ldie = die; latched = 1; adv_open = 0; t_adv_rise = $realtime;
        if (laddr == 22'h3FFFFF) begin
            guard_touched = 1; err("access to guard word 3FFFFFh");
        end
    end
    always @(dq) if (latched && !drv_en && ($realtime - t_adv_rise < T_AVH) && $realtime != t_adv_rise)
        err("DQ changed inside address hold");

    // write
    always @(negedge we_n) if (ce_active) begin
        if (!latched) err("WE# low before address latched");
        if (!oe_n)    err("WE# and OE# both low");
        t_we_fall = $realtime; lane_l = {ub_n, lb_n}; wr_open = 1;
    end
    always @(posedge we_n) if (wr_open) begin
        if (!ce_active)                          err("WE# rise without CE#");
        if ($realtime - t_we_fall < T_WP)        err("WE# pulse < t_wp");
        if ($realtime - t_dq_chg  < T_DW)        err("write data setup < t_dw");
        if ($realtime - t_adv_fall < T_AW)       err("write end < t_aw after ADV#");
        if ($realtime - t_ce_fall  < T_CW)       err("write end < t_cw after CE#");
        if ({ub_n, lb_n} !== lane_l)             err("byte lanes changed during write");
        if (ce_active) begin
            if (ldie) begin
                if (!lane_l[0]) mem1[laddr][7:0]  = dq[7:0];
                if (!lane_l[1]) mem1[laddr][15:8] = dq[15:8];
            end else begin
                if (!lane_l[0]) mem0[laddr][7:0]  = dq[7:0];
                if (!lane_l[1]) mem0[laddr][15:8] = dq[15:8];
            end
            ops_write = ops_write + 1;
        end
        wr_open = 0;
    end

    // read: data is X until every access time has elapsed
    real t_oe_fall, t_valid;
    always @(negedge oe_n) begin
        if (ce_active && adv_open) err("OE# low while address is on the bus");
        else if (ce_active && latched && we_n) begin
            t_oe_fall = $realtime;
            gen = gen + 1; my = gen; drv_en = 1; dq_drv = 16'hxxxx;
            t_valid = t_adv_rise + T_AADV;
            if (t_adv_fall + T_AA > t_valid)   t_valid = t_adv_fall + T_AA;
            if (t_ce_fall  + T_CO > t_valid)   t_valid = t_ce_fall  + T_CO;
            if (t_oe_fall  + T_OE > t_valid)   t_valid = t_oe_fall  + T_OE;
            rem = t_valid - $realtime;
            if (rem > 0) #(rem);
            if (my == gen && drv_en) begin
                dq_drv = ldie ? mem1[laddr] : mem0[laddr];
                ops_read = ops_read + 1;
            end
        end
    end
    always @(posedge oe_n) begin drv_en = 0; gen = gen + 1; end
endmodule
`default_nettype wire
