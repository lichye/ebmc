module state_dead(input bit clk, trip);
  bit spent = 0;
  always_ff @(posedge clk) spent <= spent || trip;
  keep: assume property (@(posedge clk) !spent);
  p: assert property (@(posedge clk) !trip);
  hit: cover property (@(posedge clk) trip);
endmodule
