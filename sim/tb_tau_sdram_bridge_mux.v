`timescale 1ns/1ps
`default_nettype none
module tb_tau_sdram_bridge_mux;
 reg clk=0,rst=1,dr=0,dw=0,wr=0,ww=0,bb=0,bd=0; always #5 clk=~clk;
 reg [24:0] da=0,wa=0; reg [31:0] dd=0,wd=0,br=0; reg [3:0] db=0,wb=0;
 wire acc,done_d,done_w,busy,start,bwrite,wbstart; wire [24:0] ba; wire [31:0] bwdata,rd_d,rd_w; wire [3:0] bbe;
 integer e=0; task c(input x,input [511:0] s); begin if(!x)begin $display("FAIL: %0s",s);e=e+1;end else $display("ok:   %0s",s);end endtask
 tau_sdram_bridge_mux dut(.clk(clk),.rst(rst),.diag_req(dr),.diag_write(dw),.diag_addr(da),.diag_wdata(dd),.diag_be(db),.wb_req(wr),.wb_write(ww),.wb_addr(wa),.wb_wdata(wd),.wb_be(wb),.wb_accept(acc),.diag_done(done_d),.wb_done(done_w),.diag_rdata(rd_d),.wb_rdata(rd_w),.busy(busy),.bridge_start(start),.bridge_write(bwrite),.bridge_addr(ba),.bridge_wdata(bwdata),.bridge_be(bbe),.bridge_wb_start(wbstart),.bridge_busy(bb),.bridge_done(bd),.bridge_rdata(br));
 initial begin repeat(2)@(posedge clk);rst=0;
  @(negedge clk); dr=1;da=25'h12;dd=32'hA;db=4'h3;wr=1;wa=25'h34;wd=32'hB;wb=4'hC;
  @(posedge clk);#1;c(start&&bwrite==0&&ba==25'h12&&!acc,"diagnostic wins simultaneous request");
  @(negedge clk);dr=0;bb=1; @(negedge clk);bb=0;bd=1;br=32'h1111; @(posedge clk);#1;c(done_d&&!done_w&&rd_d==32'h1111,"completion returns only to diagnostic owner");
  @(negedge clk);bd=0; @(posedge clk);#1;c(start&&acc&&wbstart&&ba==25'h34&&bwdata==32'hB,"held WB request receives next owner slot");
  @(negedge clk);wr=0;bb=1; @(negedge clk);bb=0;bd=1;br=32'h2222; @(posedge clk);#1;c(done_w&&!done_d&&rd_w==32'h2222,"completion returns only to WB owner");
  $display("\n%0s (%0d failures)",e?"FAILED":"PASSED",e);$finish; end
endmodule
`default_nettype wire
