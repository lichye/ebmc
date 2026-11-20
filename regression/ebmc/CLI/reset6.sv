module top(input clk, input rst_n);
  reg q = 0;
  always @(posedge clk) q <= 1;
  p: assert property (@(posedge clk) q == 0);
endmodule
