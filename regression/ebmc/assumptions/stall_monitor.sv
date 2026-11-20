module main(input clk, rst, stall, other);
  reg last_reset = 0;
  always @(posedge clk) last_reset <= rst;
  a_reset: assume property (@(posedge clk) rst |=> !stall);
  p_reset: assert property (@(posedge clk) !(last_reset && stall));
  p_other: assert property (@(posedge clk) !(last_reset && other));
endmodule
