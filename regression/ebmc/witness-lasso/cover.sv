module main(input bit clk, trip, alarm);
  due: assume property (@(posedge clk) trip |=> alarm);
  quiet: assume property (@(posedge clk) !alarm);
  reachable: cover property (@(posedge clk) !alarm);
  terminal_hit: cover property (@(posedge clk) trip);
  impossible: cover property (@(posedge clk) alarm);
  safe: assert property (@(posedge clk) !alarm);
endmodule
