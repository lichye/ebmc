module tail(input bit clk, trip, alarm);
  future: assume property (@(posedge clk) trip |-> ##8 alarm);
`ifndef ALLOW_ALARM
  quiet: assume property (@(posedge clk) !alarm);
`endif
  unreachable: assert property (@(posedge clk) !trip);
endmodule
