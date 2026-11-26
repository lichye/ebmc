module main(input bit clk);
  bit released = 0;
  always_ff @(posedge clk)
    released <= 1;

  hold_reset: assume property (@(posedge clk) !released |=> !released);
  initial_reset: cover property (@(posedge clk) !released);
endmodule
