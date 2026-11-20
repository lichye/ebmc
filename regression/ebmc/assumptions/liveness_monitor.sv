module main(input clk, p);
  a_live: assume property (@(posedge clk) s_eventually p);
  p_false: assert property (@(posedge clk) 1'b0);
endmodule
