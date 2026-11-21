module leaf #(parameter int W=1) (input bit clk, rst, input bit [W-1:0] a);
  bit [W-1:0] q;
  always_ff @(posedge clk) if (rst) q <= '0; else q <= a;
  p_clean: assert property (@(posedge clk) disable iff (rst) q == '0);
endmodule
module bridge(input bit clk, rst, permit, input bit [1:0] a);
  leaf #(.W(2)) u(.clk(clk), .rst(rst), .a(a));
  env_zero: assume property (@(posedge clk) a == 2'd0);
  source_permit: assume property (@(posedge clk) permit);
endmodule
